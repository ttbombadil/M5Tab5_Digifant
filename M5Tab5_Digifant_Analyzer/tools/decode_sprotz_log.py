#!/usr/bin/env python3
"""Convert version-1 and version-2 Digifant SPROTZEN logs to CSV."""

import argparse
import csv
import struct
import sys
from pathlib import Path

HEADER_SIZE = 32
RECORD_PREFIX_SIZE = 72
FIELD_SIZE = 40
FIELD_COUNT = 26
RECORD_SIZE = RECORD_PREFIX_SIZE + FIELD_COUNT * FIELD_SIZE
KINDS = {1: "SNAPSHOT", 2: "START", 3: "STOP", 4: "MARKER"}
FIELD_KEYS = [(0, zone) for zone in range(10)] + [
    (group, zone) for group in range(1, 5) for zone in range(1, 5)
]


def decode_v1_bytes(data: bytes, output) -> None:
    if len(data) < HEADER_SIZE or data[:8] != b"DGFTSPT1":
        raise ValueError("not a DGFTSPT1 log")
    version, header_size, record_size, field_count = struct.unpack_from("<HHIH", data, 8)
    if (version, header_size, record_size, field_count) != (1, HEADER_SIZE, RECORD_SIZE, FIELD_COUNT):
        raise ValueError(
            f"unsupported schema version={version} header={header_size} "
            f"record={record_size} fields={field_count}"
        )
    payload = len(data) - HEADER_SIZE
    if payload % RECORD_SIZE:
        raise ValueError(f"truncated log: {payload % RECORD_SIZE} trailing bytes")

    columns = [
        "kind", "event_subtype", "event_timestamp_us", "snapshot_timestamp_us", "sequence", "session_epoch",
        "transport_generation", "frame_count", "frame_drops", "rx_drops", "parser_rejects",
        "action_failures", "snapshot_overwrites", "fault_count", "rpm", "byte_fault",
        "validity", "k409", "kwp", "ecu",
    ]
    for group, zone in FIELD_KEYS:
        prefix = f"g{group:03d}_z{zone}"
        columns.extend([
            f"{prefix}_raw", f"{prefix}_value", f"{prefix}_formula", f"{prefix}_nwb",
            f"{prefix}_unit", f"{prefix}_semantic", f"{prefix}_semantic_evidence",
            f"{prefix}_formula_evidence", f"{prefix}_status", f"{prefix}_timestamp_us",
            f"{prefix}_sequence", f"{prefix}_session_epoch", f"{prefix}_transport_generation",
        ])
    writer = csv.DictWriter(output, fieldnames=columns)
    writer.writeheader()

    for offset in range(HEADER_SIZE, len(data), RECORD_SIZE):
        record = data[offset:offset + RECORD_SIZE]
        if struct.unpack_from("<I", record, 0)[0] != 0x31524344:
            raise ValueError(f"invalid record magic at byte {offset}")
        kind = record[6]
        event_us, snapshot_us = struct.unpack_from("<QQ", record, 8)
        sequence, session, generation, frames, frame_drops, rx_drops, rejects, actions, overwrites, faults = \
            struct.unpack_from("<IIIIIIIIII", record, 24)
        rpm = struct.unpack_from("<H", record, 64)[0]
        flags = record[68]
        row = {
            "kind": KINDS.get(kind, f"UNKNOWN_{kind}"), "event_timestamp_us": event_us,
            "event_subtype": "MARKER_LEGACY" if kind == 4 else "",
            "snapshot_timestamp_us": snapshot_us, "sequence": sequence, "session_epoch": session,
            "transport_generation": generation, "frame_count": frames, "frame_drops": frame_drops,
            "rx_drops": rx_drops, "parser_rejects": rejects, "action_failures": actions,
            "snapshot_overwrites": overwrites, "fault_count": faults, "rpm": rpm,
            "byte_fault": record[66], "validity": record[67], "k409": int(bool(flags & 1)),
            "kwp": int(bool(flags & 2)), "ecu": int(bool(flags & 4)),
        }
        for index, (expected_group, expected_zone) in enumerate(FIELD_KEYS):
            at = RECORD_PREFIX_SIZE + index * FIELD_SIZE
            group, zone, raw, formula, nwb, unit, semantic, sem_ev, formula_ev, status = record[at:at + 10]
            value = struct.unpack_from("<f", record, at + 12)[0]
            timestamp = struct.unpack_from("<Q", record, at + 16)[0]
            field_sequence = struct.unpack_from("<I", record, at + 24)[0]
            field_session = struct.unpack_from("<I", record, at + 28)[0]
            field_generation = struct.unpack_from("<I", record, at + 32)[0]
            if (group, zone) != (expected_group, expected_zone):
                raise ValueError(f"field order mismatch at byte {offset + at}: {(group, zone)}")
            prefix = f"g{group:03d}_z{zone}"
            row.update({
                f"{prefix}_raw": raw, f"{prefix}_value": value, f"{prefix}_formula": formula,
                f"{prefix}_nwb": nwb, f"{prefix}_unit": unit, f"{prefix}_semantic": semantic,
                f"{prefix}_semantic_evidence": sem_ev, f"{prefix}_formula_evidence": formula_ev,
                f"{prefix}_status": status, f"{prefix}_timestamp_us": timestamp,
                f"{prefix}_sequence": field_sequence, f"{prefix}_session_epoch": field_session,
                f"{prefix}_transport_generation": field_generation,
            })
        writer.writerow(row)


V2_HEADER_SIZE = 64
V2_RECORD_HEADER_SIZE = 26
V2_KINDS = {1: "ECU_SNAPSHOT", 2: "IMU_SAMPLE", 3: "START", 4: "STOP",
            5: "MARKER", 6: "IMU_ORIENTATION"}
EVENT_PAYLOAD_SCHEMA_V1 = 1
EVENT_SUBTYPES = {1: "SPROTZ_START", 2: "SPROTZ_STOP", 3: "MARKER"}


def _fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def _decode_marker_event_subtype(payload: bytes) -> str:
    if len(payload) == 0:
        return "MARKER_LEGACY"
    if len(payload) != 2:
        raise ValueError(f"invalid V2 MARKER payload length {len(payload)}")
    schema, subtype = payload
    if schema != EVENT_PAYLOAD_SCHEMA_V1:
        return f"UNKNOWN_EVENT_SCHEMA_{schema}"
    return EVENT_SUBTYPES.get(subtype, f"UNKNOWN_EVENT_SUBTYPE_{subtype}")


def decode_v2_bytes(data: bytes, output) -> None:
    if len(data) < V2_HEADER_SIZE or data[:8] != b"DGFTSPT2":
        raise ValueError("not a DGFTSPT2 log")
    version, header_size = struct.unpack_from("<HH", data, 8)
    if version != 2 or header_size != V2_HEADER_SIZE:
        raise ValueError("unsupported DLOG V2 header")
    columns = ["kind", "event_subtype", "timestamp_us", "sequence", "validity", "accel_x_mg",
               "accel_y_mg", "accel_z_mg", "gyro_x_mdps", "gyro_y_mdps",
               "gyro_z_mdps", "payload_length"]
    columns += [f"g{group:03d}_z{zone}_raw" for group, zone in FIELD_KEYS]
    writer = csv.DictWriter(output, fieldnames=columns)
    writer.writeheader()
    offset = V2_HEADER_SIZE
    while offset < len(data):
        if len(data) - offset < V2_RECORD_HEADER_SIZE:
            raise ValueError(f"truncated V2 record header at byte {offset}")
        magic, rec_version, kind, _flags, rec_header, payload_len, timestamp, checksum = \
            struct.unpack_from("<I H B B H I Q I", data, offset)
        if magic != 0x32434552 or rec_version != 2 or rec_header != V2_RECORD_HEADER_SIZE:
            raise ValueError(f"invalid V2 record header at byte {offset}")
        if payload_len > RECORD_SIZE or offset + rec_header + payload_len > len(data):
            raise ValueError(f"invalid V2 record length at byte {offset}")
        payload = data[offset + rec_header:offset + rec_header + payload_len]
        if _fnv1a(payload) != checksum:
            raise ValueError(f"V2 payload checksum mismatch at byte {offset}")
        row = {"kind": V2_KINDS.get(kind, f"UNKNOWN_{kind}"), "event_subtype": "",
               "timestamp_us": timestamp,
               "payload_length": payload_len}
        if kind == 1:
            if payload_len != RECORD_SIZE:
                raise ValueError("invalid ECU_SNAPSHOT payload length")
            row["sequence"] = struct.unpack_from("<I", payload, 24)[0]
            for index, (group, zone) in enumerate(FIELD_KEYS):
                at = RECORD_PREFIX_SIZE + index * FIELD_SIZE
                actual_group, actual_zone = payload[at], payload[at + 1]
                if (actual_group, actual_zone) != (group, zone):
                    raise ValueError("V2 ECU field order mismatch")
                row[f"g{group:03d}_z{zone}_raw"] = payload[at + 2]
        elif kind == 2:
            if payload_len != 40:
                raise ValueError("invalid IMU_SAMPLE payload length")
            values = struct.unpack_from("<QI6iB", payload, 0)
            row.update({"sequence": values[1], "accel_x_mg": values[2],
                        "accel_y_mg": values[3], "accel_z_mg": values[4],
                        "gyro_x_mdps": values[5], "gyro_y_mdps": values[6],
                        "gyro_z_mdps": values[7], "validity": values[8]})
        elif kind == 5:
            row["event_subtype"] = _decode_marker_event_subtype(payload)
        writer.writerow(row)
        offset += rec_header + payload_len


def decode(path: Path, output) -> None:
    data = path.read_bytes()
    if data[:8] == b"DGFTSPT1":
        decode_v1_bytes(data, output)
    elif data[:8] == b"DGFTSPT2":
        decode_v2_bytes(data, output)
    else:
        raise ValueError("unknown DLOG magic")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    try:
        if args.output:
            with args.output.open("w", newline="", encoding="utf-8") as output:
                decode(args.input, output)
        else:
            decode(args.input, sys.stdout)
    except (OSError, ValueError) as error:
        print(f"decode_sprotz_log: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
