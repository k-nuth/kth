#!/usr/bin/env python3

"""Tests for the Conan dependency update checker."""

import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import check_conan_updates as checker


class ConanUpdateCheckerTests(unittest.TestCase):
    def write_conanfile(self, text: str) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "conanfile.py"
        path.write_text(text)
        return path

    def test_cci_and_kth_packages_use_their_declared_sources(self):
        conanfile = self.write_conanfile(
            'self.requires("fmt/12.1.0")\n'
            'self.requires("utxoz/0.8.0")\n'
            'self.tool_requires("secp256k1-precompute/1.0.0")\n'
        )

        with mock.patch.object(checker, "fetch_cci_versions", return_value=["12.2.0"]) as cci, \
             mock.patch.object(
                 checker,
                 "fetch_conan_remote_versions",
                 side_effect=[["1.0.0"], ["0.8.0"]],
             ) as custom:
            updates, failures = checker.check_updates(conanfile)

        self.assertEqual(updates, [("fmt", "12.1.0", "12.2.0")])
        self.assertEqual(failures, [])
        cci.assert_called_once_with("fmt")
        self.assertEqual(
            custom.call_args_list,
            [
                mock.call("secp256k1-precompute", "kth"),
                mock.call("utxoz", "kth"),
            ],
        )

    def test_lookup_failure_makes_the_result_incomplete(self):
        conanfile = self.write_conanfile('self.requires("fmt/12.1.0")\n')
        with mock.patch.object(
            checker,
            "fetch_cci_versions",
            side_effect=checker.PackageLookupError("network unavailable"),
        ):
            updates, failures = checker.check_updates(conanfile)

        self.assertEqual(updates, [])
        self.assertEqual(failures, [("fmt", "network unavailable")])

    def test_empty_cci_versions_are_an_incomplete_lookup(self):
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b"versions: {}\n"

        with mock.patch.object(checker.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(
                checker.PackageLookupError,
                "CCI response contained no versions",
            ):
                checker.fetch_cci_versions("fmt")

    def test_unexpected_exception_uses_incomplete_exit_code(self):
        stderr = io.StringIO()
        with mock.patch.object(checker, "main", side_effect=ValueError("broken response")), \
             mock.patch.object(checker.sys, "stderr", stderr):
            with self.assertRaises(SystemExit) as raised:
                checker.entrypoint()

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("Unexpected dependency-check failure", stderr.getvalue())

    def test_no_updates_is_a_complete_result(self):
        conanfile = self.write_conanfile('self.requires("fmt/12.2.0")\n')
        with mock.patch.object(checker, "fetch_cci_versions", return_value=["12.2.0"]):
            updates, failures = checker.check_updates(conanfile)

        self.assertEqual(updates, [])
        self.assertEqual(failures, [])

    @mock.patch("subprocess.run")
    def test_conan_remote_json_is_parsed(self, run):
        run.return_value = subprocess.CompletedProcess(
            args=[],
            returncode=0,
            stdout=json.dumps({
                "kth": {
                    "utxoz/0.8.0": {},
                    "utxoz/0.9.0-commit.12": {},
                }
            }),
            stderr="",
        )

        self.assertEqual(
            checker.fetch_conan_remote_versions("utxoz", "kth"),
            ["0.8.0", "0.9.0-commit.12"],
        )

    @mock.patch("subprocess.run")
    def test_conan_remote_error_is_not_not_found_or_up_to_date(self, run):
        run.return_value = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr="remote unavailable"
        )

        with self.assertRaisesRegex(checker.PackageLookupError, "remote unavailable"):
            checker.fetch_conan_remote_versions("utxoz", "kth")


if __name__ == "__main__":
    unittest.main()
