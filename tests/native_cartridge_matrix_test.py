"""Pictures alone cannot establish which language's cartridge was delivered."""
from io import BytesIO
from pathlib import Path
import sys
import unittest
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_cartridge_matrix import compare_frame, validate_delivery


def png(image):
    output = BytesIO()
    image.save(output, format='PNG')
    return output.getvalue()


class MatrixOracleTest(unittest.TestCase):
    def setUp(self):
        self.image = Image.new('RGBA', (256, 144), (12, 34, 56, 255))
        self.reference = self.image.tobytes()

    def test_all_rgb_channels_compared(self):
        self.assertEqual(compare_frame(png(self.image), [self.reference]), [0])
        for channel in range(3):
            with self.subTest(channel=channel):
                changed = self.image.copy()
                color = list(changed.getpixel((255, 143)))
                color[channel] += 1
                changed.putpixel((255, 143), tuple(color))
                with self.assertRaisesRegex(AssertionError, 'Full scaler RGB mismatch'):
                    compare_frame(png(changed), [self.reference])

    def test_either_frozen_animation_pose_allowed(self):
        other = Image.new('RGBA', (256, 144), (56, 34, 12, 255)).tobytes()
        self.assertEqual(compare_frame(png(self.image), [other, self.reference]), [73728, 0])

    def test_incomplete_wrong_geometry_and_reference_rejected(self):
        for capture, refs in [(png(self.image)[:-1], [self.reference]),
                              (png(Image.new('RGB', (256, 143))), [self.reference]),
                              (png(self.image), [self.reference[:-1]])]:
            with self.subTest(size=len(capture)):
                with self.assertRaises(AssertionError):
                    compare_frame(capture, refs)

    def test_identical_picture_cannot_satisfy_wrong_language_delivery(self):
        # The language demos can draw exactly the same picture.
        compare_frame(png(self.image), [self.reference])
        with self.assertRaisesRegex(AssertionError, 'FPGA cartridge bytes differ'):
            validate_delivery(b'-- script: lua', b'-- script: ruby', 6, 15, 6, 0)

    def test_wrong_ack_size_or_stock_source_metadata_rejected(self):
        validate_delivery(b'cart', b'cart', 6, 4, 6, 0)
        for ticket, size, ack, length in [(5, 4, 5, 0), (6, 3, 6, 0),
                                           (6, 4, 2, 0), (6, 4, 6, 8)]:
            with self.subTest(ticket=ticket, size=size, ack=ack, length=length):
                with self.assertRaises(AssertionError):
                    validate_delivery(b'cart', b'cart', ticket, size, ack, length)


if __name__ == '__main__':
    unittest.main(verbosity=2)
