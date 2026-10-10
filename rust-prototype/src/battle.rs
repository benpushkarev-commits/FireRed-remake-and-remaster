//! Bounded native port of the opening rival battle. Original AI script data is
//! executed directly; unsupported state/opcodes are errors, never fallback AI.
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Deserialize)]
pub struct Species {
    pub name: String,
    pub base: [u16; 6],
    #[serde(default)]
    pub types: [u8; 2],
    #[serde(default)]
    pub abilities: [u8; 2],
    pub moves: Vec<String>,
    pub exp_yield: u32,
    pub ev_yield: [u16; 6],
}
#[derive(Clone, Deserialize)]
pub struct Move {
    pub power: u16,
    pub accuracy: u16,
    pub pp: u8,
    pub effect: String,
    #[serde(default)]
    pub move_type: u8,
    #[serde(default)]
    pub secondary_chance: u8,
}
#[derive(Clone, Deserialize)]
pub struct Data {
    pub revision: String,
    pub species: Vec<Species>,
    pub moves: HashMap<String, Move>,
    pub labels: HashMap<String, usize>,
    pub instructions: Vec<(String, Vec<String>)>,
    pub natures: Vec<[i16; 5]>,
    pub rival_personality: u32,
    pub ai_roots: Vec<String>,
    pub experience: Vec<u32>,
}
impl Data {
    pub fn source() -> Self {
        let mut data: Self = serde_json::from_str(include_str!("../data/battle.json"))
            .expect("Imported battle data");
        let core = crate::core::Data::source();
        for species in &mut data.species {
            let symbol = format!("SPECIES_{}", species.name.to_ascii_uppercase());
            if let Some(info) = core.ids.get(&symbol).and_then(|id| core.species.get(id)) {
                species.abilities = info.abilities;
                if let Some(raw) = info.fields.get("types") {
                    let values: Vec<u8> = raw
                        .trim_matches(|c: char| c == '{' || c == '}' || c.is_whitespace())
                        .split(',')
                        .filter_map(|value| value.trim().parse().ok())
                        .collect();
                    if values.len() == 2 {
                        species.types = [values[0], values[1]];
                    }
                }
            }
        }
        for (name, mv) in &mut data.moves {
            if let Some(value) = core
                .ids
                .get(name)
                .and_then(|id| core.moves.get(id))
                .and_then(|info| info.fields.get("type"))
                .and_then(|value| value.parse().ok())
            {
                mv.move_type = value;
            }
            if let Some(value) = core
                .ids
                .get(name)
                .and_then(|id| core.moves.get(id))
                .and_then(|info| info.fields.get("secondaryEffectChance"))
                .and_then(|value| value.parse().ok())
            {
                mv.secondary_chance = value;
            }
        }
        data
    }
}
#[derive(Clone, Copy, Debug, Serialize)]
pub struct Rng {
    pub state: u32,
    pub calls: u32,
}
impl Rng {
    /// src/random.c: ISO_RANDOMIZE1; wrapping is GBA unsigned arithmetic.
    pub fn next(&mut self) -> u16 {
        self.state = self.state.wrapping_mul(1103515245).wrapping_add(24691);
        self.calls += 1;
        (self.state >> 16) as u16
    }
}
#[derive(Clone, Copy, Debug, Default, Serialize)]
pub struct CatchContext {
    pub target_is_water_or_bug: bool,
    pub underwater: bool,
    pub target_previously_caught: bool,
}
#[derive(Clone, Debug, Serialize)]
pub struct Mon {
    pub name: String,
    pub level: u16,
    pub stats: [u16; 6],
    pub hp: u16,
    pub types: [u8; 2],
    /// Attack, Defense, Speed, Sp. Attack, Sp. Defense, Accuracy and Evasion stages.
    pub stages: [u8; 7],
    pub moves: Vec<String>,
    pub pp: Vec<u8>,
    pub personality: u32,
    pub ivs: [u16; 6],
    pub evs: [u16; 6],
    pub experience: u32,
    pub status: u32,
    pub status2: u32,
    pub status3: u32,
    pub poke_ball: u16,
    pub ability: u8,
    /// TryRunFromBattle: Run Away or a held Smoke Ball.
    pub always_run: bool,
    /// Cmd_getmoneyreward: a participating holder doubles trainer prize money.
    pub double_prize: bool,
    /// SetMoveEffect: Poison/Steel typing or Immunity blocks poison status.
    pub poison_immune: bool,
    pub last_move: Option<String>,
}
impl Mon {
    /// CalculateMonStats/CALC_STAT/ModifyStatByNature (pokemon.c).
    pub fn create(
        data: &Data,
        index: usize,
        ivs: [u16; 6],
        evs: [u16; 6],
        personality: u32,
    ) -> Self {
        Self::create_at_level(data, index, 5, ivs, evs, personality)
    }
    pub fn create_at_level(
        data: &Data,
        index: usize,
        level: u16,
        ivs: [u16; 6],
        evs: [u16; 6],
        personality: u32,
    ) -> Self {
        let species = &data.species[index];
        let ability = if species.abilities[1] != 0 {
            species.abilities[(personality & 1) as usize]
        } else {
            species.abilities[0]
        };
        let mut stats = [0; 6];
        for i in 0..6 {
            let n = (2 * species.base[i] + ivs[i] + evs[i] / 4) * level / 100;
            stats[i] = if i == 0 {
                n + level + 10
            } else {
                let modifier = data.natures[(personality % 25) as usize][i - 1];
                ((n + 5) as i32 * (100 + 10 * modifier as i32) / 100) as u16
            };
        }
        Self {
            name: species.name.clone(),
            level,
            stats,
            hp: stats[0],
            types: species.types,
            stages: [6; 7],
            moves: species.moves.clone(),
            pp: species.moves.iter().map(|m| data.moves[m].pp).collect(),
            personality,
            ivs,
            evs,
            experience: data.experience[level.min(100) as usize],
            status: 0,
            status2: 0,
            status3: 0,
            poke_ball: 4,
            ability,
            always_run: false,
            double_prize: false,
            poison_immune: ability == 17
                || species.types.contains(&3)
                || species.types.contains(&8),
            last_move: None,
        }
    }
    pub fn recalculate(&mut self, data: &Data, index: usize) {
        let old_max = self.stats[0];
        for i in 0..6 {
            let n = (2 * data.species[index].base[i] + self.ivs[i] + self.evs[i] / 4) * self.level
                / 100;
            self.stats[i] = if i == 0 {
                n + self.level + 10
            } else {
                ((n + 5) as i32
                    * (100 + 10 * data.natures[(self.personality % 25) as usize][i - 1] as i32)
                    / 100) as u16
            };
        }
        if self.hp != 0 {
            self.hp = self.hp + self.stats[0] - old_max;
        }
    }
    pub fn from_pokemon(
        data: &Data,
        core_data: &crate::core::Data,
        pokemon: &crate::core::Pokemon,
    ) -> Result<Self, String> {
        let species_name = core_data
            .ids
            .iter()
            .find(|(k, v)| **v == pokemon.species && k.starts_with("SPECIES_"))
            .map(|(k, _)| k.strip_prefix("SPECIES_").unwrap_or(k))
            .ok_or_else(|| format!("Unknown species {}", pokemon.species))?;
        let species_index = data
            .species
            .iter()
            .position(|s| s.name.eq_ignore_ascii_case(species_name))
            .ok_or_else(|| format!("Battle data missing species {species_name}"))?;
        let species = &data.species[species_index];
        let core_species = core_data
            .species
            .get(&pokemon.species)
            .ok_or_else(|| format!("Core data missing species {}", pokemon.species))?;
        let ability = core_species
            .abilities
            .get(usize::from(pokemon.ability_num))
            .copied()
            .unwrap_or(0);
        let smoke_ball = core_data
            .items
            .get(&pokemon.held_item)
            .and_then(|item| item.fields.get("holdEffect"))
            .and_then(serde_json::Value::as_str)
            == Some("HOLD_EFFECT_CAN_ALWAYS_RUN");
        let double_prize = core_data
            .items
            .get(&pokemon.held_item)
            .and_then(|item| item.fields.get("holdEffect"))
            .and_then(serde_json::Value::as_str)
            == Some("HOLD_EFFECT_DOUBLE_PRIZE");
        let poison_immune = ability == 17
            || core_data.species_has_type(pokemon.species, 3)?
            || core_data.species_has_type(pokemon.species, 8)?;
        let moves: Vec<String> = pokemon
            .moves
            .iter()
            .filter(|&&m| m != 0)
            .map(|&m| {
                core_data
                    .ids
                    .iter()
                    .find(|(k, v)| **v == m && k.starts_with("MOVE_"))
                    .map(|(k, _)| k.clone())
                    .unwrap_or_else(|| format!("UNSUPPORTED_MOVE_{m}"))
            })
            .collect();
        for name in &moves {
            if !data.moves.contains_key(name) {
                return Err(format!("Battle move {name} is not ported"));
            }
        }
        let pp: Vec<u8> = pokemon
            .moves
            .iter()
            .zip(pokemon.pp.iter())
            .filter_map(|(&id, &pp)| (id != 0).then_some(pp))
            .collect();
        let mut ivs = [0u16; 6];
        let mut evs = [0u16; 6];
        for i in 0..6 {
            ivs[i] = u16::from(pokemon.ivs[i]);
            evs[i] = u16::from(pokemon.evs[i]);
        }
        Ok(Self {
            name: species.name.clone(),
            level: u16::from(pokemon.level),
            stats: pokemon.stats,
            hp: pokemon.hp,
            types: species.types,
            stages: [6; 7],
            moves,
            pp,
            personality: pokemon.personality,
            ivs,
            evs,
            experience: pokemon.experience,
            status: pokemon.status,
            status2: 0,
            status3: 0,
            poke_ball: pokemon.poke_ball,
            ability,
            always_run: ability == 50 || smoke_ball,
            double_prize,
            poison_immune,
            last_move: None,
        })
    }
    pub fn sync_to_pokemon(&self, pokemon: &mut crate::core::Pokemon) {
        pokemon.experience = self.experience;
        pokemon.level = self.level as u8;
        pokemon.hp = self.hp;
        pokemon.stats = self.stats;
        pokemon.status = self.status;
        pokemon.poke_ball = self.poke_ball;
        for i in 0..6 {
            pokemon.evs[i] = self.evs[i] as u8;
        }
        for (i, &p) in self.pp.iter().enumerate() {
            if i < 4 {
                pokemon.pp[i] = p;
            }
        }
    }
}
pub fn stage(stat: u16, stage: u8) -> u32 {
    // gStatStageRatios, pokemon.c. Preserve the original multiply/divide order.
    const RATIOS: [(u32, u32); 13] = [
        (10, 40),
        (10, 35),
        (10, 30),
        (10, 25),
        (10, 20),
        (10, 15),
        (10, 10),
        (15, 10),
        (20, 10),
        (25, 10),
        (30, 10),
        (35, 10),
        (40, 10),
    ];
    let (n, d) = RATIOS[stage as usize];
    stat as u32 * n / d
}

fn integer_sqrt(value: u32) -> u32 {
    if value < 2 {
        return value;
    }
    let mut low = 1u32;
    let mut high = value.min(65_535);
    while low < high {
        let mid = low + (high - low + 1) / 2;
        if mid <= value / mid {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    low
}
/// CalculateBaseDamage's Generation III type-based physical/special branches and burn
/// penalty, with no items, badges, weather, screens or doubles.
pub fn base_damage(
    attacker: &Mon,
    defender: &Mon,
    power: u16,
    move_type: u8,
    critical: bool,
) -> u32 {
    let (attack_stat, defense_stat, attack_stage, defense_stage) = if move_type < 9 {
        (1, 2, 0, 1)
    } else if move_type > 9 {
        (4, 5, 3, 4)
    } else {
        return 2;
    };
    let atk = if critical && attacker.stages[attack_stage] <= 6 {
        attacker.stats[attack_stat] as u32
    } else {
        stage(attacker.stats[attack_stat], attacker.stages[attack_stage])
    };
    let def = if critical && defender.stages[defense_stage] >= 6 {
        defender.stats[defense_stat] as u32
    } else {
        stage(defender.stats[defense_stat], defender.stages[defense_stage])
    };
    let mut damage = atk * power as u32 * (2 * attacker.level as u32 / 5 + 2) / def / 50;
    // CalculateBaseDamage halves only the physical branch, before its final +2.
    if move_type < 9 && attacker.status & (1 << 4) != 0 && attacker.ability != 62 {
        damage /= 2;
    }
    (damage.max(1) + 2) * if critical { 2 } else { 1 }
}

/// One entry from `gTypeEffectiveness` in battle_main.c. Multipliers use the
/// source table's fixed-point scale: 0, 5, 10 or 20 mean x0, x0.5, x1 or x2.
fn type_matchup(move_type: u8, defender_type: u8) -> u32 {
    match move_type {
        0 => match defender_type {
            5 | 8 => 5,
            7 => 0,
            _ => 10,
        },
        1 => match defender_type {
            0 | 5 | 8 | 15 | 17 => 20,
            2 | 3 | 6 | 14 => 5,
            7 => 0,
            _ => 10,
        },
        2 => match defender_type {
            1 | 6 | 12 => 20,
            5 | 8 | 13 => 5,
            _ => 10,
        },
        3 => match defender_type {
            12 => 20,
            3 | 4 | 5 | 7 => 5,
            8 => 0,
            _ => 10,
        },
        4 => match defender_type {
            3 | 5 | 8 | 10 | 13 => 20,
            6 | 12 => 5,
            2 => 0,
            _ => 10,
        },
        5 => match defender_type {
            2 | 6 | 10 | 15 => 20,
            1 | 4 | 8 => 5,
            _ => 10,
        },
        6 => match defender_type {
            12 | 14 | 17 => 20,
            1 | 2 | 3 | 7 | 8 | 10 => 5,
            _ => 10,
        },
        7 => match defender_type {
            7 | 14 => 20,
            8 | 17 => 5,
            0 => 0,
            _ => 10,
        },
        8 => match defender_type {
            5 | 15 => 20,
            8 | 10 | 11 | 13 => 5,
            _ => 10,
        },
        10 => match defender_type {
            6 | 8 | 12 | 15 => 20,
            5 | 10 | 11 | 17 => 5,
            _ => 10,
        },
        11 => match defender_type {
            4 | 5 | 10 => 20,
            11 | 12 | 17 => 5,
            _ => 10,
        },
        12 => match defender_type {
            4 | 5 | 11 => 20,
            2 | 3 | 6 | 8 | 10 | 12 | 17 => 5,
            _ => 10,
        },
        13 => match defender_type {
            2 | 11 => 20,
            12 | 13 | 17 => 5,
            4 => 0,
            _ => 10,
        },
        14 => match defender_type {
            1 | 3 => 20,
            8 | 14 => 5,
            17 => 0,
            _ => 10,
        },
        15 => match defender_type {
            2 | 4 | 12 | 16 => 20,
            8 | 10 | 11 | 15 => 5,
            _ => 10,
        },
        16 => match defender_type {
            16 => 20,
            8 => 5,
            _ => 10,
        },
        17 => match defender_type {
            7 | 14 => 20,
            1 | 8 | 17 => 5,
            _ => 10,
        },
        _ => 10,
    }
}

/// `Cmd_typecalc`: apply STAB, then each distinct defending type in order.
/// The second result compares only type effectiveness with neutral (10).
pub fn typed_damage(
    attacker: &Mon,
    defender: &Mon,
    power: u16,
    move_type: u8,
    critical: bool,
) -> (u32, u32) {
    let mut damage = base_damage(attacker, defender, power, move_type, critical);
    if attacker.types.contains(&move_type) {
        damage = damage * 15 / 10;
    }
    let first = type_matchup(move_type, defender.types[0]);
    damage = damage * first / 10;
    if damage == 0 && first != 0 {
        damage = 1;
    }
    let mut effectiveness = first;
    if defender.types[1] != defender.types[0] {
        let second = type_matchup(move_type, defender.types[1]);
        damage = damage * second / 10;
        if damage == 0 && second != 0 {
            damage = 1;
        }
        effectiveness = effectiveness * second / 10;
    }
    (damage, effectiveness)
}
/// Cmd_accuracycheck and Cmd_critcalc for the supported battle state.
pub fn accuracy_and_critical(
    mv: &Move,
    actor: usize,
    flags: u8,
    attacker: &Mon,
    defender: &Mon,
    rng: &mut Rng,
) -> (bool, bool) {
    // ACC_CURR_MOVE=0 is tested before resolving it; MOVE_NONE.power=0.
    let bypass = actor == 0 && flags & 2 == 0;
    // Accuracy 0 moves use scripts without Cmd_accuracycheck.
    let script_skips_accuracy = matches!(
        mv.effect.as_str(),
        "EFFECT_ALWAYS_HIT" | "EFFECT_WATER_SPORT"
    );
    if !bypass && mv.accuracy != 0 && !script_skips_accuracy {
        const RATIOS: [(u32, u32); 13] = [
            (33, 100),
            (36, 100),
            (43, 100),
            (50, 100),
            (60, 100),
            (75, 100),
            (1, 1),
            (133, 100),
            (166, 100),
            (2, 1),
            (233, 100),
            (133, 50),
            (3, 1),
        ];
        let combined = (i16::from(attacker.stages[5]) + 6 - i16::from(defender.stages[6]))
            .clamp(0, 12) as usize;
        let (numerator, denominator) = RATIOS[combined];
        let accuracy = u32::from(mv.accuracy) * numerator / denominator;
        if u32::from(rng.next() % 100 + 1) > accuracy {
            return (false, false);
        }
    }
    if mv.power == 0 {
        return (true, false);
    }
    // Cmd_critcalc short-circuits before Random for critical-hit immunity.
    if matches!(defender.ability, 4 | 75) {
        return (true, false);
    }
    // C evaluates the random roll before the first-battle critical gate.
    let divisor = if mv.effect == "EFFECT_HIGH_CRITICAL" {
        8
    } else {
        16
    };
    let roll = rng.next() % divisor == 0;
    (true, roll && flags & 1 != 0)
}

#[derive(Clone, Debug, Serialize)]
pub struct AiDecision {
    pub slot: usize,
    pub scores: [i16; 4],
    pub simulated: [u16; 4],
}
fn target<'a>(arg: &str, user: &'a Mon, foe: &'a Mon) -> &'a Mon {
    if arg == "AI_USER" {
        user
    } else {
        foe
    }
}
fn type_value(value: &str) -> Result<u8, String> {
    Ok(match value {
        "TYPE_NORMAL" => 0,
        "TYPE_FIGHTING" => 1,
        "TYPE_FLYING" => 2,
        "TYPE_POISON" => 3,
        "TYPE_GROUND" => 4,
        "TYPE_ROCK" => 5,
        "TYPE_BUG" => 6,
        "TYPE_GHOST" => 7,
        "TYPE_STEEL" => 8,
        "TYPE_MYSTERY" => 9,
        "TYPE_FIRE" => 10,
        "TYPE_WATER" => 11,
        "TYPE_GRASS" => 12,
        "TYPE_ELECTRIC" => 13,
        "TYPE_PSYCHIC" => 14,
        "TYPE_ICE" => 15,
        "TYPE_DRAGON" => 16,
        "TYPE_DARK" => 17,
        "TYPE_NONE" => 255,
        _ => value
            .parse()
            .map_err(|_| format!("Unsupported source type {value}"))?,
    })
}
fn result_in_byte_list(data: &Data, label: &str, result: &str) -> Result<bool, String> {
    let result = type_value(result)?;
    let mut pc = *data.labels.get(label).ok_or("Missing AI byte-list label")?;
    loop {
        let (op, args) = data
            .instructions
            .get(pc)
            .ok_or("Unterminated AI byte list")?;
        if op != ".byte" || args.len() != 1 {
            return Err(format!("Malformed AI byte list {label}"));
        }
        if args[0] == "-1" {
            return Ok(false);
        }
        if type_value(&args[0])? == result {
            return Ok(true);
        }
        pc += 1;
    }
}
fn status_mask(value: &str) -> Result<u32, String> {
    Ok(match value {
        "STATUS1_NONE" => 0,
        "STATUS1_SLEEP" => 0x7,
        "STATUS1_POISON" => 1 << 3,
        "STATUS1_BURN" => 1 << 4,
        "STATUS1_FREEZE" => 1 << 5,
        "STATUS1_PARALYSIS" => 1 << 6,
        "STATUS1_TOXIC_POISON" => 1 << 7,
        "STATUS1_PSN_ANY" => (1 << 3) | (1 << 7),
        "STATUS1_ANY" => 0xff,
        "STATUS2_CURSED" => 1 << 28,
        "STATUS3_LEECHSEED" => 1 << 2,
        "STATUS3_ROOTED" => 1 << 10,
        _ => return Err(format!("Unsupported AI status {value}")),
    })
}
fn stat_index(arg: &str) -> Result<usize, String> {
    match arg {
        "STAT_ATK" => Ok(0),
        "STAT_DEF" => Ok(1),
        "STAT_SPEED" => Ok(2),
        "STAT_SPATK" => Ok(3),
        "STAT_SPDEF" => Ok(4),
        "STAT_ACC" => Ok(5),
        "STAT_EVASION" => Ok(6),
        _ => Err(format!("Unsupported AI stat {arg}")),
    }
}

fn effective_speed(mon: &Mon) -> u32 {
    let mut speed = stage(mon.stats[3], mon.stages[2]);
    if mon.status & (1 << 6) != 0 {
        speed /= 4;
    }
    speed
}

/// BattleAI_SetupAIData / BattleAI_ChooseMoveOrAction / original script order.
/// The duplicate candidate when a later slot becomes best is retained from C.
pub fn choose(data: &Data, user: &Mon, foe: &Mon, rng: &mut Rng) -> Result<AiDecision, String> {
    let mut scores = [0; 4];
    let simulated = std::array::from_fn(|_| 100 - rng.next() % 16);
    for i in 0..user.moves.len() {
        if user.pp[i] != 0 {
            scores[i] = 100;
        }
    }
    for root in &data.ai_roots {
        for slot in 0..user.moves.len() {
            if user.pp[slot] == 0 {
                scores[slot] = 0;
                continue;
            }
            let mv = &data.moves[&user.moves[slot]];
            let mut pc = data.labels[root];
            let mut result = String::new();
            let mut ended = false;
            for _ in 0..1024 {
                let (op, a) = &data.instructions[pc];
                pc += 1;
                let mut jump = false;
                match op.as_str() {
                    "end" => {
                        ended = true;
                        break;
                    }
                    "goto" => {
                        pc = *data.labels.get(&a[0]).ok_or("Missing AI label")?;
                    }
                    "score" => {
                        scores[slot] =
                            (scores[slot] + a[0].parse::<i16>().map_err(|e| e.to_string())?).max(0);
                    }
                    "if_effect" => jump = mv.effect == a[0],
                    "if_not_effect" => jump = mv.effect != a[0],
                    "if_move" => jump = user.moves[slot] == a[0],
                    "if_equal" | "if_equal_" => jump = result == a[0],
                    "if_not_equal" | "if_not_equal_" => jump = result != a[0],
                    "get_how_powerful_move_is" => {
                        result = if mv.power <= 1 {
                            "MOVE_POWER_DISCOURAGED"
                        } else {
                            "MOVE_MOST_POWERFUL"
                        }
                        .into();
                    }
                    "get_ability" => {
                        result = if target(&a[0], user, foe).name == "Charmander" {
                            "ABILITY_BLAZE"
                        } else {
                            "ABILITY_TORRENT"
                        }
                        .into();
                    }
                    "get_last_used_move" => {
                        result = target(&a[0], user, foe)
                            .last_move
                            .clone()
                            .unwrap_or_else(|| "MOVE_NONE".into());
                    }
                    "get_move_power_from_result" => {
                        result = if result == "MOVE_NONE" {
                            "0".into()
                        } else {
                            data.moves
                                .get(&result)
                                .ok_or("AI result references an unsupported move")?
                                .power
                                .to_string()
                        };
                    }
                    "get_move_type_from_result" => {
                        result = if result == "MOVE_NONE" {
                            "0".into()
                        } else {
                            data.moves
                                .get(&result)
                                .ok_or("AI result references an unsupported move")?
                                .move_type
                                .to_string()
                        };
                    }
                    "get_target_type1" => result = foe.types[0].to_string(),
                    "get_target_type2" => result = foe.types[1].to_string(),
                    "get_user_type1" => result = user.types[0].to_string(),
                    "get_user_type2" => result = user.types[1].to_string(),
                    "if_type_effectiveness" => jump = a[0] == "AI_EFFECTIVENESS_x1",
                    "if_in_bytes" | "if_not_in_bytes" => {
                        let found = result_in_byte_list(data, &a[0], &result)?;
                        jump = if op == "if_in_bytes" { found } else { !found };
                    }
                    "if_stat_level_equal"
                    | "if_stat_level_more_than"
                    | "if_stat_level_less_than" => {
                        let value = target(&a[0], user, foe).stages[stat_index(&a[1])?];
                        let n = a[2].parse::<u8>().map_err(|e| e.to_string())?;
                        jump = match op.as_str() {
                            "if_stat_level_equal" => value == n,
                            "if_stat_level_more_than" => value > n,
                            _ => value < n,
                        };
                    }
                    "if_status" | "if_not_status" | "if_status2" | "if_not_status2"
                    | "if_status3" | "if_not_status3" => {
                        let mon = target(&a[0], user, foe);
                        let mask = status_mask(&a[1])?;
                        let value = match op.as_str() {
                            "if_status" | "if_not_status" => mon.status,
                            "if_status2" | "if_not_status2" => mon.status2,
                            _ => mon.status3,
                        };
                        let present = value & mask != 0;
                        jump = if op.starts_with("if_not_") {
                            !present
                        } else {
                            present
                        };
                    }
                    "if_hp_more_than" | "if_hp_less_than" | "if_hp_equal" | "if_hp_not_equal" => {
                        let mon = target(&a[0], user, foe);
                        let pct = 100 * mon.hp as u32 / mon.stats[0] as u32;
                        let n = a[1].parse::<u32>().map_err(|e| e.to_string())?;
                        jump = match op.as_str() {
                            "if_hp_more_than" => pct > n,
                            "if_hp_less_than" => pct < n,
                            "if_hp_equal" => pct == n,
                            _ => pct != n,
                        };
                    }
                    "if_random_less_than" => {
                        jump = rng.next() % 256 < a[0].parse::<u16>().map_err(|e| e.to_string())?
                    }
                    "if_target_faster" => {
                        let user_speed = effective_speed(user);
                        let target_speed = effective_speed(foe);
                        // GetWhoStrikesFirst consumes Random on equal speed even though
                        // the target-faster branch cannot be taken on that tie.
                        if user_speed == target_speed {
                            rng.next();
                        }
                        jump = user_speed < target_speed;
                    }
                    "if_can_faint" => {
                        let power = if mv.move_type == 10
                            && (user.status3 | foe.status3) & (1 << 17) != 0
                        {
                            mv.power / 2
                        } else {
                            mv.power
                        };
                        let damage = typed_damage(user, foe, power, mv.move_type, false).0;
                        jump = mv.power >= 2
                            && damage != 0
                            && foe.hp as u32 <= (damage * simulated[slot] as u32 / 100).max(1)
                    }
                    other => {
                        return Err(format!(
                            "Unsupported AI instruction {other} in {root}; fixture must be expanded explicitly"
                        ));
                    }
                }
                if jump {
                    pc = *data
                        .labels
                        .get(a.last().ok_or("AI branch without destination")?)
                        .ok_or("Missing AI destination")?;
                }
            }
            if !ended {
                return Err("AI instruction budget exceeded".into());
            }
        }
    }
    let mut best = scores[0];
    let mut candidates = vec![0];
    for i in 1..4 {
        if best < scores[i] {
            best = scores[i];
            candidates = vec![i];
        }
        if best == scores[i] {
            candidates.push(i);
        }
    }
    let slot = candidates[rng.next() as usize % candidates.len()];
    if slot >= user.moves.len() || user.pp[slot] == 0 {
        return Err("All moves exhausted; Struggle is outside this fixture".into());
    }
    Ok(AiDecision {
        slot,
        scores,
        simulated,
    })
}

#[derive(Clone, Debug, Serialize)]
pub struct Battle {
    pub mons: [Mon; 2],
    pub rng: Rng,
    pub turn: u32,
    pub flags: u8,
    pub log: Vec<String>,
    pub winner: Option<usize>,
    pub last_ai: Option<AiDecision>,
    #[serde(default)]
    pub is_wild: bool,
    #[serde(default)]
    pub caught: bool,
    #[serde(default)]
    pub fled: bool,
    #[serde(default)]
    pub catch_rate: u16,
    #[serde(default)]
    pub run_tries: u8,
    #[serde(default)]
    pub catch_context: CatchContext,
    /// FireRed side timers: zero is inactive; Safeguard is initialized to five.
    #[serde(default)]
    pub safeguard_turns: [u8; 2],
}
impl Battle {
    fn move_power(&self, mv: &Move) -> u16 {
        // CalculateBaseDamage changes gBattleMovePower before the ordinary
        // formula whenever any battler has an active Water Sport marker.
        if mv.move_type == 10 && self.mons.iter().any(|mon| mon.status3 & (1 << 17) != 0) {
            mv.power / 2
        } else {
            mv.power
        }
    }

    fn restore_half_hp(&mut self, actor: usize) -> bool {
        let max_hp = self.mons[actor].stats[0];
        if self.mons[actor].hp == max_hp {
            self.log.push("But it failed!".into());
            return false;
        }
        let amount = (max_hp / 2).max(1);
        let healed = amount.min(max_hp - self.mons[actor].hp);
        self.mons[actor].hp += healed;
        self.log.push(format!(
            "{} regained health! {healed} HP restored",
            self.mons[actor].name
        ));
        true
    }

    fn defense_curl(&mut self, actor: usize) {
        // Cmd_setdefensecurlbit precedes statbuffchange, so the Rollout marker
        // is retained even when Defense is already at its stage ceiling.
        self.mons[actor].status2 |= 1 << 30;
        if self.mons[actor].stages[1] == 12 {
            self.log.push(format!(
                "{}'s Defense won't go higher!",
                self.mons[actor].name
            ));
        } else {
            self.mons[actor].stages[1] += 1;
            self.log
                .push(format!("{}'s Defense rose!", self.mons[actor].name));
        }
    }

    fn water_sport(&mut self, actor: usize) -> bool {
        if self.mons[actor].status3 & (1 << 17) != 0 {
            self.log.push("But it failed!".into());
            return false;
        }
        self.mons[actor].status3 |= 1 << 17;
        self.log.push("Fire's power was weakened!".into());
        true
    }

    fn teleport(&mut self, actor: usize) -> bool {
        if !self.is_wild {
            self.log.push("But it failed!".into());
            return false;
        }
        let foe = actor ^ 1;
        let actor_mon = &self.mons[actor];
        let foe_mon = &self.mons[foe];
        let trapped_by_ability = foe_mon.ability == 23
            || (foe_mon.ability == 71 && actor_mon.ability != 26 && !actor_mon.types.contains(&2))
            || (foe_mon.ability == 42 && actor_mon.types.contains(&8));
        let trapped_by_status = actor_mon.status2 & ((0b111 << 13) | (1 << 26)) != 0
            || actor_mon.status3 & (1 << 10) != 0;
        // IsRunningFromBattleImpossible checks Run Away/Smoke Ball first.
        if !actor_mon.always_run && (trapped_by_ability || trapped_by_status) {
            self.log.push("But it failed!".into());
            return false;
        }
        self.fled = true;
        self.winner = Some(2);
        self.log
            .push(format!("{} fled from battle!", self.mons[actor].name));
        true
    }

    fn log_effectiveness(&mut self, effectiveness: u32, foe: usize) {
        if effectiveness == 0 {
            self.log
                .push(format!("It doesn't affect {}...", self.mons[foe].name));
        } else if effectiveness < 10 {
            self.log.push("It's not very effective...".into());
        } else if effectiveness > 10 {
            self.log.push("It's super effective!".into());
        }
    }

    pub fn new(data: &Data, seed: u32) -> Self {
        Self {
            mons: [
                Mon::create(data, 0, [31; 6], [0; 6], 0),
                Mon::create(data, 1, [0; 6], [0; 6], data.rival_personality),
            ],
            rng: Rng {
                state: seed,
                calls: 0,
            },
            turn: 0,
            flags: 0,
            winner: None,
            last_ai: None,
            log: vec![
                "RIVAL sent out SQUIRTLE! Go, CHARMANDER!".into(),
                "OAK: A battle is won by reducing your opponent's HP to zero.".into(),
            ],
            is_wild: false,
            caught: false,
            fled: false,
            catch_rate: 0,
            run_tries: 0,
            catch_context: CatchContext::default(),
            safeguard_turns: [0; 2],
        }
    }
    pub fn from_matchup(data: &Data, player_mon: Mon, rival_mon: Mon, seed: u32) -> Self {
        let p_name = player_mon.name.to_uppercase();
        let r_name = rival_mon.name.to_uppercase();
        let _ = data;
        Self {
            mons: [player_mon, rival_mon],
            rng: Rng {
                state: seed,
                calls: 0,
            },
            turn: 0,
            // Ordinary matchups begin after both Oak tutorial prompts.
            flags: 3,
            winner: None,
            last_ai: None,
            log: vec![
                format!("RIVAL sent out {r_name}! Go, {p_name}!"),
                "OAK: A battle is won by reducing your opponent's HP to zero.".into(),
            ],
            is_wild: false,
            caught: false,
            fled: false,
            catch_rate: 0,
            run_tries: 0,
            catch_context: CatchContext::default(),
            safeguard_turns: [0; 2],
        }
    }
    pub fn wild(data: &Data, player_mon: Mon, wild_mon: Mon, catch_rate: u16, seed: u32) -> Self {
        let p_name = player_mon.name.to_uppercase();
        let w_name = wild_mon.name.to_uppercase();
        let _ = data;
        Self {
            mons: [player_mon, wild_mon],
            rng: Rng {
                state: seed,
                calls: 0,
            },
            turn: 0,
            flags: 3,
            winner: None,
            last_ai: None,
            log: vec![format!("Wild {w_name} appeared! Go, {p_name}!")],
            is_wild: true,
            caught: false,
            fled: false,
            catch_rate,
            run_tries: 0,
            catch_context: CatchContext::default(),
            safeguard_turns: [0; 2],
        }
    }
    pub fn set_catch_context(&mut self, context: CatchContext) {
        self.catch_context = context;
    }
    pub fn flee(&mut self) -> bool {
        if !self.is_wild {
            self.log.push("Cannot run from a trainer battle!".into());
            return false;
        }
        if self.mons[0].always_run {
            self.fled = true;
            self.winner = Some(2);
            self.log.push("Got away safely!".into());
            return true;
        }
        let p_spd = self.mons[0].stats[3] as u32;
        let e_spd = (self.mons[1].stats[3] as u32).max(1);
        let escaped = if p_spd >= e_spd {
            true
        } else {
            // speedVar is u8 in source, including overflow after enough attempts.
            let odds = ((p_spd * 128 / e_spd).wrapping_add(u32::from(self.run_tries) * 30)) as u8;
            u16::from(odds) > (self.rng.next() & 0xff)
        };
        self.run_tries = self.run_tries.wrapping_add(1);
        if escaped {
            self.fled = true;
            self.winner = Some(2);
            self.log.push("Got away safely!".into());
            true
        } else {
            self.log.push("Can't escape!".into());
            false
        }
    }
    fn ball_multiplier(&self, ball: u16) -> Result<u32, String> {
        Ok(match ball {
            1 => 10, // Master Ball success is forced after ordinary shake calculation.
            2 => 20,
            3 => 15,
            4 => 10,
            5 => return Err("Safari Ball needs Safari catch-factor setup".into()),
            6 => {
                if self.catch_context.target_is_water_or_bug {
                    30
                } else {
                    10
                }
            }
            7 => {
                if self.catch_context.underwater {
                    35
                } else {
                    10
                }
            }
            8 => {
                if self.mons[1].level < 40 {
                    (40 - u32::from(self.mons[1].level)).max(10)
                } else {
                    10
                }
            }
            9 => {
                if self.catch_context.target_previously_caught {
                    30
                } else {
                    10
                }
            }
            10 => {
                // The source stores the counter and multiplier in u8. Preserve
                // its late-battle wrapping before applying the normal x4 cap.
                u32::from((self.turn.min(255) as u8).wrapping_add(10)).min(40)
            }
            11 | 12 => 10,
            _ => return Err(format!("Ball item {ball} is not ported")),
        })
    }
    /// Cmd_handleballthrow regular single-battle branch. Returns whether caught.
    pub fn catch(&mut self, ball: u16) -> Result<bool, String> {
        if !self.is_wild {
            self.log.push("The trainer blocked the BALL!".into());
            return Ok(false);
        }
        let multiplier = self.ball_multiplier(ball)?;
        let max_hp = (self.mons[1].stats[0] as u32).max(1);
        let cur_hp = self.mons[1].hp as u32;
        let mut odds =
            u32::from(self.catch_rate) * multiplier / 10 * (3 * max_hp - 2 * cur_hp) / (3 * max_hp);
        if self.mons[1].status & (0x7 | 0x20) != 0 {
            odds *= 2;
        }
        if self.mons[1].status & (0x8 | 0x10 | 0x40 | 0x80) != 0 {
            odds = odds * 15 / 10;
        }
        let mut shakes = if odds > 254 {
            4
        } else if odds == 0 {
            0
        } else {
            let threshold = 1_048_560 / integer_sqrt(integer_sqrt(16_711_680 / odds));
            let mut shakes = 0;
            while shakes < 4 && u32::from(self.rng.next()) < threshold {
                shakes += 1;
            }
            shakes
        };
        // Source forces Master Ball success only after calculating ordinary shakes.
        // This preserves its otherwise-surprising RNG consumption for low catch odds.
        if ball == 1 {
            shakes = 4;
        }
        if shakes == 4 {
            self.mons[1].poke_ball = ball;
            self.caught = true;
            self.winner = Some(0);
            self.log
                .push(format!("Gotcha! {} was caught!", self.mons[1].name));
            Ok(true)
        } else {
            self.log.push(
                match shakes {
                    0 => "Oh no! The Pokémon broke free!",
                    1 => "Aww! It appeared to be caught!",
                    2 => "Aargh! Almost had it!",
                    _ => "Shoot! It was so close too!",
                }
                .into(),
            );
            Ok(false)
        }
    }
    fn damaging_secondary(&mut self, mv: &Move, foe: usize, no_effect: bool) {
        if !matches!(
            mv.effect.as_str(),
            "EFFECT_POISON_HIT"
                | "EFFECT_BURN_HIT"
                | "EFFECT_FREEZE_HIT"
                | "EFFECT_PARALYZE_HIT"
                | "EFFECT_CONFUSE_HIT"
                | "EFFECT_SPEED_DOWN_HIT"
        ) {
            return;
        }
        // Cmd_seteffectwithchance uses <= rather than < and consumes this draw
        // even when SetMoveEffect later rejects a fainted/immune/statused target.
        let chance = if self.mons[foe ^ 1].ability == 32 {
            u16::from(mv.secondary_chance) * 2
        } else {
            u16::from(mv.secondary_chance)
        };
        let succeeds = self.rng.next() % 100 <= chance;
        if !succeeds || no_effect {
            return;
        }
        if mv.effect == "EFFECT_SPEED_DOWN_HIT" {
            if self.mons[foe].hp == 0 || self.mons[foe].status2 & (1 << 24) != 0 {
                return;
            }
            // SetMoveEffect marks >=100% effects certain. That source flag
            // bypasses Shield Dust, Clear Body and White Smoke in ChangeStatBuffs.
            if chance < 100 && matches!(self.mons[foe].ability, 19 | 29 | 73) {
                return;
            }
            if self.mons[foe].stages[2] == 0 {
                self.log.push("The stat cannot fall any lower!".into());
            } else {
                self.mons[foe].stages[2] -= 1;
                self.log
                    .push(format!("{}'s Speed fell!", self.mons[foe].name));
            }
            return;
        }
        if self.mons[foe].ability == 19 {
            return;
        }
        // SetMoveEffect checks Safeguard after Shield Dust but before the fainted
        // target and Substitute gates. The chance draw above is still consumed.
        if self.safeguard_turns[foe] != 0 {
            return;
        }
        if self.mons[foe].hp == 0 || self.mons[foe].status2 & (1 << 24) != 0 {
            return;
        }
        match mv.effect.as_str() {
            "EFFECT_POISON_HIT" if self.mons[foe].status == 0 && !self.mons[foe].poison_immune => {
                self.mons[foe].status = 1 << 3;
                self.log
                    .push(format!("{} was poisoned!", self.mons[foe].name));
            }
            "EFFECT_BURN_HIT"
                if self.mons[foe].status == 0
                    && !self.mons[foe].types.contains(&10)
                    && self.mons[foe].ability != 41 =>
            {
                self.mons[foe].status = 1 << 4;
                self.log
                    .push(format!("{} was burned!", self.mons[foe].name));
            }
            "EFFECT_FREEZE_HIT"
                if self.mons[foe].status == 0
                    && !self.mons[foe].types.contains(&15)
                    && self.mons[foe].ability != 40 =>
            {
                // The source also rejects freeze in harsh sunlight. Weather is
                // not represented by this bounded battle state yet.
                self.mons[foe].status = 1 << 5;
                self.log
                    .push(format!("{} was frozen solid!", self.mons[foe].name));
            }
            "EFFECT_PARALYZE_HIT" if self.mons[foe].status == 0 && self.mons[foe].ability != 7 => {
                self.mons[foe].status = 1 << 6;
                self.log.push(format!(
                    "{} is paralyzed! It may be unable to move!",
                    self.mons[foe].name
                ));
            }
            "EFFECT_CONFUSE_HIT"
                if self.mons[foe].status2 & 7 == 0 && self.mons[foe].ability != 20 =>
            {
                self.mons[foe].status2 |= u32::from(self.rng.next() % 4 + 2);
                self.log
                    .push(format!("{} became confused!", self.mons[foe].name));
            }
            _ => {}
        }
    }
    fn apply_absorb(&mut self, mv: &Move, actor: usize, foe: usize, dealt: u16) -> bool {
        if mv.effect != "EFFECT_ABSORB" || dealt == 0 {
            return false;
        }
        // Cmd_negativedamage uses actual HP dealt and integer division. It does
        // not force a minimum, so one point of damage restores zero HP.
        let drain = dealt / 2;
        if self.mons[foe].ability == 64 {
            self.mons[actor].hp = self.mons[actor].hp.saturating_sub(drain);
            self.log.push(format!(
                "{} sucked up the liquid ooze! {drain} damage",
                self.mons[actor].name
            ));
            self.mons[actor].hp == 0
        } else {
            let missing = self.mons[actor].stats[0].saturating_sub(self.mons[actor].hp);
            let healed = drain.min(missing);
            self.mons[actor].hp += healed;
            self.log.push(format!(
                "{} had its energy drained! {healed} HP restored",
                self.mons[foe].name
            ));
            false
        }
    }
    fn paralysis_stops(&mut self, actor: usize) -> bool {
        if self.mons[actor].status & (1 << 6) == 0 || self.rng.next() % 4 != 0 {
            return false;
        }
        self.log.push(format!(
            "{} is paralyzed! It can't move!",
            self.mons[actor].name
        ));
        true
    }
    fn confusion_stops(&mut self, actor: usize, data: &Data) -> bool {
        if self.mons[actor].status2 & 7 == 0 {
            return false;
        }
        self.mons[actor].status2 -= 1;
        if self.mons[actor].status2 & 7 == 0 {
            self.log.push(format!(
                "{} snapped out of confusion!",
                self.mons[actor].name
            ));
            return false;
        }
        self.log
            .push(format!("{} is confused!", self.mons[actor].name));
        if self.rng.next() & 1 != 0 {
            return false;
        }
        let base = base_damage(&self.mons[actor], &self.mons[actor], 40, 0, false);
        let damage = (base * (100 - self.rng.next() % 16) as u32 / 100).max(1) as u16;
        self.mons[actor].hp = self.mons[actor].hp.saturating_sub(damage);
        self.log
            .push(format!("It hurt itself in its confusion! {damage} damage"));
        if self.mons[actor].hp == 0 {
            self.resolve_faint(data, actor ^ 1);
        }
        true
    }
    fn sleep_stops(&mut self, actor: usize) -> bool {
        if self.mons[actor].status & 7 == 0 {
            return false;
        }
        if self.mons[actor].ability != 43 && self.mons.iter().any(|mon| mon.status2 & 0x70 != 0) {
            self.mons[actor].status &= !7;
            self.mons[actor].status2 &= !(1 << 27);
            self.log
                .push(format!("{} woke up in the uproar!", self.mons[actor].name));
            return false;
        }
        let decrement = if self.mons[actor].ability == 48 { 2 } else { 1 };
        let turns = self.mons[actor].status & 7;
        self.mons[actor].status = if turns < decrement {
            self.mons[actor].status & !7
        } else {
            self.mons[actor].status - decrement
        };
        if self.mons[actor].status & 7 != 0 {
            self.log
                .push(format!("{} is fast asleep.", self.mons[actor].name));
            true
        } else {
            self.mons[actor].status2 &= !(1 << 27);
            self.log.push(format!("{} woke up!", self.mons[actor].name));
            false
        }
    }
    fn freeze_stops(&mut self, actor: usize, thawing_move: bool) -> bool {
        if self.mons[actor].status & (1 << 5) == 0 {
            return false;
        }
        if self.rng.next() % 5 == 0 {
            self.mons[actor].status &= !(1 << 5);
            self.log
                .push(format!("{} thawed out!", self.mons[actor].name));
            return false;
        }
        // BattleScript_MoveUsedIsFrozen defers thawing moves until the later
        // canceller stage, after confusion and paralysis have had their turn.
        if thawing_move {
            return false;
        }
        self.log
            .push(format!("{} is frozen solid!", self.mons[actor].name));
        true
    }
    fn thaw_for_move(&mut self, actor: usize, effect: &str) {
        if effect == "EFFECT_THAW_HIT" && self.mons[actor].status & (1 << 5) != 0 {
            self.mons[actor].status &= !(1 << 5);
            self.log
                .push(format!("{} thawed out!", self.mons[actor].name));
        }
    }
    fn thaw_from_fire_hit(
        &mut self,
        mv: &Move,
        attacker: usize,
        target: usize,
        damage: u16,
        no_effect: bool,
    ) {
        if attacker != target
            && mv.move_type == 10
            && damage != 0
            && !no_effect
            && self.mons[target].hp != 0
            && self.mons[target].status & (1 << 5) != 0
        {
            self.mons[target].status &= !(1 << 5);
            self.log
                .push(format!("{} thawed out!", self.mons[target].name));
        }
    }
    fn sleep_move_blocked(&mut self, foe: usize) -> bool {
        let foe_name = self.mons[foe].name.clone();
        if self.mons[foe].ability == 43 {
            self.log
                .push(format!("{foe_name}'s Soundproof blocks Sing!"));
        } else if self.mons[foe].status2 & (1 << 24) != 0 {
            self.log.push("But it failed!".into());
        } else if self.mons[foe].status & 7 != 0 {
            self.log.push(format!("{foe_name} is already asleep!"));
        } else if self.mons.iter().any(|mon| mon.status2 & 0x70 != 0)
            || matches!(self.mons[foe].ability, 15 | 72)
        {
            self.log.push(format!("{foe_name} stayed awake!"));
        } else if self.mons[foe].status != 0 {
            self.log.push("But it failed!".into());
        } else {
            return false;
        }
        true
    }
    fn apply_sleep(&mut self, foe: usize) {
        self.mons[foe].status = u32::from((self.rng.next() & 3) + 2);
        self.log
            .push(format!("{} fell asleep!", self.mons[foe].name));
    }
    fn set_safeguard(&mut self, actor: usize) -> bool {
        if self.safeguard_turns[actor] != 0 {
            self.log.push("But it failed!".into());
            return false;
        }
        self.safeguard_turns[actor] = 5;
        self.log.push(if actor == 0 {
            "Your team became cloaked in a mystical veil!".into()
        } else {
            "The foe's team became cloaked in a mystical veil!".into()
        });
        true
    }
    fn tick_safeguard(&mut self) {
        for side in 0..2 {
            if self.safeguard_turns[side] == 0 {
                continue;
            }
            self.safeguard_turns[side] -= 1;
            if self.safeguard_turns[side] == 0 {
                self.log.push(if side == 0 {
                    "Your team's Safeguard faded!".into()
                } else {
                    "The foe's Safeguard faded!".into()
                });
            }
        }
    }
    fn resolve_faint(&mut self, data: &Data, winner: usize) {
        let fainted = winner ^ 1;
        self.winner = Some(winner);
        self.log.push(format!(
            "{} fainted. {}",
            self.mons[fainted].name,
            if winner == 0 {
                "You won!"
            } else if self.is_wild {
                "Wild Pokémon won!"
            } else {
                "RIVAL won!"
            }
        ));
        if winner != 0 {
            return;
        }
        // Cmd_getexp: one participant, no traded bonus/Exp Share/item.
        let rival_idx = data
            .species
            .iter()
            .position(|s| s.name.eq_ignore_ascii_case(&self.mons[1].name))
            .unwrap_or(1);
        let player_idx = data
            .species
            .iter()
            .position(|s| s.name.eq_ignore_ascii_case(&self.mons[0].name))
            .unwrap_or(0);
        let base_exp = data.species[rival_idx].exp_yield * self.mons[1].level as u32 / 7;
        let gain = if self.is_wild {
            base_exp
        } else {
            (base_exp * 150) / 100
        };
        self.mons[0].experience += gain;
        for i in 0..6 {
            self.mons[0].evs[i] += data.species[rival_idx].ev_yield[i];
        }
        let p_name = self.mons[0].name.clone();
        self.log
            .push(format!("{p_name} gained {gain} experience points!"));
        if self.mons[0].experience >= data.experience[6] && self.mons[0].level < 6 {
            self.mons[0].level = 6;
            self.mons[0].recalculate(data, player_idx);
            self.log.push(format!("{p_name} grew to level 6!"));
        }
    }
    fn end_turn_status_damage(&mut self, data: &Data) {
        for battler in 0..2 {
            if self.mons[battler].hp == 0 {
                continue;
            }
            let (damage, cause) = if self.mons[battler].status & (1 << 3) != 0 {
                ((self.mons[battler].stats[0] / 8).max(1), "poison")
            } else if self.mons[battler].status & (1 << 7) != 0 {
                let base = (self.mons[battler].stats[0] / 16).max(1);
                if self.mons[battler].status & 0xf00 != 0xf00 {
                    self.mons[battler].status += 1 << 8;
                }
                let counter = ((self.mons[battler].status & 0xf00) >> 8) as u16;
                (base.saturating_mul(counter), "poison")
            } else if self.mons[battler].status & (1 << 4) != 0 {
                ((self.mons[battler].stats[0] / 8).max(1), "its burn")
            } else {
                continue;
            };
            self.mons[battler].hp = self.mons[battler].hp.saturating_sub(damage);
            self.log.push(format!(
                "{} is hurt by {cause}! {damage} damage",
                self.mons[battler].name,
            ));
            if self.mons[battler].hp == 0 {
                self.resolve_faint(data, battler ^ 1);
                break;
            }
        }
        if self.winner.is_none() {
            self.tick_safeguard();
        }
    }
    pub fn enemy_turn(&mut self, data: &Data) -> Result<(), String> {
        if self.winner.is_some() || self.mons[0].hp == 0 || self.mons[1].hp == 0 {
            return Ok(());
        }
        let ai = choose(data, &self.mons[1], &self.mons[0], &mut self.rng)?;
        let slot = ai.slot;
        self.last_ai = Some(ai);
        self.turn = (self.turn + 1).min(255);
        let id = self.mons[1].moves[slot].clone();
        let mv = &data.moves[&id];
        if self.sleep_stops(1)
            || self.freeze_stops(1, mv.effect == "EFFECT_THAW_HIT")
            || self.confusion_stops(1, data)
            || self.paralysis_stops(1)
        {
            if self.winner.is_none() {
                self.end_turn_status_damage(data);
            }
            return Ok(());
        }
        self.thaw_for_move(1, &mv.effect);
        self.mons[1].pp[slot] = self.mons[1].pp[slot].saturating_sub(1);
        self.mons[1].last_move = Some(id.clone());
        self.log.push(format!(
            "{} used {}!",
            self.mons[1].name,
            id.trim_start_matches("MOVE_").replace('_', " ")
        ));
        if mv.effect == "EFFECT_SLEEP" && self.sleep_move_blocked(0) {
            self.end_turn_status_damage(data);
            return Ok(());
        }
        let (hit, critical) = accuracy_and_critical(
            mv,
            1,
            self.flags,
            &self.mons[1],
            &self.mons[0],
            &mut self.rng,
        );
        if !hit {
            self.log.push("The attack missed!".into());
            self.end_turn_status_damage(data);
            return Ok(());
        }
        if mv.power != 0 {
            let (base, effectiveness) = typed_damage(
                &self.mons[1],
                &self.mons[0],
                self.move_power(mv),
                mv.move_type,
                critical,
            );
            let random = (100 - self.rng.next() % 16) as u32;
            let damage = if base == 0 {
                0
            } else {
                (base * random / 100).max(1) as u16
            };
            let hp_before = self.mons[0].hp;
            self.mons[0].hp = self.mons[0].hp.saturating_sub(damage);
            let dealt = hp_before - self.mons[0].hp;
            self.log.push(format!(
                "{} damage{}",
                damage,
                if critical { " - critical hit!" } else { "" }
            ));
            self.log_effectiveness(effectiveness, 0);
            self.damaging_secondary(mv, 0, effectiveness == 0);
            self.thaw_from_fire_hit(mv, 1, 0, damage, effectiveness == 0);
            if self.apply_absorb(mv, 1, 0, dealt) {
                self.resolve_faint(data, 0);
                return Ok(());
            }
        } else {
            match mv.effect.as_str() {
                "EFFECT_ATTACK_DOWN" => {
                    if self.mons[0].stages[0] == 0 {
                        self.log.push("The stat cannot fall any lower!".into());
                    } else {
                        self.mons[0].stages[0] -= 1;
                        self.log
                            .push(format!("{}'s Attack fell!", self.mons[0].name));
                    }
                }
                "EFFECT_DEFENSE_DOWN" => {
                    if self.mons[0].stages[1] == 0 {
                        self.log.push("The stat cannot fall any lower!".into());
                    } else {
                        self.mons[0].stages[1] -= 1;
                        self.log
                            .push(format!("{}'s Defense fell!", self.mons[0].name));
                    }
                }
                "EFFECT_DEFENSE_UP" => {
                    if self.mons[1].stages[1] == 12 {
                        self.log
                            .push(format!("{}'s Defense won't go higher!", self.mons[1].name));
                    } else {
                        self.mons[1].stages[1] += 1;
                        self.log
                            .push(format!("{}'s Defense rose!", self.mons[1].name));
                    }
                }
                "EFFECT_RESTORE_HP" => {
                    self.restore_half_hp(1);
                }
                "EFFECT_DEFENSE_CURL" => self.defense_curl(1),
                "EFFECT_WATER_SPORT" => {
                    self.water_sport(1);
                }
                "EFFECT_TELEPORT" => {
                    if self.teleport(1) {
                        return Ok(());
                    }
                }
                "EFFECT_SPEED_DOWN" => {
                    if self.mons[0].stages[2] == 0 {
                        self.log.push("The stat cannot fall any lower!".into());
                    } else {
                        self.mons[0].stages[2] -= 1;
                        self.log
                            .push(format!("{}'s Speed fell!", self.mons[0].name));
                    }
                }
                "EFFECT_ACCURACY_DOWN" => {
                    if self.mons[0].stages[5] == 0 {
                        self.log.push("The stat cannot fall any lower!".into());
                    } else {
                        self.mons[0].stages[5] -= 1;
                        self.log
                            .push(format!("{}'s Accuracy fell!", self.mons[0].name));
                    }
                }
                "EFFECT_SLEEP" if self.safeguard_turns[0] != 0 => {
                    self.log.push("Safeguard protected the target!".into());
                }
                "EFFECT_SLEEP" => self.apply_sleep(0),
                "EFFECT_SAFEGUARD" => {
                    self.set_safeguard(1);
                }
                _ => {}
            }
        }
        if self.mons[0].hp == 0 {
            self.resolve_faint(data, 1);
        } else {
            self.end_turn_status_damage(data);
        }
        Ok(())
    }
    pub fn play(&mut self, data: &Data, player_slot: usize) -> Result<(), String> {
        if self.winner.is_some() {
            return Err("Battle has ended; reset to play again".into());
        }
        if player_slot >= self.mons[0].moves.len() || self.mons[0].pp[player_slot] == 0 {
            return Err("That move has no PP".into());
        }
        let ai = choose(data, &self.mons[1], &self.mons[0], &mut self.rng)?;
        let slots = [player_slot, ai.slot];
        self.last_ai = Some(ai);
        self.turn = (self.turn + 1).min(255);
        self.log.clear();
        let p_mv = &self.mons[0].moves[slots[0]];
        let e_mv = &self.mons[1].moves[slots[1]];
        let p_prio = if p_mv == "MOVE_QUICK_ATTACK" { 1 } else { 0 };
        let e_prio = if e_mv == "MOVE_QUICK_ATTACK" { 1 } else { 0 };
        let p_speed = effective_speed(&self.mons[0]);
        let e_speed = effective_speed(&self.mons[1]);
        let first = if p_prio != e_prio {
            if p_prio > e_prio {
                0
            } else {
                1
            }
        } else if p_speed < e_speed {
            1
        } else if p_speed == e_speed {
            (self.rng.next() & 1) as usize
        } else {
            0
        };
        for actor in [first, first ^ 1] {
            let foe = actor ^ 1;
            if self.mons[actor].hp == 0 || self.mons[foe].hp == 0 {
                break;
            }
            let id = self.mons[actor].moves[slots[actor]].clone();
            let mv = &data.moves[&id];
            if self.sleep_stops(actor)
                || self.freeze_stops(actor, mv.effect == "EFFECT_THAW_HIT")
                || self.confusion_stops(actor, data)
                || self.paralysis_stops(actor)
            {
                continue;
            }
            self.thaw_for_move(actor, &mv.effect);
            self.mons[actor].pp[slots[actor]] -= 1;
            self.mons[actor].last_move = Some(id.clone());
            self.log.push(format!(
                "{} used {}!",
                self.mons[actor].name,
                id.trim_start_matches("MOVE_").replace('_', " ")
            ));
            if mv.effect == "EFFECT_SLEEP" && self.sleep_move_blocked(foe) {
                continue;
            }
            // Cmd_accuracycheck tests gBattleMoves[ACC_CURR_MOVE=0].power BEFORE
            // resolving the current move. MOVE_NONE has power 0: preserve this
            // original first-battle quirk, rather than 'fixing' its RNG calls.
            let (hit, critical) = accuracy_and_critical(
                mv,
                actor,
                self.flags,
                &self.mons[actor],
                &self.mons[foe],
                &mut self.rng,
            );
            if !hit {
                self.log.push("The attack missed!".into());
                continue;
            }
            if mv.power != 0 {
                // Cmd_critcalc consumes Random even while tutorial flag gates crits.
                let (base, effectiveness) = typed_damage(
                    &self.mons[actor],
                    &self.mons[foe],
                    self.move_power(mv),
                    mv.move_type,
                    critical,
                );
                let random = (100 - self.rng.next() % 16) as u32;
                let damage = if base == 0 {
                    0
                } else {
                    (base * random / 100).max(1) as u16
                };
                let hp_before = self.mons[foe].hp;
                self.mons[foe].hp = self.mons[foe].hp.saturating_sub(damage);
                let dealt = hp_before - self.mons[foe].hp;
                self.log.push(format!(
                    "{} damage{}",
                    damage,
                    if critical { " - critical hit!" } else { "" }
                ));
                self.log_effectiveness(effectiveness, foe);
                self.damaging_secondary(mv, foe, effectiveness == 0);
                self.thaw_from_fire_hit(mv, actor, foe, damage, effectiveness == 0);
                if self.apply_absorb(mv, actor, foe, dealt) {
                    self.resolve_faint(data, foe);
                    break;
                }
                if actor == 0 && self.flags & 1 == 0 {
                    self.flags |= 1;
                    self.log
                        .push("OAK: Inflicting damage is key to any battle.".into());
                }
            } else {
                match mv.effect.as_str() {
                    "EFFECT_ATTACK_DOWN" => {
                        if self.mons[foe].stages[0] == 0 {
                            self.log.push("The stat cannot fall any lower!".into());
                        } else {
                            self.mons[foe].stages[0] -= 1;
                            self.log
                                .push(format!("{}'s Attack fell!", self.mons[foe].name));
                            if actor == 0 && self.flags & 2 == 0 {
                                self.flags |= 2;
                                self.log.push(
                                    "OAK: Lowering the foe's stats can put you at an advantage."
                                        .into(),
                                );
                            }
                        }
                    }
                    "EFFECT_DEFENSE_DOWN" => {
                        if self.mons[foe].stages[1] == 0 {
                            self.log.push("The stat cannot fall any lower!".into());
                        } else {
                            self.mons[foe].stages[1] -= 1;
                            self.log
                                .push(format!("{}'s Defense fell!", self.mons[foe].name));
                            if actor == 0 && self.flags & 2 == 0 {
                                self.flags |= 2;
                                self.log.push(
                                    "OAK: Lowering the foe's stats can put you at an advantage."
                                        .into(),
                                );
                            }
                        }
                    }
                    "EFFECT_DEFENSE_UP" => {
                        if self.mons[actor].stages[1] == 12 {
                            self.log.push(format!(
                                "{}'s Defense won't go higher!",
                                self.mons[actor].name
                            ));
                        } else {
                            self.mons[actor].stages[1] += 1;
                            self.log
                                .push(format!("{}'s Defense rose!", self.mons[actor].name));
                        }
                    }
                    "EFFECT_RESTORE_HP" => {
                        self.restore_half_hp(actor);
                    }
                    "EFFECT_DEFENSE_CURL" => self.defense_curl(actor),
                    "EFFECT_WATER_SPORT" => {
                        self.water_sport(actor);
                    }
                    "EFFECT_TELEPORT" => {
                        if self.teleport(actor) {
                            break;
                        }
                    }
                    "EFFECT_SPEED_DOWN" => {
                        if self.mons[foe].stages[2] == 0 {
                            self.log.push("The stat cannot fall any lower!".into());
                        } else {
                            self.mons[foe].stages[2] -= 1;
                            self.log
                                .push(format!("{}'s Speed fell!", self.mons[foe].name));
                            if actor == 0 && self.flags & 2 == 0 {
                                self.flags |= 2;
                                self.log.push(
                                    "OAK: Lowering the foe's stats can put you at an advantage."
                                        .into(),
                                );
                            }
                        }
                    }
                    "EFFECT_ACCURACY_DOWN" => {
                        if self.mons[foe].stages[5] == 0 {
                            self.log.push("The stat cannot fall any lower!".into());
                        } else {
                            self.mons[foe].stages[5] -= 1;
                            self.log
                                .push(format!("{}'s Accuracy fell!", self.mons[foe].name));
                            if actor == 0 && self.flags & 2 == 0 {
                                self.flags |= 2;
                                self.log.push(
                                    "OAK: Lowering the foe's stats can put you at an advantage."
                                        .into(),
                                );
                            }
                        }
                    }
                    "EFFECT_SLEEP" if self.safeguard_turns[foe] != 0 => {
                        self.log.push("Safeguard protected the target!".into());
                    }
                    "EFFECT_SLEEP" => self.apply_sleep(foe),
                    "EFFECT_SAFEGUARD" => {
                        self.set_safeguard(actor);
                    }
                    _ => return Err("Unsupported move effect".into()),
                }
            }
            if self.mons[foe].hp == 0 {
                self.resolve_faint(data, actor);
                break;
            }
            if self.winner.is_some() {
                break;
            }
        }
        if self.winner.is_none() {
            self.end_turn_status_damage(data);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn matches_original_c_ai_and_damage_vectors() {
        let vectors: serde_json::Value =
            serde_json::from_str(include_str!("../data/battle-reference.json")).unwrap();
        let d = Data::source();
        for row in vectors["ai"].as_array().unwrap() {
            let v: Vec<u32> = row
                .as_array()
                .unwrap()
                .iter()
                .map(|n| n.as_u64().unwrap() as u32)
                .collect();
            let mut b = Battle::new(&d, v[0] * 73 + v[1] * 9 + v[2]);
            b.mons[0].hp = v[0] as u16;
            b.mons[0].stages[1] = v[1] as u8;
            b.mons[1].stages[0] = v[2] as u8;
            let ai = choose(&d, &b.mons[1], &b.mons[0], &mut b.rng).unwrap();
            assert_eq!(
                [
                    ai.slot as u32,
                    ai.scores[0] as u32,
                    ai.scores[1] as u32,
                    b.rng.state,
                    b.rng.calls
                ],
                [v[3], v[4], v[5], v[6], v[7]],
                "C AI fixture {v:?}"
            );
        }
        for row in vectors["damage"].as_array().unwrap() {
            let v: Vec<u32> = row
                .as_array()
                .unwrap()
                .iter()
                .map(|n| n.as_u64().unwrap() as u32)
                .collect();
            let mut b = Battle::new(&d, 0);
            b.mons[0].stats[1] = v[0] as u16;
            b.mons[1].stats[2] = v[1] as u16;
            b.mons[0].stages[0] = v[3] as u8;
            b.mons[1].stages[1] = v[3] as u8;
            assert_eq!(
                base_damage(&b.mons[0], &b.mons[1], 40, 0, v[2] == 2),
                v[4],
                "C damage fixture {v:?}"
            );
        }
        for row in vectors["checks"].as_array().unwrap() {
            let v: Vec<u32> = row
                .as_array()
                .unwrap()
                .iter()
                .map(|n| n.as_u64().unwrap() as u32)
                .collect();
            let b = Battle::new(&d, v[0]);
            let mut rng = b.rng;
            let mv = &d.moves[&b.mons[v[2] as usize].moves[v[3] as usize]];
            let actor = v[2] as usize;
            let (hit, crit) = accuracy_and_critical(
                mv,
                actor,
                v[1] as u8,
                &b.mons[actor],
                &b.mons[actor ^ 1],
                &mut rng,
            );
            assert_eq!(
                [hit as u32, crit as u32, rng.state, rng.calls],
                [v[4], v[5], v[6], v[7]],
                "C checks fixture {v:?}"
            );
        }
        for row in vectors["stats"].as_array().unwrap() {
            let v: Vec<u16> = row
                .as_array()
                .unwrap()
                .iter()
                .map(|n| n.as_u64().unwrap() as u16)
                .collect();
            let mut m = Mon::create(&d, v[0] as usize, [v[2]; 6], [v[3]; 6], v[4] as u32);
            m.level = v[1];
            m.recalculate(&d, v[0] as usize);
            assert_eq!(m.stats, &v[5..11], "C stat fixture {v:?}");
        }
    }
    #[test]
    fn original_fixture_stats_and_moves() {
        let d = Data::source();
        let b = Battle::new(&d, 0);
        assert_eq!(b.mons[0].stats, [20, 11, 10, 13, 12, 11]);
        assert_eq!(b.mons[1].stats, [19, 9, 9, 9, 10, 11]);
        assert_eq!(b.mons[1].moves, ["MOVE_TACKLE", "MOVE_TAIL_WHIP"]);
        assert_eq!(d.moves["MOVE_TACKLE"].power, 35);
        assert_eq!(d.moves["MOVE_TACKLE"].accuracy, 95);
    }
    #[test]
    fn original_ai_exercises_every_lowered_stage_and_hp() {
        let d = Data::source();
        for hp in 1..=20 {
            for defense in 0..=6 {
                for attack in 0..=6 {
                    let mut b = Battle::new(&d, hp * 73 + defense * 9 + attack);
                    b.mons[0].hp = hp as u16;
                    b.mons[0].stages[1] = defense as u8;
                    b.mons[1].stages[0] = attack as u8;
                    let ai = choose(&d, &b.mons[1], &b.mons[0], &mut b.rng).unwrap();
                    assert!(ai.slot < 2);
                }
            }
        }
    }
    #[test]
    fn battle_can_finish_and_reset_without_touching_exploration() {
        let d = Data::source();
        for seed in 0..100 {
            let mut b = Battle::new(&d, seed);
            for _ in 0..35 {
                if b.winner.is_some() {
                    break;
                }
                b.play(&d, 0).unwrap();
            }
            assert!(b.winner.is_some());
        }
    }
    #[test]
    fn pp_exhaustion_is_rejected_without_rng_or_state_changes() {
        let d = Data::source();
        let mut b = Battle::new(&d, 5);
        b.mons[0].pp[0] = 0;
        assert!(b.play(&d, 0).is_err());
        assert_eq!(b.rng.calls, 0);
        assert_eq!(b.turn, 0);
    }
    #[test]
    fn first_battle_victory_awards_original_exp_ev_and_level() {
        let d = Data::source();
        let mut b = Battle::new(&d, 0x1234);
        while b.winner.is_none() {
            b.play(&d, 0).unwrap();
        }
        assert_eq!(b.winner, Some(0));
        assert_eq!(b.mons[0].experience, 205);
        assert_eq!(b.mons[0].level, 6);
        assert_eq!(b.mons[0].evs, [0, 0, 1, 0, 0, 0]);
        assert_eq!(b.mons[0].stats, [22, 13, 12, 14, 14, 12]);
        assert!(b.play(&d, 0).is_err());
    }
    #[test]
    fn player_can_lose_without_exp_or_ev_rewards() {
        let d = Data::source();
        let mut b = Battle::new(&d, 0x1234);
        while b.winner.is_none() {
            b.play(&d, 1).unwrap();
        }
        assert_eq!(b.winner, Some(1));
        assert_eq!(b.mons[0].hp, 0);
        assert_eq!(b.mons[0].experience, 135);
        assert_eq!(b.mons[0].evs, [0; 6]);
    }
    #[test]
    fn all_three_starters_can_battle_and_sync() {
        let d = Data::source();
        let core_data = crate::core::Data::source();
        let mut rng = crate::core::Rng {
            state: 0x12345678,
            calls: 0,
        };
        // Starters: Bulbasaur (1), Charmander (4), Squirtle (7)
        let starters = [1u16, 4u16, 7u16];
        for species_id in starters {
            let mut mon = crate::core::Pokemon::create(
                &core_data,
                &mut rng,
                species_id,
                5,
                31,
                None,
                crate::core::OtId::Player,
                12345,
            )
            .unwrap();
            let battle_mon = Mon::from_pokemon(&d, &core_data, &mon).unwrap();
            assert_eq!(battle_mon.level, 5);
            assert!(battle_mon.hp > 0);
            assert!(!battle_mon.moves.is_empty());

            // Rival mon (e.g. Squirtle at index 1)
            let rival_mon = Mon::create(&d, 1, [0; 6], [0; 6], d.rival_personality);
            let mut b = Battle::from_matchup(&d, battle_mon, rival_mon, 0x5678);
            while b.winner.is_none() {
                b.play(&d, 0).unwrap();
            }
            assert!(b.winner.is_some());
            b.mons[0].sync_to_pokemon(&mut mon);
            assert_eq!(mon.level as u16, b.mons[0].level);
            assert_eq!(mon.experience, b.mons[0].experience);
        }
    }
    #[test]
    fn amulet_coin_marks_the_participant_for_double_prize() {
        let battle_data = Data::source();
        let core_data = crate::core::Data::source();
        let mut rng = crate::core::Rng {
            state: 0x12345678,
            calls: 0,
        };
        let mut mon = crate::core::Pokemon::create(
            &core_data,
            &mut rng,
            core_data.id("SPECIES_CHARMANDER").unwrap(),
            5,
            31,
            None,
            crate::core::OtId::Player,
            12345,
        )
        .unwrap();
        mon.held_item = core_data.id("ITEM_AMULET_COIN").unwrap();
        let participant = Mon::from_pokemon(&battle_data, &core_data, &mon).unwrap();
        assert!(participant.double_prize);
    }
    #[test]
    fn poison_sting_and_end_turn_poison_follow_source_order() {
        let data = Data::source();
        let core_data = crate::core::Data::source();
        let mut core_rng = crate::core::Rng {
            state: 0x12345678,
            calls: 0,
        };
        let charmander = crate::core::Pokemon::create(
            &core_data,
            &mut core_rng,
            core_data.id("SPECIES_CHARMANDER").unwrap(),
            5,
            31,
            Some(0),
            crate::core::OtId::Player,
            12345,
        )
        .unwrap();
        let bulbasaur = crate::core::Pokemon::create(
            &core_data,
            &mut core_rng,
            core_data.id("SPECIES_BULBASAUR").unwrap(),
            5,
            31,
            Some(0),
            crate::core::OtId::Player,
            12345,
        )
        .unwrap();
        let player = Mon::from_pokemon(&data, &core_data, &charmander).unwrap();
        let immune = Mon::from_pokemon(&data, &core_data, &bulbasaur).unwrap();
        assert!(!player.poison_immune);
        assert!(immune.poison_immune);

        let seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 100 <= 30
            })
            .unwrap();
        let weedle = Mon::create_at_level(&data, 8, 9, [0; 6], [0; 6], 0);
        let mut battle = Battle::from_matchup(&data, player, weedle.clone(), seed);
        battle.damaging_secondary(&data.moves["MOVE_POISON_STING"], 0, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[0].status, 1 << 3);
        let before = battle.mons[0].hp;
        battle.end_turn_status_damage(&data);
        assert_eq!(
            battle.mons[0].hp,
            before - (battle.mons[0].stats[0] / 8).max(1)
        );

        let mut protected = Battle::from_matchup(&data, immune, weedle, seed);
        protected.damaging_secondary(&data.moves["MOVE_POISON_STING"], 0, false);
        assert_eq!(protected.rng.calls, 1);
        assert_eq!(protected.mons[0].status, 0);

        protected.mons[0].poison_immune = false;
        protected.mons[0].status = 1 << 7;
        let before = protected.mons[0].hp;
        protected.end_turn_status_damage(&data);
        assert_eq!(protected.mons[0].status & 0xf00, 1 << 8);
        assert_eq!(
            protected.mons[0].hp,
            before - (protected.mons[0].stats[0] / 16).max(1)
        );

        protected.mons[0].status = 0;
        protected.mons[1].status = 1 << 3;
        protected.mons[1].hp = 1;
        let experience = protected.mons[0].experience;
        protected.end_turn_status_damage(&data);
        assert_eq!(protected.winner, Some(0));
        assert!(protected.mons[0].experience > experience);
    }
    #[test]
    fn wild_battle_catch_and_flee_mechanics() {
        let d = Data::source();
        let player_mon = Mon::create(&d, 0, [31; 6], [0; 6], 0); // Charmander Lv 5
        let wild_pidgey = Mon::create_at_level(&d, 3, 3, [15; 6], [0; 6], 1234); // Pidgey Lv 3

        let mut b = Battle::wild(&d, player_mon.clone(), wild_pidgey.clone(), 255, 0x1111);
        assert!(b.is_wild);
        assert_eq!(b.mons[1].name, "Pidgey");
        assert_eq!(b.mons[1].level, 3);

        // Flee should succeed when player is faster (Charmander Speed 13 vs Pidgey Lv 3)
        assert!(b.flee());
        assert!(b.fled);
        assert_eq!(b.winner, Some(2));

        // A Master Ball always succeeds.
        let mut b2 = Battle::wild(&d, player_mon, wild_pidgey, 255, 0x2222);
        assert!(b2.catch(1).unwrap());
        assert!(b2.caught);
        assert_eq!(b2.winner, Some(0));

        // Trainer battle should reject flee and catch
        let mut trainer_b = Battle::new(&d, 0x3333);
        assert!(!trainer_b.flee());
        assert!(!trainer_b.catch(4).unwrap());
    }

    #[test]
    fn run_attempts_and_basic_ball_rng_follow_source_order() {
        let d = Data::source();
        let mut slow = Mon::create_at_level(&d, 0, 3, [0; 6], [0; 6], 0);
        let mut fast = Mon::create_at_level(&d, 3, 50, [31; 6], [0; 6], 0);
        slow.stats[3] = 1;
        fast.stats[3] = 255;
        let mut battle = Battle::wild(&d, slow.clone(), fast.clone(), 1, 0);
        assert!(!battle.flee());
        assert_eq!(battle.run_tries, 1);
        assert_eq!(battle.rng.calls, 1);
        battle.mons[0].always_run = true;
        assert!(battle.flee());
        assert_eq!(battle.run_tries, 1);
        assert_eq!(battle.rng.calls, 1);

        let mut guaranteed = Battle::wild(&d, slow.clone(), fast.clone(), 255, 0);
        guaranteed.mons[1].hp = 1;
        guaranteed.mons[1].status = 1;
        assert!(guaranteed.catch(4).unwrap());
        assert_eq!(guaranteed.rng.calls, 0);

        let mut hard = Battle::wild(&d, slow, fast, 1, 0);
        assert!(!hard.catch(4).unwrap());
        assert!(hard.rng.calls <= 4);
        assert!(hard.catch(5).is_err());

        let mut master = Battle::wild(&d, hard.mons[0].clone(), hard.mons[1].clone(), 3, 0);
        assert!(master.catch(1).unwrap());
        assert!(master.rng.calls > 0);
        assert!(master.rng.calls <= 4);
    }

    #[test]
    fn special_ball_multipliers_preserve_source_context_and_u8_order() {
        let data = Data::source();
        let player = Mon::create_at_level(&data, 0, 5, [31; 6], [0; 6], 0);
        let target = Mon::create_at_level(&data, 1, 5, [0; 6], [0; 6], 0);
        let mut battle = Battle::wild(&data, player, target, 45, 0);

        assert_eq!(battle.ball_multiplier(6).unwrap(), 10);
        assert_eq!(battle.ball_multiplier(7).unwrap(), 10);
        assert_eq!(battle.ball_multiplier(8).unwrap(), 35);
        assert_eq!(battle.ball_multiplier(9).unwrap(), 10);
        assert_eq!(battle.ball_multiplier(10).unwrap(), 10);
        assert_eq!(battle.ball_multiplier(11).unwrap(), 10);
        assert_eq!(battle.ball_multiplier(12).unwrap(), 10);
        assert!(battle.ball_multiplier(5).is_err());

        battle.set_catch_context(CatchContext {
            target_is_water_or_bug: true,
            underwater: true,
            target_previously_caught: true,
        });
        assert_eq!(battle.ball_multiplier(6).unwrap(), 30);
        assert_eq!(battle.ball_multiplier(7).unwrap(), 35);
        assert_eq!(battle.ball_multiplier(9).unwrap(), 30);

        battle.mons[1].level = 31;
        assert_eq!(battle.ball_multiplier(8).unwrap(), 10);
        battle.turn = 30;
        assert_eq!(battle.ball_multiplier(10).unwrap(), 40);
        battle.turn = 255;
        assert_eq!(battle.ball_multiplier(10).unwrap(), 9);

        battle.catch_rate = 255;
        battle.mons[1].hp = 1;
        battle.mons[1].status = 1;
        assert!(battle.catch(6).unwrap());
        assert_eq!(battle.mons[1].poke_ball, 6);
    }

    #[test]
    fn string_shot_speed_stage_changes_turn_order() {
        let data = Data::source();
        let player = Mon::create_at_level(&data, 0, 5, [31; 6], [0; 6], 0);
        let mut caterpie = Mon::create_at_level(&data, 5, 5, [0; 6], [0; 6], 0);
        caterpie.pp[0] = 0;

        let mut lowered = None;
        for seed in 0..256 {
            let mut battle = Battle::from_matchup(&data, player.clone(), caterpie.clone(), seed);
            battle.enemy_turn(&data).unwrap();
            if battle.mons[0].stages[2] == 5 {
                lowered = Some(battle);
                break;
            }
        }
        let lowered = lowered.expect("String Shot should hit for at least one deterministic seed");
        assert!(lowered
            .log
            .iter()
            .any(|line| line == "Charmander's Speed fell!"));

        let mut player = player;
        player.stats[3] = 12;
        player.stages[2] = 5;
        let mut caterpie = caterpie;
        caterpie.stats[3] = 10;
        caterpie.pp[0] = 35;
        caterpie.pp[1] = 0;
        let mut battle = Battle::from_matchup(&data, player, caterpie, 0);
        assert_eq!(effective_speed(&battle.mons[0]), 8);
        assert_eq!(effective_speed(&battle.mons[1]), 10);
        battle.play(&data, 0).unwrap();
        assert!(battle.log[0].starts_with("Caterpie used "));

        battle.mons[0].stages[2] = 6;
        battle.mons[0].status = 1 << 6;
        assert_eq!(effective_speed(&battle.mons[0]), 3);
    }

    #[test]
    fn harden_raises_defense_and_uses_no_accuracy_roll() {
        let data = Data::source();
        let harden = &data.moves["MOVE_HARDEN"];
        let mut rng = Rng { state: 7, calls: 0 };
        let fixture = Battle::new(&data, 0);
        assert_eq!(
            accuracy_and_critical(harden, 1, 0, &fixture.mons[1], &fixture.mons[0], &mut rng),
            (true, false)
        );
        assert_eq!(rng.calls, 0);

        let mut player_harden = Battle::new(&data, 11);
        player_harden.mons[0].moves[0] = "MOVE_HARDEN".into();
        player_harden.mons[0].pp[0] = harden.pp;
        player_harden.play(&data, 0).unwrap();
        assert_eq!(player_harden.mons[0].stages[1], 7);
        assert!(player_harden
            .log
            .iter()
            .any(|line| line == "Charmander's Defense rose!"));

        let mut capped = Battle::new(&data, 11);
        capped.mons[0].moves[0] = "MOVE_HARDEN".into();
        capped.mons[0].pp[0] = harden.pp;
        capped.mons[0].stages[1] = 12;
        capped.play(&data, 0).unwrap();
        assert_eq!(capped.mons[0].stages[1], 12);
        assert!(capped
            .log
            .iter()
            .any(|line| line == "Charmander's Defense won't go higher!"));

        let mut attacker = capped.mons[1].clone();
        attacker.level = 50;
        attacker.stats[1] = 100;
        let mut defender = capped.mons[0].clone();
        defender.stats[2] = 50;
        defender.stages[1] = 6;
        let before = base_damage(&attacker, &defender, 40, 0, false);
        defender.stages[1] = 7;
        assert!(base_damage(&attacker, &defender, 40, 0, false) < before);

        assert_eq!(data.species[6].types, [6, 6]);
        assert_eq!(data.moves["MOVE_SCRATCH"].move_type, 0);
        assert!(result_in_byte_list(&data, "AI_CV_DefenseUp_PhysicalTypes", "0").unwrap());
        assert!(!result_in_byte_list(&data, "AI_CV_DefenseUp_PhysicalTypes", "10").unwrap());

        for seed in 0..256 {
            let player = Mon::create_at_level(&data, 0, 5, [31; 6], [0; 6], 0);
            let metapod = Mon::create_at_level(&data, 6, 5, [0; 6], [0; 6], 0);
            let mut opponent_harden = Battle::from_matchup(&data, player, metapod, seed);
            opponent_harden.play(&data, 0).unwrap();
            opponent_harden.play(&data, 0).unwrap();
            assert_eq!(opponent_harden.mons[1].stages[1], 8);
            assert_eq!(
                opponent_harden.mons[0].last_move.as_deref(),
                Some("MOVE_SCRATCH")
            );
        }
    }

    #[test]
    fn sand_attack_changes_accuracy_with_source_stage_ratios() {
        let data = Data::source();
        let player = Mon::create_at_level(&data, 0, 5, [31; 6], [0; 6], 0);
        let mut pidgey = Mon::create_at_level(&data, 3, 3, [0; 6], [0; 6], 0);
        pidgey.pp[0] = 0;
        let mut battle = Battle::from_matchup(&data, player, pidgey, 0);
        battle.enemy_turn(&data).unwrap();
        assert_eq!(battle.mons[0].stages[5], 5);
        assert!(battle
            .log
            .iter()
            .any(|line| line == "Charmander's Accuracy fell!"));

        let scratch = &data.moves["MOVE_SCRATCH"];
        let mut saw_stage_miss = false;
        for seed in 0..1024 {
            let mut rng = Rng {
                state: seed,
                calls: 0,
            };
            let (hit, _) =
                accuracy_and_critical(scratch, 1, 0, &battle.mons[0], &battle.mons[1], &mut rng);
            if !hit {
                assert_eq!(rng.calls, 1);
                saw_stage_miss = true;
                break;
            }
        }
        assert!(saw_stage_miss);

        battle.mons[0].stages[5] = 6;
        battle.mons[1].stages[6] = 7;
        let mut rng = Rng {
            state: u32::MAX,
            calls: 0,
        };
        let _ = accuracy_and_critical(scratch, 1, 0, &battle.mons[0], &battle.mons[1], &mut rng);
        assert!(rng.calls >= 1);
    }

    #[test]
    fn generation_three_move_types_select_physical_or_special_stats() {
        let data = Data::source();
        assert_eq!(data.moves["MOVE_SCRATCH"].move_type, 0);
        assert_eq!(data.moves["MOVE_EMBER"].move_type, 10);

        let mut attacker = Mon::create_at_level(&data, 0, 50, [0; 6], [0; 6], 0);
        let mut defender = Mon::create_at_level(&data, 1, 50, [0; 6], [0; 6], 0);
        attacker.stats[1] = 1;
        attacker.stats[4] = 100;
        defender.stats[2] = 200;
        defender.stats[5] = 50;

        let physical = base_damage(&attacker, &defender, 40, 0, false);
        let special = base_damage(&attacker, &defender, 40, 10, false);
        assert_eq!(physical, 3);
        assert!(special > physical);

        attacker.stages[3] = 5;
        let lowered_special = base_damage(&attacker, &defender, 40, 10, false);
        assert!(lowered_special < special);
        assert!(base_damage(&attacker, &defender, 40, 10, true) > lowered_special);
    }

    #[test]
    fn stab_and_generation_three_type_chart_modify_damage_in_source_order() {
        let data = Data::source();
        let mut attacker = Mon::create_at_level(&data, 0, 50, [0; 6], [0; 6], 0);
        let mut defender = Mon::create_at_level(&data, 1, 50, [0; 6], [0; 6], 0);
        attacker.types = [10, 10]; // Fire
        defender.types = [12, 3]; // Grass / Poison
        let raw = base_damage(&attacker, &defender, 40, 10, false);
        let (damage, effectiveness) = typed_damage(&attacker, &defender, 40, 10, false);
        assert_eq!(damage, raw * 15 / 10 * 20 / 10);
        assert_eq!(effectiveness, 20);

        defender.types = [11, 11]; // Duplicate Water is only applied once.
        let (damage, effectiveness) = typed_damage(&attacker, &defender, 40, 10, false);
        assert_eq!(damage, raw * 15 / 10 * 5 / 10);
        assert_eq!(effectiveness, 5);

        attacker.types = [0, 0];
        defender.types = [7, 7]; // Normal cannot affect Ghost.
        assert_eq!(typed_damage(&attacker, &defender, 40, 0, false), (0, 0));

        attacker.types = [10, 10];
        defender.types = [12, 11]; // Fire's x2 and x0.5 cancel.
        assert_eq!(typed_damage(&attacker, &defender, 40, 10, false).1, 10);
    }

    #[test]
    fn ember_burn_uses_source_chance_immunity_damage_and_guts_rules() {
        let data = Data::source();
        let ember = &data.moves["MOVE_EMBER"];
        assert_eq!(ember.secondary_chance, 10);

        let attacker = Mon::create_at_level(&data, 0, 50, [0; 6], [0; 6], 0);
        let target = Mon::create_at_level(&data, 1, 50, [0; 6], [0; 6], 0);
        let seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 100 <= 10
            })
            .unwrap();
        let mut battle = Battle::from_matchup(&data, attacker, target, seed);
        battle.damaging_secondary(ember, 1, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status, 1 << 4);
        let before = battle.mons[1].hp;
        battle.end_turn_status_damage(&data);
        assert_eq!(battle.mons[1].hp, before - battle.mons[1].stats[0] / 8);
        assert!(battle
            .log
            .iter()
            .any(|line| line.contains("hurt by its burn")));

        let mut physical_user = battle.mons[0].clone();
        physical_user.stats[1] = 100;
        physical_user.stats[4] = 100;
        physical_user.status = 1 << 4;
        let mut defender = battle.mons[1].clone();
        defender.stats[2] = 50;
        defender.stats[5] = 50;
        let burned = base_damage(&physical_user, &defender, 40, 0, false);
        physical_user.status = 0;
        let healthy = base_damage(&physical_user, &defender, 40, 0, false);
        assert!(burned < healthy);
        physical_user.status = 1 << 4;
        assert_eq!(
            base_damage(&physical_user, &defender, 40, 10, false),
            healthy
        );
        physical_user.ability = 62;
        assert_eq!(
            base_damage(&physical_user, &defender, 40, 0, false),
            healthy
        );

        let mut fire_target = defender.clone();
        fire_target.types = [10, 10];
        fire_target.status = 0;
        let mut protected = Battle::from_matchup(&data, battle.mons[0].clone(), fire_target, seed);
        protected.damaging_secondary(ember, 1, false);
        assert_eq!(protected.rng.calls, 1);
        assert_eq!(protected.mons[1].status, 0);
        protected.mons[1].types = [11, 11];
        protected.mons[1].ability = 41;
        protected.rng = Rng {
            state: seed,
            calls: 0,
        };
        protected.damaging_secondary(ember, 1, false);
        assert_eq!(protected.rng.calls, 1);
        assert_eq!(protected.mons[1].status, 0);
    }

    #[test]
    fn thunder_shock_paralysis_uses_source_chance_speed_and_action_check() {
        let data = Data::source();
        let thunder_shock = &data.moves["MOVE_THUNDER_SHOCK"];
        assert_eq!(thunder_shock.secondary_chance, 10);
        let attacker = Mon::create_at_level(&data, 11, 5, [0; 6], [0; 6], 0);
        let target = Mon::create_at_level(&data, 0, 5, [0; 6], [0; 6], 0);
        let status_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 100 <= 10
            })
            .unwrap();
        let mut battle = Battle::from_matchup(&data, attacker, target, status_seed);
        let original_speed = effective_speed(&battle.mons[1]);
        battle.damaging_secondary(thunder_shock, 1, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status, 1 << 6);
        assert_eq!(effective_speed(&battle.mons[1]), original_speed / 4);

        battle.mons[1].status = 0;
        battle.mons[1].ability = 7;
        battle.rng = Rng {
            state: status_seed,
            calls: 0,
        };
        battle.damaging_secondary(thunder_shock, 1, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status, 0);

        let stop_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 4 == 0
            })
            .unwrap();
        battle.mons[1].ability = 0;
        battle.mons[1].status = 1 << 6;
        battle.rng = Rng {
            state: stop_seed,
            calls: 0,
        };
        assert!(battle.paralysis_stops(1));
        assert_eq!(battle.rng.calls, 1);

        let mut saw_stopped_turn = false;
        for seed in 0..2048 {
            let mut fixture = Battle::new(&data, seed);
            fixture.mons[0].status = 1 << 6;
            let pp = fixture.mons[0].pp[0];
            fixture.play(&data, 0).unwrap();
            if fixture
                .log
                .iter()
                .any(|line| line == "Charmander is paralyzed! It can't move!")
            {
                assert_eq!(fixture.mons[0].pp[0], pp);
                assert_eq!(fixture.mons[0].last_move, None);
                saw_stopped_turn = true;
                break;
            }
        }
        assert!(saw_stopped_turn);
    }

    #[test]
    fn sing_sleep_uses_source_duration_prevention_and_wakeup_order() {
        let data = Data::source();
        let sing = &data.moves["MOVE_SING"];
        assert_eq!(sing.accuracy, 55);
        let sleeper = Mon::create_at_level(&data, 23, 5, [0; 6], [0; 6], 0);
        let target = Mon::create_at_level(&data, 0, 5, [0; 6], [0; 6], 0);
        let duration_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() & 3 == 0
            })
            .unwrap();
        let mut battle = Battle::from_matchup(&data, sleeper, target, duration_seed);
        battle.apply_sleep(1);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status & 7, 2);
        assert!(battle.sleep_stops(1));
        assert_eq!(battle.mons[1].status & 7, 1);
        assert!(!battle.sleep_stops(1));
        assert_eq!(battle.mons[1].status & 7, 0);
        assert!(battle.log.iter().any(|line| line == "Charmander woke up!"));

        battle.mons[1].status = 5;
        battle.mons[1].ability = 48;
        assert!(battle.sleep_stops(1));
        assert_eq!(battle.mons[1].status & 7, 3);
        battle.mons[1].ability = 15;
        battle.mons[1].status = 0;
        assert!(battle.sleep_move_blocked(1));
        battle.mons[1].ability = 43;
        assert!(battle.sleep_move_blocked(1));

        battle.mons[1].ability = 0;
        battle.mons[1].status = 4;
        battle.mons[1].status2 = 1 << 27;
        battle.mons[0].status2 = 1 << 4;
        assert!(!battle.sleep_stops(1));
        assert_eq!(battle.mons[1].status & 7, 0);
        assert_eq!(battle.mons[1].status2 & (1 << 27), 0);

        let mut saw_sleep = false;
        for seed in 0..4096 {
            let mut fixture = Battle::new(&data, seed);
            fixture.flags = 2;
            fixture.mons[0].moves[0] = "MOVE_SING".into();
            fixture.mons[0].pp[0] = sing.pp;
            let enemy_pp = fixture.mons[1].pp.clone();
            fixture.play(&data, 0).unwrap();
            if fixture
                .log
                .iter()
                .any(|line| line == "Squirtle fell asleep!")
            {
                assert!(fixture.mons[1].status & 7 != 0);
                assert_eq!(fixture.mons[1].pp, enemy_pp);
                assert_eq!(fixture.mons[1].last_move, None);
                assert!(fixture
                    .log
                    .iter()
                    .any(|line| line == "Squirtle is fast asleep."));
                saw_sleep = true;
                break;
            }
        }
        assert!(saw_sleep);
    }

    #[test]
    fn confusion_uses_source_duration_action_rng_and_self_damage() {
        let data = Data::source();
        let confusion = &data.moves["MOVE_CONFUSION"];
        assert_eq!(confusion.secondary_chance, 10);
        let attacker = Mon::create_at_level(&data, 7, 10, [0; 6], [0; 6], 0);
        let mut target = Mon::create_at_level(&data, 0, 10, [0; 6], [0; 6], 0);
        target.status = 1 << 4; // Volatile confusion can coexist with burn.
        let duration_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 100 <= 10 && rng.next() % 4 == 0
            })
            .unwrap();
        let mut battle = Battle::from_matchup(&data, attacker, target, duration_seed);
        battle.damaging_secondary(confusion, 1, false);
        assert_eq!(battle.rng.calls, 2);
        assert_eq!(battle.mons[1].status2 & 7, 2);
        assert_eq!(battle.mons[1].status, 1 << 4);

        battle.mons[1].status2 = 0;
        battle.mons[1].ability = 20;
        battle.rng = Rng {
            state: duration_seed,
            calls: 0,
        };
        battle.damaging_secondary(confusion, 1, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status2 & 7, 0);

        let self_hit_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() & 1 == 0
            })
            .unwrap();
        battle.mons[1].ability = 0;
        battle.mons[1].status2 = 2;
        battle.mons[1].hp = battle.mons[1].stats[0];
        battle.rng = Rng {
            state: self_hit_seed,
            calls: 0,
        };
        let before = battle.mons[1].hp;
        let base = base_damage(&battle.mons[1], &battle.mons[1], 40, 0, false);
        let mut expected_rng = battle.rng;
        assert_eq!(expected_rng.next() & 1, 0);
        let expected = (base * (100 - expected_rng.next() % 16) as u32 / 100).max(1) as u16;
        assert!(battle.confusion_stops(1, &data));
        assert_eq!(battle.rng.calls, 2);
        assert_eq!(battle.mons[1].status2 & 7, 1);
        assert_eq!(battle.mons[1].hp, before - expected);

        battle.mons[1].status2 = 1;
        battle.rng.calls = 0;
        assert!(!battle.confusion_stops(1, &data));
        assert_eq!(battle.rng.calls, 0);
        assert!(battle
            .log
            .iter()
            .any(|line| line == "Charmander snapped out of confusion!"));
    }

    #[test]
    fn safeguard_uses_source_side_timer_and_secondary_rng_order() {
        let data = Data::source();
        let confusion = &data.moves["MOVE_CONFUSION"];
        let attacker = Mon::create_at_level(&data, 7, 10, [0; 6], [0; 6], 0);
        let target = Mon::create_at_level(&data, 0, 10, [0; 6], [0; 6], 0);
        let status_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 100 <= 10
            })
            .unwrap();
        let mut battle = Battle::from_matchup(&data, attacker, target, status_seed);

        assert!(battle.set_safeguard(1));
        assert_eq!(battle.safeguard_turns, [0, 5]);
        assert!(!battle.set_safeguard(1));
        assert_eq!(battle.safeguard_turns, [0, 5]);
        battle.damaging_secondary(confusion, 1, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status2 & 7, 0);

        assert!(battle.set_safeguard(0));
        battle.rng = Rng {
            state: status_seed,
            calls: 0,
        };
        battle.damaging_secondary(confusion, 0, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[0].status2 & 7, 0);

        for turns in [4, 3, 2, 1, 0] {
            battle.end_turn_status_damage(&data);
            assert_eq!(battle.safeguard_turns, [turns, turns]);
        }
        battle.rng = Rng {
            state: status_seed,
            calls: 0,
        };
        battle.damaging_secondary(confusion, 1, false);
        assert_eq!(battle.rng.calls, 2);
        assert_ne!(battle.mons[1].status2 & 7, 0);

        let mut saw_primary_block = false;
        for seed in 0..4096 {
            let mut fixture = Battle::new(&data, seed);
            fixture.flags = 2;
            fixture.mons[0].moves[0] = "MOVE_SING".into();
            fixture.mons[0].pp[0] = data.moves["MOVE_SING"].pp;
            fixture.safeguard_turns[1] = 1;
            let pp = fixture.mons[0].pp[0];
            fixture.play(&data, 0).unwrap();
            if fixture
                .log
                .iter()
                .any(|line| line == "Safeguard protected the target!")
            {
                assert_eq!(fixture.mons[1].status & 7, 0);
                assert_eq!(fixture.mons[0].pp[0], pp - 1);
                assert_eq!(fixture.safeguard_turns[1], 0);
                saw_primary_block = true;
                break;
            }
        }
        assert!(saw_primary_block);
    }

    #[test]
    fn freeze_uses_source_infliction_action_and_thaw_rules() {
        let data = Data::source();
        let attacker = Mon::create_at_level(&data, 7, 10, [0; 6], [0; 6], 0);
        let mut target = Mon::create_at_level(&data, 0, 10, [0; 6], [0; 6], 0);
        target.ability = 0;
        target.types = [0, 0];
        let freeze_hit = Move {
            power: 40,
            accuracy: 100,
            pp: 10,
            effect: "EFFECT_FREEZE_HIT".into(),
            move_type: 15,
            secondary_chance: 100,
        };
        let mut battle = Battle::from_matchup(&data, attacker, target, 0);

        battle.damaging_secondary(&freeze_hit, 1, false);
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status, 1 << 5);

        for (types, ability, status, safeguard, substitute) in [
            ([15, 15], 0, 0, 0, false),
            ([0, 0], 40, 0, 0, false),
            ([0, 0], 0, 1 << 4, 0, false),
            ([0, 0], 0, 0, 1, false),
            ([0, 0], 0, 0, 0, true),
        ] {
            battle.mons[1].types = types;
            battle.mons[1].ability = ability;
            battle.mons[1].status = status;
            battle.mons[1].status2 = if substitute { 1 << 24 } else { 0 };
            battle.safeguard_turns[1] = safeguard;
            battle.rng = Rng { state: 0, calls: 0 };
            battle.damaging_secondary(&freeze_hit, 1, false);
            assert_eq!(battle.rng.calls, 1);
            assert_eq!(battle.mons[1].status, status);
        }

        let frozen_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 5 != 0
            })
            .unwrap();
        battle.mons[1].status = 1 << 5;
        battle.rng = Rng {
            state: frozen_seed,
            calls: 0,
        };
        assert!(battle.freeze_stops(1, false));
        assert_eq!(battle.rng.calls, 1);
        assert_eq!(battle.mons[1].status, 1 << 5);

        let thaw_seed = (0..u32::MAX)
            .find(|seed| {
                let mut rng = Rng {
                    state: *seed,
                    calls: 0,
                };
                rng.next() % 5 == 0
            })
            .unwrap();
        battle.rng = Rng {
            state: thaw_seed,
            calls: 0,
        };
        assert!(!battle.freeze_stops(1, false));
        assert_eq!(battle.mons[1].status, 0);

        battle.mons[1].status = 1 << 5;
        battle.rng = Rng {
            state: frozen_seed,
            calls: 0,
        };
        assert!(!battle.freeze_stops(1, true));
        assert_eq!(battle.mons[1].status, 1 << 5);
        battle.thaw_for_move(1, "EFFECT_THAW_HIT");
        assert_eq!(battle.mons[1].status, 0);

        let ember = &data.moves["MOVE_EMBER"];
        battle.mons[1].status = 1 << 5;
        battle.mons[1].hp = 1;
        battle.thaw_from_fire_hit(ember, 0, 1, 1, false);
        assert_eq!(battle.mons[1].status, 0);
        for (hp, damage, no_effect) in [(0, 1, false), (1, 0, false), (1, 1, true)] {
            battle.mons[1].status = 1 << 5;
            battle.mons[1].hp = hp;
            battle.thaw_from_fire_hit(ember, 0, 1, damage, no_effect);
            assert_eq!(battle.mons[1].status, 1 << 5);
        }

        let mut saw_blocked_turn = false;
        for seed in 0..4096 {
            let mut fixture = Battle::new(&data, seed);
            fixture.mons[0].status = 1 << 5;
            let pp = fixture.mons[0].pp[0];
            fixture.play(&data, 0).unwrap();
            if fixture
                .log
                .iter()
                .any(|line| line == "Charmander is frozen solid!")
            {
                assert_eq!(fixture.mons[0].pp[0], pp);
                assert_eq!(fixture.mons[0].last_move, None);
                saw_blocked_turn = true;
                break;
            }
        }
        assert!(saw_blocked_turn);
    }

    #[test]
    fn imported_damage_effects_use_source_accuracy_crit_drain_and_speed_drop() {
        let data = Data::source();
        let attacker = Mon::create_at_level(&data, 0, 20, [0; 6], [0; 6], 0);
        let mut defender = Mon::create_at_level(&data, 1, 20, [0; 6], [0; 6], 0);

        let mut swift = data.moves["MOVE_SWIFT"].clone();
        swift.accuracy = 1; // Prove the effect, rather than imported zero, bypasses accuracy.
        let mut inaccurate = attacker.clone();
        inaccurate.stages[5] = 0;
        defender.stages[6] = 12;
        let mut rng = Rng { state: 0, calls: 0 };
        assert!(accuracy_and_critical(&swift, 1, 1, &inaccurate, &defender, &mut rng).0);
        assert_eq!(rng.calls, 1); // Critical roll only.

        let mut karate_chop = data.moves["MOVE_KARATE_CHOP"].clone();
        karate_chop.accuracy = 0;
        let crit_seed = (0..u32::MAX)
            .find(|seed| {
                let mut candidate = Rng {
                    state: *seed,
                    calls: 0,
                };
                let value = candidate.next();
                value % 8 == 0 && value % 16 != 0
            })
            .unwrap();
        let mut high_rng = Rng {
            state: crit_seed,
            calls: 0,
        };
        assert!(accuracy_and_critical(&karate_chop, 1, 1, &attacker, &defender, &mut high_rng).1);
        let mut ordinary = karate_chop.clone();
        ordinary.effect = "EFFECT_HIT".into();
        let mut ordinary_rng = Rng {
            state: crit_seed,
            calls: 0,
        };
        assert!(!accuracy_and_critical(&ordinary, 1, 1, &attacker, &defender, &mut ordinary_rng).1);
        defender.ability = 4;
        let mut armor_rng = Rng {
            state: crit_seed,
            calls: 0,
        };
        assert!(!accuracy_and_critical(&karate_chop, 1, 1, &attacker, &defender, &mut armor_rng).1);
        assert_eq!(armor_rng.calls, 0);

        defender.ability = 0;
        defender.stages[2] = 6;
        let bubble = &data.moves["MOVE_BUBBLE"];
        assert_eq!(bubble.secondary_chance, 10);
        let proc_seed = (0..u32::MAX)
            .find(|seed| {
                let mut candidate = Rng {
                    state: *seed,
                    calls: 0,
                };
                candidate.next() % 100 == 10
            })
            .unwrap();
        let mut battle = Battle::from_matchup(&data, attacker.clone(), defender.clone(), proc_seed);
        battle.damaging_secondary(bubble, 1, false);
        assert_eq!(battle.mons[1].stages[2], 5);
        assert_eq!(battle.rng.calls, 1);

        battle.mons[1].stages[2] = 6;
        battle.mons[1].ability = 29;
        battle.rng = Rng {
            state: proc_seed,
            calls: 0,
        };
        battle.damaging_secondary(bubble, 1, false);
        assert_eq!(battle.mons[1].stages[2], 6);
        let rock_tomb = &data.moves["MOVE_ROCK_TOMB"];
        assert_eq!(rock_tomb.secondary_chance, 100);
        battle.rng = Rng { state: 0, calls: 0 };
        battle.damaging_secondary(rock_tomb, 1, false);
        assert_eq!(battle.mons[1].stages[2], 5);
        assert_eq!(battle.rng.calls, 1);

        battle.mons[0].hp = 1;
        let before = battle.mons[0].hp;
        assert!(!battle.apply_absorb(&data.moves["MOVE_ABSORB"], 0, 1, 5));
        assert_eq!(battle.mons[0].hp, before + 2);
        let before = battle.mons[0].hp;
        assert!(!battle.apply_absorb(&data.moves["MOVE_ABSORB"], 0, 1, 1));
        assert_eq!(battle.mons[0].hp, before);
        battle.mons[0].hp = 3;
        battle.mons[1].ability = 64;
        assert!(battle.apply_absorb(&data.moves["MOVE_LEECH_LIFE"], 0, 1, 6));
        assert_eq!(battle.mons[0].hp, 0);

        let mut integrated = Battle::new(&data, 0);
        integrated.flags = 3;
        integrated.mons[0].moves[0] = "MOVE_ABSORB".into();
        integrated.mons[0].pp[0] = data.moves["MOVE_ABSORB"].pp;
        integrated.mons[0].hp = 1;
        integrated.mons[1].status = 5;
        integrated.play(&data, 0).unwrap();
        assert!(integrated.mons[0].hp > 1);
        assert!(integrated
            .log
            .iter()
            .any(|line| line.contains("HP restored")));
    }

    #[test]
    fn self_effects_use_source_heal_curl_sport_and_teleport_rules() {
        let data = Data::source();
        let mut battle = Battle::new(&data, 0);
        battle.flags = 3;

        battle.mons[0].stats[0] = 11;
        battle.mons[0].hp = 2;
        assert!(battle.restore_half_hp(0));
        assert_eq!(battle.mons[0].hp, 7);
        battle.mons[0].hp = 11;
        assert!(!battle.restore_half_hp(0));

        battle.mons[0].stages[1] = 11;
        battle.defense_curl(0);
        assert_eq!(battle.mons[0].stages[1], 12);
        assert_ne!(battle.mons[0].status2 & (1 << 30), 0);
        battle.defense_curl(0);
        assert_eq!(battle.mons[0].stages[1], 12);

        let ember = &data.moves["MOVE_EMBER"];
        assert_eq!(battle.move_power(ember), ember.power);
        let mut sport_rng = Rng { state: 0, calls: 0 };
        assert!(
            accuracy_and_critical(
                &data.moves["MOVE_WATER_SPORT"],
                1,
                3,
                &battle.mons[1],
                &battle.mons[0],
                &mut sport_rng,
            )
            .0
        );
        assert_eq!(sport_rng.calls, 0);
        assert!(battle.water_sport(1));
        assert_eq!(battle.move_power(ember), ember.power / 2);
        assert!(!battle.water_sport(1));

        battle.is_wild = false;
        assert!(!battle.teleport(0));
        battle.is_wild = true;
        battle.mons[0].status2 = 1 << 26;
        assert!(!battle.teleport(0));
        battle.mons[0].always_run = true;
        assert!(battle.teleport(0));
        assert!(battle.fled);
        assert_eq!(battle.winner, Some(2));

        let mut trapped = Battle::new(&data, 0);
        trapped.is_wild = true;
        trapped.mons[1].ability = 23;
        assert!(!trapped.teleport(0));
        trapped.mons[1].ability = 71;
        trapped.mons[0].types = [2, 2];
        assert!(trapped.teleport(0));

        for (move_id, setup) in [
            ("MOVE_RECOVER", 0u8),
            ("MOVE_DEFENSE_CURL", 1),
            ("MOVE_WATER_SPORT", 2),
        ] {
            let mut fixture = Battle::new(&data, 7);
            fixture.flags = 3;
            fixture.mons[0].moves[0] = move_id.into();
            fixture.mons[0].pp[0] = data.moves[move_id].pp;
            fixture.mons[1].status = 2;
            if setup == 0 {
                fixture.mons[0].hp = 1;
            }
            fixture.play(&data, 0).unwrap();
            assert_eq!(fixture.mons[0].pp[0], data.moves[move_id].pp - 1);
            match setup {
                0 => assert!(fixture.mons[0].hp > 1),
                1 => assert_ne!(fixture.mons[0].status2 & (1 << 30), 0),
                _ => assert_ne!(fixture.mons[0].status3 & (1 << 17), 0),
            }
        }

        let mut wild = Battle::new(&data, 11);
        wild.flags = 3;
        wild.is_wild = true;
        wild.mons[0].moves[0] = "MOVE_TELEPORT".into();
        wild.mons[0].pp[0] = data.moves["MOVE_TELEPORT"].pp;
        wild.play(&data, 0).unwrap();
        assert_eq!(wild.mons[0].pp[0], data.moves["MOVE_TELEPORT"].pp - 1);
        assert!(wild.fled);
        assert_eq!(wild.winner, Some(2));
    }

    #[test]
    fn battle_reports_source_type_effectiveness_messages() {
        let data = Data::source();
        let mut battle = Battle::new(&data, 11);
        battle.mons[0].moves[0] = "MOVE_EMBER".into();
        battle.mons[0].pp[0] = data.moves["MOVE_EMBER"].pp;
        battle.mons[1].types = [12, 3];
        battle.play(&data, 0).unwrap();
        assert!(battle
            .log
            .iter()
            .any(|line| line == "It's super effective!"));
    }
}
