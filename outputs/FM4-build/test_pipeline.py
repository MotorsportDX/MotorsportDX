"""Synthetic tests: no FM4 bytes or original game assets are needed."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import pipeline
from prepare import switches, BASE

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
            executable=root/'default.xex'
            executable.write_text('synthetic executable placeholder')
            with patch.object(pipeline,'XEX_HASHES',{'default.xex':pipeline.digest(executable)}):
                with self.assertRaises(ValueError): pipeline.validate_discs(root,None)
                (root/'Media').mkdir()
                (root/'Database.xmplr').write_text('synthetic placeholder')
                self.assertEqual(pipeline.validate_discs(root,None),(root.resolve(),None))
                with self.assertRaises(ValueError): pipeline.validate_discs(root,root/'missing disc 2')
                executable.write_text('wrong revision')
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
            self.assertEqual(pipeline.look_cvars(root,'original'),{})
            cvars=pipeline.look_cvars(root,'pc,sharpen=0.6')
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
            self.assertEqual(pipeline.look_cvars(root,'original,sun=1.5'),
                             {'rtv_occlusion_awaited_scale':'1.5',
                              'rtv_occlusion_awaited_limit':str(pipeline.SUN_LIMIT)})
            self.assertEqual(pipeline.look_cvars(root,'pc,sun=0')['rtv_occlusion_awaited_scale'],'0')
            with self.assertRaises(RuntimeError):
                pipeline.look_cvars(root,'pc,sun=-1')
            # Development: the repository files, every knob, hot reload and live knobs.
            for look in ('pc,sharpen=0.6','original'):
                cvars=pipeline.look_cvars(root,look,shader_dev=True)
                self.assertEqual(Path(cvars['shader_mod_path']),pipeline.LOOK_DIRECTORY)
                defines=dict(define.split('=',1) for define in cvars['shader_mod_defines'].split(';'))
                self.assertEqual(set(defines),set(defaults))
                self.assertEqual(cvars['shader_mod_hot_reload'],'true')
                self.assertEqual(cvars['shader_mod_live_knobs'],'true')
            self.assertEqual(defines['FM4_LOOK_SHARPEN'],repr(float(defaults['FM4_LOOK_SHARPEN'])))

    def test_look_track_settings(self):
        with tempfile.TemporaryDirectory(prefix='fm4 look test ') as directory:
            root=Path(directory)
            knobs=pipeline.look_knobs('pc,road_gloss=0.5,sky=0.8')
            self.assertEqual(pipeline.look_track_settings(knobs),
                             'RoadSpecular.RoadSpecular*=0.5;RoadSpecular.RoadSpecular2*=0.5;'
                             'skyScale.value*=0.8')
            # pc has sky 0.7 (the 2x sweep of 18 October 2026); the other presets keep the sky.
            self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs('pc')),'skyScale.value*=0.7')
            self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs('aces')),'')
            # A knob at 1 edits nothing, so the files are not copied at all.
            self.assertEqual(pipeline.look_track_settings(pipeline.look_knobs('pc,sky=1')),'')
            # Track knobs are not shader defines, and alone they need no shader mods.
            cvars=pipeline.look_cvars(root,'pc,road_gloss=0.5')
            self.assertNotIn('road_gloss',cvars['shader_mod_defines'])
            self.assertEqual(pipeline.look_cvars(root,'original,sky=0.8'),{})
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
            # Every knob of the look files fits the 32 live knob slots (shader_mod_live_knobs).
            self.assertLessEqual(len(pipeline.look_knob_defaults()),32)

    def test_new_clone_has_all_source_inputs(self):
        for name in ('rexglue.cmake','guard-generated.py','dump-image.cpp','switch-recipes.json'):
            self.assertTrue((pipeline.HERE/name).is_file(),name)
        for name in ('CMakeLists.txt','thirdparty/CMakeLists.txt','src/graphics/shaders/fm4-resolve/resolve.xesli'):
            self.assertTrue((pipeline.RUNTIME/name).is_file(),name)
        for recipe in json.loads((pipeline.HERE/'switch-recipes.json').read_text()):
            self.assertEqual(set(recipe),{'start','end','address','register','count'})
            self.assertNotIn('labels',recipe)

if __name__=='__main__': unittest.main()
