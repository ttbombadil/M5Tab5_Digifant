# EspUsbHost 2.7.8 pin

The build uses the patched, single-source fork in
`M5Tab5_Digifant_Analyzer/src/esp_usb_host_fork/`. This directory retains the
EspUsbHost 2.7.8 pin and license metadata; its former duplicate `src/` copy was
removed in R4 after a byte-for-byte/hash comparison. The hashes below document
the upstream files before the V2 port patch.

| file | SHA-256 before port patch |
|---|---|
| `src/EspUsbHost.cpp` | `ce5f117f31a403b5e514d45d5a671aed2ad0274391d82b99b170a48bb41c0420` |
| `src/EspUsbHost.h` | `87486c7eec9e1374fbbbd65bd1540db1d0edff5f1fed794a30d2f296740338e9` |
| `src/EspUsbHostCcid.cpp` | `5ed4e2cfcd57b0eed2365a9686d37876958b8e01e0cf3cc371f160719363bf95` |
| `src/EspUsbHostHid.cpp` | `d52b296433200eb14486b90633d12edad074092e73f3897869e9924aa8aefd34` |

Only the tokenized serial-OUT completion/retirement path and the tokenized
FTDI-control entry point are patched in the build fork. The upstream license is
retained.
