"""Regression tests for statistics, provenance and preservation (stdlib only)."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from campaign import verify_run
from report import report, summarize

EXECUTABLE = None
if "--executable" in sys.argv:
    index = sys.argv.index("--executable")
    EXECUTABLE = Path(sys.argv[index + 1]).resolve()
    del sys.argv[index : index + 2]


class BenchmarkToolsTests(unittest.TestCase):
    """Check statistical resolution, build provenance, tamper rejection and preservation of existing results."""

    @unittest.skipUnless(EXECUTABLE, "No benchmark executable supplied")
    def test_real_run_identity_and_no_overwrite(self):
        """Verify a real run records its identity and refuses to replace an existing output directory."""
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "run"
            command = [str(EXECUTABLE), "--suite", "validators", "--smoke", "--output", str(output)]
            subprocess.run(command, check=True, capture_output=True)
            metadata = verify_run(output, EXECUTABLE)
            self.assertIn(metadata["build_type"], ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"))
            self.assertEqual(metadata["arguments"][1:], command[1:])
            before = {p.name: p.read_bytes() for p in output.iterdir() if p.is_file()}
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("already exists", rejected.stderr)
            self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir() if p.is_file()})

    def test_repetition_and_quantile_resolution(self):
        """Verify trial separation, nearest-rank quantiles and explicit sparse-sample limits."""
        rows = [
            dict(scenario="x", size="10", metric="ms", unit="ms", trial=str(i // 3), value=str(i)) for i in range(6)
        ]
        pooled = summarize(rows)[0]
        self.assertEqual(pooled["n"], 6)
        self.assertTrue(pooled["p95_is_max_by_sample_count"])
        self.assertTrue(pooled["p99_is_max_by_sample_count"])
        individual = summarize(rows, per_trial=True)
        self.assertEqual([r["trial"] for r in individual], [0, 1])
        self.assertEqual([r["median"] for r in individual], [1, 4])
        rows = [dict(rows[0], value=str(i)) for i in range(100)]
        result = summarize(rows)[0]
        self.assertFalse(result["p99_is_max_by_sample_count"])
        self.assertEqual(result["p99"], 98)
        rows = [dict(rows[0], value=str(i)) for i in range(120)]
        result = summarize(rows)[0]
        self.assertEqual(result["p99_rank"], 119)
        self.assertEqual(result["observations_above_p99_rank"], 1)
        with self.assertRaises(ValueError):
            summarize([dict(rows[0], value="nan")])

    def test_report_never_overwrites_raw_or_previous_report(self):
        """Verify report generation preserves raw inputs and rejects an existing destination."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            metadata = root / "metadata.json"
            samples = root / "samples.csv"
            metadata.write_text(json.dumps(dict(build_type="Release", revision="abc", cases=[], run_completed=True)))
            samples.write_text("scenario,size,trial,sample,metric,value,unit\nx,10,0,0,time,1,ms\n")
            before = (metadata.read_bytes(), samples.read_bytes())
            output = report(root, root / "new-report")
            self.assertIn("weak pixel check", (output / "report.html").read_text())
            with self.assertRaises(FileExistsError):
                report(root, output)
            self.assertEqual(before, (metadata.read_bytes(), samples.read_bytes()))

    def test_manifest_refreshes_without_configuration(self):
        """Verify rebuilding captures source edits even when CMake configuration has not changed."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "build").mkdir()
            source = root / "src" / "fixture.cpp"
            source.write_text("before")
            (root / "build" / "CMakeCache.txt").write_text("fixture:STRING=unchanged\n")
            output = root / "build" / "BuildManifest.h"
            command = [
                sys.executable,
                str(Path(__file__).with_name("build_manifest.py")),
                "--source",
                str(root),
                "--build",
                str(root / "build"),
                "--output",
                str(output),
                "--config",
                "Release",
                "--compiler",
                "test",
                "--compiler-version",
                "1",
                "--cmake-version",
                "1",
            ]
            subprocess.run(command, check=True)
            self.assertIn(hashlib.sha256(b"before").hexdigest(), output.read_text())
            source.write_text("after")
            subprocess.run(command, check=True)
            self.assertIn(hashlib.sha256(b"after").hexdigest(), output.read_text())
            self.assertNotIn(hashlib.sha256(b"before").hexdigest(), output.read_text())

    def test_identity_rejects_tampering(self):
        """Reject independently modified manifest or executable bytes during provenance verification."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "benchmark.exe"
            executable.write_bytes(b"binary")
            manifest = root / "build_manifest.json"
            manifest.write_bytes(b"manifest")
            metadata = dict(
                run_completed=True,
                manifest_sha256=hashlib.sha256(b"manifest").hexdigest(),
                executable_sha256=hashlib.sha256(b"binary").hexdigest(),
            )
            (root / "metadata.json").write_text(json.dumps(metadata))
            verify_run(root, executable)
            manifest.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                verify_run(root, executable)
            manifest.write_bytes(b"manifest")
            executable.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                verify_run(root, executable)


if __name__ == "__main__":
    unittest.main()
