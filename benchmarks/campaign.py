"""Run sequential independent benchmark processes; retain each run and repetition."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import uuid
from report import summarize

PROFILES = {
    "dev": ["--sizes", "16,100,500", "--trials", "1", "--samples", "5",
            "--edit-samples", "1", "--warmup", "2", "--endurance-seconds", "1", "--lifecycle-seconds", "1", "--coverage-samples", "1"],
    "extended": ["--sizes", "100,500,1000,2000,4096", "--trials", "10", "--samples", "200",
                 "--edit-samples", "40", "--warmup", "16", "--endurance-seconds", "600", "--lifecycle-seconds", "600"],
    "validation": ["--sizes", "100,500,1000,2000,4096", "--trials", "3", "--samples", "200",
                   "--edit-samples", "40", "--warmup", "8", "--coverage-samples", "5",
                   "--endurance-seconds", "120", "--lifecycle-seconds", "120"],
}


def now():
    return datetime.now(timezone.utc).isoformat()


def verify_run(directory, executable):
    metadata = json.loads((directory / "metadata.json").read_text(encoding="utf-8"))
    if not metadata["run_completed"]:
        raise ValueError("Benchmark did not complete")
    for path, key in ((directory / "build_manifest.json", "manifest_sha256"),
                      (executable, "executable_sha256")):
        if hashlib.sha256(path.read_bytes()).hexdigest() != metadata[key]:
            raise ValueError(f"Checksum mismatch: {path}")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--profile", choices=PROFILES, default="dev")
    parser.add_argument("--runs", type=int, default=2)
    parser.add_argument("--suite", default="all")
    parser.add_argument("--timeout-seconds", type=float, default=14400)
    parser.add_argument("--output", type=Path)
    arguments = sys.argv[1:]
    separator = arguments.index("--") if "--" in arguments else len(arguments)
    extra = arguments[separator + 1:]
    args = parser.parse_args(arguments[:separator])
    if args.runs < 1 or args.timeout_seconds <= 0:
        parser.error("runs and timeout must be positive")
    executable = args.executable.resolve(strict=True)
    directory = args.output or Path("out/benchmarks") / ("campaign-" + uuid.uuid4().hex)
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    if "--output" in extra:
        parser.error("Each process must use its own campaign-managed output directory")
    record = dict(campaign_id=directory.name, profile=args.profile, started_utc=now(),
                  arguments=sys.argv, python=sys.version, host=platform.platform(), runs=[])
    summary = []
    try:
        for index in range(args.runs):
            child = directory / f"process-{index:03d}-{uuid.uuid4().hex[:8]}"
            command = [str(executable), *PROFILES[args.profile], "--suite", args.suite,
                       *extra, "--output", str(child)]
            entry = dict(index=index, command=command, started_utc=now(), directory=str(child))
            record["runs"].append(entry)
            (directory / "campaign.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
            # A process-level watchdog also covers initialization, warmup and driver calls.
            with (directory / f"process-{index:03d}.log").open("x", encoding="utf-8") as log:
                completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                           timeout=args.timeout_seconds, check=False)
            entry.update(returncode=completed.returncode, finished_utc=now())
            if completed.returncode:
                raise RuntimeError(f"Independent process {index} failed; retained {child}")
            metadata = verify_run(child, executable)
            entry.update(run_id=metadata["run_id"], process_id=metadata.get("process_id"),
                         executable_sha256=metadata["executable_sha256"],
                         manifest_sha256=metadata["manifest_sha256"])
            with (child / "samples.csv").open(newline="", encoding="utf-8") as stream:
                raw = list(csv.DictReader(stream))
            records = summarize(raw, run_id=metadata["run_id"])
            summary.extend(records)
            for filename, rows in (("summary.csv", records),
                                   ("summary-by-trial.csv", summarize(raw, per_trial=True, run_id=metadata["run_id"]))):
                with (child / filename).open("x", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
            print(f"Independent process {index + 1}/{args.runs} complete: {child}", flush=True)
        if summary:
            with (directory / "summary-by-process.csv").open("x", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
                writer.writeheader()
                writer.writerows(summary)
        record["completed"] = True
    except BaseException as error:
        record.update(completed=False, error=str(error))
        raise
    finally:
        record["finished_utc"] = now()
        (directory / "campaign.json").write_text(json.dumps(record, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
