"""Capture build inputs on EVERY benchmark build, not at CMake configuration time."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(root, *args):
    try:
        result = subprocess.run(args, cwd=root, capture_output=True, text=True, check=False)
    except OSError:
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--compiler-version", required=True)
    parser.add_argument("--cmake-version", required=True)
    args = parser.parse_args()
    root, build = args.source.resolve(), args.build.resolve()
    files = []
    for directory in ("src", "external", "cmake", "assets", "benchmarks"):
        for path in (root / directory).rglob("*"):
            if (path.is_file() and "results" not in path.relative_to(root).parts
                    and "__pycache__" not in path.parts):
                files.append(path)
    files += [p for p in root.glob("CMake*") if p.is_file()]
    entries = {str(p.relative_to(root)).replace("\\", "/"): sha256(p) for p in sorted(files)}
    cache = build / "CMakeCache.txt"
    dependencies = {}
    tools = {"git_version": command(root, "git", "--version")}
    runtime_resources = {}
    # Include headers and sources from the ACTUAL FetchContent source overrides.
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith("CMAKE_MAKE_PROGRAM:"):
            tools["build_tool_version"] = command(root, line.split("=", 1)[1], "--version")
        if line.startswith("LOGIC_SIMULATOR_FONT:"):
            font = Path(line.split("=", 1)[1])
            if font.is_file():
                runtime_resources[str(font)] = sha256(font)
        if ("_SOURCE_DIR:" in line or line.startswith("FETCHCONTENT_SOURCE_DIR_")) and "=" in line:
            name, value = line.split("=", 1)
            directory = Path(value)
            if directory.is_dir() and directory.resolve() != root:
                hashes = {str(p.relative_to(directory)).replace("\\", "/"): sha256(p)
                          for p in sorted(directory.rglob("*"))
                          if p.is_file() and p.suffix in (".h", ".hpp", ".inl", ".c", ".cpp")
                          and ".git" not in p.parts}
                if hashes:
                    dependencies[name.split(":")[0]] = {"directory": str(directory), "files": hashes}
    configuration = {}
    for name in ("CMakeCache.txt", "compile_commands.json", "build.ninja", "CMakeFiles/rules.ninja"):
        path = build / name
        if path.is_file():
            configuration[name] = {"sha256": sha256(path), "text": path.read_text(encoding="utf-8")}
    manifest = dict(schema=2, captured_utc=datetime.now(timezone.utc).isoformat(),
                    source_root=str(root), build_root=str(build), configuration=args.config,
                    revision=command(root, "git", "rev-parse", "HEAD"),
                    git_status=command(root, "git", "status", "--porcelain"),
                    compiler=args.compiler, compiler_version=args.compiler_version,
                    cmake_version=args.cmake_version, python_version=sys.version,
                    host=platform.platform(), files=entries, dependencies=dependencies,
                    build_files=configuration, tools=tools, runtime_resources=runtime_resources)
    payload = json.dumps(manifest, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    digest = hashlib.sha256(payload.encode()).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    chunks = ',\n'.join('R"manifest(' + payload[i:i+8000] + ')manifest"'
                        for i in range(0, len(payload), 8000))
    args.output.write_text('#pragma once\ninline constexpr const char* benchmarkManifestChunks[] = {\n'
                           + chunks + '\n};\ninline constexpr auto benchmarkManifestHash = "'
                           + digest + '";\n', encoding="utf-8")


if __name__ == "__main__":
    main()
