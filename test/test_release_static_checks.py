import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('checks',Path(__file__).resolve().parents[1]/'scripts/release_static_checks.py')
checks=importlib.util.module_from_spec(spec);spec.loader.exec_module(checks)

class Patterns(unittest.TestCase):
    def scan(self,source):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'res').mkdir();(root/'res/exists.png').touch()
            return checks.patterns('src/example.cpp',source,root)
    def test_missing_asset(self):
        found=self.scan('asset::plugin(pluginInstance, "res/missing.png"); asset::plugin(pluginInstance,"res/exists.png");')
        self.assertEqual([f['id'] for f in found],['assetPathMismatch'])
    def test_helper_is_flagged_draw_is_safe(self):
        found=self.scan('void loadArtwork() { artwork = APP->window->loadImage(path); }\nvoid draw(const DrawArgs& args) override { if (ok) { loadImage(path); } }')
        self.assertEqual([f['id'] for f in found],['fontImageCachedOutsideDraw'])
    def test_style_and_network(self):
        found=self.scan('void drawLayer(const DrawArgs& args,int layer) override { if (!module) return; }\n bind(fd,a,b);')
        self.assertEqual([f['severity'] for f in found],['style','warning'])
    def test_comments_are_ignored(self):
        self.assertEqual(self.scan('// loadImage(x);\n/* bind(x); */'),[])

class DiagnosticScope(unittest.TestCase):
    def test_sdk_only_and_mixed_locations(self):
        import xml.etree.ElementTree as ET
        root=Path('/test/sdk')
        e=ET.fromstring('<error id="uninitvar"><location file="/test/sdk/include/a.hpp"/></error>')
        self.assertTrue(checks.is_external_diagnostic(e,(root,)))
        ET.SubElement(e,'location',file='/test/project/src/a.cpp')
        self.assertFalse(checks.is_external_diagnostic(e,(root,)))
    def test_parse_failure_never_hidden(self):
        import xml.etree.ElementTree as ET
        e=ET.fromstring('<error id="syntaxError"><location file="/test/sdk/a.hpp"/></error>')
        self.assertFalse(checks.is_external_diagnostic(e,(Path('/test/sdk'),)))

if __name__ == '__main__': unittest.main()
