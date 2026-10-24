import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'components/recipes/debian-image.sh'


class DebianImageForkTest(unittest.TestCase):
    def run_recipe(self, corrupt=False, upstream=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'tools/linuxfs').mkdir(parents=True)
            digest = hashlib.sha256(b'image').hexdigest()
            (root / 'tools/linuxfs/debian-image.env').write_text(
                'DEBIAN_IMAGE_TAG=pinned\nDEBIAN_IMAGE_ASSET=image.zst\n'
                f'DEBIAN_IMAGE_SHA256={digest}\n')
            commands = {
                'gh': '#!/bin/bash\necho "$*" >> "$TEST_LOG"\n'
                      '[[ "$*" == *"-R Droid-Deck/DroidDeck "* ]] || exit 1\n'
                      'printf "%s" "$TEST_IMAGE" > /tmp/debian-image.tar.zst\n',
                'zstd': '#!/bin/sh\ncat /tmp/debian-image.tar.zst\n',
                'docker': '#!/bin/sh\necho "docker $*" >> "$TEST_LOG"\n'
                          '[ "$1" != load ] || cat >/dev/null\n',
            }
            for name, text in commands.items():
                path = root / name
                path.write_text(text)
                path.chmod(0o755)
            log = root / 'calls'
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                       GITHUB_REPOSITORY='Droid-Deck/DroidDeck' if upstream else 'fork/DroidDeck',
                       TEST_LOG=str(log), TEST_IMAGE='corrupt' if corrupt else 'image')
            result = subprocess.run(['bash', str(SCRIPT)], cwd=root, env=env, capture_output=True, text=True)
            return result, log.read_text()

    def test_fork_uses_checksum_pinned_upstream_image(self):
        result, calls = self.run_recipe()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('-R fork/DroidDeck ', calls)
        self.assertIn('-R Droid-Deck/DroidDeck ', calls)
        self.assertIn('docker load', calls)
        self.assertNotIn('docker pull', calls)

    def test_corrupt_download_is_not_loaded_or_replaced(self):
        result, calls = self.run_recipe(corrupt=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('docker load', calls)
        self.assertNotIn('docker pull', calls)

    def test_upstream_does_not_download_twice(self):
        result, calls = self.run_recipe(upstream=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls.count('-R Droid-Deck/DroidDeck '), 1)
