#!/usr/bin/env python3
"""List Cyrillic-only single words from a Nutrimatic index dump with frequency >= N."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

DUMP_LINE = re.compile(r"^\s*(\d+)\s+\[(.*)\]\s*$")
CYRILLIC_ONLY = re.compile(r"^[а-яё]+$")


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def parse_dump_line(line: str) -> tuple[int, str] | None:
    match = DUMP_LINE.match(line)
    if not match:
        return None
    count = int(match.group(1))
    # Bracket content is the raw phrase; trailing space marks a word boundary.
    text = match.group(2).strip()
    return count, text


def iter_dump_lines(index_path: Path, dump_index: Path):
    proc = subprocess.Popen(
        [str(dump_index), str(index_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    assert proc.stdout is not None
    try:
        for line in proc.stdout:
            yield line
    finally:
        proc.stdout.close()
        stderr = proc.stderr.read() if proc.stderr else ""
        code = proc.wait()
        if code != 0:
            raise RuntimeError(
                f"{dump_index} failed with exit code {code}: {stderr.strip()}"
            )


def iter_index_txt(txt_path: Path):
    with txt_path.open(encoding="utf-8", errors="replace") as fp:
        for line in fp:
            yield line


def collect_russian_words(lines, min_count: int) -> list[tuple[int, str]]:
    results: list[tuple[int, str]] = []
    for line in lines:
        parsed = parse_dump_line(line)
        if parsed is None:
            continue
        count, text = parsed
        if count < min_count or not text:
            continue
        if " " in text:
            continue
        if CYRILLIC_ONLY.match(text):
            results.append((count, text))
    results.sort(key=lambda item: (-item[0], item[1]))
    return results


def main() -> int:
    root = project_root()
    default_output = Path(__file__).resolve().parent / "output" / "russian_words.txt"

    parser = argparse.ArgumentParser(
        description=(
            "Extract Cyrillic-only single words from a Nutrimatic index "
            "text dump with frequency >= N."
        )
    )
    parser.add_argument(
        "--index",
        type=Path,
        default=root / "wiki-merged.index",
        help="Path to merged .index file (default: wiki-merged.index)",
    )
    parser.add_argument(
        "--index-txt",
        type=Path,
        help="Use an existing dump-index text file instead of running dump-index",
    )
    parser.add_argument(
        "--dump-index",
        type=Path,
        default=root / "build" / "dump-index",
        help="Path to dump-index binary (default: build/dump-index)",
    )
    parser.add_argument(
        "-n",
        "--min-count",
        type=int,
        default=10,
        help="Minimum occurrence count (default: 10)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=default_output,
        help=f"Output file (default: {default_output})",
    )
    parser.add_argument(
        "--save-dump",
        type=Path,
        help="Also save the full index text dump to this path",
    )
    args = parser.parse_args()

    if args.min_count < 1:
        parser.error("--min-count must be >= 1")

    if args.index_txt:
        if not args.index_txt.is_file():
            parser.error(f"index txt not found: {args.index_txt}")
        source = iter_index_txt(args.index_txt)
        source_name = str(args.index_txt)
    else:
        if not args.index.is_file():
            parser.error(f"index not found: {args.index}")
        if not args.dump_index.is_file():
            parser.error(f"dump-index not found: {args.dump_index}")
        source = iter_dump_lines(args.index, args.dump_index)
        source_name = f"{args.dump_index} {args.index}"

    if args.save_dump:
        args.save_dump.parent.mkdir(parents=True, exist_ok=True)
        with args.save_dump.open("w", encoding="utf-8") as dump_fp:
            buffered_lines = []
            for line in source:
                dump_fp.write(line)
                buffered_lines.append(line)
            source = buffered_lines
        source_name = str(args.save_dump)

    print(f"Reading {source_name} ...", file=sys.stderr)
    words = collect_russian_words(source, args.min_count)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as out:
        for count, text in words:
            out.write(f"{count}\t{text}\n")

    print(
        f"Wrote {len(words)} Cyrillic-only words with count >= {args.min_count} "
        f"to {args.output}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
