"""Import original wild encounter tables from pinned pokefirered source."""
import json
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / 'reference/pokefirered'
REV = '037335f4c725d7c9aecdac87066f2002b4bd7e14'

raw = (REF / 'src/data/wild_encounters.json').read_bytes()
source_hash = hashlib.sha256(raw).hexdigest()
data = json.loads(raw.decode('utf-8'))
group = data["wild_encounter_groups"][0]

encounters = {}
for e in group["encounters"]:
    # Normalize map name: MAP_ROUTE1 -> route1, MAP_VIRIDIAN_FOREST -> viridian_forest
    map_key = e["map"].replace("MAP_", "").lower()
    entry = {}
    if "land_mons" in e and e["land_mons"]:
        entry["land"] = {
            "rate": e["land_mons"]["encounter_rate"],
            "slots": [
                {
                    "species": m["species"].replace("SPECIES_", "").title(),
                    "min_level": m["min_level"],
                    "max_level": m["max_level"]
                }
                for m in e["land_mons"]["mons"]
            ]
        }
    if "water_mons" in e and e["water_mons"]:
        entry["water"] = {
            "rate": e["water_mons"]["encounter_rate"],
            "slots": [
                {
                    "species": m["species"].replace("SPECIES_", "").title(),
                    "min_level": m["min_level"],
                    "max_level": m["max_level"]
                }
                for m in e["water_mons"]["mons"]
            ]
        }
    if entry:
        encounters[map_key] = entry

out_data = {
    "revision": REV,
    "encounters": encounters
}

(ROOT / 'data/encounters.json').write_text(json.dumps(out_data, indent=2) + '\n', encoding='utf-8')
(ROOT / 'data/encounters-provenance.json').write_text(json.dumps({
    "revision": REV,
    "source": "src/data/wild_encounters.json",
    "sha256": source_hash,
    "total_maps": len(encounters)
}, indent=2) + '\n', encoding='utf-8')

print(f"Imported wild encounters for {len(encounters)} maps.")
