#!/usr/bin/env python3
"""Documentation checks for DshotDisplay.

Enforces what actually goes wrong, and nothing else:

  1. every function declared in a header carries a doc comment
  2. every @param names a parameter that exists in the signature it precedes
  3. @param coverage is all-or-nothing -- Doxygen warns on partial coverage
     regardless of WARN_NO_PARAMDOC, so documenting one argument out of six
     is worse than documenting none
  4. every @ref points at something that exists
  5. Doxygen commands are escaped when merely being talked about; writing
     "@mainpage" in prose makes Doxygen execute it, which silently produces a
     second main page
  6. backticks inside a doc comment are balanced, otherwise Doxygen hits the
     end of the block still waiting to close a code span

Deliberately narrower than Doxygen's WARN_IF_UNDOCUMENTED, which also fires on
file-static internals. A comment on `s_scroll` saying "scroll position" is
noise; a public function with no documentation, or an @param that silently
stopped matching after a rename, is not.

Exit status is the number of problems, so CI fails without extra plumbing.
"""

import glob
import re
import sys

HEADERS = sorted(glob.glob("src/*.h"))
SOURCES = sorted(glob.glob("src/*.cpp") + glob.glob("*.ino") + HEADERS)


def collect_symbols():
    """Everything an @ref could legitimately point at."""
    symbols = set()
    for path in SOURCES:
        text = open(path, encoding="utf-8").read()
        base = path.split("/")[-1]
        symbols.update({path, base, base.replace(".ino", "")})
        symbols.update(re.findall(r"^\s*#define\s+(\w+)", text, re.M))
        symbols.update(re.findall(r"@defgroup\s+(\w+)", text))
        symbols.update(re.findall(r"@page\s+(\w+)", text))
        symbols.update(re.findall(r"^\s*enum\s+(?:class\s+)?(\w+)", text, re.M))
        symbols.update(re.findall(r"^\s*struct\s+(\w+)", text, re.M))
        symbols.update(re.findall(r"^\s*extern\s+[\w\s:*]+?(\w+)\s*[\[;]", text, re.M))
        symbols.update(
            re.findall(r"^\s*(?:static\s+)?(?:volatile\s+)?[\w:*<>]+\s+(\w+)\s*[=;(\[]",
                       text, re.M))
    return symbols


def main():
    problems = []
    documented = 0
    symbols = collect_symbols()

    for path in SOURCES:
        text = open(path, encoding="utf-8").read()

        # --- 2. @param names must match the signature that follows ---
        for m in re.finditer(r"/\*\*(.*?)\*/\s*\n([^\n]*)", text, re.S):
            names = []
            for group in re.findall(r"@param(?:\[[^\]]*\])?\s+([\w,]+)", m.group(1)):
                names.extend(group.split(","))
            if not names:
                continue
            documented += 1
            window = m.group(2) + text[m.end(2):m.end(2) + 300]
            sig = re.match(r".*?\((.*?)\)", window, re.S)
            if not sig:
                problems.append(f"{path}: @param block precedes no signature")
                continue
            actual = []
            for arg in sig.group(1).split(","):
                found = re.findall(r"(\w+)\s*$", arg.strip())
                if found and found[0] != "void":
                    actual.append(found[0])
            for name in names:
                if name not in actual:
                    problems.append(
                        f"{path}: @param '{name}' is not in ({', '.join(actual)})")

            # --- 3. all-or-nothing coverage ---
            undocumented = [a for a in actual if a not in names]
            if undocumented:
                problems.append(
                    f"{path}: partial @param coverage, missing "
                    f"{', '.join(undocumented)} (document all or none)")

        # --- 5. commands referred to in prose must be escaped ---
        for m in re.finditer(r"/\*\*(.*?)\*/", text, re.S):
            body = m.group(1)
            line = text[:m.start()].count("\n") + 1
            # A real @page/@mainpage is the first command in its block; any
            # later one is prose that Doxygen will execute.
            for cmd in ("mainpage", "page"):
                for hit in re.finditer(r"(?<!\\)@" + cmd + r"\b(.*)", body):
                    before = body[:hit.start()].strip(" *\n")
                    if before:
                        problems.append(
                            f"{path}:{line}: unescaped @{cmd} in prose "
                            f"(write \\@{cmd})")
                    elif cmd == "page" and not hit.group(1).strip():
                        problems.append(f"{path}:{line}: @page has no identifier")

            # --- 6. balanced backticks ---
            if body.count("`") % 2:
                problems.append(
                    f"{path}:{line}: odd number of backticks in a doc comment")

        # --- 4. @ref targets must exist ---
        for ref in re.findall(r"@ref\s+([\w./]+)", text):
            target = ref.rstrip(".")
            if target in symbols:
                continue
            if target.endswith((".h", ".md", ".ino", ".cpp")):
                continue
            problems.append(f"{path}: @ref '{target}' does not resolve")

    # --- 1. public functions in headers need documentation ---
    for path in HEADERS:
        text = open(path, encoding="utf-8").read()
        for m in re.finditer(r"^((?:const\s+)?[\w:*]+(?:\s*\*)?)\s+(\w+)\s*\([^;]*\);",
                             text, re.M):
            before = text[:m.start()].rstrip()
            if not before.endswith("*/"):
                problems.append(f"{path}: {m.group(2)}() has no doc comment")

    print(f"checked {len(SOURCES)} files, {documented} documented signatures")
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for p in problems:
            print(f"  {p}")
        return len(problems)
    print("documentation checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
