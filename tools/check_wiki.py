#!/usr/bin/env python3
"""Structural checks for the wiki in wiki/.

A GitHub wiki has no build step, so nothing tells you a link is broken until a
reader clicks it. These are the four ways this particular wiki can break, and
nothing else:

  1. a link points at a page that does not exist -- usually a rename, since page
     names come from filenames and a rename is silent;
  2. a link points at an anchor no heading produces;
  3. an image is referenced that the test suite does not capture, which is what
     happens when a screenshot is dropped or renamed upstream;
  4. the two languages drift apart -- a page added in one and not the other, or
     a page missing the switcher that gets you to its counterpart.

Run it after `python3 tools/make_previews.py --wiki`, or the image check has
nothing to check against. Exit status is the number of problems, so CI fails
without extra plumbing.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WIKI = os.path.join(ROOT, "wiki")
IMG = os.path.join(WIKI, "img")

# Each English page and its German counterpart. Written out rather than
# derived, because the pairing is a fact about the content and there is no
# naming rule that could express it -- "SD Logging" and "SD-Aufzeichnung" are
# the same page and share no substring worth matching on.
PAIRS = [
    ("First-Run", "Erste-Schritte"),
    ("The-Screens", "Die-Bildschirme"),
    ("Telemetry", "Telemetrie"),
    ("SD-Logging", "SD-Aufzeichnung"),
    ("AM32-Configuration", "AM32-Konfiguration"),
    ("Troubleshooting", "Fehlersuche"),
]

# Pages that are not part of the language pairing: the shared landing page and
# GitHub's two special includes.
SINGLETONS = ["Home", "_Sidebar", "_Footer"]

LINK = re.compile(r"\[([^\]]*)\]\(([^)]+)\)")
MD_IMG = re.compile(r"!\[([^\]]*)\]\(([^)]+)\)")
HTML_IMG = re.compile(r"<img\s+[^>]*src=\"([^\"]+)\"", re.I)
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*$", re.M)


def slug(text):
    """GitHub's heading-anchor rule, as far as this wiki exercises it.

    Lowercase, drop punctuation, spaces to hyphens. Non-ASCII letters survive --
    which matters here, because the German pages have headings that GitHub
    anchors with their umlauts intact.
    """
    s = text.strip().lower()
    s = re.sub(r"`|\*|_", "", s)
    s = re.sub(r"[^\w\s-]", "", s, flags=re.UNICODE)
    return re.sub(r"\s+", "-", s.strip())


def anchors(body):
    return {slug(m.group(2)) for m in HEADING.finditer(body)}


def main():
    pages = {}
    if os.path.isdir(WIKI):
        for name in sorted(os.listdir(WIKI)):
            if name.endswith(".md"):
                with open(os.path.join(WIKI, name), encoding="utf-8") as f:
                    pages[name[:-3]] = f.read()

    # No pages is not a failure. This tool is allowed to exist in a tree that
    # has no manual yet -- it did for exactly one commit -- and a checker that
    # fails on the absence of the thing it checks is a checker that has to be
    # introduced in the same commit as its subject, which is a worse split.
    if not pages:
        print("wiki: no pages in wiki/, nothing to check")
        return 0

    problems = []

    # --- 4a. both languages carry the same set of pages -------------------
    expected = set(SINGLETONS)
    for en, de in PAIRS:
        expected.add(en)
        expected.add(de)
    for missing in sorted(expected - set(pages)):
        problems.append("missing page: %s.md" % missing)
    for extra in sorted(set(pages) - expected):
        problems.append(
            "%s.md is not in PAIRS or SINGLETONS -- add it, in both languages"
            % extra
        )

    # --- 4b. every paired page links to its counterpart -------------------
    for en, de in PAIRS:
        for page, other in ((en, de), (de, en)):
            body = pages.get(page)
            if body is None:
                continue
            head = body.split("\n", 1)[0]
            if "(%s)" % other not in head:
                problems.append(
                    "%s.md: first line is not the language switcher pointing at %s"
                    % (page, other)
                )

    # --- 1 and 2. internal links --------------------------------------------
    for page, body in sorted(pages.items()):
        for m in LINK.finditer(body):
            # `![alt](src)` is an image, and the link pattern matches its tail.
            # Tell them apart by the character in front of the bracket -- the
            # first version of this reconstructed "![%s](%s)" and tested *that*
            # against the image pattern, which of course always matched, so
            # every link on the wiki was skipped and this check verified
            # nothing. It took a deliberately broken link to notice.
            if m.start() > 0 and body[m.start() - 1] == "!":
                continue
            target = m.group(2)
            if re.match(r"^(https?:|mailto:|#)", target):
                continue
            if target.startswith("img/"):
                continue
            name, _, anchor = target.partition("#")
            if name and name not in pages:
                problems.append(
                    "%s.md: link to page '%s', which does not exist" % (page, name)
                )
                continue
            if anchor:
                target_body = pages[name] if name else body
                if anchor not in anchors(target_body):
                    problems.append(
                        "%s.md: link to '%s#%s', but no heading there makes that anchor"
                        % (page, name or page, anchor)
                    )

    # --- 3. images ----------------------------------------------------------
    have = set(os.listdir(IMG)) if os.path.isdir(IMG) else set()
    if not have:
        problems.append(
            "wiki/img/ is empty or absent -- run: python3 tools/make_previews.py --wiki"
        )
    used = set()
    for page, body in sorted(pages.items()):
        srcs = [s for _, s in MD_IMG.findall(body)] + HTML_IMG.findall(body)
        for src in srcs:
            if src.startswith("http"):
                continue
            if not src.startswith("img/"):
                problems.append("%s.md: image '%s' is not under img/" % (page, src))
                continue
            base = src[len("img/"):]
            used.add(base)
            if have and base not in have:
                problems.append(
                    "%s.md: image '%s' was not rendered by the test suite" % (page, src)
                )

    for problem in problems:
        print("wiki: %s" % problem, file=sys.stderr)

    unused = sorted(have - used)
    if unused:
        # Not a failure. Every captured screen is published whether or not a
        # page shows it, and having spares is better than a page that wants a
        # screenshot nobody takes.
        print("wiki: %d rendered screen(s) not shown on any page: %s"
              % (len(unused), ", ".join(unused)))

    if problems:
        print("\n%d wiki problem(s)" % len(problems), file=sys.stderr)
        return len(problems)

    print("wiki: %d pages, %d images, links and anchors all resolve"
          % (len(pages), len(used)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
