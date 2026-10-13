import io
import struct
import unittest
import zlib

from tools.capture_screen import END, encode_png, read_snapshot


class FragmentedPort(io.BytesIO):
    def read(self, size=-1):
        return super().read(min(size, 7))


class ScreenshotTests(unittest.TestCase):
    def test_png_has_correct_rgb_rows_and_chunk_checksums(self):
        pixels = bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255))
        png = encode_png(2, 2, pixels)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        chunks = {}
        while offset < len(png):
            size = struct.unpack(">I", png[offset:offset + 4])[0]
            kind = png[offset + 4:offset + 8]
            data = png[offset + 8:offset + 8 + size]
            checksum = struct.unpack(">I", png[offset + 8 + size:offset + 12 + size])[0]
            self.assertEqual(checksum, zlib.crc32(kind + data) & 0xFFFFFFFF)
            chunks[kind] = data
            offset += size + 12
        self.assertEqual(struct.unpack(">IIBBBBB", chunks[b"IHDR"]), (2, 2, 8, 2, 0, 0, 0))
        self.assertEqual(zlib.decompress(chunks[b"IDAT"]), b"\0" + pixels[:6] + b"\0" + pixels[6:])
        self.assertEqual(chunks[b"IEND"], b"")

    def test_framed_transfer_ignores_logs_and_handles_fragmented_reads(self):
        pixels = b"\xff\0\0\0\xff\0"
        port = FragmentedPort(b"RADIO diagnostic\n\nATS_SCREENSHOT RGB888 2 1 6\n" + pixels + END)
        self.assertEqual(read_snapshot(port, 1), (2, 1, pixels))

    def test_invalid_dimensions_rejected(self):
        for header in (b"0 1 0", b"1025 1 3075", b"2 1 5"):
            with self.assertRaises(ValueError):
                read_snapshot(io.BytesIO(b"ATS_SCREENSHOT RGB888 " + header + b"\n"), 1)

    def test_truncation_and_wrong_end_marker_rejected(self):
        for payload in (b"\xff", b"\xff\0\0" + b"x" * len(END)):
            with self.assertRaises(RuntimeError):
                read_snapshot(io.BytesIO(b"ATS_SCREENSHOT RGB888 1 1 3\n" + payload), 0.01)

    def test_framebuffer_error_is_reported(self):
        with self.assertRaisesRegex(RuntimeError, "framebuffer unavailable"):
            read_snapshot(io.BytesIO(b"SCREENSHOT_ERROR framebuffer unavailable\r\n"), 1)


if __name__ == "__main__":
    unittest.main()
