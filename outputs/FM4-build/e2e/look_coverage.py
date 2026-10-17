"""Look mod coverage: which ucode hashes the look mods must hit, per source and per mode.

Scans the same shader containers as material_atlas.py (Media\\Shaders, the track archives
Media\\tracks\\<track>\\bin.zip, the DLC archives in the BuildRoot user data, and default.xex
through <BuildRoot>\\loaded-image.bin) and computes for every pixel shader the hash the runtime
keys shaders and mods by: XXH3_64 of the big-endian ucode (pipeline_cache.cpp LoadShader). The
XXH3 secret is read from the runtime's xxHash header in the BuildRoot.

Roles, from the names in the constant table:
  final combine   filmicParams1 (variant from vignetteColor, dofFocalParams, watermarkLayout)
  motion blur     invCurrentVPThenPrevVP
  paint family    PaintColor, StripeColor and TwoToneColor (body paint and the other paint
                  techniques of a car shader set); plus the custom paint techniques
The car shader set of each render scenario comes from Media\\RenderScenarios (CarShader;
shaders_v16 when absent). With --dump, hashes seen in dump_shaders runs are marked.

  look_coverage.py --build-root <BuildRoot> [--look work\\fm4-rex\\look] [--dump <dir> ...]
      [--json <out.json>]

The output names game shaders by hash and constant names only: keep the JSON in the BuildRoot.
"""
import argparse, collections, json, re, struct
from pathlib import Path

import material_atlas as ma

M64 = (1 << 64) - 1
P32_1, P32_2, P32_3 = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D
P64_1, P64_2, P64_3 = 0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9
P64_4, P64_5 = 0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5
MX1, MX2 = 0x165667919E3779F9, 0x9FB21C651E98DF25


class Xxh3:
    """XXH3_64bits with seed 0 and the default secret (xxHash 0.8, scalar path)."""

    def __init__(self, header):
        text = Path(header).read_text(errors='replace')
        block = re.search(r'XXH3_kSecret\[[^\]]*\]\s*=\s*\{([^}]*)\}', text).group(1)
        self.secret = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', block))
        assert len(self.secret) == 192

    @staticmethod
    def _r64(b, i):
        return struct.unpack_from('<Q', b, i)[0]

    @staticmethod
    def _fold(a, b):
        p = a * b
        return (p ^ (p >> 64)) & M64

    @staticmethod
    def _avalanche(h):
        h ^= h >> 37
        h = (h * MX1) & M64
        return h ^ (h >> 32)

    @staticmethod
    def _avalanche64(h):
        h ^= h >> 33
        h = (h * P64_2) & M64
        h ^= h >> 29
        h = (h * P64_3) & M64
        return h ^ (h >> 32)

    def _mix16(self, data, i, s):
        return self._fold(self._r64(data, i) ^ self._r64(self.secret, s),
                          self._r64(data, i + 8) ^ self._r64(self.secret, s + 8))

    def __call__(self, data):
        n, k, r = len(data), self.secret, self._r64
        if n == 0:
            return self._avalanche64(r(k, 56) ^ r(k, 64))
        if n <= 3:
            c = (data[0] << 16) | (data[n >> 1] << 24) | data[n - 1] | (n << 8)
            return self._avalanche64(c ^ (struct.unpack_from('<I', k, 0)[0] ^ struct.unpack_from('<I', k, 4)[0]))
        if n <= 8:
            v = struct.unpack_from('<I', data, n - 4)[0] + (struct.unpack_from('<I', data, 0)[0] << 32)
            h = v ^ (r(k, 8) ^ r(k, 16))
            h ^= ((h << 49 | h >> 15) & M64) ^ ((h << 24 | h >> 40) & M64)
            h = (h * MX2) & M64
            h ^= (h >> 35) + n
            h = (h * MX2) & M64
            return h ^ (h >> 28)
        if n <= 16:
            lo = r(data, 0) ^ (r(k, 24) ^ r(k, 32))
            hi = r(data, n - 8) ^ (r(k, 40) ^ r(k, 48))
            swapped = int.from_bytes(lo.to_bytes(8, 'little'), 'big')
            return self._avalanche((n + swapped + hi + self._fold(lo, hi)) & M64)
        if n <= 128:
            acc = n * P64_1
            for i in range(4):
                if n > 32 * i:
                    acc += self._mix16(data, 16 * i, 32 * i) + self._mix16(data, n - 16 * (i + 1), 32 * i + 16)
            return self._avalanche(acc & M64)
        if n <= 240:
            acc = n * P64_1
            for i in range(8):
                acc += self._mix16(data, 16 * i, 16 * i)
            acc = self._avalanche(acc & M64)
            end = self._mix16(data, n - 16, 136 - 17)
            for i in range(8, n // 16):
                end += self._mix16(data, 16 * i, 16 * (i - 8) + 3)
            return self._avalanche((acc + end) & M64)
        acc = [P32_3, P64_1, P64_2, P64_3, P64_4, P32_2, P64_5, P32_1]

        def accumulate(at, s):
            for lane in range(8):
                v = r(data, at + 8 * lane)
                key = v ^ r(k, s + 8 * lane)
                acc[lane ^ 1] = (acc[lane ^ 1] + v) & M64
                acc[lane] = ((key & 0xFFFFFFFF) * (key >> 32) + acc[lane]) & M64

        stripes = (len(k) - 64) // 8
        block = 64 * stripes
        blocks = (n - 1) // block
        for b in range(blocks):
            for s in range(stripes):
                accumulate(b * block + 64 * s, 8 * s)
            for lane in range(8):
                a = acc[lane]
                a ^= a >> 47
                a ^= r(k, len(k) - 64 + 8 * lane)
                acc[lane] = (a * P32_1) & M64
        for s in range(((n - 1) - block * blocks) // 64):
            accumulate(blocks * block + 64 * s, 8 * s)
        accumulate(n - 64, len(k) - 64 - 7)
        result = n * P64_1
        for i in range(4):
            result += self._fold(acc[2 * i] ^ r(k, 11 + 16 * i), acc[2 * i + 1] ^ r(k, 11 + 16 * i + 8))
        return self._avalanche(result & M64)


CUSTOM_PAINTS = ('bentley_paint', 'delorean_paint', 'reventon_paint', 'sagaris_paint', 'matte_colors')


def role(names, source_names):
    if 'filmicParams1' in names:
        variant = 'FinalCombine_BloomTonemap' if 'colorGradingTexScale' in names else 'FinalCombine_NoPostEffects'
        for name, suffix in (('dofFocalParams', '_DOF'), ('vignetteColor', '_Vignette'), ('watermarkLayout', '_Watermark')):
            if name in names:
                variant += suffix
        return 'final combine', variant
    if 'invCurrentVPThenPrevVP' in names:
        return 'motion blur', 'sample loop' if 'numTexSamples' in names else 'fixed taps'
    if {'PaintColor', 'StripeColor', 'TwoToneColor'} <= names:
        return 'paint family', 'env cube' if 'envSampler' in names else 'static cube' if 'envStaticSampler' in names else 'no cube'
    custom = [s for s in source_names if Path(s).stem.lower() in CUSTOM_PAINTS]
    if custom:
        return 'custom paint', Path(custom[0]).stem
    return None, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-root', required=True)
    parser.add_argument('--look', default=str(Path(__file__).resolve().parents[3] / 'work/fm4-rex/look'))
    parser.add_argument('--dump', action='append', default=[], help='dump_shaders directory of a run')
    parser.add_argument('--json', help='write the coverage here (outside the repository)')
    options = parser.parse_args()
    root = Path(options.build_root)
    media = Path(json.loads((root / 'local.json').read_text())['disc1']) / 'Media'
    xxh3 = Xxh3(root / 'runtime-source/thirdparty/xxHash/xxhash.h')
    lzx = ma.Lzx(root)

    shaders = {}
    def add(data, source):
        for _, c in ma.containers(data):
            if struct.unpack_from('>I', c)[0] & 1:
                continue
            code = ma.microcode(c)
            e = shaders.setdefault('%016X' % xxh3(code), {'sources': set(), 'table': None})
            e['sources'].add(source)
            if e['table'] is None:
                e['table'] = ma.constant_table(c)
    for fxobj in sorted((media / 'Shaders').rglob('*.fxobj')):
        add(fxobj.read_bytes(), 'shaders/' + fxobj.relative_to(media / 'Shaders').as_posix())
    for archive in sorted((media / 'tracks').glob('*/bin.zip')):
        for name, data in ma.zip_entries(archive, '', lzx):
            add(data, f'track/{archive.parent.name}/{name}')
    for archive in sorted((root / 'user-data').rglob('DLCZips/*.zip')):
        for name, data in ma.zip_entries(archive, '.fxobj', lzx):
            add(data, f'dlc/{archive.stem}/{name}')
    add((root / 'loaded-image.bin').read_bytes(), 'default.xex')

    mods = {p.name[7:23].upper() for p in Path(options.look).glob('shader_*.mod.hlsli')}
    seen = set()
    for dump in options.dump:
        seen |= {p.name[7:23].upper() for p in Path(dump).glob('shader_*')}

    rows = []
    for h, e in shaders.items():
        names = {t[0] for t in e['table']}
        r, variant = role(names, e['sources'])
        if r:
            sets = sorted({Path(s).stem.replace('_v16', '') for s in e['sources'] if s.startswith('shaders/Cars/')})
            rows.append({'hash': h, 'role': r, 'variant': variant, 'mod': h in mods,
                         'seen': (h in seen) if options.dump else None, 'car_sets': sets,
                         'sources': sorted(e['sources']),
                         'constants': [f'{n}={g}{i}' for n, g, i, _ in e['table']]})
    rows.sort(key=lambda x: (x['role'], x['variant'], x['hash']))

    scenarios = {}
    for xml in sorted((media / 'RenderScenarios').glob('*.xml')):
        m = re.search(r'<CarShader\s+value="([^"]+)"', xml.read_text(errors='replace'))
        scenarios[xml.stem] = m.group(1) if m else 'shaders'

    track_roles = collections.Counter()
    for row in rows:
        for s in row['sources']:
            if s.startswith(('track/', 'dlc/')):
                track_roles[s.split('/')[1]] += 1
    tracks = sorted(p.parent.name for p in (media / 'tracks').glob('*/bin.zip'))

    if options.json:
        Path(options.json).write_text(json.dumps({'roles': rows, 'scenarios': scenarios,
                                                  'track_role_shaders': dict(track_roles)}, indent=1))
    print(f'{len(shaders)} pixel shaders; mods for {len(mods)} hashes in {options.look}')
    for row in rows:
        if row['role'] == 'paint family' and not any(s in ('shaders', 'shaders_16car', 'shaders_splitscreen',
                                                         'shaders_ui', 'shaders_slod') for s in row['car_sets']):
            continue
        where = ','.join(row['car_sets']) or ','.join(sorted({s.split('/')[0] for s in row['sources']}))
        seen_text = '' if row['seen'] is None else (' seen' if row['seen'] else ' not-seen')
        print(f"{row['role']:14} {row['hash']} {'MOD' if row['mod'] else '---'}{seen_text} "
              f"{row['variant']:44} {where[:60]}")
    print('\nrender scenario -> car shader set')
    for name, car_set in scenarios.items():
        print(f'  {name:32} {car_set}')
    print(f'\n{len(tracks)} track archives; role shaders inside track or DLC archives: '
          f'{sum(track_roles.values())} {dict(track_roles)}')


if __name__ == '__main__':
    main()
