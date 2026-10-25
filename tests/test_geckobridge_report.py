"""Synthetic fixtures only: no proprietary mod assets and no Java code execution."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
SCRIPT=ROOT/"tools"/"geckobridge_report.py"

class ReportTests(unittest.TestCase):
    def test_source_only_and_comparison(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp)
            jar=root/"sample.jar"
            animation={"animations":{"animation.test.idle":{"loop":True,"bones":{"head":{"rotation":{"0.0":[0,0,0]}}}}}}
            with zipfile.ZipFile(jar,"w") as z:
                z.writestr("assets/test/animations/mob.animation.json",json.dumps(animation))
                z.writestr("com/example/Sample.class",b"com/geckolib/animation/AnimationController")
            out=root/"reports"
            subprocess.run([sys.executable,str(SCRIPT),"--jar",str(jar),"--out",str(out)],check=True,capture_output=True,text=True)
            data=json.loads((out/"conversion_report.json").read_text())
            self.assertEqual(data["overall"],"AUDIT_ONLY_NOT_CONVERSION_VERIFIED")
            self.assertEqual(data["inventory"]["geckolib_namespace"],"5")
            self.assertTrue((out/"conversion_report.md").is_file())
            rp=root/"RP"/"animations"
            rp.mkdir(parents=True)
            (rp/"mob.json").write_text(json.dumps({"animations":animation["animations"]}))
            subprocess.run([sys.executable,str(SCRIPT),"--jar",str(jar),"--bedrock-rp",str(rp.parent),"--out",str(out)],check=True,capture_output=True,text=True)
            data=json.loads((out/"conversion_report.json").read_text())
            self.assertEqual(len(data["conversion_comparison"]["matched_animations"]),1)
            self.assertEqual(data["overall"],"AUDIT_ONLY_NOT_CONVERSION_VERIFIED")

if __name__=="__main__":unittest.main()
