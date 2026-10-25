import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

import numpy as np
import soundfile as sf

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from audition_recording import pack_recording


class RecordingAudition(unittest.TestCase):
    def test_preserves_every_frame_channel_and_level(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for channels in (1,2):
                left=np.linspace(-.25,.25,4096,dtype=np.float32)
                audio=left[:,None] if channels==1 else np.column_stack((left,-left*.5))
                source=root/f'{channels}.wav';sf.write(source,audio,48000,subtype='FLOAT')
                before=source.read_bytes();bank=root/f'{channels}/audition.bank'
                decoded,rate,pcm_sha=pack_recording(source,bank,72)
                self.assertEqual(source.read_bytes(),before)
                self.assertEqual(rate,48000);self.assertTrue(np.array_equal(decoded,audio))
                self.assertEqual(pcm_sha,hashlib.sha256(audio.T.astype('<f4').tobytes()).hexdigest())
                with bank.open('rb') as stream:
                    self.assertEqual(stream.read(8),b'TBBANK04');self.assertEqual(struct.unpack('<2I',stream.read(8)),(12,0))
                    real=0
                    for _ in range(12):
                        h=struct.unpack('<15I',stream.read(60));payload=stream.read(h[5]*h[14]*4)
                        self.assertEqual(h[2:5],(72,1,48000));self.assertEqual(h[6:14],(0,)*8)
                        if h[:2]==(0,1):
                            self.assertEqual(h[5],len(audio));self.assertEqual(h[14],channels)
                            self.assertEqual(payload,audio.T.astype('<f4').tobytes());real+=1
                        else:self.assertEqual(payload,bytes(32))
                    self.assertEqual(real,1);self.assertEqual(stream.read(),b'')

    def test_rejects_unsupported_audio_without_transforming_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            cases=[np.ones((4096,3),dtype=np.float32)*.1,np.ones((4096,1),dtype=np.float32)*1.1,
                   np.full((4096,1),np.nan,dtype=np.float32),np.zeros((4096,1),dtype=np.float32),
                   np.ones((128,1),dtype=np.float32)*.1]
            for i,audio in enumerate(cases):
                source=root/f'bad{i}.wav';sf.write(source,audio,48000,subtype='FLOAT');before=source.read_bytes()
                bank=root/f'invalid{i}/audition.bank'
                with self.assertRaises(ValueError):pack_recording(source,bank,60)
                self.assertEqual(source.read_bytes(),before);self.assertFalse(bank.exists())

    def test_anchor_is_bounded_midi_mapping(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'note.wav';sf.write(source,np.ones(4096)*.1,48000,subtype='FLOAT')
            for key in (-1,128):
                with self.assertRaises(ValueError):pack_recording(source,root/'invalid.bank',key)


if __name__=='__main__':unittest.main()
