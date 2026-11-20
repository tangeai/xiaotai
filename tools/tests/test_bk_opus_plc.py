"""Replay every interior one- and two-packet loss in both shipped voice prompts."""
import array
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / 'platforms/beken/runtime'


class OpusPLC(unittest.TestCase):
    def test_voice_replay(self):
        # On a host install libopus-dev, or point to SDK-compatible headers and libopus.
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            headers = os.getenv('OPUS_HEADERS', '/usr/include/opus')
            (temp / 'modules').symlink_to(Path(headers).resolve())
            exe = temp / 'replay'
            library = os.getenv('OPUS_LIBRARY', '-lopus')
            rpath = [f'-Wl,-rpath,{Path(library).parent}'] if Path(library).is_absolute() else []
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                            '-I', tmp, '-I', str(RUNTIME / 'include'),
                            str(ROOT / 'tests/audio/bk_opus_plc_replay.c'),
                            str(RUNTIME / 'src/xiaotai_opus_plc.c'),
                            library, *rpath, '-lm', '-o', str(exe)], check=True)
            for voice in ('female', 'male'):
                with self.subTest(voice=voice), wave.open(str(
                        ROOT / f'product/assets/audio/prompts/ai_ack_{voice}_8k.wav')) as wav:
                    self.assertEqual((wav.getframerate(), wav.getnchannels(), wav.getsampwidth()),
                                     (8000, 1, 2))
                    samples = array.array('h', wav.readframes(wav.getnframes()))
                    wide = array.array('h')
                    for a, b in zip(samples, samples[1:]):
                        wide.extend([a, (a+b)//2])
                    print(voice, flush=True)
                    subprocess.run([str(exe)], input=wide.tobytes(), check=True)


if __name__ == '__main__':
    unittest.main()
