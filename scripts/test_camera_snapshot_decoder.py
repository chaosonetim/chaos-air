import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib


spec = importlib.util.spec_from_file_location(
    "decoder", Path(__file__).with_name("Decode-CameraSnapshot.py")
)
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class DecoderTests(unittest.TestCase):
    def test_rgb565_primary_colors_and_endpoints(self):
        self.assertEqual(decoder.unpack_rgb565([0, 0xF800, 0x07E0, 0x001F, 0xFFFF]),
                         [0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])

    def test_rgb565_frame_accepts_full_uint16_range(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "frame.bin"
            path.write_bytes(b"\xff\xff" * (decoder.WIDTH * decoder.HEIGHT))
            raw, samples = decoder.read_frame(path, "rgb565")
            self.assertEqual(len(raw), 614400)
            self.assertEqual(samples[0], 65535)

    def test_constant_color_has_no_checkerboard_including_edges(self):
        colors = (120, 180, 80)
        samples = [colors[decoder.bayer_color(y, x)] for y in range(6) for x in range(8)]
        self.assertEqual(decoder.demosaic(samples, 8, 6), list(colors) * 48)

    def test_interpolates_each_channel_on_correct_axis(self):
        def value(y, x, channel):
            return 100 * (channel + 1) + 2 * y + 4 * x

        samples = [value(y, x, decoder.bayer_color(y, x)) for y in range(6) for x in range(8)]
        rgb = decoder.demosaic(samples, 8, 6)
        for y in range(1, 5):
            for x in range(1, 7):
                self.assertEqual(rgb[(y * 8 + x) * 3:(y * 8 + x) * 3 + 3],
                                 [value(y, x, c) for c in range(3)])

    def test_rejects_wrong_size_and_out_of_range(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "frame.bin"
            path.write_bytes(b"\0\0")
            with self.assertRaisesRegex(ValueError, "bytes"):
                decoder.read_frame(path)
            path.write_bytes(struct.pack("<H", 1024) + bytes(decoder.WIDTH * decoder.HEIGHT * 2 - 2))
            with self.assertRaisesRegex(ValueError, "range"):
                decoder.read_frame(path)

    def test_png_shape_pixels_and_crc(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "preview.png"
            decoder.write_png(path, [0, 50, 100, 100, 50, 0], 2, 1, 3, 0, 100)
            png = path.read_bytes()
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            offset = 8
            chunks = {}
            while offset < len(png):
                length = struct.unpack(">I", png[offset:offset + 4])[0]
                kind = png[offset + 4:offset + 8]
                data = png[offset + 8:offset + 8 + length]
                crc = struct.unpack(">I", png[offset + 8 + length:offset + 12 + length])[0]
                self.assertEqual(crc, decoder.binascii.crc32(kind + data) & 0xFFFFFFFF)
                chunks[kind] = data
                offset += length + 12
            self.assertEqual(struct.unpack(">2I5B", chunks[b"IHDR"]), (2, 1, 8, 2, 0, 0, 0))
            self.assertEqual(zlib.decompress(chunks[b"IDAT"]), bytes([0, 0, 127, 255, 255, 127, 0]))


if __name__ == "__main__":
    unittest.main()
