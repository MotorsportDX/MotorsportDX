"""Synthetic tests: no FM4 bytes or original game assets are needed."""
import json
import os
import re
from pathlib import Path
import struct
import tempfile
import time
import unittest
from unittest.mock import patch

import pipeline
from prepare import switches, BASE
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'e2e'))
from e2e import is_clear_gray, washed_out_bands

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
            # aa=fsr: FSR at the scene_color point, the look's sharpening as its RCAS.
            defines=defines_of('pc,aa=fsr')[1]
            self.assertEqual((defines['FSR'],defines['FSR_SHARPNESS'],defines['TAA'],
                              defines['TAA_POINT'],defines['FM4_LOOK_SHARPEN']),
                             ('1','0.4','1','1','0.0'))
            self.assertNotIn('FSR',defines_of('pc,aa=taa')[1])
            # Upscaling sharpens more: 0.4 times the ratio above 1; fsr_sharpen overrides it.
            sharpness=lambda look,ratio: pipeline.fsr_sharpness(pipeline.look_knobs(look),ratio)
            self.assertEqual([sharpness('pc,aa=fsr',ratio) for ratio in (1.0,1.25,1.5,1.7,2.0)],
                             [0.4,0.5,0.6,0.68,0.8])
            self.assertEqual(sharpness('pc,aa=fsr,sharpen=0.9',2.0),1.0)
            self.assertEqual(sharpness('pc,aa=fsr,fsr_sharpen=0.3',1.5),0.3)
            self.assertEqual(defines_of('pc,aa=fsr,fsr_sharpen=0.7')[1]['FSR_SHARPNESS'],'0.7')
            # aa=taau: TAA at the scene_color point, the upscaler while upscaling (with FSR's
            # sharpening as TAA_SHARPEN and the jitter of every frame).
            self.assertEqual(defines_of('pc,aa=taau')[1],defines_of('pc,taa_point=1')[1])
            taau=dict(d.split('=',1) for d in pipeline.look_defines(pipeline.look_knobs('pc,aa=taau'),1.5))
            self.assertEqual((taau['TAA_UPSCALE'],taau['TAA_SHARPEN'],taau['TAA_POINT'],
                              taau['FM4_LOOK_SHARPEN'],taau['TAA_JITTER_MOTION'],
                              taau['TAA_JITTER_RATE']),('1','0.6','1','0.0','0.0','0.0'))
            self.assertNotIn('FSR',taau)
            with self.assertRaises(RuntimeError):
                pipeline.look_cvars(root,'pc,taa=0,aa=taau')
            with self.assertRaises(RuntimeError):
                pipeline.look_cvars(root,'pc,aa=fsr,fsr_sharpen=1.5')
            self.assertEqual(pipeline.scene_upscale_ratio({'native_scene_input_scale':'1',
                                                           'native_draw_scale':'3/2'}),1.5)
            self.assertEqual(pipeline.scene_upscale_ratio({'native_draw_scale':'3/2'}),1.0)
            for look in ('pc,aa=dlss','pc,taa=0,aa=fsr'):
                with self.assertRaises(RuntimeError):
                    pipeline.look_cvars(root,look)
            overlay=pipeline.info_overlay(pipeline.look_cvars(root,'pc,aa=fsr'),{},'pc,aa=fsr','high')
            self.assertIn('Anti-aliasing: FSR 3.1 before the post chain (scene_color), sharpen 0.4, '
                          'scene jittered every frame',overlay)
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
            # cockpit_fill and cockpit_light are defines of fm4-cockpit.hlsli: 0, the game's
            # interior, by default. The presets have cockpit_fill=1 since 26 October 2026 and no
            # longer cockpit_light (0.5 from 20 October 2026); original has neither.
            defines=dict(define.split('=',1) for define in
                         pipeline.look_cvars(root,'pc,cockpit_light=0.3,cockpit_fill=0.5')
                         ['shader_mod_defines'].split(';'))
            self.assertEqual(defines['FM4_LOOK_COCKPIT_LIGHT'],'0.3')
            self.assertEqual(defines['FM4_LOOK_COCKPIT_FILL'],'0.5')
            pc=pipeline.look_cvars(root,'pc')['shader_mod_defines'].split(';')
            self.assertIn('FM4_LOOK_COCKPIT_FILL=1.0',pc)
            self.assertFalse([define for define in pc if define.startswith('FM4_LOOK_COCKPIT_LIGHT')])
            for look in pipeline.LOOK_PRESETS:
                self.assertEqual(pipeline.look_knobs(look).get('cockpit_fill'),
                                 None if look=='original' else '1')
                self.assertIsNone(pipeline.look_knobs(look).get('cockpit_light'))
            self.assertEqual(pipeline.look_knob_defaults()['FM4_LOOK_COCKPIT_LIGHT'],'0.0')
            self.assertEqual(pipeline.look_knob_defaults()['FM4_LOOK_COCKPIT_FILL'],'0.0')

    def test_one_pass_msaa(self):
        """-Native runs races in one pass with 2x MSAA by default: the title's 2x scenario made
        one tile, the runtime's private scene depth, and the native cvars with the sized host
        render targets (bit 8). -MsaaSamples 4 runs scenario 3 in one pass with 4x MSAA.
        -MsaaSamples 0 and -SingleTile keep one tile without MSAA, -Msaa the tiles of -Quality;
        without -Native or with -Rov, -MsaaSamples is refused."""
        with tempfile.TemporaryDirectory(prefix='fm4 one pass test ') as directory:
            root=Path(directory)
            for samples in (None,2):
                cvars,env,_=pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',native=True,
                                                 msaa_samples=samples,monitor=(1920,1080,1.0))
                self.assertEqual(env['FM4_TILING_SCENARIO'],'1')
                self.assertEqual(env['FM4_ONE_PASS_MSAA'],'2')
                self.assertNotIn('TilingScenario',env.get('FM4_RACE_SETTINGS',''))
                self.assertEqual(cvars['native_one_pass_msaa'],'2')
                # A 1080p screen's own scale, 3/2 (test_screen_scale).
                self.assertEqual(cvars['native_rt_surfaces'],'122')
                self.assertEqual(cvars['native_draw_scale'],'3/2')
                self.assertEqual(cvars['host_render_thread'],'true')
            cvars,env,_=pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',native=True,
                                             msaa_samples=4,monitor=(1920,1080,1.0))
            self.assertEqual(env['FM4_TILING_SCENARIO'],'3')
            self.assertEqual(env['FM4_ONE_PASS_MSAA'],'4')
            self.assertEqual(cvars['native_one_pass_msaa'],'4')
            self.assertEqual(cvars['native_rt_surfaces'],'26')
            for options in ({'msaa_samples':0},{'single_tile':True}):
                cvars,env,_=pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',native=True,
                                                 monitor=(1920,1080,1.0),**options)
                self.assertEqual(env['FM4_TILING_SCENARIO'],'2')
                self.assertNotIn('FM4_ONE_PASS_MSAA',env)
                self.assertNotIn('native_one_pass_msaa',cvars)
                self.assertEqual(cvars['native_rt_surfaces'],'2')
            cvars,env,_=pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',native=True,msaa=True,
                                             monitor=(1920,1080,1.0))
            self.assertNotIn('FM4_ONE_PASS_MSAA',env)
            self.assertNotIn('host_render_thread',cvars)
            cvars,env,_=pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',monitor=(1920,1080,1.0))
            self.assertNotIn('FM4_ONE_PASS_MSAA',env)
            self.assertNotIn('native_one_pass_msaa',cvars)
            for samples in (0,2,4):
                with self.assertRaises(ValueError):
                    pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',msaa_samples=samples,
                                         monitor=(1920,1080,1.0))
                with self.assertRaises(ValueError):
                    pipeline.launch_plan(root,r'C:\FM4 test\Disc 1',msaa_samples=samples,native=True,
                                         rov=True,monitor=(1920,1080,1.0))

    def test_screen_scale(self):
        """The native path in one pass with 2x MSAA renders at the screen's own scale, the largest
        16:9 picture that fits (d3d-hle-n5.md 18.19): a fraction sets native_draw_scale, its whole
        part draw_resolution_scale and bit 32 of native_rt_surfaces. Integrated GPUs keep 1x
        unless -ResolutionScale panel; -Supersample takes the whole part plus one; RTV, -Rov and
        one pass 4x keep the whole multiple that covers the screen."""
        self.assertEqual(pipeline.screen_scale(1920,1080),(3,2))
        self.assertEqual(pipeline.screen_scale(2560,1440),(2,1))
        self.assertEqual(pipeline.screen_scale(2560,1600),(2,1))
        self.assertEqual(pipeline.screen_scale(2880,1800),(9,4))
        self.assertEqual(pipeline.screen_scale(3840,2160),(3,1))
        self.assertEqual(pipeline.screen_scale(1280,800),(1,1))
        self.assertEqual(pipeline.screen_scale(1024,768),(1,1))
        self.assertEqual(pipeline.screen_scale(1366,768),(16,15))
        disc=r'C:\FM4 test\Disc 1'
        def plan(monitor,**options):
            with tempfile.TemporaryDirectory(prefix='fm4 screen scale test ') as directory:
                return pipeline.launch_plan(Path(directory),disc,monitor=monitor,**options)
        def scales(cvars):
            return (cvars.get('draw_resolution_scale_x'),cvars.get('native_draw_scale'),
                    cvars.get('native_rt_surfaces'))
        cvars,_,description=plan((1920,1080,1.0),native=True)
        self.assertEqual(scales(cvars),(None,'3/2','122'))
        self.assertIn('render 3/2 (1920x1080)',description)
        cvars,_,description=plan((2560,1440,1.0),native=True)
        self.assertEqual(scales(cvars),('2',None,'90'))
        # 16:10: Vert+, 1280x800 (d3d-hle-n5.md 18.23), the whole 2 through the scene draws' path.
        cvars,_,description=plan((2560,1600,1.5),native=True)
        self.assertEqual(scales(cvars),('2','2','122'))
        self.assertEqual(cvars['native_draw_scale_variant'],'true')
        self.assertEqual(cvars['native_logical_height'],'800')
        self.assertNotIn('native_logical_width',cvars)
        self.assertIn('render 2x (2560x1600), logical 1280x800 Vert+',description)
        self.assertNotIn('boxed',description)
        cvars,_,description=plan((2880,1800,2.0),native=True)
        self.assertEqual(scales(cvars),('2','9/4','122'))
        self.assertIn('render 9/4 (2880x1800), logical 1280x800 Vert+',description)
        # 21:9: Hor+, 1720x720.
        cvars,_,description=plan((3440,1440,1.0),native=True)
        self.assertEqual(scales(cvars),('2','2','122'))
        self.assertEqual(cvars['native_logical_width'],'1720')
        self.assertIn('render 2x (3440x1440), logical 1720x720 Hor+',description)
        cvars,_,description=plan((2560,1080,1.0),native=True)
        self.assertEqual(cvars['native_logical_width'],'1704')
        self.assertIn('render 3/2 (2556x1080), logical 1704x720 Hor+, pillarboxed',description)
        self.assertEqual(pipeline.logical_view(1920,1200),(1280,800,3,2))
        self.assertEqual(pipeline.logical_view(1920,1080),(1280,720,3,2))
        self.assertEqual(pipeline.logical_view(3840,2160),(1280,720,3,1))
        cvars,_,_=plan((3840,2160,2.0),native=True)
        self.assertEqual(scales(cvars),('3',None,'90'))
        cvars,_,_=plan((1920,1080,1.0),native=True,supersample=True)
        self.assertEqual(scales(cvars),('2',None,'90'))
        integrated=(1920,1080,1.5,120,48,120,True)
        cvars,_,description=plan(integrated)
        self.assertEqual(scales(cvars),(None,None,'90'))
        self.assertIn('render 1x (1280x720) for an integrated GPU',description)
        cvars,_,description=plan((2560,1600,1.5,240,60,240,True))
        self.assertNotIn('native_logical_height',cvars)
        self.assertIn('render 1x (1280x720) for an integrated GPU, 16:9 letterboxed',description)
        cvars,_,description=plan(integrated,resolution_scale='panel')
        self.assertEqual(scales(cvars),(None,'3/2','122'))
        cvars,_,_=plan((1920,1080,1.0),native=True,msaa_samples=4)
        self.assertEqual(scales(cvars),('2',None,'26'))
        cvars,_,description=plan((1920,1080,1.0))
        self.assertEqual(scales(cvars),('2',None,None))
        self.assertNotIn('letterboxed',description)
        cvars,_,_=plan((1920,1080,1.0),rov=True)
        self.assertEqual(scales(cvars),('2',None,None))
        for options in ({},{'rov':True},{'native':True,'single_tile':True}):
            with self.assertRaises(ValueError):
                plan((1920,1080,1.0),resolution_scale='panel',**options)

    def test_upscale(self):
        """-Upscale (fsr-plan.md 11): the scene at the output's lines over the ratio, at least 720
        (lower is clamped and said so), the other surfaces at its whole part; native changes
        nothing; integrated GPUs upscale to the screen's own scale; it needs aa=fsr and the
        native path in one pass with 2x MSAA."""
        disc=r'C:\FM4 test\Disc 1'
        def plan(monitor,upscale,look='pc,aa=fsr',**options):
            with tempfile.TemporaryDirectory(prefix='fm4 upscale test ') as directory:
                return pipeline.launch_plan(Path(directory),disc,monitor=monitor,look=look,
                                            upscale=upscale,**options)
        def scales(cvars):
            return (cvars.get('native_scene_input_scale'),cvars.get('native_draw_scale'),
                    cvars.get('draw_resolution_scale_x'))
        uhd,qhd,fhd=(3840,2160,2.0),(2560,1440,1.0),(1920,1080,1.0)
        for monitor,mode,expected,text in (
                (uhd,'quality',('2','3','2'),'FSR quality 1440p -> 2160p'),
                (uhd,'balanced',('1271/720','3','1'),'FSR balanced 1271p -> 2160p'),
                (uhd,'performance',('3/2','3','1'),'FSR performance 1080p -> 2160p'),
                (qhd,'quality',('4/3','2','1'),'FSR quality 960p -> 1440p'),
                (qhd,'balanced',('847/720','2','1'),'FSR balanced 847p -> 1440p'),
                (qhd,'performance',('1','2','1'),'FSR performance 720p -> 1440p'),
                (fhd,'quality',('1','3/2','1'),'FSR quality 720p -> 1080p'),
                (fhd,'performance',('1','3/2','1'),'FSR performance 720p -> 1080p (clamped to 720p)'),
                (fhd,'1.25',('6/5','3/2','1'),'FSR 1.25 864p -> 1080p')):
            cvars,_,description=plan(monitor,mode,native=True)
            self.assertEqual(scales(cvars),expected,(monitor,mode))
            self.assertEqual(cvars['native_rt_surfaces'],'122')
            self.assertIn(text,description)
        # The quality mode's RCAS: the look's 0.4 plus 0.2 at ratio 1.5.
        cvars,_,_=plan(fhd,'quality',native=True)
        self.assertIn('FSR_SHARPNESS=0.6',cvars['shader_mod_defines'])
        cvars,_,description=plan(fhd,'quality',native=True,look='pc,aa=taau')
        self.assertEqual(scales(cvars),('1','3/2','1'))
        self.assertIn('TAAU quality 720p -> 1080p',description)
        self.assertIn('TAA_UPSCALE=1',cvars['shader_mod_defines'])
        # Entry 0 at the scene's scale with -Native in one pass 2x (bit 64), k without it.
        cvars,env,_=plan(fhd,'native',native=True,look='pc')
        self.assertEqual((env.get('FM4_ONE_PASS_ENTRY0'),cvars['native_rt_surfaces']),('4','122'))
        cvars,env,_=plan(fhd,'native',native=True,look='pc',entry0='k')
        self.assertEqual((env.get('FM4_ONE_PASS_ENTRY0'),cvars['native_rt_surfaces']),(None,'58'))
        cvars,env,_=plan(fhd,'native',rov=True,look='pc')
        self.assertNotIn('FM4_ONE_PASS_ENTRY0',env)
        with self.assertRaises(ValueError):
            plan(fhd,'native',native=True,look='pc',entry0='x')
        native,_,description=plan(fhd,'native',native=True)
        self.assertEqual(scales(native),(None,'3/2',None))
        self.assertNotIn('FSR',description)
        cvars,_,description=plan((1920,1080,1.5,120,48,120,True),'quality')
        self.assertEqual(scales(cvars),('1','3/2','1'))
        self.assertNotIn('integrated GPU,',description)
        cvars,_,description=plan((1280,720,1.0),'quality',native=True,display='720p')
        self.assertNotIn('native_scene_input_scale',cvars)
        self.assertIn('upscale quality: none at 720p (clamped to 720p)',description)
        for monitor,mode,options in ((fhd,'quality',{'native':True,'look':'pc'}),
                                     (fhd,'quality',{'rov':True}),
                                     (fhd,'quality',{'native':True,'msaa_samples':4}),
                                     (fhd,'ultra',{'native':True}),(fhd,'4',{'native':True})):
            with self.assertRaises(ValueError):
                plan(monitor,mode,**options)

    def test_vrr_range_and_vsync(self):
        # The main PC's panel: a DisplayID 2.0 extension with Adaptive-Sync descriptors of 60 to
        # 240 Hz and 50 to 60 Hz.
        base = bytearray(128)
        base[18:20] = b'\x01\x04'
        displayid = bytearray(128)
        displayid[0:5] = bytes([0x70, 0x20, 0x20, 0x00, 0x00])
        displayid[5:20] = bytes([0x2B, 0x00, 0x0C, 0x27, 0x00, 0x3C, 0xEF, 0x00, 0x00,
                                 0x27, 0x00, 0x32, 0x3B, 0x00, 0x00])
        self.assertEqual(pipeline.vrr_range(bytes(base + displayid)), (60, 240))
        # EDID 1.4 range limits of 48 to 120 Hz count only with the continuous frequency bit.
        base[0x18] = 0
        base[54:72] = bytes([0, 0, 0, 0xFD, 0, 48, 120, 30, 140, 30]) + bytes(8)
        self.assertEqual(pipeline.vrr_range(bytes(base)), (0, 0))
        base[0x18] = 1
        self.assertEqual(pipeline.vrr_range(bytes(base)), (48, 120))
        ally = (1920, 1080, 1.5, 120, 48, 120)
        self.assertEqual(pipeline.present_settings('auto', 120, ally),
                         ({'present_min_interval_us': '8333'}, 117,
                          'VSync auto: VRR 48-120 Hz, presents at most 120 a second, races at 117 fps'))
        self.assertEqual(pipeline.present_settings('auto', None, (1920, 1080, 1.0, 60, 0, 0))[0],
                         {'present_vsync': 'true'})
        self.assertEqual(pipeline.present_settings('off', 240, ally), ({}, 240, 'VSync off'))

    def test_keyboard_cvars(self):
        # The launchers' defaults set nothing beyond launch_cvars, which turns the model on.
        self.assertEqual(pipeline.keyboard_cvars(None), {})
        self.assertEqual(pipeline.keyboard_cvars('smooth=1,steer_attack=0.30,precision=0.5'), {})
        self.assertEqual(pipeline.keyboard_cvars('smooth=0'), {'mnk_smooth': 'false'})
        self.assertEqual(pipeline.keyboard_cvars('steer_attack=0.4, title_deadzones=0,mouse_steering=1'),
                         {'mnk_steer_attack': '0.4', 'mnk_title_deadzones': 'false',
                          'mnk_mouse_steering': 'true'})
        self.assertEqual(pipeline.keyboard_cvars('precision=0.7,precision=0.5'), {})
        cvars = pipeline.launch_cvars(Path('root'), 'disc', Path('root/logs/runtime.log'))
        self.assertEqual(cvars['mnk_smooth'], 'true')
        self.assertEqual(cvars['keybind_precision'], 'Control')
        self.assertEqual(cvars['keybind_lstick_up'], '')
        self.assertEqual(cvars['keybind_start'], 'Escape,Return')
        keys = [key.strip() for name, value in cvars.items() if name.startswith('keybind_')
                for key in value.split(',') if key.strip()]
        self.assertEqual(len(keys), len(set(keys)), 'a key bound twice')
        for bad, message in (('steer_attack=3', 'number from 0 to 2'), ('smooth=2', 'one of 0, 1'),
                             ('speed=1', 'Unknown -Keyboard knob')):
            with self.assertRaises(ValueError) as caught:
                pipeline.keyboard_cvars(bad)
            self.assertIn(message, str(caught.exception))

    def test_pad_cvars(self):
        # The launchers' defaults: rumble 1.5 over the runtime's 1, nothing else.
        self.assertEqual(pipeline.pad_cvars(None), {'pad_rumble_strength': '1.5'})
        # The runtime's defaults set no cvar, also rumble 1 against the launchers' 1.5.
        self.assertEqual(pipeline.pad_cvars('driver=default,rumble=1.0,thread=0,misc1=guide'), {})
        self.assertEqual(pipeline.pad_cvars('driver=rawinput, trigger_rumble=1,rumble=2'),
                         {'sdl_joystick_driver': 'rawinput', 'pad_trigger_rumble': 'true',
                          'pad_rumble_strength': '2.0'})
        self.assertEqual(pipeline.pad_cvars('driver=rawinput'),
                         {'pad_rumble_strength': '1.5', 'sdl_joystick_driver': 'rawinput'})
        self.assertEqual(pipeline.pad_cvars('driver=gameinput,rumble=1'),
                         {'sdl_joystick_driver': 'gameinput'})
        # Later items win, also back to a default.
        self.assertEqual(pipeline.pad_cvars('rumble=2,anti_deadzone=1,rumble=1'),
                         {'pad_steering_anti_deadzone': 'true'})
        self.assertEqual(pipeline.pad_cvars('anti_deadzone=1,stick_deadzone=0.05'),
                         {'pad_rumble_strength': '1.5', 'pad_steering_anti_deadzone': 'true',
                          'pad_stick_deadzone': '0.05'})
        self.assertEqual(pipeline.pad_cvars('rumble=2,rumble=1.5'), {'pad_rumble_strength': '1.5'})
        self.assertEqual(pipeline.pad_cvars('rumble=1,right_paddle1=none,touchpad=back'),
                         {'pad_right_paddle1': 'none', 'pad_touchpad': 'back'})
        for bad in ('rumble=2.5', 'stick_outer=0.4', 'driver=xinput', 'thread=on', 'misc1=z',
                    'rumble', 'paddle1=a', 'rumble=nan'):
            with self.assertRaises(ValueError, msg=bad):
                pipeline.pad_cvars(bad)
        # Every knob's cvar and runtime default match the runtime's (the launcher reads this
        # table).
        runtime = Path(__file__).resolve().parents[2]/'work/runtime/src/input'
        sources = ((runtime/'pad_settings.cpp').read_text(encoding='utf-8') +
                   (runtime/'sdl/sdl_input_driver.cpp').read_text(encoding='utf-8'))
        for name, knob in pipeline.PAD_KNOBS.items():
            match = re.search(rf"\({knob['cvar']}, ([^,]+),", sources)
            self.assertIsNotNone(match, name)
            value = {'true': '1', 'false': '0'}.get(match.group(1).strip(),
                                                    match.group(1).strip().strip('"'))
            expected = knob.get('runtime_default', knob['default'])
            if 'choices' in knob:
                self.assertEqual(value, expected, name)
            else:
                self.assertEqual(float(value), float(expected), name)
            # A string cvar's allowed values are the knob's choices.
            if 'choices' in knob and knob['choices'] != ['0', '1']:
                definition = sources[match.start():].split('REXCVAR_DEFINE', 1)[0]
                allowed = re.search(r'\.allowed\(\{([^}]*)\}\)', definition)
                self.assertIsNotNone(allowed, name)
                self.assertEqual(re.findall(r'"([^"]*)"', allowed.group(1)), knob['choices'], name)

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

    def test_copy_source_replaces_newer_destination(self):
        # Another checkout compiled its own, newer version of a file: older source content
        # replaces it with the current time, so the build recompiles; equal content keeps the time.
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            source,destination=directory/'source.cpp',directory/'mirror.cpp'
            source.write_text('old')
            destination.write_text('newer elsewhere')
            os.utime(source,(1_000_000,1_000_000))
            os.utime(destination,(2_000_000_000,2_000_000_000))
            before=time.time()
            pipeline.copy_source(source,destination)
            self.assertEqual(destination.read_text(),'old')
            self.assertGreaterEqual(destination.stat().st_mtime,before-2)
            self.assertLess(destination.stat().st_mtime,2_000_000_000)
            os.utime(destination,(1_500_000_000,1_500_000_000))
            pipeline.copy_source(source,destination)
            self.assertEqual(destination.stat().st_mtime,1_500_000_000)

    def test_source_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'runtime-source/src').mkdir(parents=True)
            (root/'runtime-source/src/a.cpp').write_text('a')
            (root/'runtime-source/.fm4-runtime-files.json').write_text(json.dumps(['src/a.cpp']))
            (root/'project/src').mkdir(parents=True)
            (root/'project/src/b.cpp').write_text('b')
            state=pipeline.source_state(root)
            self.assertEqual(set(state),{'head','dirty','tree'})
            (root/'project/src/b.cpp').write_text('changed')
            self.assertNotEqual(pipeline.source_state(root)['tree'],state['tree'])

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

    def test_washed_out_bands(self):
        """A race capture with detail everywhere has no washed-out band; one whose upper half is
        flat white (a build that lost the scene) has bands 0 and 1; a bright band with detail
        (a sunlit sky with clouds) is not washed out."""
        width,height=640,360
        detail=b''.join(bytes([(x*7+y*3)%256]*3) for y in range(height) for x in range(width))
        self.assertEqual(washed_out_bands(detail,width,height),[])
        half=bytearray(detail)
        half[:height//2*width*3]=bytes([250,248,240])*(height//2*width)
        self.assertEqual(washed_out_bands(bytes(half),width,height),[0,1])
        bright=bytearray(detail)
        bright[:height//4*width*3]=b''.join(bytes([200+(x%2)*55]*3) for y in range(height//4) for x in range(width))
        self.assertEqual(washed_out_bands(bytes(bright),width,height),[])

if __name__=='__main__': unittest.main()
