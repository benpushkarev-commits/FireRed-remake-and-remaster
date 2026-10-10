//! Imported wild tables and source-based encounter primitives.
//! Source: pokefirered/src/wild_encounter.c. Regular land walking only;
//! water, roamers, Unown and map-header overrides remain unsupported.
use serde::Deserialize;
use std::collections::HashMap;

#[derive(Clone, Debug, Deserialize)]
pub struct WildSlot {
    pub species: String,
    pub min_level: u8,
    pub max_level: u8,
}

#[derive(Clone, Debug, Deserialize)]
pub struct WildGroup {
    pub rate: u8,
    pub slots: Vec<WildSlot>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct MapEncounters {
    pub land: Option<WildGroup>,
    pub water: Option<WildGroup>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Data {
    pub revision: String,
    pub encounters: HashMap<String, MapEncounters>,
}

/// DoWildEncounterRateTest / ApplyFluteEncounterRateMod in source order.
/// Ability values are GetAbilityEncounterRateModType: 1 Stench, 2 Illuminate.
/// Flute values are GetFluteEncounterRateModType: 1 white, 2 black.
#[derive(Clone, Copy, Debug, Default)]
pub struct RateModifiers {
    pub cycling: bool,
    pub rate_buff: u16,
    pub flute: u8,
    pub cleanse_tag: bool,
    pub ability: u8,
}

impl RateModifiers {
    /// GetAbilityEncounterRateModType uses party slot zero even if fainted.
    /// Native Pokemon currently cannot represent eggs; egg handling is deferred.
    pub fn from_party(data: &crate::core::Data, state: &crate::core::State) -> Self {
        let lead = state.party.first();
        let ability = lead
            .and_then(|p| {
                data.species
                    .get(&p.species)
                    .and_then(|s| s.abilities.get(usize::from(p.ability_num)))
            })
            .copied();
        Self {
            flute: if state.events.flag_get(0x803) {
                1
            } else if state.events.flag_get(0x804) {
                2
            } else {
                0
            },
            cleanse_tag: lead.is_some_and(|p| p.held_item == 190),
            ability: match ability {
                Some(1) => 1,
                Some(35) => 2,
                _ => 0,
            },
            // Cycling has no native avatar state yet; running is not cycling.
            ..Self::default()
        }
    }

    pub fn threshold(self, base_rate: u8, ignore_ability: bool) -> u16 {
        let mut rate = u32::from(base_rate) * 16;
        if self.cycling {
            rate = rate * 80 / 100;
        }
        rate = rate.wrapping_add(u32::from(self.rate_buff) * 16 / 200);
        match self.flute {
            1 => rate = rate.wrapping_add(rate / 2),
            2 => rate /= 2,
            _ => {}
        }
        if self.cleanse_tag {
            rate = rate.wrapping_mul(2) / 3;
        }
        if !ignore_ability {
            match self.ability {
                1 => rate /= 2,
                2 => rate = rate.wrapping_mul(2),
                _ => {}
            }
        }
        rate.min(1600) as u16
    }
}

/// ChooseWildMonLevel consumes Random() even when the bounds are equal.
pub fn choose_level<F: FnMut() -> u32>(slot: &WildSlot, rng: &mut F) -> u8 {
    let lo = slot.min_level.min(slot.max_level);
    let hi = slot.min_level.max(slot.max_level);
    lo + (rng() % (u32::from(hi) - u32::from(lo) + 1)) as u8
}

/// Transient separate RNG and counters from WildEncounterData (not save data).
#[derive(Clone, Copy, Debug, Default)]
pub struct EncounterState {
    pub rng_state: u32,
    pub rate_buff: u16,
    pub steps_since_encounter: u8,
    pub previous_behavior: u16,
}

/// Host supplies the first conscious non-egg party member's level for Repel.
#[derive(Clone, Copy, Debug, Default)]
pub struct LandStep {
    pub behavior: u16,
    pub eligible_terrain: bool,
    pub disabled: bool,
    pub repel_steps: u16,
    pub repel_lead_level: Option<u8>,
    pub modifiers: RateModifiers,
}

/// MetatileBehavior_IsForcedMovementTile excludes these steps from Repel use.
pub fn forced_movement(behavior: u16) -> bool {
    matches!(behavior, 0x40..=0x48 | 0x50..=0x57 | 0x13 | 0x23)
}

/// GenerateWildMon -> CreateMonWithNature. Unown requires its separate generator.
pub fn create_land_mon(
    data: &crate::core::Data,
    state: &mut crate::core::State,
    name: &str,
    level: u8,
) -> Result<crate::core::Pokemon, String> {
    let species = data.id(&format!("SPECIES_{}", name.to_ascii_uppercase()))?;
    if species == 201 || (252..=276).contains(&species) {
        return Err("Unown generation is not ported".into());
    }
    let nature = u32::from(state.rng.next16() % 25);
    let personality = loop {
        let p = state.rng.next32();
        if p % 25 == nature {
            break p;
        }
    };
    crate::core::Pokemon::create(
        data,
        &mut state.rng,
        species,
        level,
        32,
        Some(personality),
        crate::core::OtId::Player,
        state.trainer_id,
    )
}

impl EncounterState {
    /// SeedWildEncounterRng resets modifiers as well as its private RNG.
    pub fn seeded(seed: u16) -> Self {
        Self {
            rng_state: u32::from(seed),
            ..Default::default()
        }
    }

    pub fn rate_roll(&mut self, threshold: u16) -> bool {
        // WildEncounterRandom uses ISO_RANDOMIZE2, unlike the main Random().
        self.rng_state = self.rng_state.wrapping_mul(1103515245).wrapping_add(12345);
        (self.rng_state >> 16) % 1600 < u32::from(threshold)
    }

    pub fn reset_modifiers(&mut self) {
        self.rate_buff = 0;
        self.steps_since_encounter = 0;
    }

    pub fn failed_rate(&mut self, base_rate: u8, repel_steps: u16) {
        // AddToWildEncounterRateBuff clears rather than accumulates under Repel.
        self.rate_buff = if repel_steps == 0 {
            self.rate_buff.wrapping_add(u16::from(base_rate))
        } else {
            0
        };
    }

    /// HandleWildEncounterCooldown. None means no encounter table/terrain.
    pub fn cooldown_allows<F: FnMut() -> u32>(
        &mut self,
        base_rate: Option<u8>,
        modifiers: RateModifiers,
        rng: &mut F,
    ) -> bool {
        let Some(rate) = base_rate else { return false };
        let base_steps = if rate >= 80 {
            0
        } else if rate < 10 {
            8
        } else {
            8 - rate / 10
        };
        let mut min_steps = u32::from(base_steps) * 256;
        let mut early_rate = 5 * 256;
        match modifiers.flute {
            1 => {
                min_steps -= min_steps / 2;
                early_rate += early_rate / 2;
            }
            2 => {
                min_steps *= 2;
                early_rate /= 2;
            }
            _ => {}
        }
        if modifiers.cleanse_tag {
            min_steps += min_steps / 3;
            early_rate -= early_rate / 3;
        }
        match modifiers.ability {
            1 => {
                min_steps *= 2;
                early_rate /= 2;
            }
            2 => {
                min_steps /= 2;
                early_rate *= 2;
            }
            _ => {}
        }
        if u32::from(self.steps_since_encounter) >= min_steps / 256 {
            return true;
        }
        self.steps_since_encounter = self.steps_since_encounter.wrapping_add(1);
        rng() % 100 < early_rate / 256
    }
}

impl Data {
    pub fn source() -> Self {
        serde_json::from_str(include_str!("../data/encounters.json"))
            .expect("Imported encounter data")
    }

    /// Regular land branch of TryStandardWildEncounter/StandardWildEncounter.
    /// Main Random() and the private encounter RNG are intentionally separate.
    /// Roamers, Unown generation and map-header overrides remain unsupported.
    pub fn land_step<F: FnMut() -> u32>(
        &self,
        map: &str,
        state: &mut EncounterState,
        step: LandStep,
        mut random: F,
    ) -> Option<(String, u8)> {
        let previous = state.previous_behavior;
        state.previous_behavior = step.behavior;
        let land = self.encounters.get(map).and_then(|m| m.land.as_ref());
        let base_rate = land.filter(|_| step.eligible_terrain).map(|g| g.rate);
        if !state.cooldown_allows(base_rate, step.modifiers, &mut random) || step.disabled {
            return None;
        }
        let land = land?;
        // Original tables have exactly 12 slots; reject bad imports explicitly.
        if land.slots.len() != 12 {
            return None;
        }
        if previous != step.behavior && random() % 100 >= 60 {
            return None;
        }
        let modifiers = RateModifiers {
            rate_buff: state.rate_buff,
            ..step.modifiers
        };
        if !state.rate_roll(modifiers.threshold(land.rate, false)) {
            state.failed_rate(land.rate, step.repel_steps);
            return None;
        }
        let roll = random() % 100;
        let boundaries = [20, 40, 50, 60, 70, 80, 85, 90, 94, 98, 99, 100];
        let index = boundaries.iter().position(|&end| roll < end).unwrap();
        let slot = &land.slots[index];
        let level = choose_level(slot, &mut random);
        if step.repel_steps != 0 && !step.repel_lead_level.is_some_and(|lead| level >= lead) {
            state.failed_rate(land.rate, step.repel_steps);
            return None;
        }
        state.reset_modifiers();
        Some((slot.species.clone(), level))
    }

    /// Roll land encounter for given map.
    /// Returns Some((species_name, level)) if an encounter triggered.
    pub fn roll_land<F: FnMut() -> u32>(&self, map: &str, mut rng: F) -> Option<(String, u8)> {
        let map_data = self.encounters.get(map)?;
        let land = map_data.land.as_ref()?;
        if land.slots.is_empty() {
            return None;
        }

        // Compatibility adapter: the host currently supplies a shared RNG.
        // FireRed uses WildEncounterRandom for this test and Random for slots.
        let roll = rng() % 1600;
        if roll >= u32::from(RateModifiers::default().threshold(land.rate, false)) {
            return None;
        }

        // Slot roll (0..99)
        let slot_roll = (rng() % 100) as u8;
        let slot_idx = match slot_roll {
            0..=19 => 0,
            20..=39 => 1,
            40..=49 => 2,
            50..=59 => 3,
            60..=69 => 4,
            70..=79 => 5,
            80..=84 => 6,
            85..=89 => 7,
            90..=93 => 8,
            94..=97 => 9,
            98 => 10,
            _ => 11,
        };
        let slot = &land.slots[slot_idx.min(land.slots.len() - 1)];
        let level = choose_level(slot, &mut rng);

        Some((slot.species.clone(), level))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_modifiers_use_slot_zero_and_white_flute_priority() {
        let core = crate::core::Data::source();
        let mut state = crate::core::State::new(&core, 42, 1234);
        let mut mon = crate::core::Pokemon::create(
            &core,
            &mut state.rng,
            core.ids["SPECIES_GRIMER"],
            10,
            31,
            Some(0),
            crate::core::OtId::Player,
            state.trainer_id,
        )
        .unwrap();
        mon.hp = 0;
        mon.held_item = core.ids["ITEM_CLEANSE_TAG"];
        state.party.push(mon);
        state.events.flag_set(0x803);
        state.events.flag_set(0x804);
        let modifiers = RateModifiers::from_party(&core, &state);
        assert_eq!(modifiers.ability, 1);
        assert_eq!(modifiers.flute, 1);
        assert!(modifiers.cleanse_tag);
        assert!(!modifiers.cycling);
    }

    #[test]
    fn wild_creation_retains_nature_random_ivs_and_native_identity() {
        let core = crate::core::Data::source();
        let mut state = crate::core::State::new(&core, 42, 1234);
        let mut expected_rng = state.rng.clone();
        let nature = u32::from(expected_rng.next16() % 25);
        let personality = loop {
            let p = expected_rng.next32();
            if p % 25 == nature {
                break p;
            }
        };
        let expected = crate::core::Pokemon::create(
            &core,
            &mut expected_rng,
            16,
            4,
            32,
            Some(personality),
            crate::core::OtId::Player,
            state.trainer_id,
        )
        .unwrap();
        let wild = create_land_mon(&core, &mut state, "Pidgey", 4).unwrap();
        assert_eq!(wild.personality, personality);
        assert_eq!(wild.ivs, expected.ivs);
        assert_eq!(wild.moves, expected.moves);
        assert_eq!(wild.ot_id, state.trainer_id);
        assert_eq!(state.rng.calls, expected_rng.calls);
        let before = state.rng.calls;
        assert!(create_land_mon(&core, &mut state, "UNOWN", 4).is_err());
        assert!(create_land_mon(&core, &mut state, "MISSING", 4).is_err());
        assert_eq!(state.rng.calls, before);
    }

    #[test]
    fn repel_expiry_original_script_reaches_message_and_end() {
        use crate::field::{Host, Step, Vm};
        let core = crate::core::Data::source();
        let data = crate::field::Data::source();
        let mut state = crate::core::State::new(&core, 42, 1234);
        state.events.var_set(0x4020, 1).unwrap();
        assert!(state.events.update_repel_counter(false, false).unwrap());
        let mut vm = Vm::start(&data, "EventScript_RepelWoreOff").unwrap();
        let mut message = false;
        let mut ended = false;
        for _ in 0..32 {
            match vm.run(&data, &core, &mut state, 128).unwrap() {
                Step::Host(Host::Message(address)) => {
                    assert!(data.text(address).unwrap().contains("REPEL"));
                    message = true;
                }
                Step::End => {
                    ended = true;
                    break;
                }
                _ => {}
            }
        }
        assert!(message && ended);
        assert!(!state.events.update_repel_counter(false, false).unwrap());
    }

    #[test]
    fn imported_encounter_attributes_cover_cave_land_without_grass_behavior() {
        let world = crate::World::load(std::path::Path::new("data/world.json")).unwrap();
        let cave = world.map("rock_tunnel_1f");
        assert!(
            cave.encounter_types
                .iter()
                .zip(&cave.behaviors)
                .any(|(&kind, &behavior)| kind == 1 && behavior != 2)
        );
        let route = world.map("route1");
        assert!(route.encounter_types.contains(&1));
        assert_eq!(route.encounter_type(crate::Cell(-1, -1)), 0);
        let mut broken = world.clone();
        broken.maps[0].encounter_types.pop();
        assert!(broken.validate().is_err());
    }

    #[test]
    fn regular_land_step_preserves_gates_repel_and_rng_separation() {
        let data = Data::source();
        let mut state = EncounterState::seeded(0);
        state.steps_since_encounter = 6;
        let mut calls = 0;
        assert!(
            data.land_step(
                "route1",
                &mut state,
                LandStep {
                    behavior: 2,
                    eligible_terrain: true,
                    ..Default::default()
                },
                || {
                    calls += 1;
                    99
                }
            )
            .is_none()
        );
        assert_eq!(state.rng_state, 0); // terrain gate uses main RNG only
        assert_eq!(calls, 1);
        // With stable terrain and seed zero, first private rate roll is zero.
        let step = LandStep {
            behavior: 2,
            eligible_terrain: true,
            repel_steps: 5,
            repel_lead_level: Some(100),
            ..Default::default()
        };
        assert!(data.land_step("route1", &mut state, step, || 0).is_none());
        assert_eq!(state.rng_state, 12345);
        assert_eq!(state.rate_buff, 0);
        assert_eq!(state.steps_since_encounter, 6);
        state.rng_state = 0;
        let result = data.land_step(
            "route1",
            &mut state,
            LandStep {
                repel_steps: 0,
                ..step
            },
            || 0,
        );
        assert!(result.is_some());
        assert_eq!(state.steps_since_encounter, 0);
    }

    #[test]
    fn cooldown_stops_consuming_random_after_required_steps() {
        let mut state = EncounterState::seeded(0);
        let mut calls = 0;
        let mut rng = || {
            calls += 1;
            99
        };
        assert!(!state.cooldown_allows(None, Default::default(), &mut rng));
        for _ in 0..6 {
            assert!(!state.cooldown_allows(Some(21), Default::default(), &mut rng));
        }
        assert!(state.cooldown_allows(Some(21), Default::default(), &mut rng));
        assert_eq!(calls, 6);
        state.failed_rate(21, 0);
        assert_eq!(state.rate_buff, 21);
        state.failed_rate(21, 1);
        assert_eq!(state.rate_buff, 0);
        state.reset_modifiers();
        assert_eq!(state.steps_since_encounter, 0);
    }

    #[test]
    fn encounter_rate_rng_is_private_and_advances_even_at_zero_threshold() {
        let mut state = EncounterState::seeded(0);
        assert!(!state.rate_roll(0));
        assert_eq!(state.rng_state, 12345);
        assert!(state.rate_roll(1600));
        assert_eq!(state.rng_state, 3554416254);
    }

    #[test]
    fn fixed_level_still_consumes_random_and_reversed_bounds_are_supported() {
        let mut calls = 0;
        let mut rng = || {
            calls += 1;
            7
        };
        let mut slot = WildSlot {
            species: "Pidgey".into(),
            min_level: 5,
            max_level: 5,
        };
        assert_eq!(choose_level(&slot, &mut rng), 5);
        slot.min_level = 8;
        slot.max_level = 4;
        assert_eq!(choose_level(&slot, &mut rng), 6);
        assert_eq!(calls, 2);
    }

    #[test]
    fn rate_modifiers_preserve_source_integer_order_and_cap() {
        let modifiers = RateModifiers {
            cycling: true,
            rate_buff: 21,
            flute: 1,
            cleanse_tag: true,
            ability: 1,
        };
        // 336 -> 268 -> 269 -> 403 -> 268 -> 134.
        assert_eq!(modifiers.threshold(21, false), 134);
        assert_eq!(modifiers.threshold(21, true), 268);
        assert_eq!(
            RateModifiers {
                ability: 2,
                ..Default::default()
            }
            .threshold(80, false),
            1600
        );
    }

    #[test]
    fn encounter_tables_load_and_route1_has_land_encounters() {
        let data = Data::source();
        assert!(data.encounters.len() >= 100);
        let route1 = data.encounters.get("route1").expect("route1 encounters");
        let land = route1.land.as_ref().expect("route1 land mons");
        assert_eq!(land.rate, 21);
        assert_eq!(land.slots.len(), 12);
        for slot in &land.slots {
            assert!(["Pidgey", "Rattata"].contains(&slot.species.as_str()));
            assert!(slot.min_level >= 2 && slot.max_level <= 5);
        }
    }

    #[test]
    fn roll_land_respects_encounter_chance() {
        let data = Data::source();
        // High roll (>= 21 * 16 = 336) should not trigger encounter
        let no_enc = data.roll_land("route1", || 500);
        assert!(no_enc.is_none());

        // Low roll (< 336) should trigger encounter with valid species and level
        let enc = data.roll_land("route1", || 10);
        assert!(enc.is_some());
        let (species, level) = enc.unwrap();
        assert!(["Pidgey", "Rattata"].contains(&species.as_str()));
        assert!(level >= 2 && level <= 5);
    }
}
