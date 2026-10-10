"""Bake source tile artwork into local textures; no billboards or AI-generated assets."""
import collections, hashlib, json, pathlib, struct, subprocess
from PIL import Image, ImageDraw

ROOT=pathlib.Path(__file__).resolve().parents[1]
REF=ROOT/'reference/pokefirered'
REV='037335f4c725d7c9aecdac87066f2002b4bd7e14'
assert subprocess.check_output(['git','-C',str(REF),'rev-parse','HEAD'],text=True).strip()==REV
OUT=ROOT/'assets/textures';OUT.mkdir(parents=True,exist_ok=True)
world=json.loads((ROOT/'data/world.json').read_text(encoding='utf-8'))
layouts={x['id']:x for x in json.loads((REF/'data/layouts/layouts.json').read_text())['layouts'] if 'id' in x}
sets = {
    'gTileset_General': 'primary/general',
    'gTileset_PalletTown': 'secondary/pallet_town',
    'gTileset_Building': 'primary/building',
    'gTileset_GenericBuilding1': 'secondary/generic_building_1',
    'gTileset_GenericBuilding2': 'secondary/generic_building_2',
    'gTileset_Lab': 'secondary/lab',
    'gTileset_ViridianCity': 'secondary/viridian_city',
    'gTileset_Mart': 'secondary/mart',
    'gTileset_PokemonCenter': 'secondary/pokemon_center',
    'gTileset_School': 'secondary/school',
    'gTileset_ViridianGym': 'secondary/viridian_gym',
    'gTileset_ViridianForest': 'secondary/viridian_forest',
    'gTileset_PewterCity': 'secondary/pewter_city',
    'gTileset_PewterGym': 'secondary/pewter_gym',
    'gTileset_Museum': 'secondary/museum',
    'gTileset_CeruleanCity': 'secondary/cerulean_city',
    'gTileset_Cave': 'secondary/cave',
    'gTileset_CeruleanGym': 'secondary/cerulean_gym',
    'gTileset_BikeShop': 'secondary/bike_shop',
    'gTileset_SeaCottage': 'secondary/sea_cottage',
    'gTileset_BurgledHouse': 'secondary/burgled_house',
    'gTileset_VermilionCity': 'secondary/vermilion_city',
    'gTileset_VermilionGym': 'secondary/vermilion_gym',
    'gTileset_FanClubDaycare': 'secondary/fan_club_daycare',
    'gTileset_UndergroundPath': 'secondary/underground_path',
    'gTileset_DiglettsCave': 'secondary/digletts_cave',
    'gTileset_SSAnne': 'secondary/ss_anne',
    'gTileset_LavenderTown': 'secondary/lavender_town',
    'gTileset_RockTunnel': 'secondary/rock_tunnel',
    'gTileset_PokemonTower': 'secondary/pokemon_tower',
    'gTileset_PowerPlant': 'secondary/power_plant',
    'gTileset_CeladonCity': 'secondary/celadon_city',
    'gTileset_CeladonGym': 'secondary/celadon_gym',
    'gTileset_DepartmentStore': 'secondary/department_store',
    'gTileset_GameCorner': 'secondary/game_corner',
    'gTileset_SilphCo': 'secondary/silph_co',
    'gTileset_Condominiums': 'secondary/condominiums',
    'gTileset_RestaurantHotel': 'secondary/restaurant_hotel',
    'gTileset_FuchsiaCity': 'secondary/fuchsia_city',
    'gTileset_FuchsiaGym': 'secondary/fuchsia_gym',
    'gTileset_SafariZoneBuilding': 'secondary/safari_zone_building',
}
hashes={};outputs={}
def read(path):
    data=(REF/path).read_bytes();hashes[str(path)]=hashlib.sha256(data).hexdigest();return data
def rgb555(v):return tuple(((v>>s)&31)*255//31 for s in [0,5,10])
rom=(ROOT.parent/'FireRedLeafGreenRecomp/variants/firered/roms/firered_usa.gba').read_bytes()
assert hashlib.sha1(rom).hexdigest()=='41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc'
symbols={}
for line in (ROOT.parent/'FireRedLeafGreenRecomp/variants/firered/symbols/all_symbols.tsv').read_text().splitlines():
    fields=line.split('\t')
    if len(fields)>=6 and not line.startswith('#'):symbols[fields[5]]=int(fields[0],16)-0x08000000
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
cache={}
def tileset(symbol):
    if symbol in cache:return cache[symbol]
    path=pathlib.Path('data/tilesets')/sets[symbol]
    art_path=pathlib.Path('data/tilesets/secondary/condominiums') if symbol=='gTileset_SilphCo' else path
    read(art_path/'tiles.png');tiles=Image.open(REF/art_path/'tiles.png');assert tiles.mode=='P'
    palettes=[]
    for i in range(16):
        lines=read(art_path/f'palettes/{i:02}.pal').decode().splitlines()[3:]
        palettes.append([tuple(map(int,line.split())) for line in lines if line.strip()])
    raw=read(path/'metatiles.bin');metatiles=list(struct.iter_unpack('<8H',raw))
    # Cross-check PNG indices, palettes and metatile definitions against the local ROM.
    offset=symbols[symbol];tp,pp,mp=struct.unpack_from('<3I',rom,offset+4)
    packed=lz(tp-0x08000000) if rom[offset] else rom[tp-0x08000000:tp-0x08000000+tiles.width*tiles.height//2]
    assert len(packed)<=tiles.width*tiles.height//2, (symbol,len(packed),tiles.size)
    for i,b in enumerate(packed):
        tile_index,within=divmod(i,32);tx=tile_index%(tiles.width//8)*8;ty=tile_index//(tiles.width//8)*8
        px=(within%4)*2;py=within//4
        assert tiles.getpixel((tx+px,ty+py))==b&15
        assert tiles.getpixel((tx+px+1,ty+py))==b>>4
    assert rom[mp-0x08000000:mp-0x08000000+len(raw)]==raw
    # Palette files are 8-bit representations of the original 5-bit colours.
    for i,palette in enumerate(palettes):
        for j,color in enumerate(palette):
            value=struct.unpack_from('<H',rom,pp-0x08000000+(i*16+j)*2)[0]
            assert all(abs(a-b)<=1 for a,b in zip(color,rgb555(value)))
    cache[symbol]=(tiles,palettes,metatiles);return cache[symbol]
def decode(primary,secondary,mid):
    pt,pp,pm=tileset(primary);st,sp,sm=tileset(secondary)
    words=pm[mid] if mid<640 else sm[mid-640]
    image=Image.new('RGBA',(16,16))
    for layer in range(2):
        for quad in range(4):
            word=words[layer*4+quad];tid=word&1023;pal=(word>>12)&15
            source=pt if tid<640 else st;index=tid if tid<640 else tid-640
            tx=index%(source.width//8)*8;ty=index//(source.width//8)*8
            if ty+8 > source.height:
                continue
            palette=pp[pal] if pal<7 else sp[pal]
            for y in range(8):
                for x in range(8):
                    v=source.getpixel((tx+(7-x if word&1024 else x),ty+(7-y if word&2048 else y)))
                    if v:image.putpixel((quad%2*8+x,quad//2*8+y),palette[v]+(255,))
    return image
def save(name,image):
    path=OUT/name;image.convert('RGB').save(path);outputs[name]=hashlib.sha256(path.read_bytes()).hexdigest()
for m in world['maps']:
    original=json.loads(read(pathlib.Path('data/maps')/m['source']/'map.json'))
    layout=layouts[original['layout']];primary=layout['primary_tileset'];secondary=layout['secondary_tileset']
    decoded={mid:decode(primary,secondary,mid) for mid in set(v&1023 for v in m['cells'])}
    plain=collections.Counter(v&1023 for i,v in enumerate(m['cells']) if not (v>>10)&3 and m['behaviors'][i]==0)
    if not plain: plain=collections.Counter(v&1023 for i,v in enumerate(m['cells']))
    base=decoded[plain.most_common(1)[0][0]]
    w=len(m['tiles'][0]);h=len(m['tiles']);ground=Image.new('RGBA',(w*16,h*16),(189,255,139,255))
    preview=ground.copy()
    for i,v in enumerate(m['cells']):
        at=(i%w*16,i//w*16);art=decoded[v&1023]
        ground.alpha_composite(base,at)
        # Do not flatten roofs, trees or furniture onto the ground below the model.
        if not m['indoor'] and not (v>>10)&3 and m['behaviors'][i] not in [0x60,0x61,0x62,0x63,0x69]:ground.alpha_composite(art,at)
        preview.alpha_composite(art,at)
    save(m['id']+'_ground.png',ground)
    if m['id']=='pallet':
        (ROOT/'captures').mkdir(exist_ok=True);preview.resize((w*32,h*32),Image.Resampling.NEAREST).save(ROOT/'captures/source-pallet-art.png')
        sheet=Image.new('RGB',(16*80,7*90),(60,70,80));draw=ImageDraw.Draw(sheet)
        for i,mid in enumerate(range(640,752)):
            try:art=decode(primary,secondary,mid)
            except IndexError:continue
            x=i%16*80;y=i//16*90;sheet.paste(art.resize((64,64),Image.Resampling.NEAREST),(x,y),art.resize((64,64),Image.Resampling.NEAREST));draw.text((x,y+65),f'{mid:03X}',fill='white')
        sheet.save(ROOT/'captures/source-building-tiles.png')
        def repeat(art,nx,ny):
            result=Image.new('RGBA',(art.width*nx,art.height*ny),(240,224,184,255))
            for y in range(ny):
                for x in range(nx):result.alpha_composite(art,(x*art.width,y*art.height))
            return result
        save('roof.png',repeat(decode(primary,secondary,0x28a),4,4))
        save('lab_roof.png',repeat(decode(primary,secondary,0x2b1),4,4))
        siding=decode(primary,secondary,0x29a).crop((0,9,16,16))
        save('wall.png',repeat(siding,6,4))
        save('lab_wall.png',repeat(decode(primary,secondary,0x2c2),6,2))
        windows=Image.new('RGBA',(32,16));windows.alpha_composite(decode(primary,secondary,0x299),(0,0));windows.alpha_composite(decode(primary,secondary,0x29a),(16,0))
        save('window.png',windows.crop((4,0,20,10)))
        save('door.png',decode(primary,secondary,0x2a3))

red=[rgb555(v) for v in struct.unpack_from('<16H',rom,symbols['gObjectEventPal_Player'])]
# Sprite palette and General greenery colour the solid abstract character and trees.
(ROOT/'data/art.json').write_text(json.dumps({'red_palette':red,'tree_palette':tileset('gTileset_General')[1][0]},indent=2))
(ROOT/'data/art-provenance.json').write_text(json.dumps({'repository':'https://github.com/pret/pokefirered','revision':REV,'rom_sha1':hashlib.sha1(rom).hexdigest(),'rom_verified_tilesets':list(cache),'source_hashes':hashes,'textures':outputs},indent=2))
print('Baked',len(outputs),'source textures; ROM-verified tiles, palettes and metatile definitions')
