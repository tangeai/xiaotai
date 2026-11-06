"""Drive the actual WAV parser and single conversion to the hardware rate."""
import ctypes
import io
import pathlib
import struct
import subprocess
import tempfile
import unittest
import wave

ROOT=pathlib.Path(__file__).resolve().parents[2]

class Audio(ctypes.Structure):
    _fields_=[('pcm',ctypes.c_void_p),('frames',ctypes.c_size_t),
              ('samples_16k',ctypes.c_size_t),('sample_rate',ctypes.c_uint),
              ('channels',ctypes.c_uint)]

def wav(rate,channels,values):
    output=io.BytesIO()
    with wave.open(output,'wb') as w:
        w.setparams((channels,2,rate,0,'NONE',''))
        w.writeframes(struct.pack('<'+'h'*len(values),*values))
    return output.getvalue()

class RecognitionWAV16Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(); path=pathlib.Path(cls.tmp.name)/'wav.so'
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'product/include'),str(ROOT/'product/src/xiaotai_recognition.c'),'-o',str(path)],check=True)
        cls.lib=ctypes.CDLL(str(path))
        cls.parse=cls.lib.xiaotai_recognition_wav_parse
        cls.parse.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(Audio)]
        cls.parse.restype=ctypes.c_bool
        cls.convert=cls.lib.xiaotai_recognition_wav16
        cls.convert.argtypes=[ctypes.POINTER(Audio),ctypes.c_void_p,ctypes.c_size_t]
        cls.convert.restype=ctypes.c_bool

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def test_supported_rates_convert_once_and_16k_is_exact(self):
        for rate in (8000,12000,16000,24000,44100,48000):
            for channels in (1,2):
                raw=wav(rate,channels,[1234]*(rate*channels))
                data=ctypes.create_string_buffer(raw);info=Audio()
                self.assertTrue(self.parse(data,len(raw),ctypes.byref(info)))
                self.assertEqual((info.sample_rate,info.channels,info.samples_16k),(rate,channels,16000))
                out=(ctypes.c_int16*16000)()
                self.assertTrue(self.convert(ctypes.byref(info),out,16000))
                self.assertEqual(set(out),{1234})
        values=[-32768,-1234,0,1234,32767]*3200
        raw=wav(16000,1,values);data=ctypes.create_string_buffer(raw);info=Audio()
        self.assertTrue(self.parse(data,len(raw),ctypes.byref(info)))
        self.assertEqual(ctypes.string_at(info.pcm,len(values)*2),raw[44:])
        out=(ctypes.c_int16*len(values))()
        self.assertTrue(self.convert(ctypes.byref(info),out,len(values)))
        self.assertEqual(list(out),values)
        self.assertFalse(self.convert(ctypes.byref(info),out,1))
        raw=wav(8000,1,[0,8000,16000,24000]);data=ctypes.create_string_buffer(raw);info=Audio()
        self.assertTrue(self.parse(data,len(raw),ctypes.byref(info)))
        out=(ctypes.c_int16*8)()
        self.assertTrue(self.convert(ctypes.byref(info),out,8))
        self.assertEqual(list(out),[0,4000,8000,12000,16000,20000,24000,24000])

    def test_rejects_truncated_compressed_and_inconsistent_wav(self):
        valid=wav(16000,1,[1234]*100);info=Audio()
        for n in range(44): self.assertFalse(self.parse(valid,n,ctypes.byref(info)))
        for offset,value,code in ((20,3,'H'),(22,3,'H'),(24,96000,'I'),(28,123,'I'),(32,4,'H'),(34,8,'H')):
            bad=bytearray(valid);struct.pack_into('<'+code,bad,offset,value)
            self.assertFalse(self.parse(bytes(bad),len(bad),ctypes.byref(info)))
        self.assertFalse(self.parse(valid,len(valid)-2,ctypes.byref(info)))
        self.assertFalse(self.parse(b'\0'*100,100,ctypes.byref(info)))

    def test_streaming_headers_and_padded_chunks(self):
        valid=wav(24000,1,[1234]*24000)
        for riff,size in ((0xffffffff,0xffffffff),(2147483583,2147483547)):
            raw=bytearray(valid);struct.pack_into('<I',raw,4,riff);struct.pack_into('<I',raw,40,size)
            info=Audio();data=ctypes.create_string_buffer(bytes(raw))
            self.assertTrue(self.parse(data,len(raw),ctypes.byref(info)))
            self.assertEqual(info.samples_16k,16000)
        raw=bytearray(valid[:36]+b'JUNK'+struct.pack('<I',1)+b'x\0'+valid[36:])
        struct.pack_into('<I',raw,4,len(raw)-8);info=Audio()
        self.assertTrue(self.parse(bytes(raw),len(raw),ctypes.byref(info)))

if __name__=='__main__': unittest.main()
