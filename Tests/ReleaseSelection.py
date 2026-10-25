import sys
from pathlib import Path
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_bank import sustain_release


class ReleaseSelection(unittest.TestCase):
    rate = 4000

    def audio(self, envelope):
        t = np.arange(len(envelope)) / self.rate
        return envelope[:, None] * np.column_stack((np.sin(2*np.pi*73*t), .7*np.sin(2*np.pi*73*t+.8)))

    def test_quiet_body_after_loud_attack_is_not_a_release(self):
        t = np.arange(self.rate*5) / self.rate
        envelope = np.where(t < .4, 1., .15) * np.exp(-np.maximum(0, t-4)*12)
        result = sustain_release(self.audio(envelope), self.rate, self.rate)
        self.assertIsNotNone(result)
        self.assertGreater(result['start'], int(self.rate*3.7))
        self.assertLess(result['start'], int(self.rate*4.2))

    def test_later_room_decay_does_not_replace_the_main_ending(self):
        t = np.arange(self.rate*5) / self.rate
        envelope = np.maximum(.0008, np.exp(-np.maximum(0, t-3)*15))
        envelope[t >= 4.4] *= .01
        result = sustain_release(self.audio(envelope), self.rate, self.rate)
        self.assertIsNotNone(result)
        self.assertGreater(result['start'], int(self.rate*2.7))
        self.assertLess(result['start'], int(self.rate*3.2))

    def test_phrase_dip_that_recovers_is_not_a_release(self):
        t = np.arange(self.rate*6) / self.rate
        envelope = np.ones(len(t))
        envelope[(t >= 2) & (t < 2.5)] = .1
        envelope *= np.exp(-np.maximum(0, t-5)*15)
        result = sustain_release(self.audio(envelope), self.rate, self.rate)
        self.assertIsNotNone(result)
        self.assertGreater(result['start'], int(self.rate*4.7))

    def test_unfinished_or_silent_source_keeps_generic_release(self):
        for envelope in (np.ones(self.rate*3), np.zeros(self.rate*3)):
            self.assertIsNone(sustain_release(self.audio(envelope), self.rate, self.rate))

    def test_release_before_loop_end_is_not_reused(self):
        t = np.arange(self.rate*4) / self.rate
        envelope = np.exp(-np.maximum(0, t-2)*15)
        self.assertIsNone(sustain_release(self.audio(envelope), self.rate, int(self.rate*3)))


if __name__ == '__main__':
    unittest.main()
