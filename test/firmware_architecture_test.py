from pathlib import Path
import unittest


ROOT = Path(__file__).parents[1]


class FirmwareArchitectureTest(unittest.TestCase):
    def test_main_composes_dedicated_engine_and_ble_service(self):
        main = (ROOT / 'src' / 'main.cpp').read_text(encoding='utf-8')

        self.assertIn('#include "autoclicker_engine.h"', main)
        self.assertIn('#include "ble_service.h"', main)
        self.assertLess(len(main.splitlines()), 180)

    def test_reset_command_returns_the_engine_to_initial_state(self):
        ble_service = (ROOT / 'src' / 'ble_service.cpp').read_text(encoding='utf-8')
        engine = (ROOT / 'src' / 'autoclicker_engine.cpp').read_text(encoding='utf-8')

        self.assertIn('strcasecmp(cmd, "RESET")', ble_service)
        self.assertIn('engine_->reset()', ble_service)
        self.assertIn('void AutoclickerEngine::reset()', engine)

    def test_default_press_angle_is_75_degrees(self):
        header = (ROOT / 'src' / 'autoclicker_engine.h').read_text(encoding='utf-8')

        self.assertIn('pressAngle = 75', header)


if __name__ == '__main__':
    unittest.main()
