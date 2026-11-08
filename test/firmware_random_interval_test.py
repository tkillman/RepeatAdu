from pathlib import Path
import unittest


SOURCE = Path(__file__).parents[1] / 'src' / 'main.cpp'


class RandomIntervalFirmwareContractTest(unittest.TestCase):
    def test_firmware_accepts_a_minimum_and_maximum_interval(self):
        source = SOURCE.read_text(encoding='utf-8')

        self.assertIn('intervalMinSec', source)
        self.assertIn('intervalMaxSec', source)
        self.assertIn('intervalMinSeconds', source)
        self.assertIn('intervalMaxSeconds', source)

    def test_firmware_selects_a_random_interval_for_each_wait(self):
        source = SOURCE.read_text(encoding='utf-8')

        self.assertIn('random(', source)
        self.assertIn('selectedIntervalMs', source)


if __name__ == '__main__':
    unittest.main()
