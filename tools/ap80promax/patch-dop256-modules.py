#!/usr/bin/env python3
"""Enable the AP80 Pro Max vendor ALSA stack to negotiate 705.6/768 kHz.

The HiBy R3 II 2025 firmware uses the same byte-identical X1600 AIC driver,
but advertises 705.6/768 kHz in its ES9219 DAI and routes DSD256 through
705.6 kHz DoP.  Keep these binary changes tightly bound to the two known
AP80 vendor modules: refuse any input whose complete SHA-256 is not known.
"""

from __future__ import annotations

import hashlib
import os
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ModulePatch:
    relative_path: str
    original_sha256: str
    patched_sha256: str
    offset: int
    original: bytes
    replacement: bytes
    description: str


PATCHES = (
    ModulePatch(
        relative_path="module_driver/x1600_hidizs_ap80max_card.ko",
        original_sha256=(
            "2cb807ff23b8126c5e8bc7c8c88b715c"
            "a9937f11603aabcfb61642654cdad0fb"
        ),
        patched_sha256=(
            "9352812737d4449cd0dac8175738c7e4"
            "a3f539b92b451a89fd720edabbbc3d4b"
        ),
        offset=0x3D4,
        original=bytes.fromhex("25 30 00 00"),  # move a2, zero
        replacement=bytes.fromhex("40 31 02 00"),  # sll a2, v0, 5
        description="derive 22.5792/24.576 MHz MCLK for 705.6/768 kHz",
    ),
    ModulePatch(
        relative_path="module_driver/codec_es9219_v2.ko",
        original_sha256=(
            "35ebc8576c36423f546c3650d4c57e76"
            "b6ded949107b909a195b19208e3b89d8"
        ),
        patched_sha256=(
            "0493de2dcdeee9d5e2a68f8ef53da2a"
            "b62a8fb49f8fcd16dee0042c2b1372839"
        ),
        offset=0x2B08,
        original=bytes.fromhex("ea 7e 00 00"),  # rates mask through 384 kHz
        replacement=bytes.fromhex("ea fe 01 00"),  # add 705.6/768 kHz
        description="advertise the two high-rate ES9219 DAI capabilities",
    ),
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patch_module(rootfs: Path, spec: ModulePatch) -> None:
    path = rootfs / spec.relative_path
    data = bytearray(path.read_bytes())
    current_sha256 = digest(data)

    if current_sha256 == spec.patched_sha256:
        if data[spec.offset : spec.offset + len(spec.replacement)] != spec.replacement:
            raise RuntimeError(f"{path}: patched digest but unexpected patch bytes")
        print(f"{path}: already patched")
        return

    if current_sha256 != spec.original_sha256:
        raise RuntimeError(
            f"{path}: unsupported module SHA-256 {current_sha256}; refusing to patch"
        )

    end = spec.offset + len(spec.original)
    if data[spec.offset:end] != spec.original:
        raise RuntimeError(f"{path}: expected instruction/data bytes not found")

    data[spec.offset:end] = spec.replacement
    if digest(data) != spec.patched_sha256:
        raise RuntimeError(f"{path}: patched output digest does not match")

    temporary = path.with_name(f".{path.name}.dop256.tmp")
    temporary.write_bytes(data)
    os.chmod(temporary, path.stat().st_mode)
    os.replace(temporary, path)
    print(f"{path}: {spec.description}")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} ROOTFS", file=sys.stderr)
        return 2

    rootfs = Path(sys.argv[1]).resolve()
    if not rootfs.is_dir():
        print(f"not a rootfs directory: {rootfs}", file=sys.stderr)
        return 2

    try:
        for patch in PATCHES:
            patch_module(rootfs, patch)
    except (OSError, RuntimeError) as error:
        print(f"DSD256 module patch failed: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
