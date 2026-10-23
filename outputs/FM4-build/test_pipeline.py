"""Synthetic tests: no FM4 bytes or original game assets are needed."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import pipeline
from prepare import switches, BASE
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'e2e'))
from e2e import is_clear_gray

class PipelineTests(unittest.TestCase):
    def dispatch(self):
        # Construct a synthetic lis/addi/lwzx/mtctr/bctr dispatch for r3.
        words = [(15 << 26) | (4 << 21) | 0x8200,
                 (14 << 26) | (4 << 21) | (4 << 16) | 64,
                 (31 << 26) | (5 << 21) | (4 << 16) | (3 << 11) | (23 << 1),
                 (31 << 26) | (5 << 21) | (9 << 16) | (467 << 1), 0x4E800420]
        image = bytearray(128)
        for i, word in enumerate(words): struct.pack_into('>I',image,i*4,word)
        struct.pack_into('>I',image,64,BASE+96)
        recipe = {'start':BASE,'end':BASE+16,'address':BASE,'register':3,'count':1}
        return image,recipe

    def test_reads_destination_from_input(self):
        image,recipe=self.dispatch()
        self.assertEqual(switches(image,[recipe])[0]['labels'],[BASE+96])
        struct.pack_into('>I',image,64,BASE+100)
        self.assertEqual(switches(image,[recipe])[0]['labels'],[BASE+100])

    def test_rejects_unsupported_or_bad_destinations(self):
        image,recipe=self.dispatch()
        with self.assertRaises(ValueError): switches(image[:65],[recipe])
        struct.pack_into('>I',image,64,3)
        with self.assertRaises(ValueError): switches(image,[recipe])
        image[0:4]=bytes(4)
        with self.assertRaises(ValueError): switches(image,[recipe])

    def test_rejects_incomplete_dispatch(self):
        image,recipe=self.dispatch()
        recipe['end']=BASE+12
        with self.assertRaises(ValueError): switches(image,[recipe])

    def test_hash_validation_and_disc2_paths(self):
        with tempfile.TemporaryDirectory(prefix='fm4 source test ') as directory:
            root=Path(directory)
            names=('default.xex','module.xex')
            for name in names: (root/name).write_text('synthetic executable placeholder '+name)
            first={name:pipeline.digest(root/name) for name in names}
            second={'default.xex':'0'*64,'module.xex':first['module.xex']}
            releases={'First':{'media_id':'00000001','executables':first},
                      'Second':{'media_id':'00000002','executables':second}}
            with patch.object(pipeline,'EXECUTABLES',names),patch.object(pipeline,'RELEASES',releases):
                with self.assertRaises(ValueError): pipeline.validate_discs(root,None)
                (root/'Media').mkdir()
                (root/'Database.xmplr').write_text('synthetic placeholder')
                self.assertEqual(pipeline.validate_discs(root,None),(root.resolve(),None))
                self.assertEqual(pipeline.disc_release(root),'First')
                with self.assertRaises(ValueError): pipeline.validate_discs(root,root/'missing disc 2')
                # A set that matches no release as a whole, here one changed executable, is rejected.
                (root/'default.xex').write_text('wrong revision')
                with self.assertRaises(ValueError): pipeline.validate_discs(root,None)
                second['default.xex']=pipeline.digest(root/'default.xex')
                self.assertEqual(pipeline.disc_release(root),'Second')
                (root/'module.xex').unlink()
                with self.assertRaises(ValueError): pipeline.validate_discs(root,None)

    def package(self, path, title_id=0x4D530910, content_type=2, magic=b'LIVE'):
        # A synthetic STFS header: magic, content type and title ID, the rest zero.
        header=bytearray(0x400)
        header[:4]=magic
        struct.pack_into('>I',header,pipeline.CONTENT_TYPE_OFFSET,content_type)
        struct.pack_into('>I',header,pipeline.TITLE_ID_OFFSET,title_id)
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(header)
        return path.resolve()

    def test_content_packages(self):
        with tempfile.TemporaryDirectory(prefix='fm4 content test ') as directory:
            root=Path(directory)
            disc2=[self.package(path) for path in pipeline.disc2_packages(root/'Disc 2')]
            dlc=self.package(root/'DLC/pack one/4D53091000000010',magic=b'CON ')
            (root/'DLC/readme.txt').write_text('not a package')
            self.assertEqual(pipeline.validate_content(root/'Disc 2',None),disc2)
            self.assertEqual(pipeline.validate_content(root/'Disc 2',[root/'DLC']),disc2+[dlc])
            self.assertEqual(pipeline.validate_content(None,[dlc,root/'DLC']),[dlc])
            with self.assertRaises(ValueError): pipeline.validate_content(None,[root/'missing'])
            with self.assertRaises(ValueError): pipeline.validate_content(None,[root/'DLC/readme.txt'])
            (root/'empty').mkdir()
            with self.assertRaises(ValueError): pipeline.validate_content(None,[root/'empty'])
            other=self.package(root/'DLC/other/4D53091000000011',title_id=0x4D5307E6)
            update=self.package(root/'DLC/update/tu',content_type=0xB0000)
            # Inside a directory, packages of other titles and types are skipped; named, or
            # alone in a directory, they are rejected.
            self.assertEqual(pipeline.validate_content(None,[root/'DLC']),[dlc])
            for path in (other,update,root/'DLC/other',root/'DLC/update'):
                with self.assertRaises(ValueError): pipeline.validate_content(None,[path])
            self.package(root/'copy/4d53091000000001')
            with self.assertRaises(ValueError): pipeline.validate_content(root/'Disc 2',[root/'copy'])
            manifest=pipeline.write_content_manifest(root,[dlc])
            self.assertEqual(manifest.read_text(encoding='utf-8'),f'{dlc}\n')

    def test_look_cvars(self):
        with tempfile.TemporaryDirectory(prefix='fm4 look test ') as directory:
            root=Path(directory)
            defaults=pipeline.look_knob_defaults()
            # Every knob of the launcher is in the files, and so is the debug view.
            for name in [*pipeline.LOOK_KNOBS.values(),'FM4_LOOK_AO_DEBUG']:
                self.assertIn(name,defaults)
                float(defaults[name])
            self.assertEqual(pipeline.look_cvars(root,'original,taa=0'),{})
            # original: the game's shaders with TAA, the post passes and their params only.
            cvars=pipeline.look_cvars(root,'original')
            self.assertNotIn('shader_mod_path',cvars)
            self.assertEqual(cvars['shader_mod_defines'],'TAA=1;TAA_SHARPEN=0.0')
            self.assertEqual(Path(cvars['post_pass_path']),root/'passes')
            # pc is aces under its earlier name.
            self.assertEqual(pipeline.look_knobs('pc'),pipeline.look_knobs('aces'))
            self.assertEqual(pipeline.DEFAULT_LOOK,'aces')
            # aces, gt and neutral share their mods and differ in the tone curve only.
            mods={look:{k:v for k,v in pipeline.look_knobs(look).items() if k!='tonemap'}
                  for look in ('aces','gt','neutral')}
            self.assertEqual(mods['aces'],mods['gt'])
            self.assertEqual(mods['aces'],mods['neutral'])
            self.assertEqual([pipeline.look_knobs(look)['tonemap'] for look in ('aces','gt','neutral')],
                             ['aces','gt','neutral'])
            with self.assertRaises(RuntimeError):
                pipeline.look_knobs('filmic')
            cvars=pipeline.look_cvars(root,'pc,sharpen=0.6,taa=0')
            self.assertEqual(Path(cvars['shader_mod_path']),root/'look')
            defines=dict(define.split('=',1) for define in cvars['shader_mod_defines'].split(';'))
            self.assertEqual(defines['FM4_LOOK_SHARPEN'],'0.6')
            self.assertEqual(defines['FM4_LOOK_TONEMAP'],'2.0')
            self.assertNotIn('FM4_LOOK_VIGNETTE',defines)
            self.assertNotIn('shader_mod_hot_reload',cvars)
            # The sun knob sets runtime cvars, not a define; alone it loads no shader mods.
            self.assertNotIn('sun',''.join(defines).lower())
            self.assertEqual(cvars['rtv_occlusion_awaited_scale'],'2')
            self.assertEqual(cvars['rtv_occlusion_awaited_limit'],str(pipeline.SUN_LIMIT))
            self.assertEqual(pipeline.look_cvars(root,'original,taa=0,sun=1.5'),
                             {'rtv_occlusion_awaited_scale':'1.5',
                              'rtv_occlusion_awaited_limit':str(pipeline.SUN_LIMIT)})
            self.assertEqual(pipeline.look_cvars(root,'pc,sun=0')['rtv_occlusion_awaited_scale'],'0')
            with self.assertRaises(RuntimeError):
                pipeline.look_cvars(root,'pc,sun=-1')
            # Development: the repository files, every knob, hot reload and live knobs.
            for look in ('pc,sharpen=0.6,taa=0,windshield=0','original,taa=0'):
                cvars=pipeline.look_cvars(root,look,shader_dev=True)
                self.assertEqual(Path(cvars['shader_mod_path']),pipeline.LOOK_DIRECTORY)
                defines=dict(define.split('=',1) for define in cvars['shader_mod_defines'].split(';'))
                self.assertEqual(set(defines),set(defaults))
                self.assertEqual(cvars['shader_mod_hot_reload'],'true')
                self.assertEqual(cvars['shader_mod_live_knobs'],'true')
            self.assertEqual(defines['FM4_LOOK_SHARPEN'],repr(float(defaults['FM4_LOOK_SHARPEN'])))

    def test_post_passes(self):
        """The ao knob turns the post passes on: a copy of work/fm4-rex/passes, or with
        shader_dev the directory itself; the overlay reads GTAO's radius and quality from it."""
        with tempfile.TemporaryDirectory(prefix='fm4 passes test ') as directory:
            root=Path(directory)
            self.assertNotIn('post_pass_path',pipeline.look_cvars(root,'original,taa=0'))
            self.assertNotIn('post_pass_path',pipeline.look_cvars(root,'pc,ao=0,taa=0,windshield=0'))
            # TAA alone also turns them on.
            self.assertIn('post_pass_path',pipeline.look_cvars(root,'pc,ao=0'))
            cvars=pipeline.look_cvars(root,'pc')
            self.assertEqual(Path(cvars['post_pass_path']),root/'passes')
            for name in ('passes.toml','gtao.hlsl'):
                self.assertTrue((root/'passes'/name).is_file(),name)
            self.assertEqual(pipeline.ao_cvars(root,True),{'post_pass_path':root/'passes'})
            self.assertEqual(pipeline.ao_cvars(root,False),{})
            self.assertEqual(pipeline.look_cvars(root,'pc',shader_dev=True)['post_pass_path'],
                             pipeline.PASSES_DIRECTORY)
            self.assertNotIn('post_pass_path',pipeline.look_cvars(root,'original,taa=0',shader_dev=True))
            settings=pipeline.passes_settings(root/'passes',{})
            self.assertEqual(settings,{'GTAO_RADIUS':0.75,'GTAO_QUALITY':2.0})
            self.assertEqual(pipeline.passes_settings(root/'passes',{'GTAO_RADIUS':'1'})['GTAO_RADIUS'],
                             1.0)
            overlay=pipeline.info_overlay(cvars,{},'pc','high')
            self.assertIn('Ambient occlusion: GTAO, strength 0.6, radius 0.75 m, high',overlay)

    def test_taa(self):
        """The taa knob: the pass param TAA, the post passes on, the look's sharpening moved into
        TAA_SHARPEN, no FXAA with -SingleTile, and the overlay line."""
        with tempfile.TemporaryDirectory(prefix='fm4 taa test ') as directory:
            root=Path(directory)
            def defines_of(look):
                cvars=pipeline.look_cvars(root,look)
                return cvars,dict(d.split('=',1) for d in cvars['shader_mod_defines'].split(';'))
            cvars,defines=defines_of('pc,taa=1')
            self.assertEqual(defines['TAA'],'1')
            self.assertEqual(defines['TAA_SHARPEN'],'0.4')
            self.assertEqual(defines['FM4_LOOK_SHARPEN'],'0.0')
            self.assertEqual(defines['FM4_LOOK_TONEMAP'],'2.0')
            self.assertEqual(Path(cvars['post_pass_path']),root/'passes')
            self.assertTrue((root/'passes/taa.hlsl').is_file())
            self.assertEqual(defines_of('pc,sharpen=0.6,taa=1')[1]['TAA_SHARPEN'],'0.6')
            # Without the sharpening knob or the ao knob: no sharpening, the passes still on.
            cvars,defines=defines_of('aces,sharpen=0,ao=0,taa=1')
            self.assertEqual(defines['TAA_SHARPEN'],'0.0')
            self.assertEqual(Path(cvars['post_pass_path']),root/'passes')
            # Every preset has TAA; taa=0 gives the look's own sharpening back.
            for look in pipeline.LOOK_PRESETS:
                self.assertTrue(pipeline.taa_enabled(pipeline.look_knobs(look)),look)
            self.assertEqual(defines_of('pc')[1],defines_of('pc,taa=1')[1])
            for look in ('pc,taa=0','gt,taa=0'):
                self.assertNotIn('TAA',defines_of(look)[1])
                self.assertEqual(defines_of(look)[1]['FM4_LOOK_SHARPEN'],'0.4')
            with self.assertRaises(RuntimeError):
                pipeline.look_cvars(root,'pc,taa=-1')
            # At the scene_color point: TAA_POINT, the look keeps its sharpening, none after TAA.
            defines=defines_of('pc,taa_point=1')[1]
            self.assertEqual((defines['TAA'],defines['TAA_POINT'],defines['TAA_SHARPEN']),('1','1','0.0'))
            self.assertEqual(defines['FM4_LOOK_SHARPEN'],'0.4')
            self.assertNotIn('TAA_POINT',defines_of('pc')[1])
            self.assertNotIn('TAA',defines_of('pc,taa=0,taa_point=1')[1])
            with self.assertRaises(RuntimeError):
                pipeline.look_cvars(root,'pc,taa_point=2')
            overlay=pipeline.info_overlay(pipeline.look_cvars(root,'pc,taa_point=1'),{},
                                          'pc,taa_point=1','high')
            self.assertIn('Anti-aliasing: TAA before the post chain (scene_color)',overlay)
            self.assertEqual(pipeline.single_tile_settings('pc,taa=0'),
                             ({'FM4_TILING_SCENARIO':'2'},{'swap_post_effect':'fxaa'}))
            self.assertEqual(pipeline.single_tile_settings('pc'),({'FM4_TILING_SCENARIO':'2'},{}))
            self.assertEqual(pipeline.single_tile_settings('pc,taa=1'),({'FM4_TILING_SCENARIO':'2'},{}))
            cvars=pipeline.look_cvars(root,'pc,taa=1')
            overlay=pipeline.info_overlay(cvars,{'FM4_TILING_SCENARIO':'2'},'pc,taa=1','high')
            self.assertIn('Anti-aliasing: TAA before the HUD, sharpen 0.4, scene jittered while the '
                          'camera moves',overlay)
            self.assertIn('Quality: high, single tile|',overlay)
            self.assertNotIn('Anti-aliasing',pipeline.info_overlay(
                pipeline.look_cvars(root,'pc,taa=0'),{},'pc,taa=0','high'))

    def test_auto_single_tile(self):
        """Races render one tile without MSAA when the look has TAA, every preset, at every
        quality; -Rov, -Msaa and a look without TAA keep the tiles of the quality."""
        for look in pipeline.LOOK_PRESETS:
            self.assertTrue(pipeline.auto_single_tile(look),look)
        self.assertTrue(pipeline.auto_single_tile('pc'))
        self.assertFalse(pipeline.auto_single_tile('aces',rov=True))
        self.assertFalse(pipeline.auto_single_tile('aces',msaa=True))
        for look in ('aces,taa=0','original,taa=0'):
            self.assertFalse(pipeline.auto_single_tile(look),look)
        for quality in pipeline.QUALITY_PRESETS:
            self.assertNotIn('TilingScenario',pipeline.race_settings(quality,True))
        self.assertIn('TilingScenario=0',pipeline.race_settings('ultra'))

    def test_windshield(self):
        """The windshield knob: the pass param WINDSHIELD, the post passes on, off by default and
        in every preset but original, and the overlay line."""
        with tempfile.TemporaryDirectory(prefix='fm4 windshield test ') as directory:
            root=Path(directory)
            def defines_of(look):
                cvars=pipeline.look_cvars(root,look)
                return cvars,dict(d.split('=',1) for d in cvars['shader_mod_defines'].split(';'))
            cvars,defines=defines_of('pc,windshield=1')
            self.assertEqual(defines['WINDSHIELD'],'1.0')
            self.assertEqual(Path(cvars['post_pass_path']),root/'passes')
            self.assertTrue((root/'passes/windshield.hlsl').is_file())
            # Alone, it turns the post passes on too.
            cvars,defines=defines_of('aces,ao=0,windshield=0.5')
            self.assertEqual(defines['WINDSHIELD'],'0.5')
            self.assertEqual(Path(cvars['post_pass_path']),root/'passes')
            # pc has it at 1 since 20 October 2026, aces, gt and neutral since 20 October 2026.
            for look in pipeline.LOOK_PRESETS:
                self.assertEqual(pipeline.look_knobs(look).get('windshield'),
                                 None if look=='original' else '1')
            self.assertEqual(defines_of('pc')[1]['WINDSHIELD'],'1.0')
            self.assertNotIn('WINDSHIELD',defines_of('pc,windshield=0')[1])
            self.assertNotIn('post_pass_path',pipeline.look_cvars(root,'aces,ao=0,taa=0,windshield=0'))
            for value in ('-1','2.5'):
                with self.assertRaises(RuntimeError):
                    pipeline.look_cvars(root,f'pc,windshield={value}')
            overlay=pipeline.info_overlay(pipeline.look_cvars(root,'pc,windshield=1'),{},
                                          'pc,windshield=1','high')
            self.assertIn('Cockpit glass: strength 1',overlay)
            self.assertIn('Cockpit glass: strength 1',pipeline.info_overlay(
                pipeline.look_cvars(root,'pc'),{},'pc','high'))
            self.assertNotIn('Cockpit glass',pipeline.info_overlay(
                pipeline.look_cvars(root,'pc,windshield=0'),{},'pc,windshield=0','high'))

    def test_look_track_settings(self):
        with tempfile.TemporaryDirectory(prefix='fm4 look test ') as directory:
            root=Path(directory)
            knobs=pipeline.look_knobs('pc,road_gloss=0.5,sky=0.8')
            self.assertEqual(pipeline.look_track_settings(knobs),
                             'RoadSpecular.RoadSpecular*=0.5;RoadSpecular.RoadSpecular2*=0.5;'
                             'skyScale.value*=0.8')
            # The presets have sky 0.7 (the 2x sweep of 18 October 2026, pc's since then, aces, gt
            # and neutral since 20 October 2026); original keeps the sky.
            for look in ('pc','aces','gt','neutral'):
                self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs(look)),'skyScale.value*=0.7')
            self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs('original')),'')
            # A knob at 1 edits nothing, so the files are not copied at all.
            self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs('pc,sky=1')),'')
            # Track knobs are not shader defines, and alone they need no shader mods.
            cvars=pipeline.look_cvars(root,'pc,road_gloss=0.5')
            self.assertNotIn('road_gloss',cvars['shader_mod_defines'])
            self.assertEqual(pipeline.look_cvars(root,'original,taa=0,sky=0.8'),{})
            with self.assertRaises(RuntimeError):
                pipeline.look_track_settings({'sky':'-1'})
            overlay=pipeline.info_overlay({},{'FM4_TRACK_SETTINGS':pipeline.look_track_settings(knobs)},
                                          'pc,road_gloss=0.5,sky=0.8','high')
            self.assertIn('Track lighting: road gloss x0.5, sky x0.8',overlay)

    def test_look_nature_knobs(self):
        with tempfile.TemporaryDirectory(prefix='fm4 look test ') as directory:
            root=Path(directory)
            # sky edits skyScale in the track lighting files; the sky_* and foliage_* knobs are
            # defines of fm4-nature.hlsli.
            look='pc,sky=0.8,sky_saturation=1.2,sky_horizon=0.5,foliage_hue=-30'
            self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs(look)),
                             'skyScale.value*=0.8')
            defines=dict(define.split('=',1)
                         for define in pipeline.look_cvars(root,look)['shader_mod_defines'].split(';'))
            self.assertEqual(defines['FM4_LOOK_SKY_SATURATION'],'1.2')
            self.assertEqual(defines['FM4_LOOK_SKY_HORIZON'],'0.5')
            self.assertEqual(defines['FM4_LOOK_FOLIAGE_HUE'],'-30.0')
            self.assertNotIn('FM4_LOOK_FOLIAGE_SATURATION',defines)
            # Every knob of the look files fits the 64 live knob slots (shader_mod_live_knobs,
            # ShaderOverrides::kKnobCount).
            self.assertLessEqual(len(pipeline.look_knob_defaults()),64)

    def test_look_road_knobs(self):
        with tempfile.TemporaryDirectory(prefix='fm4 look test ') as directory:
            root=Path(directory)
            # road_gloss edits the track lighting files; the road_* shader knobs are defines of
            # fm4-road.hlsli, with the original look as their defaults.
            look=('pc,road_gloss=0.5,road_specular=0,road_sheen=1,road_roughness=0.4,road_contrast=0.3,'
                  'road_sheen_grain=2')
            self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs(look)),
                             'RoadSpecular.RoadSpecular*=0.5;RoadSpecular.RoadSpecular2*=0.5;'
                             'skyScale.value*=0.7')
            defines=dict(define.split('=',1)
                         for define in pipeline.look_cvars(root,look)['shader_mod_defines'].split(';'))
            self.assertEqual(defines['FM4_LOOK_ROAD_SPECULAR'],'0.0')
            self.assertEqual(defines['FM4_LOOK_ROAD_SHEEN'],'1.0')
            self.assertEqual(defines['FM4_LOOK_ROAD_ROUGHNESS'],'0.4')
            self.assertEqual(defines['FM4_LOOK_ROAD_CONTRAST'],'0.3')
            self.assertEqual(defines['FM4_LOOK_ROAD_SHEEN_GRAIN'],'2.0')
            self.assertNotIn('FM4_LOOK_ROAD_BRIGHTNESS',defines)
            self.assertNotIn('road_gloss',pipeline.look_cvars(root,look)['shader_mod_defines'])
            knob_defaults=pipeline.look_knob_defaults()
            for name,value in (('FM4_LOOK_ROAD_BRIGHTNESS','1.0'),('FM4_LOOK_ROAD_CONTRAST','0.0'),
                               ('FM4_LOOK_ROAD_SPECULAR','1.0'),('FM4_LOOK_ROAD_SHEEN','0.0'),
                               ('FM4_LOOK_ROAD_SHEEN_GRAIN','1.0'),('FM4_LOOK_ROAD_DEBUG','0.0')):
                self.assertEqual(knob_defaults[name],value)
            # Every road knob of the look files has a launcher name.
            self.assertTrue({name for name in knob_defaults if name.startswith('FM4_LOOK_ROAD_')}
                            <=set(pipeline.LOOK_KNOBS.values()))

    def test_look_cockpit_knob(self):
        with tempfile.TemporaryDirectory(prefix='fm4 look test ') as directory:
            root=Path(directory)
            # cockpit_light is a define of fm4-cockpit.hlsli: 0, the game's interior, by default;
            # pc has 0.5 since 20 October 2026, aces, gt and neutral since 20 October 2026.
            defines=dict(define.split('=',1) for define in
                         pipeline.look_cvars(root,'pc,cockpit_light=0.3')['shader_mod_defines'].split(';'))
            self.assertEqual(defines['FM4_LOOK_COCKPIT_LIGHT'],'0.3')
            self.assertIn('FM4_LOOK_COCKPIT_LIGHT=0.5',pipeline.look_cvars(root,'pc')['shader_mod_defines'])
            # aces, gt and neutral too since 20 October 2026; original not.
            for look in pipeline.LOOK_PRESETS:
                self.assertEqual(pipeline.look_knobs(look).get('cockpit_light'),
                                 None if look=='original' else '0.5')
            self.assertEqual(pipeline.look_knob_defaults()['FM4_LOOK_COCKPIT_LIGHT'],'0.0')

    def test_one_pass_msaa(self):
        """-Native -MsaaSamples 2 runs races in one pass with 2x MSAA: the title's 2x scenario
        made one tile, the runtime's private scene depth, and the native cvars; without -Native
        it is refused."""
        with tempfile.TemporaryDirectory(prefix='fm4 one pass test ') as directory:
            root=Path(directory)
            cvars,env,_=pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',native=True,msaa_samples=2,
                                             monitor=(1920,1080,1.0))
            self.assertEqual(env['FM4_TILING_SCENARIO'],'1')
            self.assertEqual(env['FM4_ONE_PASS_MSAA'],'2')
            self.assertNotIn('TilingScenario',env.get('FM4_RACE_SETTINGS',''))
            self.assertEqual(cvars['native_one_pass_msaa'],'2')
            self.assertEqual(cvars['host_render_thread'],'true')
            cvars,env,_=pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',monitor=(1920,1080,1.0))
            self.assertNotIn('FM4_ONE_PASS_MSAA',env)
            self.assertNotIn('native_one_pass_msaa',cvars)
            with self.assertRaises(ValueError):
                pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',msaa_samples=2,
                                     monitor=(1920,1080,1.0))

    def test_full_rate(self):
        """Replays and the countdown run at the race frame rate (FM4_FULL_RATE), shared with
        e2e.py, and the overlay says so; without the variable the line is gone."""
        env=pipeline.launch_env()
        self.assertEqual(env['FM4_FULL_RATE'],'replay,countdown')
        with tempfile.TemporaryDirectory(prefix='fm4 full rate test ') as directory:
            cvars=pipeline.look_cvars(Path(directory),'original')
            overlay=pipeline.info_overlay(cvars,{**env,'FM4_FRAME_RATE':'240'},'original','high')
            self.assertIn('Replay and countdown: 240 fps (post-race and photo mode 30)',overlay)
            overlay=pipeline.info_overlay(cvars,{'FM4_FULL_RATE':''},'original','high')
            self.assertNotIn('Replay and countdown',overlay)

    def test_new_clone_has_all_source_inputs(self):
        for name in ('rexglue.cmake','guard-generated.py','dump-image.cpp','switch-recipes.json'):
            self.assertTrue((pipeline.HERE/name).is_file(),name)
        for name in ('CMakeLists.txt','thirdparty/CMakeLists.txt','src/graphics/shaders/fm4-resolve/resolve.xesli'):
            self.assertTrue((pipeline.RUNTIME/name).is_file(),name)
        for recipe in json.loads((pipeline.HERE/'switch-recipes.json').read_text()):
            self.assertEqual(set(recipe),{'start','end','address','register','count'})
            self.assertNotIn('labels',recipe)

    def test_clear_gray(self):
        """The e2e clear color check: smooth neutral grays that pass 102 (asphalt under TAA) are no
        missing geometry; 102 over 1% of the capture more than the grays around it is."""
        width,height=200,100
        # Neutral grays 92-112 in turn, so 102 alone covers about 4.8% of the image.
        smooth=b''.join(bytes([92+(x+y)%21]*3) for y in range(height) for x in range(width))
        self.assertGreater(smooth.count(bytes([102]*3)),0.01*width*height)
        self.assertFalse(is_clear_gray(smooth,width,height))
        for rows in (3,50):
            missing=bytearray(smooth)
            missing[-rows*width*3:]=bytes([102]*3)*(rows*width)
            self.assertTrue(is_clear_gray(bytes(missing),width,height),rows)
        self.assertFalse(is_clear_gray(bytes([30,30,30])*width*height,width,height))

if __name__=='__main__': unittest.main()
