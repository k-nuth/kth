"""Exercise the temporary source bootstrap with real Conan and local HTTP."""

import gzip
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import threading
import unittest

from scripts import prepare_conan_source_cache as bootstrap


class SourceCacheTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.cache = self.root / "cache"
        self.home = self.root / "conan-home"
        self.home.mkdir()
        self.config = "core:non_interactive=True\n"
        (self.home / "global.conf").write_text(self.config)
        self.env = {**os.environ, "CONAN_HOME": str(self.home)}
        payload = io.BytesIO()
        with tarfile.open(fileobj=payload, mode="w") as archive:
            data = b"verified source fixture\n"
            member = tarfile.TarInfo("lmdb/lmdb.h")
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
        self.archive = gzip.compress(payload.getvalue(), mtime=0)
        self.sha256 = hashlib.sha256(self.archive).hexdigest()
        self.requests = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                owner.requests.append(self.path)
                if self.path in ("/limited", "/origin.tar.gz"):
                    self.send_response(429)
                    data = b"rate limited"
                else:
                    self.send_response(200)
                    data = owner.archive if self.path == "/good" else b"wrong bytes"
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.addCleanup(self.stop_server)
        self.url = f"http://127.0.0.1:{self.server.server_port}"

    def stop_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def seed(self, paths=("/good", "/origin.tar.gz")):
        return bootstrap.prepare(
            self.cache, urls=tuple(self.url + p for p in paths),
            sha256=self.sha256, env=self.env,
        )

    def test_fallback_seeds_cache_and_original_recipe_never_contacts_origin(self):
        self.seed(("/limited", "/good", "/origin.tar.gz"))
        self.assertIn("/limited", self.requests)
        self.assertIn("/good", self.requests)
        consumer = self.root / "consumer"
        consumer.mkdir()
        (consumer / "conanfile.py").write_text(f"""from conan import ConanFile
from conan.tools.files import get
class OriginalRecipe(ConanFile):
    name = "lmdb"
    version = "1.0.2"
    def source(self):
        get(self, url={self.url + '/origin.tar.gz'!r},
            sha256={self.sha256!r}, strip_root=True)
""")
        result = subprocess.run(
            ["conan", "source", str(consumer), "-cc",
             f"core.sources:download_cache={self.cache}"],
            env=self.env, capture_output=True, text=True, timeout=30, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("retrieved from local download cache", result.stdout + result.stderr)
        self.assertNotIn("/origin.tar.gz", self.requests)
        self.assertEqual((consumer / "lmdb.h").read_bytes(), b"verified source fixture\n")
        self.assertEqual((self.home / "global.conf").read_text(), self.config)

    def test_wrong_mirror_bytes_are_rejected(self):
        with self.assertRaises(subprocess.CalledProcessError) as error:
            self.seed(("/bad",))
        self.assertRegex(error.exception.output.lower(), r"sha256.*(?:failed|mismatch)")

    def test_tampered_warm_cache_is_rejected_without_network_download(self):
        self.seed()
        blobs = [p for p in self.cache.rglob(self.sha256)
                 if p.is_file() and p.read_bytes() == self.archive]
        self.assertEqual(len(blobs), 1)
        blobs[0].write_bytes(b"tampered cached archive")
        self.requests.clear()
        with self.assertRaises(subprocess.CalledProcessError) as error:
            self.seed()
        self.assertRegex(error.exception.output.lower(), r"sha256.*(?:failed|mismatch)")
        self.assertEqual(self.requests, [])


if __name__ == "__main__":
    unittest.main()
