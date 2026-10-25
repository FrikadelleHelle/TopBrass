import sys
from pathlib import Path
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_bank import joint_loop


class LoopSelection(unittest.TestCase):
    def setUp(self):
        self.rate = 2000
        t = np.arange(self.rate * 7) / self.rate
        self.audio = np.column_stack((np.sin(2*np.pi*73*t), .7*np.sin(2*np.pi*73*t+.4)))

    def test_preserves_four_seconds_before_crossfade(self):
        start, end, fade, correlation, ratios = joint_loop(self.audio, self.rate)
        self.assertGreaterEqual(end-fade, self.rate*4)
        self.assertGreater(end-start-fade, self.rate*.2)
        self.assertGreaterEqual(min(correlation), .85)
        self.assertGreaterEqual(min(ratios), .9)

    def test_rejects_source_that_has_already_ended(self):
        self.audio[self.rate*3:] = 0
        with self.assertRaises(ValueError):
            joint_loop(self.audio, self.rate)


if __name__ == '__main__':
    unittest.main()
