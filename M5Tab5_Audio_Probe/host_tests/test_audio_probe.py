#!/usr/bin/env python3
import math
from pathlib import Path
import struct
import unittest


SKETCH = Path(__file__).resolve().parents[1] / "M5Tab5_Audio_Probe.ino"


def stats(samples, threshold=32752):
    clipped = [abs(x) >= threshold for x in samples]
    return {
        "count": len(samples),
        "min": min(samples),
        "max": max(samples),
        "dc": sum(samples) / len(samples),
        "rms": math.sqrt(sum(x * x for x in samples) / len(samples)),
        "near_full_scale": sum(clipped),
        "clipping_events": sum(c and not clipped[i - 1] for i, c in enumerate(clipped)),
    }


def wav_header(frames, rate=16000, channels=2):
    data_bytes = frames * channels * 2
    return (b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, channels, rate,
                        rate * channels * 2, channels * 2, 16) +
            b"data" + struct.pack("<I", data_bytes))


class AudioProbeHostTests(unittest.TestCase):
    def test_passive_microphone_stage_has_no_runtime_calls(self):
        source = SKETCH.read_text(encoding="utf-8")
        self.assertIn("M5.Mic.config(mic)", source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        self.assertNotIn("M5.Mic.begin(", source)
        self.assertNotIn("M5.Mic.record(", source)
        self.assertNotIn("M5.Mic.end(", source)

    def test_audio_buffer_plan(self):
        sample_rate = 16000
        channels = 2
        bytes_per_sample = 2
        self.assertEqual(sample_rate * 30 * channels * bytes_per_sample, 1_920_000)
        self.assertEqual((sample_rate // 4) * channels * bytes_per_sample, 16_000)

    def test_wav_header_and_pcm_length(self):
        header = wav_header(3)
        self.assertEqual(header[:4], b"RIFF")
        self.assertEqual(header[8:16], b"WAVEfmt ")
        self.assertEqual(struct.unpack_from("<H", header, 20)[0], 1)
        self.assertEqual(struct.unpack_from("<H", header, 22)[0], 2)
        self.assertEqual(struct.unpack_from("<I", header, 24)[0], 16000)
        self.assertEqual(struct.unpack_from("<H", header, 34)[0], 16)
        self.assertEqual(struct.unpack_from("<I", header, 40)[0], 12)
        self.assertEqual(struct.unpack_from("<I", header, 4)[0], 48)

    def test_little_endian_signed_pcm_and_stereo_order(self):
        pcm = struct.pack("<hhhh", -2, 100, 32767, -32768)
        self.assertEqual(pcm, b"\xfe\xff\x64\x00\xff\x7f\x00\x80")
        decoded = struct.unpack("<hhhh", pcm)
        self.assertEqual(decoded[::2], (-2, 32767))  # channel 0
        self.assertEqual(decoded[1::2], (100, -32768))  # channel 1

    def test_deterministic_channel_metrics_and_clipping_runs(self):
        channel = [-32752, -32752, 0, 32752, 32752, 1]
        result = stats(channel)
        self.assertEqual(result["count"], 6)
        self.assertEqual(result["min"], -32752)
        self.assertEqual(result["max"], 32752)
        self.assertAlmostEqual(result["dc"], 0.1666666667)
        self.assertAlmostEqual(result["rms"], math.sqrt((32752**2 * 4 + 1) / 6))
        self.assertEqual(result["near_full_scale"], 4)
        self.assertEqual(result["clipping_events"], 2)


if __name__ == "__main__":
    unittest.main()
