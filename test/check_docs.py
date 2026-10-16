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
  7. a @ref is not immediately followed by a colon -- Doxygen's reference
     grammar includes ':' so that Class::member resolves, so "@ref foo.h: and
     then some prose" is read as a reference to "foo.h:" and does not
  8. every relative link in the running record (STATUS.md, CHANGELOG.md) and
     the design notes (docs/design/*.md) names a file that exists, and an
     anchor into a Markdown file names a heading that exists

Deliberately narrower than Doxygen's WARN_IF_UNDOCUMENTED, which also fires on
file-static internals. A comment on `s_scroll` saying "scroll position" is
noise; a public function with no documentation, or an @param that silently
stopped matching after a rename, is not.

Exit status is the number of problems, so CI fails without extra plumbing.

Paths are resolved from the repository root, found from this file's own
location, so the result does not depend on the directory the script is run
from.
"""

import glob
import functools
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

HEADERS = sorted(glob.glob("src/*.h"))
SOURCES = sorted(glob.glob("src/*.cpp") + glob.glob("*.ino") + HEADERS)
LINKED_DOCS = sorted(glob.glob("STATUS.md") + glob.glob("CHANGELOG.md") +
                     glob.glob("docs/design/*.md"))


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


def strip_code(text):
    """Drop fenced blocks and inline code spans; brackets there are not links."""
    text = re.sub(r"^```.*?^```", "", text, flags=re.S | re.M)
    return re.sub(r"`[^`\n]*`", "", text)


def heading_text(title):
    """A heading's rendered text: links become their label, markup is dropped."""
    title = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", title)   # [label](url)
    title = re.sub(r"<[^>]+>", "", title)                         # inline HTML
    return title.replace("`", "")


@functools.lru_cache(maxsize=None)
def heading_anchors(path):
    """The anchors GitHub generates for the headings of a Markdown file."""
    anchors, seen = set(), {}
    text = re.sub(r"^```.*?^```", "", open(path, encoding="utf-8").read(),
                  flags=re.S | re.M)
    for title in re.findall(r"^#{1,6}\s+(.*?)\s*#*\s*$", text, re.M):
        slug = re.sub(r"[^\w\- ]", "", heading_text(title).lower()).replace(" ", "-")
        n = seen.get(slug, 0)
        seen[slug] = n + 1
        anchors.add(slug if n == 0 else f"{slug}-{n}")
    return frozenset(anchors)


def check_links(path):
    """Rule 8: relative links in one Markdown file resolve."""
    problems = []
    # Code is not prose: a fenced block or an inline span may hold brackets.
    text = strip_code(open(path, encoding="utf-8").read())
    # Inline links: <angle-bracketed> targets may hold spaces and parentheses;
    # bare targets may hold one level of balanced parentheses. A title in
    # double quotes, single quotes or parentheses may follow.
    title = r"""(?:\s+(?:"[^"]*"|'[^']*'|\([^)]*\)))?"""
    targets = re.findall(r"\]\(\s*<([^>]+)>" + title + r"\s*\)", text)
    targets += re.findall(r"\]\(\s*((?:[^()\s<]|\([^()\s]*\))+)" + title + r"\s*\)", text)
    # Reference definitions; [^1]: is a footnote, not a link.
    targets += re.findall(r"^\s*\[(?!\^)[^\]]+\]:\s*<?([^\s>]+)>?", text, re.M)
    for target in targets:
        if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
            continue                      # https:, mailto: -- not ours to check
        file_part, _, anchor = target.partition("#")
        dest = os.path.normpath(os.path.join(os.path.dirname(path), file_part)) \
            if file_part else path
        if not os.path.exists(dest):
            problems.append(f"{path}: link '{target}' names a missing file")
        elif anchor and dest.endswith(".md") and anchor not in heading_anchors(dest):
            problems.append(f"{path}: link '{target}' names a missing heading")
    return problems


def main():
    problems = []
    documented = 0
    symbols = collect_symbols()

    if not SOURCES:
        print(f"no sources found under {ROOT}/src")
        return 1

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
                # Strip a default argument first. Without this the name is read
                # off the end of the whole clause, so `bool pressed = false`
                # reports its parameter as "false" -- and then demands that a
                # parameter called "false" be documented.
                arg = arg.split("=")[0]
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

        # --- 7. ...and must not run into a colon ---
        #
        # Rule 4 cannot see this. Its own pattern stops at the colon, so the
        # target looks fine and resolves; Doxygen's does not stop there, because
        # ':' is how Class::member is written. The result is a reference to
        # "pio_uart_rx.h:", which resolves to nothing -- and the only thing that
        # ever said so was Doxygen, in CI, after the commit.
        for m in re.finditer(r"@(?:ref|see)\s+([\w./]+):(?!:)", text):
            problems.append(
                f"{path}: @ref '{m.group(1)}:' runs into a colon; Doxygen reads "
                f"the colon as part of the name")

    # --- 1. public functions in headers need documentation ---
    for path in HEADERS:
        text = open(path, encoding="utf-8").read()
        for m in re.finditer(r"^((?:const\s+)?[\w:*]+(?:\s*\*)?)\s+(\w+)\s*\([^;]*\);",
                             text, re.M):
            before = text[:m.start()].rstrip()
            if not before.endswith("*/"):
                problems.append(f"{path}: {m.group(2)}() has no doc comment")

    # --- 8. relative links in the running record and design notes ---
    for path in LINKED_DOCS:
        problems.extend(check_links(path))

    print(f"checked {len(SOURCES)} files, {documented} documented signatures, "
          f"links in {len(LINKED_DOCS)} documents")
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for p in problems:
            print(f"  {p}")
        return len(problems)
    print("documentation checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
