//! Source-port foundation. New behaviour UNVERIFIED against original C.
//! Semantic native storage; not GBA encrypted BoxPokemon or .sav compatibility.
use serde::{Deserialize, Serialize};
use std::{collections::HashMap, path::Path};

#[derive(Deserialize)]
pub struct Species {
    pub base: [u16; 6],
    pub growth: usize,
    pub friendship: u8,
    pub abilities: [u8; 2],
    pub learnset: Vec<[u16; 2]>,
    pub fields: HashMap<String, String>,
}
#[derive(Deserialize)]
pub struct Move {
    pub pp: u8,
    pub fields: HashMap<String, String>,
}
#[derive(Deserialize)]
pub struct Item {
    pub pocket: String,
    #[serde(flatten)]
    pub fields: HashMap<String, serde_json::Value>,
}
#[derive(Deserialize)]
pub struct Data {
    pub revision: String,
    pub species: HashMap<u16, Species>,
    pub moves: HashMap<u16, Move>,
    pub items: HashMap<u16, Item>,
    pub ids: HashMap<String, u16>,
    pub natures: Vec<[i16; 5]>,
    pub experience: Vec<Vec<u32>>,
}
impl Data {
    pub fn source() -> Self {
        serde_json::from_str(include_str!("../data/core.json")).expect("pinned core tables")
    }
    pub fn id(&self, name: &str) -> Result<u16, String> {
        self.ids
            .get(name)
            .copied()
            .ok_or_else(|| format!("Unsupported source ID: {name}"))
    }
    pub fn pocket(&self, id: u16) -> Option<usize> {
        match self.items.get(&id)?.pocket.as_str() {
            "POCKET_ITEMS" => Some(0),
            "POCKET_KEY_ITEMS" => Some(1),
            "POCKET_POKE_BALLS" => Some(2),
            "POCKET_TM_CASE" => Some(3),
            "POCKET_BERRY_POUCH" => Some(4),
            _ => None,
        }
    }
    pub fn species_has_type(&self, species: u16, wanted: u8) -> Result<bool, String> {
        let types = self
            .species
            .get(&species)
            .and_then(|entry| entry.fields.get("types"))
            .ok_or_else(|| format!("Missing source types for species {species}"))?;
        types
            .trim_matches(['{', '}'])
            .split(',')
            .map(|value| {
                value
                    .trim()
                    .parse::<u8>()
                    .map_err(|_| format!("Invalid source types for species {species}"))
            })
            .try_fold(false, |found, value| Ok(found || value? == wanted))
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Rng {
    pub state: u32,
    pub calls: u64,
}
impl Rng {
    /// random.c, Random32 macro in random.h: low half is drawn first.
    pub fn next16(&mut self) -> u16 {
        self.state = self.state.wrapping_mul(1103515245).wrapping_add(24691);
        self.calls = self.calls.wrapping_add(1);
        (self.state >> 16) as u16
    }
    pub fn next32(&mut self) -> u32 {
        u32::from(self.next16()) | (u32::from(self.next16()) << 16)
    }
}
pub enum OtId {
    Player,
    Preset(u32),
    RandomNoShiny,
}
fn default_poke_ball() -> u16 {
    4
}
fn default_respawn() -> u16 {
    1
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Pokemon {
    pub species: u16,
    pub personality: u32,
    pub ot_id: u32,
    pub experience: u32,
    pub level: u8,
    pub ivs: [u8; 6],
    pub evs: [u8; 6],
    pub stats: [u16; 6],
    pub hp: u16,
    pub moves: [u16; 4],
    pub pp: [u8; 4],
    pub pp_bonuses: u8,
    pub friendship: u8,
    pub ability_num: u8,
    pub held_item: u16,
    pub status: u32,
    #[serde(default = "default_poke_ball")]
    pub poke_ball: u16,
}
impl Pokemon {
    /// Core subset of CreateBoxMon/CreateMon; met/OT text metadata deferred.
    pub fn create(
        data: &Data,
        rng: &mut Rng,
        species: u16,
        level: u8,
        fixed_iv: u8,
        personality: Option<u32>,
        ot: OtId,
        player_id: u32,
    ) -> Result<Self, String> {
        let info = data.species.get(&species).ok_or("Unsupported species")?;
        if !(1..=100).contains(&level) {
            return Err("Level outside 1..100".into());
        }
        let personality = personality.unwrap_or_else(|| rng.next32());
        let ot_id = match ot {
            OtId::Player => player_id,
            OtId::Preset(id) => id,
            OtId::RandomNoShiny => loop {
                let id = rng.next32();
                if ((id >> 16) ^ (id & 65535) ^ (personality >> 16) ^ (personality & 65535)) >= 8 {
                    break id;
                }
            },
        };
        let mut ivs = [fixed_iv; 6];
        if fixed_iv >= 32 {
            for chunk in ivs.chunks_mut(3) {
                let value = rng.next16();
                for (i, iv) in chunk.iter_mut().enumerate() {
                    *iv = ((value >> (5 * i)) & 31) as u8;
                }
            }
        }
        let mut mon = Self {
            species,
            personality,
            ot_id,
            experience: data.experience[info.growth][level as usize],
            level,
            ivs,
            evs: [0; 6],
            stats: [0; 6],
            hp: 0,
            moves: [0; 4],
            pp: [0; 4],
            pp_bonuses: 0,
            friendship: info.friendship,
            ability_num: if info.abilities[1] != 0 {
                (personality & 1) as u8
            } else {
                0
            },
            held_item: 0,
            status: 0,
            poke_ball: data.ids.get("ITEM_POKE_BALL").copied().unwrap_or(4),
        };
        // GiveBoxMonInitialMoveset/GiveMoveToBoxMon, retaining source order.
        for &[learn_level, move_id] in &info.learnset {
            if learn_level > u16::from(level) {
                break;
            }
            if mon.moves.contains(&move_id) {
                continue;
            }
            let slot = if let Some(slot) = mon.moves.iter().position(|&m| m == 0) {
                slot
            } else {
                mon.moves.rotate_left(1);
                mon.pp.rotate_left(1);
                3
            };
            mon.moves[slot] = move_id;
            mon.pp[slot] = data.moves.get(&move_id).ok_or("Missing move")?.pp;
        }
        mon.recalculate(data)?;
        Ok(mon)
    }
    /// pokemon.c:MonRestorePP/BoxMonRestorePP.
    pub fn restore_pp(&mut self, data: &Data) -> Result<(), String> {
        for i in 0..4 {
            if self.moves[i] != 0 {
                let base = u16::from(
                    data.moves
                        .get(&self.moves[i])
                        .ok_or("Unsupported move for PP restore")?
                        .pp,
                );
                let ups = u16::from((self.pp_bonuses >> (2 * i)) & 3);
                self.pp[i] = (base + base * 20 * ups / 100) as u8;
            }
        }
        Ok(())
    }
    /// CalculateMonStats/GetLevelFromMonExp, including Shedinja and HP wrap bug.
    pub fn recalculate(&mut self, data: &Data) -> Result<(), String> {
        let info = data
            .species
            .get(&self.species)
            .ok_or("Unsupported species")?;
        let table = &data.experience[info.growth];
        self.level = (1..=100)
            .take_while(|&n| table[n] <= self.experience)
            .last()
            .unwrap_or(0) as u8;
        let old_hp = i32::from(self.stats[0]);
        for i in 0..6 {
            let n =
                (2 * u32::from(info.base[i]) + u32::from(self.ivs[i]) + u32::from(self.evs[i]) / 4)
                    * u32::from(self.level)
                    / 100;
            self.stats[i] = if i == 0 {
                (n + u32::from(self.level) + 10) as u16
            } else {
                ((n + 5) as i32
                    * (100 + 10 * i32::from(data.natures[(self.personality % 25) as usize][i - 1]))
                    / 100) as u16
            };
        }
        if self.species == data.id("SPECIES_SHEDINJA")? {
            self.stats[0] = 1;
            if self.hp != 0 || old_hp == 0 {
                self.hp = 1;
            }
        } else if self.hp == 0 && old_hp == 0 {
            self.hp = self.stats[0];
        } else if self.hp != 0 {
            // Source explicitly documents this bug; do not clamp to one.
            self.hp = (i32::from(self.hp) + i32::from(self.stats[0]) - old_hp) as u16;
        }
        Ok(())
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BoxPokemon {
    pub species: u16,
    pub personality: u32,
    pub ot_id: u32,
    pub experience: u32,
    pub ivs: [u8; 6],
    pub evs: [u8; 6],
    pub moves: [u16; 4],
    pub pp: [u8; 4],
    pub pp_bonuses: u8,
    pub friendship: u8,
    pub ability_num: u8,
    pub held_item: u16,
    #[serde(default = "default_poke_ball")]
    pub poke_ball: u16,
}
impl From<Pokemon> for BoxPokemon {
    fn from(mon: Pokemon) -> Self {
        Self {
            species: mon.species,
            personality: mon.personality,
            ot_id: mon.ot_id,
            experience: mon.experience,
            ivs: mon.ivs,
            evs: mon.evs,
            moves: mon.moves,
            pp: mon.pp,
            pp_bonuses: mon.pp_bonuses,
            friendship: mon.friendship,
            ability_num: mon.ability_num,
            held_item: mon.held_item,
            poke_ball: mon.poke_ball,
        }
    }
}
pub const TOTAL_BOXES_COUNT: usize = 14;
pub const IN_BOX_COUNT: usize = 30;
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct PokemonStorage {
    pub current_box: u8,
    pub boxes: Vec<Vec<Option<BoxPokemon>>>,
    pub names: Vec<String>,
    pub wallpapers: Vec<u8>,
}
impl Default for PokemonStorage {
    /// pokemon_storage_system_menu.c:ResetPokemonStorageSystem.
    fn default() -> Self {
        Self {
            current_box: 0,
            boxes: (0..TOTAL_BOXES_COUNT)
                .map(|_| vec![None; IN_BOX_COUNT])
                .collect(),
            names: (1..=TOTAL_BOXES_COUNT)
                .map(|box_id| format!("Box {box_id}"))
                .collect(),
            wallpapers: (0..TOTAL_BOXES_COUNT)
                .map(|box_id| (box_id % 4) as u8)
                .collect(),
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GiveMonResult {
    Party,
    Pc { box_id: u8, box_position: u8 },
    Full,
}
const DEX_FLAG_BYTES: usize = 52;
// pokemon.c:sSpeciesToNationalPokedexNum for the non-linear Hoenn species IDs
// 277..=411. Kanto/Johto IDs 1..=251 already equal their National Dex numbers.
const HOENN_NATIONAL_DEX: [u16; 135] = [
    252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270,
    271, 272, 273, 274, 275, 290, 291, 292, 276, 277, 285, 286, 327, 278, 279, 283, 284, 320, 321,
    300, 301, 352, 343, 344, 299, 324, 302, 339, 340, 370, 341, 342, 349, 350, 318, 319, 328, 329,
    330, 296, 297, 309, 310, 322, 323, 363, 364, 365, 331, 332, 361, 362, 337, 338, 298, 325, 326,
    311, 312, 303, 307, 308, 333, 334, 360, 355, 356, 315, 287, 288, 289, 316, 317, 357, 293, 294,
    295, 366, 367, 368, 359, 353, 354, 336, 335, 369, 304, 305, 306, 351, 313, 314, 345, 346, 347,
    348, 280, 281, 282, 371, 372, 373, 374, 375, 376, 377, 378, 379, 382, 383, 384, 380, 381, 385,
    386, 358,
];
pub fn species_to_national_dex(species: u16) -> Option<u16> {
    match species {
        1..=251 => Some(species),
        // The unused legacy Unown species retain the source's trailing dex slots.
        252..=276 => Some(species + 135),
        277..=411 => Some(HOENN_NATIONAL_DEX[usize::from(species - 277)]),
        _ => None,
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Pokedex {
    seen: Vec<u8>,
    owned: Vec<u8>,
    pub national_enabled: bool,
    pub unown_personality: u32,
    pub spinda_personality: u32,
}
impl Default for Pokedex {
    fn default() -> Self {
        Self {
            seen: vec![0; DEX_FLAG_BYTES],
            owned: vec![0; DEX_FLAG_BYTES],
            national_enabled: false,
            unown_personality: 0,
            spinda_personality: 0,
        }
    }
}
impl Pokedex {
    fn flag(flags: &[u8], national: u16) -> bool {
        let index = usize::from(national.saturating_sub(1));
        flags
            .get(index / 8)
            .is_some_and(|byte| byte & (1 << (index & 7)) != 0)
    }
    fn set(flags: &mut [u8], national: u16) {
        let index = usize::from(national.saturating_sub(1));
        if let Some(byte) = flags.get_mut(index / 8) {
            *byte |= 1 << (index & 7);
        }
    }
    pub fn seen(&self, species: u16) -> bool {
        species_to_national_dex(species).is_some_and(|n| Self::flag(&self.seen, n))
    }
    pub fn caught(&self, species: u16) -> bool {
        species_to_national_dex(species)
            .is_some_and(|n| Self::flag(&self.seen, n) && Self::flag(&self.owned, n))
    }
    /// pokemon.c:HandleSetPokedexFlag. The native save keeps one semantic seen
    /// vector rather than the original's two anticheat replicas.
    pub fn set_seen(&mut self, species: u16, personality: u32) -> Result<(), String> {
        let national = species_to_national_dex(species)
            .ok_or_else(|| format!("Unsupported species {species} for Pokédex"))?;
        if !Self::flag(&self.seen, national) {
            Self::set(&mut self.seen, national);
            if national == 201 {
                self.unown_personality = personality;
            } else if national == 327 {
                self.spinda_personality = personality;
            }
        }
        Ok(())
    }
    pub fn set_caught(&mut self, species: u16, personality: u32) -> Result<(), String> {
        self.set_seen(species, personality)?;
        let national = species_to_national_dex(species).unwrap();
        Self::set(&mut self.owned, national);
        Ok(())
    }
    pub fn count(&self, caught: bool, national: bool) -> u16 {
        let end = if national { 386 } else { 151 };
        (1..=end)
            .filter(|&number| {
                Self::flag(&self.seen, number) && (!caught || Self::flag(&self.owned, number))
            })
            .count() as u16
    }
}
#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct Slot {
    pub item: u16,
    pub quantity: u16,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Bag {
    pub pockets: Vec<Vec<Slot>>,
}
impl Default for Bag {
    fn default() -> Self {
        Self {
            pockets: [42, 30, 13, 58, 43]
                .iter()
                .map(|&n| vec![Slot::default(); n])
                .collect(),
        }
    }
}
impl Bag {
    /// item.c: only the first matching stack counts, even for malformed saves.
    pub fn has(&self, data: &Data, item: u16, count: u16) -> bool {
        data.pocket(item)
            .and_then(|p| self.pockets[p].iter().find(|s| s.item == item))
            .is_some_and(|s| s.quantity >= count)
    }
    pub fn has_space(&self, data: &Data, item: u16, count: u16) -> bool {
        let Some(p) = data.pocket(item) else {
            return false;
        };
        if let Some(s) = self.pockets[p].iter().find(|s| s.item == item) {
            return u32::from(s.quantity) + u32::from(count) <= 999;
        }
        self.pockets[p].iter().any(|s| s.item == 0)
    }
    /// AddBagItem: container insertion occurs before checking destination space.
    pub fn add(&mut self, data: &Data, events: &mut Events, item: u16, count: u16) -> bool {
        let Some(p) = data.pocket(item) else {
            return false;
        };
        if let Some(s) = self.pockets[p].iter_mut().find(|s| s.item == item) {
            if u32::from(s.quantity) + u32::from(count) > 999 {
                return false;
            }
            s.quantity += count;
            return true;
        }
        let container = if p == 3 {
            Some("ITEM_TM_CASE")
        } else if p == 4 {
            Some("ITEM_BERRY_POUCH")
        } else {
            None
        };
        if let Some(name) = container {
            let id = data.ids[name];
            if !self.has(data, id, 1) {
                let Some(s) = self.pockets[1].iter_mut().find(|s| s.item == 0) else {
                    return false;
                };
                *s = Slot {
                    item: id,
                    quantity: 1,
                };
                if p == 4 {
                    events.flag_set(0x847);
                }
            }
        }
        if item == data.ids["ITEM_BERRY_POUCH"] {
            events.flag_set(0x847);
        }
        let Some(s) = self.pockets[p].iter_mut().find(|s| s.item == 0) else {
            return false;
        };
        *s = Slot {
            item,
            quantity: count,
        };
        true
    }
    pub fn remove(&mut self, data: &Data, item: u16, count: u16) -> bool {
        if item == 0 {
            return false;
        }
        let Some(p) = data.pocket(item) else {
            return false;
        };
        let Some(s) = self.pockets[p].iter_mut().find(|s| s.item == item) else {
            return false;
        };
        if s.quantity < count {
            return false;
        }
        s.quantity -= count;
        if s.quantity == 0 {
            s.item = 0;
        }
        true
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Events {
    pub flags: Vec<u8>,
    pub vars: Vec<u16>,
    #[serde(skip)]
    pub special_flags: [u8; 16],
    #[serde(skip)]
    pub special_vars: [u16; 21],
}
impl Default for Events {
    fn default() -> Self {
        Self {
            flags: vec![0; 0x900 / 8],
            vars: vec![0; 256],
            special_flags: [0; 16],
            special_vars: [0; 21],
        }
    }
}
impl Events {
    /// wild_encounter.c:UpdateRepelCounter. Return true requests RepelWoreOff.
    /// Union Room and quest-log playback suppress both decrement and script.
    pub fn update_repel_counter(
        &mut self,
        union_room: bool,
        quest_log_playback: bool,
    ) -> Result<bool, String> {
        if union_room || quest_log_playback {
            return Ok(false);
        }
        let steps = self.var_get(0x4020)?;
        if steps == 0 {
            return Ok(false);
        }
        self.var_set(0x4020, steps - 1)?;
        Ok(steps == 1)
    }

    // event_data.c, quest-log stopped path; invalid GBA addresses fail safely.
    fn flag_byte_mut(&mut self, id: u16) -> Option<&mut u8> {
        if id == 0 {
            None
        } else if id < 0x4000 {
            self.flags.get_mut(usize::from(id) / 8)
        } else {
            self.special_flags.get_mut(usize::from(id - 0x4000) / 8)
        }
    }
    pub fn flag_get(&self, id: u16) -> bool {
        if id == 0 {
            return false;
        }
        let byte = if id < 0x4000 {
            self.flags.get(usize::from(id) / 8)
        } else {
            self.special_flags.get(usize::from(id - 0x4000) / 8)
        };
        byte.is_some_and(|b| b & (1 << (id & 7)) != 0)
    }
    pub fn flag_set(&mut self, id: u16) -> bool {
        if let Some(b) = self.flag_byte_mut(id) {
            *b |= 1 << (id & 7);
        }
        false
    }
    pub fn flag_clear(&mut self, id: u16) -> bool {
        if let Some(b) = self.flag_byte_mut(id) {
            *b &= !(1 << (id & 7));
        }
        false
    }
    pub fn var_get(&self, id: u16) -> Result<u16, String> {
        if id < 0x4000 {
            return Ok(id);
        }
        let value = if id < 0x8000 {
            self.vars.get(usize::from(id - 0x4000))
        } else {
            self.special_vars.get(usize::from(id - 0x8000))
        };
        value
            .copied()
            .ok_or_else(|| format!("Unsupported variable {id:#x}"))
    }
    pub fn var_set(&mut self, id: u16, value: u16) -> Result<bool, String> {
        if id < 0x4000 {
            return Ok(false);
        }
        let slot = if id < 0x8000 {
            self.vars.get_mut(usize::from(id - 0x4000))
        } else {
            self.special_vars.get_mut(usize::from(id - 0x8000))
        };
        *slot.ok_or("Unsupported variable")? = value;
        Ok(true)
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct State {
    pub version: u32,
    pub revision: String,
    pub rng: Rng,
    pub trainer_id: u32,
    pub money: u32,
    pub party: Vec<Pokemon>,
    #[serde(default)]
    pub storage: PokemonStorage,
    #[serde(default)]
    pub pokedex: Pokedex,
    pub bag: Bag,
    pub events: Events,
    pub map: String,
    pub position: [i32; 2],
    #[serde(default = "default_respawn")]
    pub respawn: u16,
}
#[derive(Debug, Default, PartialEq, Eq)]
pub struct FieldPoison {
    pub poisoned: usize,
    pub fainted: Vec<usize>,
}
impl State {
    /// event_data.c:EnableNationalPokedex/IsNationalPokedexEnabled keeps three
    /// redundant markers and requires all of them when reading the state.
    pub fn enable_national_pokedex(&mut self) -> Result<(), String> {
        self.pokedex.national_enabled = true;
        self.events.var_set(0x404e, 0x6258)?;
        self.events.flag_set(0x840);
        Ok(())
    }
    pub fn national_pokedex_enabled(&self) -> bool {
        self.pokedex.national_enabled
            && self.events.var_get(0x404e) == Ok(0x6258)
            && self.events.flag_get(0x840)
    }
    /// money.c: cap includes the original unsigned-overflow check.
    pub fn add_money(&mut self, amount: u32) {
        let sum = self.money.wrapping_add(amount);
        self.money = if sum > 999999 || sum < self.money {
            999999
        } else {
            sum
        };
    }
    pub fn remove_money(&mut self, amount: u32) {
        self.money = self.money.saturating_sub(amount);
    }
    /// field_control_avatar.c:UpdatePoisonStepCounter and
    /// field_poison.c:DoPoisonFieldEffect/FaintFromFieldPoison. The caller
    /// supplies the map-type exclusion because map headers remain importer-owned.
    pub fn field_poison_step(&mut self, secret_base: bool) -> Result<FieldPoison, String> {
        if secret_base {
            return Ok(FieldPoison::default());
        }
        let counter = (self.events.var_get(0x4022)? + 1) % 5;
        self.events.var_set(0x4022, counter)?;
        if counter != 0 {
            return Ok(FieldPoison::default());
        }
        let mut result = FieldPoison::default();
        for (index, mon) in self.party.iter_mut().enumerate() {
            // GetAilmentFromStatus checks regular and toxic poison first.
            if mon.status & ((1 << 3) | (1 << 7)) == 0 {
                continue;
            }
            mon.hp = mon.hp.saturating_sub(1);
            result.poisoned += 1;
            if mon.hp == 0 {
                // FRIENDSHIP_EVENT_FAINT_OUTSIDE_BATTLE = {-5, -5, -10}.
                let loss = if mon.friendship >= 200 { 10 } else { 5 };
                mon.friendship = mon.friendship.saturating_sub(loss);
                mon.status = 0;
                result.fainted.push(index);
            }
        }
        Ok(result)
    }
    /// overworld.c:ComputeWhiteOutMoneyLoss.
    pub fn whiteout_money_loss(&self) -> u32 {
        const MULTIPLIERS: [u32; 9] = [2, 4, 6, 9, 12, 16, 20, 25, 30];
        // constants/flags.h: FLAG_BADGE01_GET through FLAG_BADGE08_GET.
        let badges = (0x820..=0x827)
            .filter(|&flag| self.events.flag_get(flag))
            .count();
        let top_level = self.party.iter().map(|mon| mon.level).max().unwrap_or(0);
        (u32::from(top_level) * 4 * MULTIPLIERS[badges]).min(self.money)
    }
    /// overworld.c:DoWhiteOut regular party/money path. The host performs the
    /// source heal-location warp after confirming that map is imported.
    pub fn whiteout(&mut self, data: &Data) -> Result<u32, String> {
        let loss = self.whiteout_money_loss();
        self.remove_money(loss);
        self.heal_party(data)?;
        Ok(loss)
    }
    /// script_pokemon_util.c HealPlayerParty and CalculatePPWithBonus.
    pub fn heal_party(&mut self, data: &Data) -> Result<(), String> {
        for mon in &mut self.party {
            mon.hp = mon.stats[0];
            mon.status = 0;
            for i in 0..4 {
                let pp = u16::from(
                    data.moves
                        .get(&mon.moves[i])
                        .ok_or("Unsupported move for healing")?
                        .pp,
                );
                let ups = u16::from((mon.pp_bonuses >> (2 * i)) & 3);
                mon.pp[i] = (pp + pp * 20 * ups / 100) as u8;
            }
        }
        Ok(())
    }
    /// Partial NewGameInitData foundation: story initialization deferred to VM.
    pub fn new(data: &Data, seed: u32, generated_trainer_id_low: u16) -> Self {
        let mut rng = Rng {
            state: seed,
            calls: 0,
        };
        let trainer_id = (u32::from(rng.next16()) << 16) | u32::from(generated_trainer_id_low);
        Self {
            version: 1,
            revision: data.revision.clone(),
            rng,
            trainer_id,
            money: 3000,
            party: Vec::new(),
            storage: PokemonStorage::default(),
            pokedex: Pokedex::default(),
            bag: Bag::default(),
            events: Events::default(),
            map: "PalletTown_PlayersHouse_2F".into(),
            position: [6, 6],
            respawn: default_respawn(),
        }
    }
    pub fn add_party(&mut self, mon: Pokemon) -> Result<(), String> {
        if self.party.len() >= 6 {
            return Err("Party full; PC transfer not ported".into());
        }
        self.party.push(mon);
        Ok(())
    }
    /// pokemon.c:GiveMonToPlayer/SendMonToPC regular path.
    pub fn give_mon(&mut self, data: &Data, mut mon: Pokemon) -> Result<GiveMonResult, String> {
        mon.ot_id = self.trainer_id;
        if self.party.len() < 6 {
            self.party.push(mon);
            return Ok(GiveMonResult::Party);
        }
        let previous_box = self.events.var_get(0x4037)? as usize;
        let start = usize::from(self.storage.current_box);
        for offset in 0..TOTAL_BOXES_COUNT {
            let box_id = (start + offset) % TOTAL_BOXES_COUNT;
            if let Some(box_position) = self.storage.boxes[box_id].iter().position(Option::is_none)
            {
                mon.restore_pp(data)?;
                self.storage.boxes[box_id][box_position] = Some(mon.into());
                self.events.var_set(0x8010, box_id as u16)?;
                self.events.var_set(0x8011, box_position as u16)?;
                if previous_box != box_id {
                    self.events.flag_clear(0x843);
                }
                self.events.var_set(0x4037, box_id as u16)?;
                return Ok(GiveMonResult::Pc {
                    box_id: box_id as u8,
                    box_position: box_position as u8,
                });
            }
        }
        Ok(GiveMonResult::Full)
    }
    pub fn player_and_storage_full(&self) -> bool {
        self.party.len() >= 6
            && self
                .storage
                .boxes
                .iter()
                .all(|slots| slots.iter().all(Option::is_some))
    }
    pub fn save(&self, path: &Path) -> Result<(), String> {
        let bytes = serde_json::to_vec_pretty(self).map_err(|e| e.to_string())?;
        let pending = path.with_extension("pending");
        std::fs::write(&pending, bytes).map_err(|e| e.to_string())?;
        std::fs::rename(&pending, path).map_err(|e| e.to_string())
    }
    pub fn load(data: &Data, path: &Path) -> Result<Self, String> {
        let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
        let state: Self = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
        state.validate(data)?;
        Ok(state)
    }
    pub fn validate(&self, data: &Data) -> Result<(), String> {
        if self.version != 1
            || self.revision != data.revision
            || self.party.len() > 6
            || usize::from(self.storage.current_box) >= TOTAL_BOXES_COUNT
            || self.storage.boxes.len() != TOTAL_BOXES_COUNT
            || self
                .storage
                .boxes
                .iter()
                .any(|slots| slots.len() != IN_BOX_COUNT)
            || self.storage.names.len() != TOTAL_BOXES_COUNT
            || self.storage.wallpapers.len() != TOTAL_BOXES_COUNT
            || self.pokedex.seen.len() != DEX_FLAG_BYTES
            || self.pokedex.owned.len() != DEX_FLAG_BYTES
            || self.events.flags.len() != 288
            || self.events.vars.len() != 256
            || self.bag.pockets.iter().map(Vec::len).collect::<Vec<_>>() != [42, 30, 13, 58, 43]
            || self.bag.pockets.iter().enumerate().any(|(p, slots)| {
                slots
                    .iter()
                    .any(|s| s.item != 0 && data.pocket(s.item) != Some(p))
            })
            || self.party.iter().any(|m| {
                !data.species.contains_key(&m.species)
                    || !(1..=100).contains(&m.level)
                    || m.ivs.iter().any(|&iv| iv > 31)
                    || m.moves.iter().any(|id| !data.moves.contains_key(id))
            })
            || self.storage.boxes.iter().flatten().flatten().any(|m| {
                !data.species.contains_key(&m.species)
                    || m.ivs.iter().any(|&iv| iv > 31)
                    || m.moves.iter().any(|id| !data.moves.contains_key(id))
            })
        {
            return Err("Unsupported or malformed native save".into());
        }
        Ok(())
    }
}

#[cfg(test)]
mod repel_tests {
    use super::{
        BoxPokemon, Data, Events, GiveMonResult, IN_BOX_COUNT, OtId, Pokemon, State,
        TOTAL_BOXES_COUNT, species_to_national_dex,
    };

    #[test]
    fn repel_counter_respects_exclusions_and_requests_script_once() {
        let mut events = Events::default();
        events.var_set(0x4020, 2).unwrap();
        assert!(!events.update_repel_counter(true, false).unwrap());
        assert!(!events.update_repel_counter(false, true).unwrap());
        assert_eq!(events.var_get(0x4020).unwrap(), 2);
        assert!(!events.update_repel_counter(false, false).unwrap());
        assert!(events.update_repel_counter(false, false).unwrap());
        assert!(!events.update_repel_counter(false, false).unwrap());
        assert_eq!(events.var_get(0x4020).unwrap(), 0);
    }

    #[test]
    fn imported_species_types_support_battle_context() {
        let data = Data::source();
        assert!(
            data.species_has_type(data.ids["SPECIES_SQUIRTLE"], 11)
                .unwrap()
        );
        assert!(
            data.species_has_type(data.ids["SPECIES_CATERPIE"], 6)
                .unwrap()
        );
        assert!(
            !data
                .species_has_type(data.ids["SPECIES_CHARMANDER"], 11)
                .unwrap()
        );
    }

    #[test]
    fn give_mon_scans_pc_boxes_in_source_order_and_restores_pp() {
        let data = Data::source();
        let mut state = State::new(&data, 42, 1234);
        let species = data.ids["SPECIES_CHARMANDER"];
        let mut mon = Pokemon::create(
            &data,
            &mut state.rng,
            species,
            5,
            31,
            Some(0),
            OtId::Preset(99),
            state.trainer_id,
        )
        .unwrap();
        mon.poke_ball = data.ids["ITEM_ULTRA_BALL"];
        for _ in 0..6 {
            assert_eq!(
                state.give_mon(&data, mon.clone()).unwrap(),
                GiveMonResult::Party
            );
        }
        assert_eq!(state.party[0].ot_id, state.trainer_id);

        state.storage.current_box = 13;
        let filler = BoxPokemon::from(mon.clone());
        state.storage.boxes[13].fill(Some(filler.clone()));
        for position in 0..5 {
            state.storage.boxes[0][position] = Some(filler.clone());
        }
        state.events.var_set(0x4037, 13).unwrap();
        state.events.flag_set(0x843);
        mon.pp[0] = 0;
        assert_eq!(
            state.give_mon(&data, mon).unwrap(),
            GiveMonResult::Pc {
                box_id: 0,
                box_position: 5
            }
        );
        let stored = state.storage.boxes[0][5].as_ref().unwrap();
        assert!(stored.pp[0] > 0);
        assert_eq!(stored.ot_id, state.trainer_id);
        assert_eq!(stored.poke_ball, data.ids["ITEM_ULTRA_BALL"]);
        assert_eq!(state.events.var_get(0x4037).unwrap(), 0);
        assert_eq!(state.events.var_get(0x8010).unwrap(), 0);
        assert_eq!(state.events.var_get(0x8011).unwrap(), 5);
        assert!(!state.events.flag_get(0x843));

        for box_id in 0..TOTAL_BOXES_COUNT {
            for position in 0..IN_BOX_COUNT {
                state.storage.boxes[box_id][position] = Some(filler.clone());
            }
        }
        assert!(state.player_and_storage_full());
        let mut rejected = state.party[0].clone();
        rejected.personality = 77;
        assert_eq!(
            state.give_mon(&data, rejected).unwrap(),
            GiveMonResult::Full
        );
        state.validate(&data).unwrap();
    }

    #[test]
    fn previous_native_saves_gain_default_storage_and_ball_metadata() {
        let data = Data::source();
        let mut state = State::new(&data, 42, 1234);
        let mon = Pokemon::create(
            &data,
            &mut state.rng,
            data.ids["SPECIES_BULBASAUR"],
            5,
            31,
            Some(0),
            OtId::Player,
            state.trainer_id,
        )
        .unwrap();
        state.party.push(mon);
        let mut value = serde_json::to_value(state).unwrap();
        let object = value.as_object_mut().unwrap();
        object.remove("storage");
        object.remove("pokedex");
        object.remove("respawn");
        object["party"].as_array_mut().unwrap()[0]
            .as_object_mut()
            .unwrap()
            .remove("poke_ball");
        let restored: State = serde_json::from_value(value).unwrap();
        assert_eq!(restored.storage.boxes.len(), TOTAL_BOXES_COUNT);
        assert_eq!(restored.party[0].poke_ball, data.ids["ITEM_POKE_BALL"]);
        assert_eq!(restored.pokedex.count(false, true), 0);
        assert_eq!(restored.respawn, 1);
        restored.validate(&data).unwrap();
    }

    #[test]
    fn pokedex_maps_internal_species_and_tracks_seen_caught() {
        assert_eq!(species_to_national_dex(1), Some(1));
        assert_eq!(species_to_national_dex(251), Some(251));
        assert_eq!(species_to_national_dex(252), Some(387));
        assert_eq!(species_to_national_dex(277), Some(252));
        assert_eq!(species_to_national_dex(301), Some(290));
        assert_eq!(species_to_national_dex(410), Some(386));
        assert_eq!(species_to_national_dex(411), Some(358));
        assert_eq!(species_to_national_dex(412), None);

        let mut dex = super::Pokedex::default();
        dex.set_seen(16, 0).unwrap();
        dex.set_seen(201, 0x1234).unwrap();
        dex.set_caught(308, 0x5678).unwrap();
        assert!(dex.seen(201));
        assert!(!dex.caught(201));
        assert!(dex.seen(308));
        assert!(dex.caught(308));
        assert_eq!(dex.unown_personality, 0x1234);
        assert_eq!(dex.spinda_personality, 0x5678);
        assert_eq!(dex.count(false, false), 1);
        assert_eq!(dex.count(true, false), 0);
        assert_eq!(dex.count(false, true), 3);
        assert_eq!(dex.count(true, true), 1);

        let data = Data::source();
        let mut state = State::new(&data, 42, 1234);
        assert!(!state.national_pokedex_enabled());
        state.enable_national_pokedex().unwrap();
        assert!(state.national_pokedex_enabled());
        state.events.flag_clear(0x840);
        assert!(!state.national_pokedex_enabled());
    }

    #[test]
    fn field_poison_uses_five_step_counter_and_faint_cleanup() {
        let data = Data::source();
        let mut state = State::new(&data, 42, 1234);
        for (hp, friendship, status) in [(2, 199, 1 << 3), (1, 200, 1 << 7), (3, 70, 0)] {
            let mut mon = Pokemon::create(
                &data,
                &mut state.rng,
                data.ids["SPECIES_BULBASAUR"],
                10,
                31,
                Some(0),
                OtId::Player,
                state.trainer_id,
            )
            .unwrap();
            mon.hp = hp;
            mon.friendship = friendship;
            mon.status = status;
            state.party.push(mon);
        }
        for expected in 1..=4 {
            assert_eq!(state.field_poison_step(false).unwrap(), Default::default());
            assert_eq!(state.events.var_get(0x4022).unwrap(), expected);
        }
        let result = state.field_poison_step(false).unwrap();
        assert_eq!(result.poisoned, 2);
        assert_eq!(result.fainted, [1]);
        assert_eq!(state.events.var_get(0x4022).unwrap(), 0);
        assert_eq!(state.party[0].hp, 1);
        assert_eq!(state.party[0].status, 1 << 3);
        assert_eq!(state.party[1].hp, 0);
        assert_eq!(state.party[1].status, 0);
        assert_eq!(state.party[1].friendship, 190);
        assert_eq!(state.party[2].hp, 3);

        state.events.var_set(0x4022, 4).unwrap();
        assert_eq!(state.field_poison_step(true).unwrap(), Default::default());
        assert_eq!(state.events.var_get(0x4022).unwrap(), 4);
    }

    #[test]
    fn whiteout_uses_badges_top_level_money_cap_and_heals() {
        let data = Data::source();
        let mut state = State::new(&data, 42, 1234);
        let mut mon = Pokemon::create(
            &data,
            &mut state.rng,
            data.ids["SPECIES_BULBASAUR"],
            10,
            31,
            Some(0),
            OtId::Player,
            state.trainer_id,
        )
        .unwrap();
        mon.hp = 0;
        mon.status = 1;
        mon.pp = [0; 4];
        state.party.push(mon);
        state.events.flag_set(0x820);
        state.events.flag_set(0x821);
        assert_eq!(state.whiteout_money_loss(), 240);
        assert_eq!(state.whiteout(&data).unwrap(), 240);
        assert_eq!(state.money, 2760);
        assert_eq!(state.party[0].hp, state.party[0].stats[0]);
        assert_eq!(state.party[0].status, 0);
        assert!(state.party[0].pp[0] > 0);

        state.money = 100;
        assert_eq!(state.whiteout_money_loss(), 100);
    }
}
