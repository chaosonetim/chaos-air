"""Decode a fixed 640x480 little-endian RGGB10 or RGB565 snapshot."""

import argparse
import binascii
from pathlib import Path
import struct
import zlib


WIDTH = 640
HEIGHT = 480


def read_frame(path, pixel_format="raw10"):
    raw = path.read_bytes()
    expected = WIDTH * HEIGHT * 2
    if len(raw) != expected:
        raise ValueError(f"Expected {expected} bytes, got {len(raw)}")
    samples = struct.unpack(f"<{WIDTH * HEIGHT}H", raw)
    if pixel_format == "raw10" and max(samples) > 1023:
        raise ValueError("Samples exceed the LSB-aligned RAW10 range (0..1023)")
    return raw, samples


def unpack_rgb565(samples):
    return [
        channel
        for value in samples
        for channel in (
            ((value >> 11) & 31) * 255 // 31,
            ((value >> 5) & 63) * 255 // 63,
            (value & 31) * 255 // 31,
        )
    ]


def bayer_color(y, x):
    if y % 2 == 0:
        return 0 if x % 2 == 0 else 1
    return 1 if x % 2 == 0 else 2


def demosaic(samples, width, height):
    rgb = []
    for y in range(height):
        for x in range(width):
            source_color = bayer_color(y, x)
            for channel in range(3):
                if channel == source_color:
                    rgb.append(samples[y * width + x])
                    continue
                if channel == 1:
                    offsets = ((-1, 0), (1, 0), (0, -1), (0, 1))
                elif source_color == 1:
                    # On red rows, red neighbors are horizontal, not vertical.
                    horizontal = (channel == 0) == (y % 2 == 0)
                    offsets = ((0, -1), (0, 1)) if horizontal else ((-1, 0), (1, 0))
                else:
                    offsets = ((-1, -1), (-1, 1), (1, -1), (1, 1))
                neighbors = [
                    samples[(y + dy) * width + x + dx]
                    for dy, dx in offsets
                    if 0 <= y + dy < height and 0 <= x + dx < width
                ]
                if not neighbors:
                    raise ValueError("Image is too small for RGGB interpolation")
                rgb.append(sum(neighbors) // len(neighbors))
    return rgb


def write_png(path, samples, width, height, channels, low, high):
    if high <= low:
        raise ValueError("Image has no contrast")
    stride = width * channels
    rows = [
        b"\0" + bytes(
            max(0, min(255, (value - low) * 255 // (high - low)))
            for value in samples[start:start + stride]
        )
        for start in range(0, len(samples), stride)
    ]

    def chunk(kind, data):
        return (
            struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)
        )

    color_type = 2 if channels == 3 else 0
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, color_type, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frame", type=Path)
    parser.add_argument("--format", choices=("raw10", "rgb565"), default="raw10")
    args = parser.parse_args()
    raw, samples = read_frame(args.frame, args.format)
    checksum = 2166136261
    for value in raw:
        checksum = ((checksum ^ value) * 16777619) & 0xFFFFFFFF
    if args.format == "rgb565":
        rgb = unpack_rgb565(samples)
        preview = args.frame.with_name(args.frame.stem + "-preview.png")
        write_png(preview, rgb, WIDTH, HEIGHT, 3, 0, 255)
        gray = [
            (77 * rgb[i] + 150 * rgb[i + 1] + 29 * rgb[i + 2]) // 256
            for i in range(0, len(rgb), 3)
        ]
        gray_path = args.frame.with_name(args.frame.stem + "-gray.png")
        write_png(gray_path, gray, WIDTH, HEIGHT, 1, 0, 255)
        print(f"RGB565: {WIDTH}x{HEIGHT}, bytes={len(raw)}, FNV-1a checksum={checksum:08X}")
        print(f"RGB preview (no host contrast stretch): {preview}")
        print(f"Grayscale preview: {gray_path}")
        return
    ordered = sorted(samples)
    low, high = ordered[len(samples) // 100], ordered[len(samples) * 99 // 100]
    if high <= low:
        low, high = min(samples), max(samples)
    rgb = demosaic(samples, WIDTH, HEIGHT)
    preview = args.frame.with_name(args.frame.stem + "-preview.png")
    write_png(preview, rgb, WIDTH, HEIGHT, 3, low, high)
    # One grayscale pixel per Bayer cell avoids interpolation artifacts.
    gray = [
        (
            samples[y * WIDTH + x] + samples[y * WIDTH + x + 1]
            + samples[(y + 1) * WIDTH + x] + samples[(y + 1) * WIDTH + x + 1]
        ) // 4
        for y in range(0, HEIGHT, 2)
        for x in range(0, WIDTH, 2)
    ]
    gray_path = args.frame.with_name(args.frame.stem + "-gray.png")
    write_png(gray_path, gray, WIDTH // 2, HEIGHT // 2, 1, low, high)
    print(f"RAW10: {WIDTH}x{HEIGHT}, bytes={len(raw)}, min={min(samples)}, max={max(samples)}")
    print(f"FNV-1a checksum={checksum:08X}, display contrast={low}..{high}")
    print(f"RGB preview (no white balance, ISP, or gamma): {preview}")
    print(f"2x2-cell grayscale preview: {gray_path}")


if __name__ == "__main__":
    main()
