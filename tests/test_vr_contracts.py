"""Guard the externally named API/env boundary against accidental text edits."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class RuntimeContracts(unittest.TestCase):
    def test_functional_literals(self):
        for name in ('src/vr_xr.c', 'src/vr_render.inc', 'src/vrde.c'):
            text = (ROOT / name).read_text()
            for literal in re.findall(r'"([^"\n]*)"', text):
                if literal.startswith(('VRDE_VR_', 'xr', '/interaction_profiles/', '/user/hand/')):
                    self.assertEqual(literal, literal.strip(), (name, literal))
                    self.assertNotIn(' ', literal, (name, literal))

    def test_entry_point_name_matches_pinned_header(self):
        header = ROOT / 'build/openxr-sdk-1.1.63/install/include/openxr/openxr_platform.h'
        if not header.exists():
            self.skipTest('build the pinned SDK first')
        source = (ROOT / 'src/vr_xr.c').read_text()
        for function in re.findall(r'xrGetInstanceProcAddr\(instance, "([^"]+)"', source):
            self.assertIn('PFN_' + function, header.read_text())

if __name__ == '__main__':
    unittest.main()
