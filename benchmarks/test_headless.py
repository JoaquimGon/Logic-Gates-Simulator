"""Integration contract for an ACTUAL build without the rendering module."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    with tempfile.TemporaryDirectory() as temporary:
        for suite in ("render", "all"):
            for required in (False, True):
                output = Path(temporary) / f"{suite}-{required}"
                command = [str(executable), "--smoke", "--suite", suite, "--output", str(output)]
                if required:
                    command.append("--require-rendering")
                result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                metadata = json.loads((output / "metadata.json").read_text(encoding="utf-8"))
                assert (result.returncode != 0) == required, (command, result.stdout, result.stderr)
                assert metadata["run_completed"] == (not required), metadata
                rendering = [case for case in metadata["cases"] if case["scenario"] == "rendering"]
                assert len(rendering) == 1 and rendering[0]["status"] == "unavailable", rendering
                if required:
                    assert "--require-rendering" in result.stderr and "without OpenGL" in result.stderr, result.stderr
                    assert "error" in metadata, metadata
    print("Headless render/all: required rendering rejected; optional unavailable accepted.")


if __name__ == "__main__":
    main()
