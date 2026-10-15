"""Checked material identifiers only; no geometry, textures or shader code."""
import argparse,json,struct
from pathlib import Path
from export_live_ui import fnv

FAMILIES=[('FDF960B5D7869030','1BFD0840B053E193',0),('F3AC1AA2FE3EA253','5F27AACEB38B1088',1),('E9DD8CD3AADD7CF1','5F27AACEB38B1088',2),('9B2CDBD500DB8645','A4A965C189287B99',3),('8B5902B7B177B0B0','A4A965C189287B99',4),('B4611D5E3A350359','1BFD0840B053E193',5),('DA9A3E04256563FF','0BFD5B9C11C8FC07',6),('176E0EF1D21F5A52','0BFD5B9C11C8FC07',7),('4B41B10E837C6871','4228617C65DB9D17',8),('E7F84460B7D340C5','CB0605B416DF85B8',9)]
FAMILIES += [('F37B3AC56DA38C91','1BFD0840B053E193',10),('432273B438E1AB50','1BFD0840B053E193',11)]
FAMILIES += [('D0D07B299C625324','052330CFA0039030',12),('6F783C70626DB428','052330CFA0039030',14)]
FAMILIES += [('9A71C0A1B069E25A','046E7D32000FC3F3',13)]
FAMILIES += [('57152B8FFBB31741','CB0605B416DF85B8',15)]
FAMILIES += [('A505100C5BC9C52F','F5F4D761A981B87D',16)]
FAMILIES += [('B4ED697CB92C1759','6E57A6E305D5D69B',17),('54728E4E265F591F','6E57A6E305D5D69B',18)]
FAMILIES += [('54728E4E265F591F','555EBAAEF8F61839',19),('54728E4E265F591F','4DFCBC49057E5721',20)]
FAMILIES += [('95DD91E3E9AE3A74','ADCA7FBBCBF53B57',21)]
# Label is the CPU FNV identifier, not a translated shader/cache signature.
FAMILIES += [('WORLD_VIDEO_FNV_3A730C5EA28F2A4D','046E7D32000FC3F3',22)]


def export(known,output):
    known,output=Path(known),Path(output);entries=[];proof=[]
    def variants(data):return {data,b''.join(data[i:i+4][::-1] for i in range(0,len(data),4))}
    for vs,ps,kind in FAMILIES:
        vertex=(known/f'shader_{vs}.ucode.bin.vert').read_bytes();pixel=(known/f'shader_{ps}.ucode.bin.frag').read_bytes();diffuse=(.5,1,.99) if kind in (1,2) else (1,1,1)
        for v in sorted(variants(vertex)):
            for p in sorted(variants(pixel)):entries.append(struct.pack('<2Q3I3f',fnv(v),fnv(p),len(v),len(p),kind,*diffuse))
        proof.append({'vs':vs,'ps':ps,'kind':kind})
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(b'DBZMAT01'+struct.pack('<I',len(entries))+b''.join(entries));output.with_suffix('.json').write_text(json.dumps({'scope':'Identifiers of checked native material semantics for geometry, characters, masks, sprites, video, postprocess and depth restore. No geometry, texture data or shader program bytes. Recognition does not establish complete game coverage or reference-free presentation.','families':proof,'records':len(entries)},indent=2));print('Material families',len(proof),'registry records',len(entries))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--known-shaders',required=True);p.add_argument('--output',required=True);a=p.parse_args();export(a.known_shaders,a.output)
