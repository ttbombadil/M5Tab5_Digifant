import csv
import io
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import decode_sprotz_log  # noqa: E402


def rec(kind, timestamp, payload=b""):
    checksum = decode_sprotz_log._fnv1a(payload)
    return struct.pack("<I H B B H I Q I", 0x32434552, 2, kind, 0, 26,
                       len(payload), timestamp, checksum) + payload


def snapshot_payload():
    payload = bytearray(decode_sprotz_log.RECORD_SIZE)
    struct.pack_into("<I", payload, 24, 77)
    for index, (group, zone) in enumerate(decode_sprotz_log.FIELD_KEYS):
        at = decode_sprotz_log.RECORD_PREFIX_SIZE + index * decode_sprotz_log.FIELD_SIZE
        payload[at] = group
        payload[at + 1] = zone
        payload[at + 2] = index & 0xFF
    return bytes(payload)


def main():
    header = bytearray(64)
    header[:8] = b"DGFTSPT2"
    struct.pack_into("<HHI", header, 8, 2, 64, 0)
    data = bytes(header) + rec(3, 100) + rec(6, 110, bytes([1, 1, 2]))
    imu = struct.pack("<QI6iB", 120, 9, 1, -2, 3, 4, -5, 6, 7) + bytes(3)
    data += rec(2, 120, imu) + rec(1, 130, snapshot_payload())
    # Existing V2 marker: payloadLength == 0.
    data += rec(5, 140)
    # New V2 marker event-payload schema 1: start, free marker, stop.
    data += rec(5, 150, bytes([1, 1])) + rec(5, 160, bytes([1, 3]))
    data += rec(5, 170, bytes([1, 2]))
    output = io.StringIO()
    decode_sprotz_log.decode_v2_bytes(data, output)
    rows = list(csv.DictReader(io.StringIO(output.getvalue())))
    assert [row["kind"] for row in rows] == ["START", "IMU_ORIENTATION", "IMU_SAMPLE",
                                               "ECU_SNAPSHOT", "MARKER", "MARKER", "MARKER",
                                               "MARKER"]
    assert [row["event_subtype"] for row in rows] == ["", "", "", "",
                                                       "MARKER_LEGACY", "SPROTZ_START",
                                                       "MARKER", "SPROTZ_STOP"]
    assert rows[2]["accel_x_mg"] == "1" and rows[2]["gyro_y_mdps"] == "-5"
    assert rows[3]["g000_z0_raw"] == "0" and rows[3]["g004_z4_raw"] == "25"

    unknown_schema = bytes(header) + rec(5, 200, bytes([2, 1]))
    output = io.StringIO()
    decode_sprotz_log.decode_v2_bytes(unknown_schema, output)
    assert list(csv.DictReader(io.StringIO(output.getvalue())))[0]["event_subtype"] == \
        "UNKNOWN_EVENT_SCHEMA_2"

    unknown_subtype = bytes(header) + rec(5, 210, bytes([1, 99]))
    output = io.StringIO()
    decode_sprotz_log.decode_v2_bytes(unknown_subtype, output)
    assert list(csv.DictReader(io.StringIO(output.getvalue())))[0]["event_subtype"] == \
        "UNKNOWN_EVENT_SUBTYPE_99"

    invalid_marker_payload = bytes(header) + rec(5, 220, bytes([1]))
    try:
        decode_sprotz_log.decode_v2_bytes(invalid_marker_payload, io.StringIO())
    except ValueError as error:
        assert "MARKER payload length" in str(error)
    else:
        raise AssertionError("invalid MARKER payload length was accepted")

    corrupt_event_payload = bytearray(bytes(header) + rec(5, 230, bytes([1, 1])))
    corrupt_event_payload[-1] = 2  # checksum still covers the original payload.
    try:
        decode_sprotz_log.decode_v2_bytes(bytes(corrupt_event_payload), io.StringIO())
    except ValueError as error:
        assert "checksum" in str(error)
    else:
        raise AssertionError("corrupt MARKER payload was accepted")

    for bad in (data[:70], data[:-1], data[:64] + bytes([0]) + data[65:]):
        try:
            decode_sprotz_log.decode_v2_bytes(bad, io.StringIO())
        except ValueError:
            pass
        else:
            raise AssertionError("corrupt V2 input was accepted")


if __name__ == "__main__":
    main()
