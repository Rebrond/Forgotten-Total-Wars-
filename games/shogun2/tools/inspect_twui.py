#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import re
import struct
from typing import Iterable


def load_bytes(path: pathlib.Path) -> bytes:
    return path.read_bytes()


def ascii_strings(data: bytes, min_len: int = 4) -> list[tuple[int, str]]:
    pattern = re.compile(rb"[\x20-\x7e]{%d,}" % min_len)
    return [(m.start(), m.group().decode("latin-1")) for m in pattern.finditer(data)]


def utf16le_strings(data: bytes, min_len: int = 4) -> list[tuple[int, str]]:
    pattern = re.compile(rb"(?:[\x20-\x7e]\x00){%d,}" % min_len)
    results: list[tuple[int, str]] = []
    for match in pattern.finditer(data):
        try:
            results.append((match.start(), match.group().decode("utf-16le")))
        except UnicodeDecodeError:
            continue
    return results


def iter_u32(data: bytes, start: int, end: int) -> Iterable[tuple[int, int]]:
    lo = max(0, start)
    hi = min(len(data), end)
    for offset in range(lo, hi - 3, 4):
        yield offset, struct.unpack_from("<I", data, offset)[0]


def hexdump(data: bytes, start: int, size: int) -> str:
    chunk = data[max(0, start): min(len(data), start + size)]
    lines: list[str] = []
    for row in range(0, len(chunk), 16):
        part = chunk[row: row + 16]
        hex_part = " ".join(f"{b:02x}" for b in part)
        ascii_part = "".join(chr(b) if 32 <= b <= 126 else "." for b in part)
        lines.append(f"{start + row:08x}  {hex_part:<47}  {ascii_part}")
    return "\n".join(lines)


def printable_nearby_ints(data: bytes, center: int, radius: int) -> list[tuple[int, int]]:
    values: list[tuple[int, int]] = []
    for offset, value in iter_u32(data, center - radius, center + radius):
        if value == 0 or value == 0xFFFFFFFF:
            continue
        if value <= 4096:
            values.append((offset, value))
    return values


def cmd_strings(path: pathlib.Path, pattern: str | None, min_len: int, limit: int) -> int:
    data = load_bytes(path)
    hits = ascii_strings(data, min_len) + utf16le_strings(data, min_len)
    hits.sort(key=lambda item: item[0])

    shown = 0
    for offset, text in hits:
        if pattern and pattern.lower() not in text.lower():
            continue
        print(f"{offset:08x}  {text}")
        shown += 1
        if shown >= limit:
            break

    return 0


def cmd_context(path: pathlib.Path, needle: str, radius: int, dump_size: int) -> int:
    data = load_bytes(path)
    text = data.decode("latin-1", errors="ignore")
    index = text.find(needle)
    if index < 0:
        print(f"Needle not found: {needle}")
        return 1

    print(f"needle={needle!r} offset=0x{index:08x}")
    print()
    print("Nearby small u32 values:")
    for offset, value in printable_nearby_ints(data, index, radius):
        print(f"  0x{offset:08x}  {value}")
    print()
    print(hexdump(data, max(0, index - radius), dump_size))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect compiled Total War UI layout files.")
    subparsers = parser.add_subparsers(dest="cmd", required=True)

    strings_parser = subparsers.add_parser("strings", help="List ASCII/UTF-16LE strings.")
    strings_parser.add_argument("path", type=pathlib.Path)
    strings_parser.add_argument("--pattern")
    strings_parser.add_argument("--min-len", type=int, default=4)
    strings_parser.add_argument("--limit", type=int, default=200)

    context_parser = subparsers.add_parser("context", help="Show hexdump and nearby ints around a needle.")
    context_parser.add_argument("path", type=pathlib.Path)
    context_parser.add_argument("needle")
    context_parser.add_argument("--radius", type=int, default=128)
    context_parser.add_argument("--dump-size", type=int, default=384)

    args = parser.parse_args()
    if args.cmd == "strings":
        return cmd_strings(args.path, args.pattern, args.min_len, args.limit)
    if args.cmd == "context":
        return cmd_context(args.path, args.needle, args.radius, args.dump_size)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
