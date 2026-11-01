"""Comprobaciones sin material de juego del contenedor TCB2 y transferencias GS."""
import struct,sys,unittest
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parent.parent
sys.path[:0]=[str(R/'awo_tools'),str(R/'mod center hd')]
import t3_effects as fx

class Effects(unittest.TestCase):
 def test_palette_and_alpha(self):
  b=bytearray(2432);struct.pack_into('<II',b,0,1,8)
  struct.pack_into('<4I',b,32,24,312,1152,1152)
  struct.pack_into('<Q',b,80,(19<<20)|(5<<26)|(5<<30))
  b[1376:1380]=bytes((10,20,30,128))
  im=fx.textures(b)[0]
  self.assertEqual((im['w'],im['h']),(32,32))
  self.assertTrue(np.all(im['rgba']==np.array([10,20,30,255],np.uint8)))
  struct.pack_into('<I',b,40,0xffffffff)
  with self.assertRaises(ValueError):fx.textures(b)
 def test_gs_address_maps_are_bijective(self):
  for depth,size in ((8,32),(8,64),(4,64)):
   n=size*size;result=np.zeros((size,size),np.uint32)
   for bit in range((n-1).bit_length()):
    values=((np.arange(n,dtype=np.uint32)>>bit)&1).astype(np.uint8)
    if depth==4:
     data=(values[0::2]|values[1::2]<<4).tobytes();plane=fx.unswizzle4(data,size,size)
    else:plane=fx.unswizzle8(values.tobytes(),size,size)
    result|=plane.astype(np.uint32)<<bit
   self.assertEqual(len(np.unique(result)),n)
 def test_set_consumes_all_resources(self):
  head=bytearray(160);struct.pack_into('<IBB',head,0,1,1,1)
  head[64:67]=bytes((0,1,0))
  b=bytearray(struct.pack('<I5I',4,64,64,64,64,224));b+=bytes(64-len(b));b+=head
  parsed=fx.read_effect(b)
  self.assertEqual(parsed['parts'],1)
  self.assertEqual(fx.encode(parsed)[:4],b'TCB2')
  b[64+5]=2
  with self.assertRaises(ValueError):fx.read_effect(b)
if __name__=='__main__':unittest.main()
