import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PRELOAD = Path(__file__).resolve().parents[1] / "linuxfs/preload"

PROGRAM = r"""
#include <stdio.h>
char *const *bl_webhelper_argv(const char *path, char *const argv[]);
int main(int argc, char **argv) {
  char *const *out = bl_webhelper_argv(argv[1], argv + 1);
  for (int i = 0; out[i]; i++) printf("%s\n", out[i]);
  return 0;
}
"""

WEBHELPER = "/root/.local/share/Steam/steamrtarm64/steamwebhelper"


@unittest.skipUnless(shutil.which("cc"), "needs cc")
class WebhelperPreloadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        source = cls.tmp / "program.c"
        source.write_text(PROGRAM)
        cls.program = cls.tmp / "program"
        subprocess.run(["cc", "-O2", "-Wall", "-Werror", "-o", str(cls.program), str(source),
                        str(PRELOAD / "webhelper.c")], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def argv(self, *args, spec=None):
        env = {k: v for k, v in os.environ.items() if k != "BL_WEBHELPER_ARGS"}
        if spec is not None:
            env["BL_WEBHELPER_ARGS"] = spec
        result = subprocess.run([str(self.program), *args], env=env, capture_output=True, text=True, check=True)
        return result.stdout.splitlines()

    def test_the_browser_process_keeps_its_helpers_in_process(self):
        out = self.argv(WEBHELPER, "-lang=en_US", "--enable-features=A", "--no-sandbox", "--disable-features=B,C")
        self.assertEqual(out, [WEBHELPER, "-lang=en_US", "--no-sandbox", "--no-zygote", "--in-process-gpu",
                               "--enable-features=A", "--disable-features=B,C,AudioServiceOutOfProcess"])

    def test_feature_lists_are_added_when_steam_passes_none(self):
        out = self.argv(WEBHELPER, spec="+X -Y")
        self.assertEqual(out, [WEBHELPER, "--enable-features=X", "--disable-features=Y"])

    def test_child_processes_and_other_programs_are_left_alone(self):
        child = [WEBHELPER, "--type=renderer", "--enable-features=A"]
        self.assertEqual(self.argv(*child), child)
        other = ["/usr/bin/steam", "--enable-features=A"]
        self.assertEqual(self.argv(*other), other)

    def test_mali_flags_preserve_separate_gpu_process(self):
        out = self.argv(WEBHELPER, "--use-angle=gl", "--enable-features=A",
                        spec="--no-zygote -AudioServiceOutOfProcess")
        self.assertIn("--no-zygote", out)
        self.assertIn("--use-angle=gl", out)
        self.assertIn("--disable-features=AudioServiceOutOfProcess", out)
        self.assertNotIn("--in-process-gpu", out)
        child = [WEBHELPER, "--type=gpu-process", "--use-angle=gl"]
        self.assertEqual(self.argv(*child, spec="--no-zygote -AudioServiceOutOfProcess"), child)

    def test_actual_mali_launch_spec_reaches_webhelper_argv(self):
        session = (PRELOAD.parent / 'overlay/usr/local/bin/droiddeck-session').read_text()
        spec = re.search(r'export BL_WEBHELPER_ARGS="([^"]+)"', session).group(1)
        out = self.argv(WEBHELPER, "--no-sandbox", "--enable-features=A", spec=spec)
        for option in ["--use-gl=angle", "--use-angle=gl", "--ozone-platform=x11", "--ignore-gpu-blocklist"]:
            self.assertIn(option, out)
        self.assertIn("--disable-features=AudioServiceOutOfProcess", out)
        self.assertIn("--no-sandbox", out)
        self.assertNotIn("--in-process-gpu", out)
        self.assertNotIn("--disable-gpu", out)

    def test_graphics_trace_reports_only_allowed_switches(self):
        env = dict(os.environ, BL_WEBHELPER_ARGS="--use-gl=angle --use-angle=gl", BL_WEBHELPER_GRAPHICS_LOG="1")
        browser = subprocess.run([str(self.program), WEBHELPER, "--auth-token=synthetic-private"], env=env,
                                 capture_output=True, text=True, check=True)
        self.assertIn("graphics exec browser: --use-gl=angle --use-angle=gl", browser.stderr)
        self.assertNotIn("synthetic-private", browser.stderr)
        gpu = subprocess.run([str(self.program), WEBHELPER, "--type=gpu-process", "--use-angle=gl"], env=env,
                             capture_output=True, text=True, check=True)
        self.assertIn("graphics exec gpu-process: --use-angle=gl", gpu.stderr)
        self.assertNotIn("--use-gl=angle", gpu.stdout)

    def test_stock_turns_it_off(self):
        args = [WEBHELPER, "--enable-features=A"]
        self.assertEqual(self.argv(*args, spec="stock"), args)

    def test_an_empty_setting_means_the_default(self):
        self.assertIn("--no-zygote", self.argv(WEBHELPER, spec=""))


if __name__ == "__main__":
    unittest.main()
