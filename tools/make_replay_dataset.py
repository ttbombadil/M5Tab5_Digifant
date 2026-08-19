#!/usr/bin/env python3
"""Convert a logger console capture into a logical KWP1281 replay CSV."""

import csv
import re
import sys
from pathlib import Path


BLOCK_LENGTH = re.compile(r"\[KWP RX\] Block Length=0x([0-9A-Fa-f]+)")
RX_BYTE = re.compile(r"rx byte\[\d+\]=0x([0-9A-Fa-f]{2})")
GROUP_MARKER = re.compile(r"\[M4 (?:HEADER|GROUP) group ([0-4])")


def parse_capture(path: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    current_group = ""

    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        group_match = GROUP_MARKER.search(line)
        if group_match:
            current_group = group_match.group(1)
            if records and records[-1]["title"] in {"0x02", "0xF4"}:
                records[-1]["group"] = current_group

        length_match = BLOCK_LENGTH.search(line)
        if length_match:
            current = {"length": int(length_match.group(1), 16), "bytes": []}
            continue

        if current is None:
            continue

        byte_match = RX_BYTE.search(line)
        if byte_match:
            current["bytes"].append(int(byte_match.group(1), 16))
            continue

        if "[KWP RX END] 0x03" not in line:
            continue

        length = current["length"]
        body = current["bytes"]
        frame = [length, *body, 0x03]
        if len(frame) == length + 1 and len(frame) >= 4:
            records.append(
                {
                    "index": len(records),
                    "group": current_group,
                    "title": f"0x{frame[2]:02X}",
                    "length": length,
                    "frame_hex": " ".join(f"{value:02X}" for value in frame),
                    "payload_hex": " ".join(f"{value:02X}" for value in frame[3:-1]),
                }
            )
        current = None

    return records


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} CAPTURE OUTPUT_CSV", file=sys.stderr)
        return 2

    records = parse_capture(Path(sys.argv[1]))
    with Path(sys.argv[2]).open("w", newline="", encoding="ascii") as output:
        writer = csv.DictWriter(
            output,
            fieldnames=["index", "group", "title", "length", "frame_hex", "payload_hex"],
        )
        writer.writeheader()
        writer.writerows(records)

    print(f"wrote {len(records)} complete blocks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())