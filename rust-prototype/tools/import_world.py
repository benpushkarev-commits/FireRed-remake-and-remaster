"""Reproducible, local import from a pinned pret checkout. Never executes guest code."""
import hashlib, json, pathlib, re, struct, subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
REF = ROOT / 'reference/pokefirered'
REV = '037335f4c725d7c9aecdac87066f2002b4bd7e14'
NAMES = {
    'PalletTown': 'pallet', 'Route1': 'route1', 'PalletTown_PlayersHouse_1F': 'home',
    'PalletTown_PlayersHouse_2F': 'bedroom', 'PalletTown_RivalsHouse': 'rival_home',
    'PalletTown_ProfessorOaksLab': 'lab',
    'ViridianCity': 'viridian', 'ViridianCity_Mart': 'viridian_mart',
    'ViridianCity_PokemonCenter_1F': 'viridian_center', 'ViridianCity_House': 'viridian_house',
    'ViridianCity_School': 'viridian_school', 'ViridianCity_Gym': 'viridian_gym',
    'Route22': 'route22', 'Route2': 'route2',
    'Route2_ViridianForest_SouthEntrance': 'route2_gate_south',
    'ViridianForest': 'viridian_forest',
    'Route2_ViridianForest_NorthEntrance': 'route2_gate_north',
    'Route2_House': 'route2_house', 'Route2_EastBuilding': 'route2_gate_east',
    'PewterCity': 'pewter', 'PewterCity_Gym': 'pewter_gym',
    'PewterCity_Mart': 'pewter_mart', 'PewterCity_PokemonCenter_1F': 'pewter_center',
    'PewterCity_Museum_1F': 'pewter_museum', 'PewterCity_House1': 'pewter_house1',
    'PewterCity_House2': 'pewter_house2',
    'Route3': 'route3', 'Route4': 'route4', 'Route4_PokemonCenter_1F': 'route4_center',
    'MtMoon_1F': 'mt_moon_1f', 'MtMoon_B1F': 'mt_moon_b1f', 'MtMoon_B2F': 'mt_moon_b2f',
    'CeruleanCity': 'cerulean', 'CeruleanCity_Gym': 'cerulean_gym',
    'CeruleanCity_BikeShop': 'cerulean_bike_shop',
    'CeruleanCity_House1': 'cerulean_house1', 'CeruleanCity_House2': 'cerulean_house2',
    'CeruleanCity_House3': 'cerulean_house3', 'CeruleanCity_House4': 'cerulean_house4',
    'CeruleanCity_House5': 'cerulean_house5', 'CeruleanCity_Mart': 'cerulean_mart',
    'CeruleanCity_PokemonCenter_1F': 'cerulean_center',
    'Route24': 'route24', 'Route25': 'route25', 'Route25_SeaCottage': 'route25_sea_cottage',
    'Route5': 'route5', 'Route5_PokemonDayCare': 'route5_daycare',
    'Route5_SouthEntrance': 'route5_gate_south',
    'UndergroundPath_NorthEntrance': 'underground_path_north_entrance',
    'UndergroundPath_NorthSouthTunnel': 'underground_path_tunnel',
    'UndergroundPath_SouthEntrance': 'underground_path_south_entrance',
    'Route6': 'route6', 'Route6_NorthEntrance': 'route6_gate_north',
    'VermilionCity': 'vermilion', 'VermilionCity_Gym': 'vermilion_gym',
    'VermilionCity_PokemonCenter_1F': 'vermilion_center',
    'VermilionCity_Mart': 'vermilion_mart',
    'VermilionCity_PokemonFanClub': 'vermilion_fan_club',
    'VermilionCity_House1': 'vermilion_house1',
    'VermilionCity_House2': 'vermilion_house2',
    'VermilionCity_House3': 'vermilion_house3',
    'SSAnne_Exterior': 'ssanne_exterior',
    'DiglettsCave_NorthEntrance': 'digletts_cave_north_entrance',
    'DiglettsCave_B1F': 'digletts_cave_b1f',
    'DiglettsCave_SouthEntrance': 'digletts_cave_south_entrance',
    'Route11': 'route11',
    'Route11_EastEntrance_1F': 'route11_gate_1f',
    'Route11_EastEntrance_2F': 'route11_gate_2f',
    'SSAnne_1F_Corridor': 'ssanne_1f_corridor',
    'SSAnne_1F_Room1': 'ssanne_1f_room1',
    'SSAnne_1F_Room2': 'ssanne_1f_room2',
    'SSAnne_1F_Room3': 'ssanne_1f_room3',
    'SSAnne_1F_Room4': 'ssanne_1f_room4',
    'SSAnne_1F_Room5': 'ssanne_1f_room5',
    'SSAnne_1F_Room6': 'ssanne_1f_room6',
    'SSAnne_1F_Room7': 'ssanne_1f_room7',
    'SSAnne_2F_Corridor': 'ssanne_2f_corridor',
    'SSAnne_2F_Room1': 'ssanne_2f_room1',
    'SSAnne_2F_Room2': 'ssanne_2f_room2',
    'SSAnne_2F_Room3': 'ssanne_2f_room3',
    'SSAnne_2F_Room4': 'ssanne_2f_room4',
    'SSAnne_2F_Room5': 'ssanne_2f_room5',
    'SSAnne_2F_Room6': 'ssanne_2f_room6',
    'SSAnne_3F_Corridor': 'ssanne_3f_corridor',
    'SSAnne_B1F_Corridor': 'ssanne_b1f_corridor',
    'SSAnne_B1F_Room1': 'ssanne_b1f_room1',
    'SSAnne_B1F_Room2': 'ssanne_b1f_room2',
    'SSAnne_B1F_Room3': 'ssanne_b1f_room3',
    'SSAnne_B1F_Room4': 'ssanne_b1f_room4',
    'SSAnne_B1F_Room5': 'ssanne_b1f_room5',
    'SSAnne_Deck': 'ssanne_deck',
    'SSAnne_Kitchen': 'ssanne_kitchen',
    'SSAnne_CaptainsOffice': 'ssanne_captains_office',
    'Route9': 'route9',
    'Route10': 'route10',
    'Route10_PokemonCenter_1F': 'route10_center',
    'RockTunnel_1F': 'rock_tunnel_1f',
    'RockTunnel_B1F': 'rock_tunnel_b1f',
    'LavenderTown': 'lavender',
    'LavenderTown_PokemonCenter_1F': 'lavender_center',
    'LavenderTown_Mart': 'lavender_mart',
    'LavenderTown_VolunteerPokemonHouse': 'lavender_volunteer_house',
    'LavenderTown_House1': 'lavender_house1',
    'LavenderTown_House2': 'lavender_house2',
    'PokemonTower_1F': 'pokemon_tower_1f',
    'PokemonTower_2F': 'pokemon_tower_2f',
    'PokemonTower_3F': 'pokemon_tower_3f',
    'PokemonTower_4F': 'pokemon_tower_4f',
    'PokemonTower_5F': 'pokemon_tower_5f',
    'PokemonTower_6F': 'pokemon_tower_6f',
    'PokemonTower_7F': 'pokemon_tower_7f',
    'PowerPlant': 'power_plant',
    'CeladonCity': 'celadon',
    'CeladonCity_Gym': 'celadon_gym',
    'CeladonCity_DepartmentStore_1F': 'celadon_dept_store_1f',
    'CeladonCity_DepartmentStore_2F': 'celadon_dept_store_2f',
    'CeladonCity_DepartmentStore_3F': 'celadon_dept_store_3f',
    'CeladonCity_DepartmentStore_4F': 'celadon_dept_store_4f',
    'CeladonCity_DepartmentStore_5F': 'celadon_dept_store_5f',
    'CeladonCity_DepartmentStore_Roof': 'celadon_dept_store_roof',
    'CeladonCity_DepartmentStore_Elevator': 'celadon_dept_store_elevator',
    'CeladonCity_GameCorner': 'celadon_game_corner',
    'CeladonCity_GameCorner_PrizeRoom': 'celadon_prize_room',
    'RocketHideout_B1F': 'rocket_hideout_b1f',
    'RocketHideout_B2F': 'rocket_hideout_b2f',
    'RocketHideout_B3F': 'rocket_hideout_b3f',
    'RocketHideout_B4F': 'rocket_hideout_b4f',
    'RocketHideout_Elevator': 'rocket_hideout_elevator',
    'CeladonCity_Condominiums_1F': 'celadon_condo_1f',
    'CeladonCity_Condominiums_2F': 'celadon_condo_2f',
    'CeladonCity_Condominiums_3F': 'celadon_condo_3f',
    'CeladonCity_Condominiums_Roof': 'celadon_condo_roof',
    'CeladonCity_Condominiums_RoofRoom': 'celadon_condo_roof_room',
    'CeladonCity_Hotel': 'celadon_hotel',
    'CeladonCity_PokemonCenter_1F': 'celadon_center',
    'CeladonCity_Restaurant': 'celadon_restaurant',
    'CeladonCity_House1': 'celadon_house1',
    'Route7': 'route7',
    'Route7_EastEntrance': 'route7_gate_east',
    'Route8': 'route8',
    'Route8_WestEntrance': 'route8_gate_west',
    'UndergroundPath_EastEntrance': 'underground_path_east_entrance',
    'UndergroundPath_EastWestTunnel': 'underground_path_east_west_tunnel',
    'UndergroundPath_WestEntrance': 'underground_path_west_entrance',
    'Route16': 'route16',
    'Route16_House': 'route16_house',
    'Route16_NorthEntrance_1F': 'route16_gate_1f',
    'Route16_NorthEntrance_2F': 'route16_gate_2f',
    'Route17': 'route17',
    'Route18': 'route18',
    'Route18_EastEntrance_1F': 'route18_gate_1f',
    'Route18_EastEntrance_2F': 'route18_gate_2f',
    'Route12': 'route12',
    'Route12_FishingHouse': 'route12_fishing_house',
    'Route12_NorthEntrance_1F': 'route12_gate_1f',
    'Route12_NorthEntrance_2F': 'route12_gate_2f',
    'Route13': 'route13',
    'Route14': 'route14',
    'Route15': 'route15',
    'Route15_WestEntrance_1F': 'route15_gate_1f',
    'Route15_WestEntrance_2F': 'route15_gate_2f',
    'FuchsiaCity': 'fuchsia',
    'FuchsiaCity_Gym': 'fuchsia_gym',
    'FuchsiaCity_House1': 'fuchsia_house1',
    'FuchsiaCity_House2': 'fuchsia_house2',
    'FuchsiaCity_House3': 'fuchsia_house3',
    'FuchsiaCity_Mart': 'fuchsia_mart',
    'FuchsiaCity_PokemonCenter_1F': 'fuchsia_center',
    'FuchsiaCity_SafariZone_Entrance': 'safari_zone_entrance',
    'FuchsiaCity_SafariZone_Office': 'safari_zone_office',
    'FuchsiaCity_WardensHouse': 'safari_wardens_house',
    'SafariZone_Center': 'safari_zone_center',
    'SafariZone_Center_RestHouse': 'safari_center_rest_house',
    'SafariZone_East': 'safari_zone_east',
    'SafariZone_East_RestHouse': 'safari_east_rest_house',
    'SafariZone_North': 'safari_zone_north',
    'SafariZone_North_RestHouse': 'safari_north_rest_house',
    'SafariZone_West': 'safari_zone_west',
    'SafariZone_West_RestHouse': 'safari_west_rest_house',
    'SafariZone_SecretHouse': 'safari_secret_house',
}
TILESETS = {
    'gTileset_General': 'primary/general', 'gTileset_PalletTown': 'secondary/pallet_town',
    'gTileset_Building': 'primary/building', 'gTileset_GenericBuilding1': 'secondary/generic_building_1',
    'gTileset_GenericBuilding2': 'secondary/generic_building_2', 'gTileset_Lab': 'secondary/lab',
    'gTileset_ViridianCity': 'secondary/viridian_city', 'gTileset_Mart': 'secondary/mart',
    'gTileset_PokemonCenter': 'secondary/pokemon_center', 'gTileset_School': 'secondary/school',
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
assert subprocess.check_output(['git','-C',str(REF),'rev-parse','HEAD'], text=True).strip() == REV
layouts = {x['id']:x for x in json.loads((REF/'data/layouts/layouts.json').read_text())['layouts'] if 'id' in x}
source_hashes = {}
def read(path):
    data=(REF/path).read_bytes();source_hashes[path]=hashlib.sha256(data).hexdigest();return data
def labels(text):
    return {m.group(1):m.group(2) for m in re.finditer(r'^(\w+)::\s*\n(.*?)(?=^\w+::|\Z)',text,re.M|re.S)}
texts={};scripts={}
for name in NAMES:
    for file,store in [('text.inc',texts),('scripts.inc',scripts)]:
        try:
            content = read(f'data/maps/{name}/{file}').decode()
        except FileNotFoundError:
            continue
        for label,body in labels(content).items():
            if file=='text.inc':
                fragments=re.findall(r'\.string "(.*)"',body)
                value=''.join(fragments).replace('\\n','\n').replace('\\l','\n').replace('$','')
                store[label]=value.replace('{PLAYER}','RED').replace('{RIVAL}','BLUE').split('\\p')
            else:store[label]=body
def first_text(script):
    body=scripts.get(script,'')
    m=re.search(r'(?:msgbox|message)\s+(\w+)',body)
    return texts.get(m[1],[]) if m else []

maps=[]; originals={}
for name,id in NAMES.items():
    original=json.loads(read(f'data/maps/{name}/map.json'))
    originals[id]=original
    layout=layouts[original['layout']]; width=layout['width'];height=layout['height']
    raw=read(layout['blockdata_filepath']);words=list(struct.unpack(f'<{width*height}H',raw))
    attrs=[]
    for symbol in [layout['primary_tileset'],layout['secondary_tileset']]:
        data=read('data/tilesets/'+TILESETS[symbol]+'/metatile_attributes.bin')
        attrs.append(struct.unpack(f'<{len(data)//4}I',data))
    behaviors=[attrs[0][v&1023]&511 if (v&1023)<640 else attrs[1][(v&1023)-640]&511 for v in words]
    encounter_types=[((attrs[0][v&1023] >> 24) & 7) if (v&1023)<640 else ((attrs[1][(v&1023)-640] >> 24) & 7) for v in words]
    rows=[]
    for y in range(height):
        row=''
        for x in range(width):
            i=y*width+x;word=words[i];mid=word&1023;b=behaviors[i]
            row+=('W' if b in [0x10,0x11,0x12,0x15] else 'g' if b==2 else 'L' if b in [0x38,0x39,0x3a,0x3b] else
                  'X' if (word>>10)&3 else 'p' if mid in [1,4,0x20,0x21,0x28,0x29] else '.')
        rows.append(row)
    npcs=[]
    for n in original['object_events']:
        # An explicit post-intro exploration checkpoint: hidden objects retain source metadata, not fake dialogue.
        if n.get('type') == 'clone' or n.get('flag', '0') != '0': continue
        script=n.get('script', '');lines=first_text(script)
        if script=='PalletTown_EventScript_SignLady':lines=texts['PalletTown_Text_RaisingMonsToo']
        if id=='home':lines=texts['PalletTown_PlayersHouse_1F_Text_YouShouldTakeQuickRest']+texts['PalletTown_PlayersHouse_1F_Text_LookingGreatTakeCare']
        npcs.append(dict(at=[n['x'],n['y']],name=n['graphics_id'].replace('OBJ_EVENT_GFX_','').replace('_',' ').title(),
                         lines=lines,script=script,elevation=n['elevation'],movement=n['movement_type'],graphics=n['graphics_id']))
    signs=[]
    for s in original['bg_events']:
        if 'script' not in s: continue
        lines=first_text(s['script'])
        if id=='home':lines=texts['PalletTown_PlayersHouse_1F_Text_MovieOnTVFourBoysOnRailroad']
        signs.append(dict(at=[s['x'],s['y']],script=s['script'],lines=lines))
    props=[]
    if id=='pallet':
        props=[dict(kind='house',at=[5,4],size=[5,3],door=[6,7],label='Your home'),dict(kind='house',at=[14,4],size=[5,3],door=[15,7],label='Rival home'),dict(kind='lab',at=[13,10],size=[7,3],door=[16,13],label='Oak laboratory')]
    # Presentation hints never determine collision: original map cells do.
    for s in signs:
        if id in ['pallet','route1']:props.append(dict(kind='sign',at=s['at'],size=[1,1]))
    spawn = {
        'pallet': [10, 10], 'route1': [12, 36], 'home': [4, 7], 'bedroom': [10, 3],
        'rival_home': [4, 7], 'lab': [6, 11], 'viridian': [23, 26], 'viridian_mart': [4, 7],
        'viridian_center': [7, 8], 'viridian_house': [4, 7], 'viridian_school': [4, 7],
        'viridian_gym': [17, 21],
        'route22': [39, 13], 'route2': [15, 69], 'route2_gate_south': [6, 7],
        'viridian_forest': [29, 61], 'route2_gate_north': [6, 7],
        'route2_house': [4, 7], 'route2_gate_east': [6, 7],
        'pewter': [23, 38], 'pewter_gym': [5, 12], 'pewter_mart': [4, 7],
        'pewter_center': [7, 8], 'pewter_museum': [14, 8],
        'pewter_house1': [4, 7], 'pewter_house2': [4, 7],
        'route3': [5, 10], 'route4': [19, 6], 'route4_center': [7, 8],
        'mt_moon_1f': [18, 37], 'mt_moon_b1f': [5, 6], 'mt_moon_b2f': [25, 21],
        'cerulean': [22, 20], 'cerulean_gym': [8, 16], 'cerulean_bike_shop': [5, 6],
        'cerulean_house1': [3, 6], 'cerulean_house2': [3, 6], 'cerulean_house3': [4, 6],
        'cerulean_house4': [4, 6], 'cerulean_house5': [4, 6], 'cerulean_mart': [4, 6],
        'cerulean_center': [7, 7], 'route24': [11, 22], 'route25': [51, 5],
        'route25_sea_cottage': [7, 8],
        'route5': [31, 32], 'route5_daycare': [4, 6], 'route5_gate_south': [4, 4],
        'underground_path_north_entrance': [6, 6], 'underground_path_tunnel': [3, 5],
        'underground_path_south_entrance': [6, 6], 'route6': [19, 14],
        'route6_gate_north': [4, 4], 'vermilion': [15, 7], 'vermilion_gym': [5, 17],
        'vermilion_center': [7, 7], 'vermilion_mart': [4, 6],
        'vermilion_fan_club': [5, 8], 'vermilion_house1': [4, 6],
        'vermilion_house2': [4, 6], 'vermilion_house3': [4, 6],
        'ssanne_exterior': [32, 6],
        'digletts_cave_north_entrance': [4, 4], 'digletts_cave_b1f': [3, 4],
        'digletts_cave_south_entrance': [4, 4], 'route11': [10, 8],
        'route11_gate_1f': [5, 6], 'route11_gate_2f': [5, 6],
        'ssanne_1f_corridor': [3, 9],
        'ssanne_1f_room1': [2, 3], 'ssanne_1f_room2': [2, 3], 'ssanne_1f_room3': [2, 3],
        'ssanne_1f_room4': [2, 3], 'ssanne_1f_room5': [2, 3], 'ssanne_1f_room6': [2, 3],
        'ssanne_1f_room7': [2, 3],
        'ssanne_2f_corridor': [3, 11],
        'ssanne_2f_room1': [4, 5], 'ssanne_2f_room2': [4, 5], 'ssanne_2f_room3': [4, 5],
        'ssanne_2f_room4': [4, 5], 'ssanne_2f_room5': [4, 5], 'ssanne_2f_room6': [4, 5],
        'ssanne_3f_corridor': [3, 4],
        'ssanne_b1f_corridor': [5, 3],
        'ssanne_b1f_room1': [4, 5], 'ssanne_b1f_room2': [4, 5], 'ssanne_b1f_room3': [4, 5],
        'ssanne_b1f_room4': [4, 5], 'ssanne_b1f_room5': [4, 5],
        'ssanne_deck': [14, 8], 'ssanne_kitchen': [7, 3], 'ssanne_captains_office': [3, 5],
        'route9': [10, 8], 'route10': [13, 21], 'route10_center': [7, 7],
        'rock_tunnel_1f': [17, 3], 'rock_tunnel_b1f': [38, 28],
        'lavender': [6, 6], 'lavender_center': [7, 7],
        'lavender_mart': [4, 6], 'lavender_volunteer_house': [4, 6],
        'lavender_house1': [4, 6], 'lavender_house2': [4, 6],
        'pokemon_tower_1f': [11, 17], 'pokemon_tower_2f': [4, 10],
        'pokemon_tower_3f': [5, 10], 'pokemon_tower_4f': [5, 10],
        'pokemon_tower_5f': [4, 10], 'pokemon_tower_6f': [12, 16],
        'pokemon_tower_7f': [12, 16], 'power_plant': [5, 37],
        'celadon': [48, 12], 'celadon_gym': [6, 17],
        'celadon_dept_store_1f': [2, 13], 'celadon_dept_store_2f': [3, 3],
        'celadon_dept_store_3f': [9, 3], 'celadon_dept_store_4f': [3, 3],
        'celadon_dept_store_5f': [9, 3], 'celadon_dept_store_roof': [15, 7],
        'celadon_dept_store_elevator': [2, 4], 'celadon_game_corner': [10, 12],
        'celadon_prize_room': [4, 7], 'rocket_hideout_b1f': [12, 3],
        'rocket_hideout_b2f': [28, 3], 'rocket_hideout_b3f': [18, 3],
        'rocket_hideout_b4f': [11, 16], 'rocket_hideout_elevator': [2, 4],
        'celadon_condo_1f': [12, 17], 'celadon_condo_2f': [4, 3],
        'celadon_condo_3f': [2, 3], 'celadon_condo_roof': [4, 3],
        'celadon_condo_roof_room': [4, 6], 'celadon_hotel': [4, 8],
        'celadon_center': [7, 7], 'celadon_restaurant': [6, 8],
        'celadon_house1': [4, 6],
        'route7': [7, 15], 'route7_gate_east': [1, 5],
        'route8': [13, 5], 'route8_gate_west': [1, 4],
        'underground_path_east_entrance': [5, 8], 'underground_path_east_west_tunnel': [76, 3],
        'underground_path_west_entrance': [5, 8],
        'route16': [10, 6], 'route16_house': [3, 7],
        'route16_gate_1f': [1, 3], 'route16_gate_2f': [10, 9],
        'route17': [20, 77], 'route18': [41, 9],
        'route18_gate_1f': [1, 6], 'route18_gate_2f': [10, 9],
        'route12': [12, 87], 'route12_fishing_house': [2, 7],
        'route12_gate_1f': [5, 1], 'route12_gate_2f': [10, 9],
        'route13': [47, 12], 'route14': [14, 37],
        'route15': [9, 11], 'route15_gate_1f': [1, 6],
        'route15_gate_2f': [10, 9],
        'fuchsia': [24, 6], 'fuchsia_gym': [6, 21],
        'fuchsia_house1': [3, 7], 'fuchsia_house2': [2, 8],
        'fuchsia_house3': [4, 7], 'fuchsia_mart': [3, 7],
        'fuchsia_center': [6, 8], 'safari_zone_entrance': [4, 1],
        'safari_zone_office': [5, 9], 'safari_wardens_house': [5, 9],
        'safari_zone_center': [25, 30], 'safari_center_rest_house': [3, 9],
        'safari_zone_east': [8, 9], 'safari_east_rest_house': [3, 9],
        'safari_zone_north': [10, 34], 'safari_north_rest_house': [3, 9],
        'safari_zone_west': [30, 5], 'safari_west_rest_house': [3, 9],
        'safari_secret_house': [3, 9],
    }[id]
    name_map = {
        'pallet': 'Pallet Town', 'route1': 'Route 1', 'home': 'Your home - downstairs',
        'bedroom': 'Your bedroom', 'rival_home': "Rival's house", 'lab': "Oak's laboratory",
        'viridian': 'Viridian City', 'viridian_mart': 'Viridian Poké Mart',
        'viridian_center': 'Viridian Pokémon Center', 'viridian_house': 'Viridian City house',
        'viridian_school': 'Viridian Pokémon Academy', 'viridian_gym': 'Viridian Gym',
        'route22': 'Route 22', 'route2': 'Route 2', 'route2_gate_south': 'Route 2 South Gate',
        'viridian_forest': 'Viridian Forest', 'route2_gate_north': 'Route 2 North Gate',
        'route2_house': 'Route 2 House', 'route2_gate_east': 'Route 2 East Gate',
        'pewter': 'Pewter City', 'pewter_gym': 'Pewter Gym', 'pewter_mart': 'Pewter Poké Mart',
        'pewter_center': 'Pewter Pokémon Center', 'pewter_museum': 'Pewter Museum',
        'pewter_house1': 'Pewter City House 1', 'pewter_house2': 'Pewter City House 2',
        'route3': 'Route 3', 'route4': 'Route 4', 'route4_center': 'Route 4 Pokémon Center',
        'mt_moon_1f': 'Mt. Moon 1F', 'mt_moon_b1f': 'Mt. Moon B1F', 'mt_moon_b2f': 'Mt. Moon B2F',
        'cerulean': 'Cerulean City', 'cerulean_gym': 'Cerulean Gym', 'cerulean_bike_shop': 'Cerulean Bike Shop',
        'cerulean_house1': 'Cerulean House 1', 'cerulean_house2': 'Burgled House', 'cerulean_house3': 'Cerulean House 3',
        'cerulean_house4': 'Cerulean House 4', 'cerulean_house5': 'Cerulean House 5', 'cerulean_mart': 'Cerulean Poké Mart',
        'cerulean_center': 'Cerulean Pokémon Center', 'route24': 'Route 24', 'route25': 'Route 25',
        'route25_sea_cottage': "Bill's Sea Cottage",
        'route5': 'Route 5', 'route5_daycare': 'Route 5 Day Care',
        'route5_gate_south': 'Route 5 South Gate',
        'underground_path_north_entrance': 'Underground Path North Entrance',
        'underground_path_tunnel': 'Underground Path',
        'underground_path_south_entrance': 'Underground Path South Entrance',
        'route6': 'Route 6', 'route6_gate_north': 'Route 6 North Gate',
        'vermilion': 'Vermilion City', 'vermilion_gym': 'Vermilion Gym',
        'vermilion_center': 'Vermilion Pokémon Center',
        'vermilion_mart': 'Vermilion Poké Mart',
        'vermilion_fan_club': 'Pokémon Fan Club',
        'vermilion_house1': 'Vermilion City House 1',
        'vermilion_house2': 'Vermilion City House 2',
        'vermilion_house3': 'Vermilion City House 3',
        'ssanne_exterior': 'S.S. Anne Exterior',
        'digletts_cave_north_entrance': "Diglett's Cave North Entrance",
        'digletts_cave_b1f': "Diglett's Cave",
        'digletts_cave_south_entrance': "Diglett's Cave South Entrance",
        'route11': 'Route 11',
        'route11_gate_1f': 'Route 11 East Gate 1F',
        'route11_gate_2f': 'Route 11 East Gate 2F',
        'ssanne_1f_corridor': 'S.S. Anne 1F Corridor',
        'ssanne_1f_room1': 'S.S. Anne 1F Cabin 1',
        'ssanne_1f_room2': 'S.S. Anne 1F Cabin 2',
        'ssanne_1f_room3': 'S.S. Anne 1F Cabin 3',
        'ssanne_1f_room4': 'S.S. Anne 1F Cabin 4',
        'ssanne_1f_room5': 'S.S. Anne 1F Cabin 5',
        'ssanne_1f_room6': 'S.S. Anne 1F Cabin 6',
        'ssanne_1f_room7': 'S.S. Anne 1F Cabin 7',
        'ssanne_2f_corridor': 'S.S. Anne 2F Corridor',
        'ssanne_2f_room1': 'S.S. Anne 2F Cabin 1',
        'ssanne_2f_room2': 'S.S. Anne 2F Cabin 2',
        'ssanne_2f_room3': 'S.S. Anne 2F Cabin 3',
        'ssanne_2f_room4': 'S.S. Anne 2F Cabin 4',
        'ssanne_2f_room5': 'S.S. Anne 2F Cabin 5',
        'ssanne_2f_room6': 'S.S. Anne 2F Cabin 6',
        'ssanne_3f_corridor': 'S.S. Anne 3F Corridor',
        'ssanne_b1f_corridor': 'S.S. Anne B1F Corridor',
        'ssanne_b1f_room1': 'S.S. Anne B1F Cabin 1',
        'ssanne_b1f_room2': 'S.S. Anne B1F Cabin 2',
        'ssanne_b1f_room3': 'S.S. Anne B1F Cabin 3',
        'ssanne_b1f_room4': 'S.S. Anne B1F Cabin 4',
        'ssanne_b1f_room5': 'S.S. Anne B1F Cabin 5',
        'ssanne_deck': 'S.S. Anne Deck',
        'ssanne_kitchen': 'S.S. Anne Kitchen',
        'ssanne_captains_office': "S.S. Anne Captain's Office",
        'route9': 'Route 9',
        'route10': 'Route 10',
        'route10_center': 'Route 10 Pokémon Center',
        'rock_tunnel_1f': 'Rock Tunnel 1F',
        'rock_tunnel_b1f': 'Rock Tunnel B1F',
        'lavender': 'Lavender Town',
        'lavender_center': 'Lavender Pokémon Center',
        'lavender_mart': 'Lavender Poké Mart',
        'lavender_volunteer_house': "Volunteer Pokémon House",
        'lavender_house1': "Lavender Town House 1",
        'lavender_house2': "Lavender Town House 2",
        'pokemon_tower_1f': 'Pokémon Tower 1F',
        'pokemon_tower_2f': 'Pokémon Tower 2F',
        'pokemon_tower_3f': 'Pokémon Tower 3F',
        'pokemon_tower_4f': 'Pokémon Tower 4F',
        'pokemon_tower_5f': 'Pokémon Tower 5F',
        'pokemon_tower_6f': 'Pokémon Tower 6F',
        'pokemon_tower_7f': 'Pokémon Tower 7F',
        'power_plant': 'Power Plant',
        'celadon': 'Celadon City',
        'celadon_gym': 'Celadon Gym',
        'celadon_dept_store_1f': 'Celadon Dept. Store 1F',
        'celadon_dept_store_2f': 'Celadon Dept. Store 2F',
        'celadon_dept_store_3f': 'Celadon Dept. Store 3F',
        'celadon_dept_store_4f': 'Celadon Dept. Store 4F',
        'celadon_dept_store_5f': 'Celadon Dept. Store 5F',
        'celadon_dept_store_roof': 'Celadon Dept. Store Roof',
        'celadon_dept_store_elevator': 'Celadon Dept. Store Elevator',
        'celadon_game_corner': 'Celadon Game Corner',
        'celadon_prize_room': 'Celadon Prize Room',
        'rocket_hideout_b1f': 'Rocket Hideout B1F',
        'rocket_hideout_b2f': 'Rocket Hideout B2F',
        'rocket_hideout_b3f': 'Rocket Hideout B3F',
        'rocket_hideout_b4f': 'Rocket Hideout B4F',
        'rocket_hideout_elevator': 'Rocket Hideout Elevator',
        'celadon_condo_1f': 'Celadon Mansion 1F',
        'celadon_condo_2f': 'Celadon Mansion 2F',
        'celadon_condo_3f': 'Celadon Mansion 3F',
        'celadon_condo_roof': 'Celadon Mansion Roof',
        'celadon_condo_roof_room': 'Celadon Mansion Roof Room',
        'celadon_hotel': 'Celadon Hotel',
        'celadon_center': 'Celadon Pokémon Center',
        'celadon_restaurant': 'Celadon Restaurant',
        'celadon_house1': 'Celadon City House 1',
        'route7': 'Route 7',
        'route7_gate_east': 'Route 7 East Gate',
        'route8': 'Route 8',
        'route8_gate_west': 'Route 8 West Gate',
        'underground_path_east_entrance': 'Underground Path East Entrance',
        'underground_path_east_west_tunnel': 'Underground Path (East-West)',
        'underground_path_west_entrance': 'Underground Path West Entrance',
        'route16': 'Route 16',
        'route16_house': 'Route 16 House',
        'route16_gate_1f': 'Route 16 Gate 1F',
        'route16_gate_2f': 'Route 16 Gate 2F',
        'route17': 'Route 17',
        'route18': 'Route 18',
        'route18_gate_1f': 'Route 18 Gate 1F',
        'route18_gate_2f': 'Route 18 Gate 2F',
        'route12': 'Route 12',
        'route12_fishing_house': 'Route 12 Fishing House',
        'route12_gate_1f': 'Route 12 Gate 1F',
        'route12_gate_2f': 'Route 12 Gate 2F',
        'route13': 'Route 13',
        'route14': 'Route 14',
        'route15': 'Route 15',
        'route15_gate_1f': 'Route 15 Gate 1F',
        'route15_gate_2f': 'Route 15 Gate 2F',
        'fuchsia': 'Fuchsia City',
        'fuchsia_gym': 'Fuchsia Gym',
        'fuchsia_house1': 'Fuchsia City House 1',
        'fuchsia_house2': 'Fuchsia City House 2',
        'fuchsia_house3': 'Fuchsia City House 3',
        'fuchsia_mart': 'Fuchsia Poké Mart',
        'fuchsia_center': 'Fuchsia Pokémon Center',
        'safari_zone_entrance': 'Safari Zone Entrance',
        'safari_zone_office': 'Safari Zone Office',
        'safari_wardens_house': "Safari Zone Warden's House",
        'safari_zone_center': 'Safari Zone Center',
        'safari_center_rest_house': 'Safari Zone Center Rest House',
        'safari_zone_east': 'Safari Zone East',
        'safari_east_rest_house': 'Safari Zone East Rest House',
        'safari_zone_north': 'Safari Zone North',
        'safari_north_rest_house': 'Safari Zone North Rest House',
        'safari_zone_west': 'Safari Zone West',
        'safari_west_rest_house': 'Safari Zone West Rest House',
        'safari_secret_house': 'Safari Zone Secret House',
    }
    maps.append(dict(id=id,name=name_map[id],spawn=spawn,tiles=rows,cells=words,behaviors=behaviors,encounter_types=encounter_types,
                     indoor=original['map_type']=='MAP_TYPE_INDOOR',allow_running=original['allow_running'],props=props,npcs=npcs,signs=signs,warps=[],connections=[],source=name))
for m in maps:
    original=originals[m['id']]
    for warp in original['warp_events']:
        target=next((v for v in maps if originals[v['id']]['id']==warp['dest_map']),None)
        if not target:continue
        destination=originals[target['id']]['warp_events'][int(warp['dest_warp_id'])]
        m['warps'].append(dict(at=[warp['x'],warp['y']],map=target['id'],to=[destination['x'],destination['y']],source_index=original['warp_events'].index(warp)))
    for c in original.get('connections') or []:
        target=next((v for v in maps if originals[v['id']]['id']==c['map']),None)
        if target:m['connections'].append(dict(direction=c['direction'],offset=c['offset'],map=target['id']))

# Independently compare all imported map cells and used behavior attributes against Ben's local rev0 ROM.
project=ROOT.parent;rom_path=project/'FireRedLeafGreenRecomp/variants/firered/roms/firered_usa.gba'
rom=rom_path.read_bytes();assert hashlib.sha1(rom).hexdigest()=='41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc'
symbols={}
for line in (project/'FireRedLeafGreenRecomp/variants/firered/symbols/all_symbols.tsv').read_text().splitlines():
    f=line.split('\t')
    if len(f)>=6 and not line.startswith('#'):symbols[f[5]]=int(f[0],16)-0x08000000
def u32(offset):return struct.unpack_from('<I',rom,offset)[0]
for m in maps:
    layout_name = layouts[originals[m['id']]['layout']]['name']
    offset = symbols[layout_name]
    w,h,_,ptr,prim,sec = struct.unpack_from('<6I',rom,offset)
    assert [w,h]==[len(m['tiles'][0]),len(m['tiles'])]
    assert list(struct.unpack_from(f'<{w*h}H',rom,ptr-0x08000000))==m['cells'],m['id']+' map mismatch'
    for i,word in enumerate(m['cells']):
        mid=word&1023;ts=(prim if mid<640 else sec)-0x08000000;idx=mid if mid<640 else mid-640
        attr=u32(ts+20)-0x08000000
        val=u32(attr+idx*4)
        assert val&511==m['behaviors'][i],m['id']+' behavior mismatch'
        assert (val>>24)&7==m['encounter_types'][i],m['id']+' encounter type mismatch'

(ROOT/'data/world.json').write_text(json.dumps(dict(maps=maps),ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'data/provenance.json').write_text(json.dumps(dict(repository='https://github.com/pret/pokefirered',revision=REV,
    rom_sha1=hashlib.sha1(rom).hexdigest(),rom_verified_maps=[m['id'] for m in maps],source_hashes=source_hashes,
    checkpoint='Post-intro exploration; running shoes granted; sign-lady scene complete. Static NPC snapshots; scripted progression and battles not yet ported.'),indent=2))
print('Imported and ROM-verified',len(maps),'maps;',sum(len(m['cells']) for m in maps),'original cells')
