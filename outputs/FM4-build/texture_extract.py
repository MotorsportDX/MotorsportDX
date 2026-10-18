"""Offline texture extraction: the textures of the track archives as runtime texture dumps.

Reads every Media\\tracks\\<track>\\bin.zip of Disc 1 (XMemCompress LZX, zip method 21,
decompressed with the runtime's LZX decoder as e2e/material_atlas.py does), finds the CAFF
containers with texture records whose data is in the container itself, and dumps each texture's
base level as <hash>.dds, the file name and contents the runtime's texture_dump_path gives the
same texture in guest memory. A pack built from these files works with texture_replace_path
without driving every track. See docs/analysis/texture-replacement.md, "Offline extraction".

The hashing and the DDS files come from texture-extract.cpp, a helper DLL built here on first use
with clang++ from the BuildRoot's runtime sources (replacement.cpp: ReadGuestBaseLevel,
HashGuestLevel, Dumper) against the installed runtime.

  texture_extract.py --build-root <BuildRoot> [--out <dir>] [--track Alps ...] [--no-dump]

Writes, in --out (default <BuildRoot>\\texextract):
  dds\\<hash>.dds, dds\\textures.txt   the dump, as texture_dump_path writes it
  index.tsv                          every texture record found, with its hash or why it has none
  panoramas.txt                      per track, the sky candidates: textures without mips, at
                                     least 1024 wide, marked 'wide' when at least twice as wide
                                     as high (the 360-degree panoramas; a few tracks split their
                                     sky into square or 2:1 tiles)
The output is game content: keep it in the BuildRoot, never in the repository.
"""
import argparse, collections, ctypes, json, os, struct, subprocess, sys, zipfile, zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'e2e'))
from material_atlas import Lzx  # noqa: E402

# xenos::TextureFormat names of the formats seen in the track archives, for the index.
FORMAT_NAMES = {2: 'k_8', 4: 'k_5_6_5', 6: 'k_8_8_8_8', 7: 'k_2_10_10_10', 10: 'k_8_8',
                15: 'k_4_4_4_4', 18: 'k_DXT1', 19: 'k_DXT2_3', 20: 'k_DXT4_5', 24: 'k_16',
                25: 'k_16_16', 32: 'k_16_16_16_16_FLOAT', 36: 'k_32_FLOAT', 49: 'k_DXN',
                58: 'k_DXT3A', 59: 'k_DXT5A', 60: 'k_CTX1'}
HELPER_RESULTS = {-1: 'no-blocks-format', -2: 'data-short', -3: 'read-failed'}
TEXTURE_TAG = b'texture\x00'


def helper(root):
    """The helper DLL, rebuilt when its sources are newer."""
    build = root / 'texextract' / 'build'
    dll = build / 'fm4-texture-extract.dll'
    sources = [HERE / 'texture-extract.cpp',
               root / 'runtime-source/src/graphics/pipeline/texture/replacement.cpp']
    if not dll.exists() or any(s.stat().st_mtime > dll.stat().st_mtime for s in sources):
        build.mkdir(parents=True, exist_ok=True)
        # The runtime's own flags for its texture code (runtime-build/build.ninja).
        subprocess.run(['clang++', '-shared', '-O2', '-std=gnu++23', '-D_DLL', '-D_MT',
                        '-DSPDLOG_COMPILED_LIB', '-DSPDLOG_FMT_EXTERNAL', '-DREX_PLATFORM_WINDOWS=1',
                        '-Xclang', '--dependent-lib=msvcrt', '-march=x86-64-v2',
                        '-isystem', str(root / 'runtime-install/include'), *map(str, sources),
                        '-L' + str(root / 'runtime-install/lib'), '-lrexruntime', '-lspdlog', '-lfmt',
                        '-fuse-ld=lld-link', '-o', str(dll)], check=True)
    os.add_dll_directory(str(root / 'game-build'))
    lib = ctypes.CDLL(str(dll))
    lib.fm4_texture_dump_open.restype = ctypes.c_int
    lib.fm4_texture_dump_open.argtypes = [ctypes.c_char_p]
    lib.fm4_texture_hash.restype = ctypes.c_int
    lib.fm4_texture_hash.argtypes = [ctypes.c_void_p, ctypes.c_size_t] + [ctypes.c_uint32] * 8 + [
        ctypes.POINTER(ctypes.c_uint64)]
    return lib


def entries(path, lzx):
    """(name, data) of every entry of a track archive that decompresses and matches its CRC."""
    archive = zipfile.ZipFile(path)
    with open(path, 'rb') as f:
        for info in archive.infolist():
            f.seek(info.header_offset + 26)
            name_length, extra_length = struct.unpack('<HH', f.read(4))
            f.seek(info.header_offset + 30 + name_length + extra_length)
            raw = f.read(info.compress_size)
            if info.compress_type == 0:
                data = raw
            elif info.compress_type == 21:
                data = lzx.inflate(raw, info.file_size)
            else:
                data = None
            if data is not None and zlib.crc32(data) & 0xFFFFFFFF == info.CRC:
                yield info.filename, data


def caff_textures(data):
    """The texture records of a CAFF container, as dicts, and the container's error if any.

    Layout (big-endian; found in the files, not documented anywhere):
      0x00  'CAFF21.11.05.0034'
      0x18  u32 chunk name count N        0x1c  u32 chunk list length M
      0x44  u32 name table offset in .data
      0x48  u32 header size: .data starts here, .gpu right after .data
      0x50  sections, 0x28 bytes each: name[8], +0x08 u32 alignment log2, +0x10 u32 size
    The name table is u32 blob size, u32 offsets[N], the names; then u32 0 and M chunks of
    u32 id, u32 offset, u32 size, u8 section (1 .data, 2 .gpu), u8 alignment log2. A chunk id has
    a .data chunk (its record) and optionally a .gpu chunk (its GPU data). A texture record is a
    .data chunk starting with 'texture', '04.05.05.0032', and from its start:
      +0x18 u32 Xbox 360 D3DFORMAT (bits 0-5 format, 6-7 endian, 8 tiled)
      +0x1c u32 kind (0 2D; 2 for a cube map)   +0x24 u16 width, u16 height
      +0x28 u32 base offset                     +0x2c u32 mip offset (0xffffffff: none, or packed)
      +0x30 u8 mip levels
    """
    if not data.startswith(b'CAFF'):
        return [], 'not-caff'
    name_count, chunk_count = struct.unpack_from('>II', data, 0x18)
    names_offset, data_start = struct.unpack_from('>II', data, 0x44)
    sections = {}
    for i in range(8):
        o = 0x50 + 0x28 * i
        name = data[o:o + 8].rstrip(b'\x00')
        if not name:
            break
        sections[name] = struct.unpack_from('>I', data, o + 0x10)[0]
    data_size, gpu_size = sections.get(b'.data', 0), sections.get(b'.gpu', 0)
    gpu_start = data_start + data_size
    if gpu_start + gpu_size != len(data):
        return [], 'bad-sections'
    table = data_start + names_offset
    blob_size = struct.unpack_from('>I', data, table)[0]
    offsets = struct.unpack_from('>%dI' % name_count, data, table + 4)
    blob = table + 4 + 4 * name_count
    names = [data[blob + o:data.index(b'\x00', blob + o)].decode('latin-1') for o in offsets]
    o = blob + blob_size + 4
    records, gpu = {}, {}
    for _ in range(chunk_count):
        chunk, offset, size, section, _alignment = struct.unpack_from('>IIIBB', data, o)
        o += 14
        if section == 1:
            records[chunk] = data_start + offset
        elif section == 2:
            gpu[chunk] = (gpu_start + offset, size)
        else:
            return [], 'bad-chunk-list'
    textures = []
    for chunk, r in sorted(records.items()):
        if data[r:r + 8] != TEXTURE_TAG:
            continue
        d3d_format, kind = struct.unpack_from('>II', data, r + 0x18)
        width, height = struct.unpack_from('>HH', data, r + 0x24)
        base, mip = struct.unpack_from('>II', data, r + 0x28)
        texture = dict(chunk=chunk, name=names[chunk - 1] if chunk <= len(names) else '',
                       d3d_format=d3d_format, format=d3d_format & 0x3F,
                       endian=(d3d_format >> 6) & 3, tiled=(d3d_format >> 8) & 1, kind=kind,
                       width=width, height=height, levels=data[r + 0x30], base=base, mip=mip)
        if chunk in gpu:
            start, size = gpu[chunk]
            texture['data'] = (start + base, size - base)
        textures.append(texture)
    return textures, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-root', required=True)
    parser.add_argument('--out', help='output directory (default <BuildRoot>\\texextract)')
    parser.add_argument('--track', action='append', default=[], help='only these tracks (folder names)')
    parser.add_argument('--no-dump', action='store_true', help='hash and index only, no DDS files')
    options = parser.parse_args()
    root = Path(options.build_root)
    out = Path(options.out) if options.out else root / 'texextract'
    tracks = Path(json.loads((root / 'local.json').read_text())['disc1']) / 'Media' / 'tracks'
    lzx = Lzx(root)
    lib = helper(root)
    if not options.no_dump:
        (out / 'dds').mkdir(parents=True, exist_ok=True)
        if lib.fm4_texture_dump_open(str(out / 'dds').encode('utf-8')):
            sys.exit('cannot dump into %s' % (out / 'dds'))
    out.mkdir(parents=True, exist_ok=True)

    rows, panoramas, totals = [], collections.defaultdict(list), collections.Counter()
    hashes = set()
    for archive in sorted(tracks.glob('*/bin.zip')):
        track = archive.parent.name
        if options.track and track.lower() not in {t.lower() for t in options.track}:
            continue
        counts = collections.Counter()
        for entry, data in entries(archive, lzx):
            if not data.startswith(b'CAFF') or TEXTURE_TAG not in data:
                continue
            textures, error = caff_textures(data)
            if error:
                counts[error] += 1
                rows.append((track, entry, '', '', '', '', '', '', error, ''))
                continue
            counts['containers'] += 1
            buffer = None
            for t in textures:
                result, name = '', ''
                if t['kind'] != 0:
                    result = 'cube'  # the runtime does not hash cube maps
                elif 'data' not in t:
                    result = 'external'  # no .gpu chunk: the data is elsewhere (streamed)
                else:
                    buffer = buffer or ctypes.create_string_buffer(data, len(data))
                    start, size = t['data']
                    value = ctypes.c_uint64()
                    code = lib.fm4_texture_hash(ctypes.addressof(buffer) + start, size, t['format'],
                                                t['width'], t['height'], (t['width'] + 31) & ~31,
                                                t['tiled'], t['endian'], 1, t['levels'],
                                                ctypes.byref(value))
                    if code:
                        result = HELPER_RESULTS.get(code, str(code))
                    else:
                        name = '%016x' % value.value
                        result = 'hashed'
                        hashes.add(name)
                        if t['levels'] == 1 and t['width'] >= 1024:
                            panoramas[track].append((name, entry, t))
                counts[result] += 1
                rows.append((track, entry, t['chunk'], t['name'],
                             FORMAT_NAMES.get(t['format'], str(t['format'])),
                             '%dx%d' % (t['width'], t['height']), t['levels'],
                             '0x%08x' % t['d3d_format'], result, name))
        totals.update(counts)
        print('%-14s %s' % (track, ', '.join('%s %d' % kv for kv in sorted(counts.items()))))

    with open(out / 'index.tsv', 'w', encoding='utf-8') as f:
        f.write('track\tentry\tchunk\tsource\tformat\tsize\tlevels\td3dformat\tresult\thash\n')
        for row in rows:
            f.write('\t'.join(map(str, row)) + '\n')
    with open(out / 'panoramas.txt', 'w', encoding='utf-8') as f:
        for track in sorted(panoramas):
            for name, entry, t in panoramas[track]:
                f.write('%s\t%s\t%s\t%s\t%dx%d\t%s\n' % (
                    track, name, entry, FORMAT_NAMES.get(t['format'], t['format']), t['width'],
                    t['height'], 'wide' if t['width'] >= 2 * t['height'] else ''))
    print('total:', ', '.join('%s %d' % kv for kv in sorted(totals.items())),
          '; %d distinct hashes; %d sky candidates on %d tracks' % (
              len(hashes), sum(map(len, panoramas.values())), len(panoramas)))
    print('wrote', out / 'index.tsv', 'and', out / 'panoramas.txt')


if __name__ == '__main__':
    main()
