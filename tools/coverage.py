#!/usr/bin/env python3
"""Report host-suite line coverage of src/, and keep the published figures.

`make -C test coverage` builds an instrumented copy of the host suite in
test/cov/ and runs it. This script reads the counters through gcov (the GNU
coverage tool, part of GCC) and renders two blocks:

  - one sentence in README.md, between the coverage:start and coverage:end
    markers;
  - a table per file in STATUS.md, between the coverage-table:start and
    coverage-table:end markers. STATUS.md and its markers are optional; when
    either is missing, the table goes to stdout instead.

    python3 tools/coverage.py            # write both blocks
    python3 tools/coverage.py --check    # fail on drift or below a floor
    python3 tools/coverage.py --print    # print both blocks, write nothing

`--check` is what CI (continuous integration) runs. It exits 1 when a block
differs from what this script would write, or when coverage is below
MIN_TOTAL or any file is below MIN_FILE.

What counts: every file under src/ that gcov reports a line for. That is each
.cpp in the Makefile's SRCS, the C files of the DShot core in src/dshot/ and
of the SD card protocol layers in src/sdio/ (the Makefile's CSRCS), ui_am32.cpp (test_ui.cpp includes it), and the headers
whose inline functions the suite calls. A line counts as covered when
any translation unit executes it. An inline function no test calls is not
emitted, so it has no lines and does not count. A source in SRCS with no
executable line has to be in DATA_ONLY, and a DATA_ONLY file that gains one
fails the run.

Needs only Python 3 and the gcov that matches the compiler. Line counts
depend on the GCC version, so CI pins its runner image.
"""

import argparse
import difflib
import json
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "src"
COV_DIR = REPO / "test" / "cov"
README = REPO / "README.md"
STATUS = REPO / "STATUS.md"

README_START = "<!-- coverage:start -->"
README_END = "<!-- coverage:end -->"
TABLE_START = "<!-- coverage-table:start -->"
TABLE_END = "<!-- coverage-table:end -->"

# Sources in SRCS that hold only tables: gcc gives them no line counter.
DATA_ONLY = {
    "src/board_desc_lcd2.cpp",
    "src/board_desc_lcd2_8.cpp",
}

# CI fails below these. They sit a few points under the measured figures:
# raise them as the suite improves, and do not lower them to pass a build.
# The per-file floor catches a file the total hides.
MIN_TOTAL = 93
MIN_FILE = 85


def tenths(covered, lines):
    """Percentage to one decimal, rounded half up, in integer arithmetic so
    every machine prints the same digit."""
    if lines == 0:
        return "0.0"
    t = (2000 * covered + lines) // (2 * lines)
    return "%d.%d" % (t // 10, t % 10)


def gcov_json(gcda):
    """The gcov JSON records for one translation unit."""
    proc = subprocess.run(
        ["gcov", "--json-format", "--stdout", gcda.name],
        cwd=gcda.parent, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit("gcov failed on %s:\n%s" % (gcda, proc.stderr))
    return [json.loads(line) for line in proc.stdout.splitlines() if line]


def measure():
    """{src-relative path: {line number: executed}} over every unit."""
    gcda = sorted(COV_DIR.glob("*.gcda"))
    if not gcda:
        sys.exit("no coverage data in %s; run `make -C test coverage` first"
                 % COV_DIR.relative_to(REPO))
    files = {}
    for path in gcda:
        for record in gcov_json(path):
            cwd = pathlib.Path(record["current_working_directory"])
            for f in record["files"]:
                src = (cwd / f["file"]).resolve()
                if SRC not in src.parents:
                    continue
                if not f["lines"]:
                    continue
                rel = src.relative_to(REPO).as_posix()
                lines = files.setdefault(rel, {})
                for line in f["lines"]:
                    n = line["line_number"]
                    lines[n] = lines.get(n, False) or line["count"] > 0
    return files


def completeness(files):
    """One message per compiled source the measurement does not account for.

    The instrumented build names each unit's notes file <binary>-<base>.gcno
    when it compiles and links in one step, and <base>.gcno for a C file
    compiled to an object of its own, so a source is found by its base name
    anywhere under src/.
    """
    problems = []
    compiled = set()
    for gcno in COV_DIR.glob("*.gcno"):
        base = gcno.stem.split("-", 1)[-1]
        compiled.update(p.relative_to(REPO).as_posix()
                        for p in SRC.rglob(base + ".*")
                        if p.suffix in (".cpp", ".c"))
    for rel in sorted(compiled - set(files) - DATA_ONLY):
        problems.append("%s is compiled and has no executable line; "
                        "add it to DATA_ONLY" % rel)
    for rel in sorted(DATA_ONLY & set(files)):
        problems.append("%s is in DATA_ONLY and has executable lines; "
                        "remove it from DATA_ONLY" % rel)
    for rel in sorted(DATA_ONLY - compiled):
        problems.append("%s is in DATA_ONLY and is not compiled into the "
                        "host suite" % rel)
    return problems


def summarise(files):
    """[(path, lines, covered)] sorted by path, and the totals."""
    rows = [(rel, len(lines), sum(lines.values()))
            for rel, lines in sorted(files.items())]
    return rows, sum(r[1] for r in rows), sum(r[2] for r in rows)


def render_readme(rows, lines, covered):
    return ("\nHost-suite line coverage of `src/` is **%s%%**: %s of %s "
            "lines in %d files. CI fails below %d%% in total or below %d%% "
            "in any file.\n"
            % (tenths(covered, lines), format(covered, ","),
               format(lines, ","), len(rows), MIN_TOTAL, MIN_FILE))


def render_table(rows, lines, covered):
    out = ["", "| File | Lines | Covered | Coverage |",
           "| --- | ---: | ---: | ---: |"]
    for rel, n, c in rows:
        out.append("| `%s` | %d | %d | %s%% |" % (rel, n, c, tenths(c, n)))
    out.append("| **Total** | **%d** | **%d** | **%s%%** |"
               % (lines, covered, tenths(covered, lines)))
    out += ["", "Generated by `tools/coverage.py`. CI runs it with "
            "`--check` and fails on drift.", ""]
    return "\n".join(out)


def splice(text, body, start, end):
    """@p text with the span between @p start and @p end replaced, or None
    when either marker is missing."""
    head, sep, rest = text.partition(start)
    if not sep or end not in rest:
        return None
    return head + start + body + end + rest.split(end, 1)[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="exit 1 on drift or below a floor")
    mode.add_argument("--print", action="store_true",
                      help="print both blocks and write nothing")
    args = ap.parse_args()

    files = measure()
    rows, lines, covered = summarise(files)
    readme = render_readme(rows, lines, covered)
    table = render_table(rows, lines, covered)
    failed = False

    for msg in completeness(files):
        print(msg, file=sys.stderr)
        failed = True

    if args.print:
        print(README_START + readme + README_END)
        print(TABLE_START + table + TABLE_END)

    targets = [(README, readme, README_START, README_END, True),
               (STATUS, table, TABLE_START, TABLE_END, False)]
    for path, body, start, end, required in targets:
        original = path.read_text() if path.exists() else None
        updated = splice(original, body, start, end) if original else None
        if updated is None:
            if required:
                sys.exit("%s has no %s / %s markers"
                         % (path.name, start, end))
            if not args.print:
                print("%s or its %s / %s markers are missing; the table "
                      "goes between them:" % (path.name, start, end))
                print(start + body + end)
            continue
        if args.print:
            continue
        if args.check:
            if updated != original:
                print("%s coverage block is out of date; run "
                      "python3 tools/coverage.py" % path.name,
                      file=sys.stderr)
                sys.stderr.writelines(difflib.unified_diff(
                    original.splitlines(True), updated.splitlines(True),
                    path.name, path.name + " (measured)"))
                failed = True
            else:
                print("%s coverage block is current" % path.name)
        elif updated != original:
            path.write_text(updated)
            print("updated %s" % path.name)
        else:
            print("%s coverage block is current" % path.name)

    print("total line coverage %s%% (%d of %d lines, %d files), floor %d%%"
          % (tenths(covered, lines), covered, lines, len(rows), MIN_TOTAL))
    if 100 * covered < MIN_TOTAL * lines:
        print("total coverage is below the %d%% floor" % MIN_TOTAL,
              file=sys.stderr)
        failed = True
    for rel, n, c in rows:
        if 100 * c < MIN_FILE * n:
            print("%s at %s%% is below the %d%% per-file floor"
                  % (rel, tenths(c, n), MIN_FILE), file=sys.stderr)
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
