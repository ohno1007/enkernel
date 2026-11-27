# Read-only mapped metadata candidate, 2026-11-27

Base: PR #1 commit ac48166a4e868fb1a47ac2ffe2217fcf6ddfddba.

The user reports that Qdrive reads the minimap data while the current Enkernel
build does not. No failing address, errno, device kernel or loaded-module
identity was available. The actual page state has not been established.

Source comparison found Qdrive's explicit read-only mapped-file path absent
from Enkernel. GameFrame's Enkernel metadata fallback instead opens procfs
map_files and is subject to the worker's procfs permissions. This change adds
the bounded mapped-file path to the existing Enkernel read entries. It does
not claim a reproduced swap-backend defect or a verified minimap recovery.

- Source diff/whitespace review: completed.
- GitHub Actions: pending for the candidate commit; existing six-KMI and SDK
  workflow retained, with no new test suite.
- New mapped-file path on-device: not performed.
- Minimap rendering and protected-state continuity: not performed.
- Module load/unload, APK replacement and production release: not performed.

Deliver as development artifacts until the affected device confirms recovery.
