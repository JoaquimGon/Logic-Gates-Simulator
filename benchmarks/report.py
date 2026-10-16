"""Summarize raw benchmark samples; optional matplotlib charts, no fabricated scores."""

import argparse
import base64
from collections import defaultdict
import csv
from datetime import datetime, timezone
import hashlib
import uuid
import html
import io
import json
import math
from pathlib import Path
import statistics
import sys


def percentile(values, fraction):
    """Nearest-rank percentile; small sample counts are explicitly identified."""
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def summarize(rows, per_trial=False, run_id="legacy-unspecified"):
    grouped = defaultdict(list)
    for row in rows:
        value = float(row["value"])
        if not math.isfinite(value):
            raise ValueError("Non-finite sample")
        key = (row["scenario"], int(row["size"]), row["metric"], row["unit"],
               int(row["trial"]) if per_trial else "all")
        grouped[key].append(value)
    summaries = []
    for (scenario, size, metric, unit, trial), values in sorted(grouped.items()):
        n = len(values)
        rank95, rank99 = math.ceil(n * .95), math.ceil(n * .99)
        summaries.append(dict(
            run_id=run_id, scope="within_process_trial" if per_trial else "pooled_within_process",
            trial=trial, scenario=scenario, size=size, metric=metric, unit=unit, n=n,
            minimum=min(values), median=statistics.median(values), p95=percentile(values, .95),
            p99=percentile(values, .99), maximum=max(values),
            mean=statistics.mean(values), stdev=statistics.stdev(values) if n > 1 else 0,
            p95_rank=rank95, p99_rank=rank99, observations_above_p95_rank=n-rank95, observations_above_p99_rank=n-rank99,
            p95_is_max_by_sample_count=rank95 == n, p99_is_max_by_sample_count=rank99 == n,
        ))
    return summaries


def charts(summaries, raw, directory):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return "<p>Charts omitted: install matplotlib to generate them; all raw data and tables remain available.</p>"
    plt.rcParams.update({"font.size": 10, "axes.grid": True, "grid.alpha": .25})
    definitions = [
        ("Propagation latency", "duration_ms", ["propagation_fanout", "propagation_chain"], "Median milliseconds", "propagation"),
        ("Connected edit latency", "duration_ms", ["move_connected", "undo_connected_move", "redo_connected_move"], "Median milliseconds", "editing"),
        ("Renderer GPU execution (1920 x 1080, offscreen)", "gpu_ms",
         ["render_same_static", "render_same_active", "render_mixed_static", "render_mixed_active", "render_mixed_grouped_static", "render_mixed_grouped_active"], "Median milliseconds", "gpu"),
        ("Renderer CPU submission (not displayed frame time)", "cpu_submit_ms",
         ["render_same_static", "render_same_active", "render_mixed_static", "render_mixed_active", "render_mixed_grouped_static", "render_mixed_grouped_active"], "Median milliseconds", "render-cpu"),
        ("Headless simulation / wall time, 1024 Hz", "sim_seconds_per_wall_second",
         ["clock_1024hz_no_probes", "clock_1024hz_8probes"], "Simulated seconds / wall second", "clocks"),
    ]
    images = []
    for title, metric, scenarios, ylabel, filename in definitions:
        fig, ax = plt.subplots(figsize=(8.2, 4.6), layout="constrained")
        drawn = False
        for scenario in scenarios:
            points = sorted((s for s in summaries if s["scenario"] == scenario and s["metric"] == metric), key=lambda s: s["size"])
            if points:
                drawn = True
                ax.plot([s["size"] for s in points], [s["median"] for s in points], marker="o", label=scenario)
        if not drawn:
            plt.close(fig)
            continue
        if metric == "sim_seconds_per_wall_second":
            ax.axhline(1, color="#b34700", linestyle="--", label="ratio = 1 (no GUI overhead included)")
        ax.set(xlabel="Total logical components" if filename in ("propagation", "clocks") else "Components", ylabel=ylabel, title=title)
        ax.legend(fontsize=8)
        target = directory / (filename + ".png")
        fig.savefig(target, dpi=160)
        buffer = io.BytesIO()
        fig.savefig(buffer, format="png", dpi=120)
        images.append(f'<img alt="{html.escape(title)}" src="data:image/png;base64,{base64.b64encode(buffer.getvalue()).decode()}">')
        plt.close(fig)
    for scenario in ("history_endurance", "scene_lifecycle_endurance"):
        points = [r for r in raw if r["scenario"] == scenario and r["metric"] == "private_bytes"]
        if not points:
            continue
        fig, ax = plt.subplots(figsize=(8.2, 4.2), layout="constrained")
        ax.plot([int(r["sample"]) for r in points], [float(r["value"]) / 1048576 for r in points], marker=".")
        ax.set(title=scenario + " (whole process)", xlabel="Memory checkpoint (approximately one second apart)", ylabel="Process private memory (MiB)")
        fig.savefig(directory / (scenario + ".png"), dpi=160)
        buffer = io.BytesIO()
        fig.savefig(buffer, format="png", dpi=120)
        images.append(f'<img alt="{scenario}" src="data:image/png;base64,{base64.b64encode(buffer.getvalue()).decode()}">')
        plt.close(fig)
    phases = ("scene_copy", "snapshot", "routing", "normalization", "connectivity", "restore")
    scenarios = ("move_connected", "undo_connected_move", "redo_connected_move")
    if any(r["metric"] == "phase_snapshot_exclusive_ms" and r["scenario"] == "move_connected" for r in summaries):
        fig, axes = plt.subplots(1, 3, figsize=(14, 5), layout="constrained")
        for ax, scenario in zip(axes, scenarios):
            sizes = sorted({r["size"] for r in summaries if r["scenario"] == scenario})
            bottom = [0.0] * len(sizes)
            for phase in (*phases, "unattributed"):
                metric = "unattributed_ms" if phase == "unattributed" else f"phase_{phase}_exclusive_ms"
                values = [next((r["mean"] for r in summaries if r["scenario"] == scenario and r["size"] == size and r["metric"] == metric), 0.0) for size in sizes]
                ax.bar([str(size) for size in sizes], values, bottom=bottom, label=phase)
                bottom = [a + b for a, b in zip(bottom, values)]
            ax.set(title=scenario, xlabel="Components", ylabel="Mean ms (exclusive phases + residual)")
        axes[-1].legend(fontsize=8)
        fig.savefig(directory / "edit-phases.png", dpi=160)
        buffer = io.BytesIO()
        fig.savefig(buffer, format="png", dpi=120)
        images.append(f'<img alt="Exclusive edit phases and residual; means add to global mean" src="data:image/png;base64,{base64.b64encode(buffer.getvalue()).decode()}">')
        plt.close(fig)
    return "".join(images)


def report(directory, output=None):
    directory = directory.resolve()
    metadata = json.loads((directory / "metadata.json").read_text(encoding="utf-8"))
    with (directory / "samples.csv").open(newline="", encoding="utf-8") as stream:
        raw = list(csv.DictReader(stream))
    if not raw:
        raise ValueError("No measurements were recorded.")
    if output is None:
        output = directory / "reports" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:8])
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    run_id = metadata.get("run_id", "legacy-unspecified")
    summaries = summarize(raw, run_id=run_id)
    repetitions = summarize(raw, per_trial=True, run_id=run_id)
    for name, records in (("summary.csv", summaries), ("summary-by-trial.csv", repetitions)):
        with (output / name).open("x", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(records[0]))
            writer.writeheader()
            writer.writerows(records)
    cases = metadata["cases"]
    limits = [c for c in cases if c["status"] != "pass"]
    intro = (f"Build: {metadata['build_type']}; CPU: {metadata.get('cpu', 'not recorded')}; "
             f"GPU: {metadata.get('gpu', 'not measured')}; revision: {str(metadata.get('revision'))[:12]}; "
             f"run: {run_id}; executable SHA-256: {metadata.get('executable_sha256', 'not recorded')}.")

    def table(headers, records):
        return '<table><thead><tr>' + ''.join(f'<th>{html.escape(str(h))}</th>' for h in headers) + '</tr></thead><tbody>' + ''.join(
            '<tr>' + ''.join(f'<td>{html.escape(str(value))}</td>' for value in record) + '</tr>' for record in records
        ) + '</tbody></table>'

    def timings(records, per_trial=False):
        return table(["Scenario", "Size", "Trial", "Metric", "n", "Median", "p95", "p99", "Maximum", "Tail resolution", "Unit"], [
            [r["scenario"], r["size"], r["trial"], r["metric"], r["n"],
             *[f'{r[k]:.5g}' for k in ("median", "p95", "p99", "maximum")],
             "; ".join(f"{q} rank=n: maximum by sample count" for q in ("p95", "p99") if r[q + "_is_max_by_sample_count"]), r["unit"]]
            for r in records if r["unit"] in ("ms", "ratio")])

    case_table = table(["Scenario", "Size", "Status", "Explanation"],
                       [(c["scenario"], c["size"], c["status"], c["detail"]) for c in cases])
    legacy = ""
    if "manifest_sha256" not in metadata:
        legacy = '<p class="notice">Legacy data: source/executable provenance and strengthened pixel/clock validation were not captured. Successful legacy rendering outcomes used a weak pixel check that could accept the background. This report cannot retroactively validate those frames.</p>'
    charts_html = charts(summaries, raw, output)
    document = f'''<!doctype html><html lang="en"><meta charset="utf-8"><title>Release benchmark report</title>
<style>body{{font:16px system-ui;max-width:1400px;margin:40px auto;padding:0 20px;color:#172536;background:#f5f7fa}}table{{border-collapse:collapse;background:white;font-size:12px;margin:20px 0}}th,td{{padding:7px;border:1px solid #d6dfe8;text-align:left}}th{{background:#e4edf5}}img{{max-width:100%;display:block;margin:24px auto}}pre{{white-space:pre-wrap}}.notice{{padding:18px;background:#fff0ce;border-left:4px solid #b66b00}}</style>
<h1>Release benchmark measurements</h1><p>{html.escape(intro)}</p>{legacy}
<p>Run completed: <strong>{metadata['run_completed']}</strong>. {len(cases)} case outcomes; {len(limits)} limits or unavailable cases.</p>
<div class="notice">Subsystem timings on this machine. No whole-app FPS, displayed latency or universal speed threshold is implied. CPU submission, GPU intervals and completed-batch averages overlap and must not be added. Fixed render resolution with fitted zoom means smaller visible symbols as N grows. Headless clock throughput excludes paced UI work. Trials repeat within one process; independent runs have separate IDs and raw files. Nearest-rank p95/p99 explicitly identify rank=n; increasing repeated samples does not create independent process runs. Preview quantiles describe batch means per operation, not individual-call tail latency. Completed and aborted propagation times use separate metric names. Edit phases expose inclusive and exclusive times: only exclusive phases plus unattributed time add to global time. Restore includes its unclassified propagation/visual synchronization. Probe calibration and paired collection-on/off samples expose overhead and noise; disabled collection in that binary still has compiled branches. More samples alone do not establish rare-pause rates or independence. Disk save/load exercises warmed OS caching and does not measure durable-media latency.</div>
{charts_html}<h2>Pooled within this process</h2>{timings(summaries)}
<h2>Each repetition within this process</h2>{timings(repetitions, True)}
<h2>Correctness and limits</h2>{case_table}
<h2>Reproduction metadata</h2><pre>{html.escape(json.dumps(metadata, indent=2))}</pre></html>'''
    (output / "report.html").write_text(document, encoding="utf-8")
    provenance = dict(generated_utc=datetime.now(timezone.utc).isoformat(), python=sys.version,
                      arguments=sys.argv, raw_directory=str(directory),
                      hashes={name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
                              for name in ("samples.csv", "metadata.json")},
                      report_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (output / "report_manifest.json").write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    print(intro)
    print(f"Completed: {metadata['run_completed']}; cases: {len(cases)}; limits/unavailable: {len(limits)}")
    for case in limits:
        print(f"LIMIT {case['scenario']} size={case['size']}: {case['detail']}")
    print(f"Report: {output / 'report.html'}")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path, help="New report directory; existing paths are rejected")
    parser.add_argument("--plot-packages", type=Path, help="Optional local directory containing matplotlib and dependencies")
    args = parser.parse_args()
    if args.plot_packages:
        sys.path.insert(0, str(args.plot_packages.resolve()))
    report(args.directory, args.output)
