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
            # Development: the repository files, every knob, hot reload and live knobs.
            for look in ('pc,sharpen=0.6','original'):
                cvars=pipeline.look_cvars(root,look,shader_dev=True)
                self.assertEqual(Path(cvars['shader_mod_path']),pipeline.LOOK_DIRECTORY)
                defines=dict(define.split('=',1) for define in cvars['shader_mod_defines'].split(';'))
                self.assertEqual(set(defines),set(defaults))
                self.assertEqual(cvars['shader_mod_hot_reload'],'true')
                self.assertEqual(cvars['shader_mod_live_knobs'],'true')
            self.assertEqual(defines['FM4_LOOK_SHARPEN'],repr(float(defaults['FM4_LOOK_SHARPEN'])))

    def test_new_clone_has_all_source_inputs(self):
        for name in ('rexglue.cmake','guard-generated.py','dump-image.cpp','switch-recipes.json'):
            self.assertTrue((pipeline.HERE/name).is_file(),name)
        for name in ('CMakeLists.txt','thirdparty/CMakeLists.txt','src/graphics/shaders/fm4-resolve/resolve.xesli'):
            self.assertTrue((pipeline.RUNTIME/name).is_file(),name)
        for recipe in json.loads((pipeline.HERE/'switch-recipes.json').read_text()):
            self.assertEqual(set(recipe),{'start','end','address','register','count'})
            self.assertNotIn('labels',recipe)

if __name__=='__main__': unittest.main()
