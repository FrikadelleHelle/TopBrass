import sys
from pathlib import Path
import unittest
import numpy as np
from scipy import signal
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from recording_audit import compare_arrays

class RecordingAudit(unittest.TestCase):
    def setUp(self):
        self.x=np.random.default_rng(37).normal(0,.05,(48000,1))
    def test_constant_gain_is_removed(self):
        r=compare_arrays(self.x,self.x*.1,48000)
        self.assertTrue(r['transparency_pass']);self.assertAlmostEqual(r['gain_correction'],10)
    def test_stereo_phase_is_not_folded_to_mono(self):
        x=np.concatenate([self.x,-self.x],axis=1);r=compare_arrays(x,x*.2,48000)
        self.assertTrue(r['transparency_pass']);self.assertAlmostEqual(r['original_channel_correlation'],-1)
    def test_delay_does_not_pass_on_spectrum_alone(self):
        r=compare_arrays(self.x,np.roll(self.x,64,axis=0),48000)
        self.assertEqual(r['lag_samples'],64);self.assertFalse(r['transparency_pass'])
    def test_warm_filter_is_detected(self):
        a=1-np.exp(-2*np.pi*700/48000);low=signal.lfilter([a],[1,-(1-a)],self.x,axis=0)
        y=1.35*low+.15*(self.x-low);r=compare_arrays(self.x,y,48000)
        self.assertGreater(r['max_abs_band_error_db'],3);self.assertFalse(r['transparency_pass'])
    def test_silent_render_is_rejected(self):
        with self.assertRaises(ValueError):compare_arrays(self.x,np.zeros_like(self.x),48000)

if __name__=='__main__':unittest.main()
