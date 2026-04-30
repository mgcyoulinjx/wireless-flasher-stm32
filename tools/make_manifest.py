#!/usr/bin/env python3
import argparse
import json
import pathlib
import zlib


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate manifest.json for the STM32 wireless flasher")
    parser.add_argument("firmware", help="Path to app.bin")
    parser.add_argument("--chip", required=True, help="STM32 chip or board name, for example stm32f103c8")
    parser.add_argument("--address", default="0x08000000", help="Flash base address, default: 0x08000000")
    parser.add_argument("--output", default="manifest.json", help="Output manifest path")
    args = parser.parse_args()

    firmware_path = pathlib.Path(args.firmware)
    if not firmware_path.is_file():
      raise SystemExit(f"Firmware file not found: {firmware_path}")

    data = firmware_path.read_bytes()
    manifest = {
        "target": "stm32",
        "chip": args.chip,
        "address": parse_int(args.address),
        "size": len(data),
        "crc32": zlib.crc32(data) & 0xFFFFFFFF,
    }

    output_path = pathlib.Path(args.output)
    output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output_path}")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
