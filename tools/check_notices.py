#!/usr/bin/env python3
"""Check that code taken from other projects keeps its copyright notice.

Three checks:

1. Every file listed in tools/third_party_notices.txt still contains the
   notice text recorded for it.
2. Any source file under the scanned folders whose header says it was copied,
   mirrored, ported, adapted or derived from somewhere must be listed there.
3. With --ref DIR (repeatable), any source file that shares at least
   --min-lines non-trivial lines with a file under DIR must be listed there.
   Use it before a release against local checkouts of the projects we learn
   from (libultraship, Dolphin, Cemu, decaf-emu, wut ...).

Exit status is 1 when anything fails, so CI and release scripts can stop.
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tools" / "third_party_notices.txt"
SCAN_DIRS = ["platform", "tools"]
SOURCE_EXT = {".c", ".cc", ".cpp", ".h", ".hpp", ".inl", ".py", ".sh", ".glsl"}
# A provenance phrase only counts when it names an outside project or a URL
# within the same sentence; "derived from the BP/XF registers" is not a copy.
PROVENANCE = re.compile(
    r"\b(mirrored|copied|ported|adapted|derived|taken|lifted|borrowed|based)\s+"
    r"(?:\w+\s+){0,3}(?:from|on)\b[^.]{0,80}?"
    r"(https?://|github|\blibultraship\b|\blus-[\w-]+|\bretroarch\b|\bdolphin-emu\b|"
    r"\bcemu\b|\bdecaf\b|\bwut\b|\bimgui\b|\bsdl\d?\b|\bmesa\b|\bppsspp\b|"
    r"\blibogc\b|\bdevkitpro\b|\baurora\b|\bdusk\b|\bkuribo\b|stack ?overflow)",
    re.IGNORECASE | re.DOTALL,
)
HEADER_LINES = 60


def load_manifest():
    entries = {}
    for n, raw in enumerate(MANIFEST.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "|" not in line:
            sys.exit(f"{MANIFEST.name}:{n}: expected 'path | notice text'")
        path, notice = (part.strip() for part in line.split("|", 1))
        entries.setdefault(path, []).append(notice)
    return entries


def source_files(dirs):
    for d in dirs:
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_file() and p.suffix in SOURCE_EXT and "__pycache__" not in p.parts:
                yield p


def norm_lines(path):
    out = set()
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return out
    for line in text.splitlines():
        s = " ".join(line.split())
        if len(s) > 30 and not s.startswith(("//", "/*", "*", "#include")):
            out.add(s)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref", action="append", default=[], help="reference source tree")
    ap.add_argument("--min-lines", type=int, default=20)
    args = ap.parse_args()

    manifest = load_manifest()
    failures = []

    for rel, notices in manifest.items():
        p = ROOT / rel
        if not p.is_file():
            failures.append(f"{rel}: listed in {MANIFEST.name} but missing")
            continue
        text = p.read_text(errors="replace")
        for notice in notices:
            if notice not in text:
                failures.append(f"{rel}: notice text not found: {notice!r}")

    for p in source_files(SCAN_DIRS):
        rel = p.relative_to(ROOT).as_posix()
        if rel in manifest or p == pathlib.Path(__file__).resolve():
            continue
        head = "\n".join(p.read_text(errors="replace").splitlines()[:HEADER_LINES])
        m = PROVENANCE.search(head)
        if m:
            failures.append(
                f"{rel}: header says '{m.group(0)}' but the file is not in "
                f"{MANIFEST.name}; keep the original notice and list it"
            )

    if args.ref:
        ref_lines = {}
        for ref in args.ref:
            for p in pathlib.Path(ref).rglob("*"):
                if p.is_file() and p.suffix in SOURCE_EXT:
                    for s in norm_lines(p):
                        ref_lines.setdefault(s, p)
        for p in source_files(SCAN_DIRS):
            rel = p.relative_to(ROOT).as_posix()
            if rel in manifest:
                continue
            shared = [s for s in norm_lines(p) if s in ref_lines]
            if len(shared) >= args.min_lines:
                example = ref_lines[shared[0]]
                failures.append(
                    f"{rel}: shares {len(shared)} lines with reference code "
                    f"(e.g. {example}); check its licence and list it"
                )

    if failures:
        print("Third-party notice check FAILED:")
        for f in failures:
            print("  " + f)
        return 1
    print(f"Third-party notice check passed ({len(manifest)} listed files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
