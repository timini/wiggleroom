import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('initializer',Path(__file__).resolve().parents[1]/'scripts/initialize_faust_members.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Initialize(unittest.TestCase):
    def test_state_and_idempotence(self):
        raw='class mydsp : public dsp {\n private:\n int index;\n float delay[8];\n FAUSTFLOAT control;\n public:\n mydsp() {}\n};'
        initialized=m.initialize(raw)
        self.assertIn('int index = {};',initialized)
        self.assertIn('float delay[8] = {};',initialized)
        self.assertIn('FAUSTFLOAT control = {};',initialized)
        self.assertEqual(m.initialize(initialized),initialized)
if __name__=='__main__': unittest.main()
