"""Build the test suite against Dear ImGui revisions and run it on each.

Usage:
    python tests/imgui_walk.py --imgui-repo <imgui clone> --work <directory> [options] [revision...]

A revision is anything 'git archive' accepts in the clone: a release tag, a
docking tag such as v1.92.9b-docking, or a commit. A '-strict' suffix builds
with IMGUI_DISABLE_OBSOLETE_FUNCTIONS and IMGUI_DISABLE_OBSOLETE_KEYIO.
Without revisions, every release from v1.89 on is walked, newest first.

The clone is only read: sources are extracted with 'git archive'. CMake and a
compiler must be on PATH; on Windows run from a Visual Studio developer prompt.
Requires Python 3.12.
"""
import argparse
import concurrent.futures
import io
import pathlib
import re
import subprocess
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
IMGUI_FILES = [
    "imconfig.h", "imgui.cpp", "imgui.h", "imgui_draw.cpp", "imgui_internal.h", "imgui_tables.cpp", "imgui_widgets.cpp",
    "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h",
    "imgui_stacklayout.cpp", "imgui_stacklayout.h", "imgui_stacklayout_internal.h",
]
STRICT_DEFINITIONS = "IMGUI_DISABLE_OBSOLETE_FUNCTIONS;IMGUI_DISABLE_OBSOLETE_KEYIO"


def run(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, errors="replace")
    return result.returncode, result.stdout + result.stderr


def release_tags(repo, oldest=(89, 0)):
    code, output = run(["git", "tag", "--list", "v1.*"], repo)
    if code != 0:
        sys.exit(output)
    releases = []
    for tag in output.split():
        match = re.fullmatch(r"v1\.(\d+)(?:\.(\d+))?([a-z]?)", tag)
        if match:
            version = (int(match.group(1)), int(match.group(2) or 0), match.group(3))
            if version[:2] >= oldest:
                releases.append((version, tag))
    return [tag for _, tag in sorted(releases, reverse=True)]


def extract(repo, revision, destination):
    if (destination / "imgui.h").exists():
        return
    code, names = run(["git", "ls-tree", "--name-only", revision], repo)
    if code != 0:
        raise RuntimeError(f"unknown revision {revision}: {names.strip()}")
    files = [name for name in IMGUI_FILES if name in names.split()]
    archive = subprocess.run(["git", "archive", "--format=tar", revision] + files, cwd=repo, capture_output=True, check=True).stdout
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(destination, filter="data")


def walk(args, revision):
    strict = revision.endswith("-strict")
    source = revision[:-len("-strict")] if strict else revision
    try:
        extract(args.imgui_repo, source, args.work / "imgui" / source)
    except RuntimeError as error:
        return revision, "NO SOURCES", str(error)

    # Commit hashes are shortened to keep build paths within Windows' limit.
    build = args.work / "build" / re.sub(r"\b([0-9a-f]{12})[0-9a-f]{28}\b", r"\1", revision)
    configure = ["cmake", "-S", str(TESTS), "-B", str(build), "-G", args.generator,
                 f"-DCMAKE_BUILD_TYPE={args.config}", f"-DIMGUI_DIR={args.work / 'imgui' / source}"]
    if strict:
        configure.append(f"-DIMGUI_NODE_EDITOR_TESTS_DEFINITIONS={STRICT_DEFINITIONS}")
    code, output = run(configure)
    if code != 0:
        return revision, "CONFIGURE FAILED", output[-2000:]

    code, output = run(["cmake", "--build", str(build), "--config", args.config, "--parallel"])
    if code != 0:
        errors = []
        for line in output.splitlines():
            if re.search(r"\berror\b", line) and line not in errors:
                errors.append(line.strip())
        return revision, f"BUILD FAILED ({len(errors)} errors)", "\n".join(errors[:10])

    code, output = run(["ctest", "--test-dir", str(build), "-C", args.config, "--output-on-failure"])
    summary = re.search(r"\d+% tests passed(?:, (\d+) tests? failed)? out of (\d+)", output)
    if not summary:
        return revision, "TESTS DID NOT RUN", output[-2000:]
    # CTest lists failed and skipped tests the same way, and counts skipped ones as passed.
    failed  = re.findall(r"^\s*\d+ - (\S+) \((?!Skipped\))", output, re.M)
    skipped = re.findall(r"^\s*\d+ - (\S+) \(Skipped\)", output, re.M)
    passed  = int(summary.group(2)) - int(summary.group(1) or 0) - len(skipped)
    status  = f"{passed}/{summary.group(2)} passed" + (f", {len(skipped)} skipped" if skipped else "")
    return revision, status if not failed else status + " FAILED", "\n".join(failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--imgui-repo", type=pathlib.Path, required=True, help="clone of Dear ImGui with release tags")
    parser.add_argument("--work", type=pathlib.Path, required=True, help="directory for extracted sources and builds")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--jobs", type=int, default=2, help="revisions built at the same time")
    parser.add_argument("revisions", nargs="*")
    args = parser.parse_args()
    args.imgui_repo = args.imgui_repo.resolve()
    args.work = args.work.resolve()

    revisions = args.revisions or release_tags(args.imgui_repo)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(walk, args, revision): revision for revision in revisions}
        for future in concurrent.futures.as_completed(futures):
            revision, status, details = future.result()
            print(f"{revision:>24}  {status}", flush=True)
            for line in details.splitlines():
                print(f"{'':>26}{line}", flush=True)


if __name__ == "__main__":
    main()
