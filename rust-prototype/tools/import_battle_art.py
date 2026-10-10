"""Extract two original battle sprites from Ben's verified USA rev0 ROM."""
import hashlib, json, struct, zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
rom=(ROOT.parent/'FireRedLeafGreenRecomp/variants/firered/roms/firered_usa.gba').read_bytes()
assert hashlib.sha1(rom).hexdigest()=='41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc'
symbols={}
for line in (ROOT.parent/'FireRedLeafGreenRecomp/variants/firered/symbols/all_symbols.tsv').read_text().splitlines():
    f=line.split('\t')
    if len(f)>=6 and not line.startswith('#'):symbols[f[5]]=int(f[0],16)-0x08000000
def lz(offset):
    assert rom[offset]==0x10
    size=int.from_bytes(rom[offset+1:offset+4],'little');offset+=4;data=bytearray()
    while len(data)<size:
        flags=rom[offset];offset+=1
        for bit in range(7,-1,-1):
            if len(data)>=size:break
            if flags&(1<<bit):
                a,b=rom[offset:offset+2];offset+=2
                length=(a>>4)+3;back=((a&15)<<8)+b+1
                for _ in range(length):data.append(data[-back])
            else:data.append(rom[offset]);offset+=1
    return bytes(data[:size])
out=ROOT/'assets/battle';out.mkdir(parents=True,exist_ok=True)
records=[]
for name,species,table in [
    ('charmander',4,'gMonBackPicTable'),
    ('squirtle',7,'gMonFrontPicTable'),
    ('bulbasaur',1,'gMonBackPicTable'),
    ('charmander_front',4,'gMonFrontPicTable'),
    ('squirtle_back',7,'gMonBackPicTable'),
    ('bulbasaur_front',1,'gMonFrontPicTable'),
    ('pidgey',16,'gMonBackPicTable'),
    ('pidgey_front',16,'gMonFrontPicTable'),
    ('rattata',19,'gMonBackPicTable'),
    ('rattata_front',19,'gMonFrontPicTable')
]:
    ptr=struct.unpack_from('<I',rom,symbols[table]+8*species)[0]-0x08000000
    palptr=struct.unpack_from('<I',rom,symbols['gMonPaletteTable']+8*species)[0]-0x08000000
    tiles=lz(ptr);pal=lz(palptr)
    assert len(tiles)>=2048 and len(pal)==32
    values=struct.unpack('<16H',pal)
    colors=[tuple(((v>>s)&31)*255//31 for s in [0,5,10])+(255 if i else 0,) for i,v in enumerate(values)]
    pixels=bytearray()
    for y in range(64):
        pixels.append(0)
        for x in range(64):
            byte=tiles[((y//8)*8+x//8)*32+(y%8)*4+(x%8)//2]
            pixels.extend(colors[(byte>>(4*(x%2)))&15])
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',64,64,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
    path=out/(name+'.png');path.write_bytes(png)
    records.append(dict(name=name,species=species,table=table,sprite_pointer=hex(ptr+0x08000000),
        palette_pointer=hex(palptr+0x08000000),source_tiles_sha256=hashlib.sha256(tiles).hexdigest(),
        output_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
(ROOT/'data/battle-art-provenance.json').write_text(json.dumps(dict(rom_sha1=hashlib.sha1(rom).hexdigest(),sprites=records),indent=2)+'\n')
print('Extracted original Charmander back and Squirtle front sprites; transparent native UI art.')
