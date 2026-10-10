//! Basic native integration check, deliberately not a behavioural parity oracle.
use firered_native_prototype::core::{Data, GiveMonResult, OtId, Pokemon, State};
fn main() -> Result<(), String> {
    let data = Data::source();
    let mut state = State::new(&data, 42, 1234);
    for species in data.species.keys() {
        for level in [1, 5, 50, 100] {
            let mon = Pokemon::create(
                &data,
                &mut state.rng,
                *species,
                level,
                32,
                None,
                OtId::Player,
                state.trainer_id,
            )?;
            if mon.level != level || mon.hp == 0 {
                return Err("Creation integration failed".into());
            }
        }
    }
    let mon = Pokemon::create(
        &data,
        &mut state.rng,
        data.id("SPECIES_CHARMANDER")?,
        5,
        32,
        None,
        OtId::Player,
        state.trainer_id,
    )?;
    state.add_party(mon.clone())?;
    for _ in 1..6 {
        if state.give_mon(&data, mon.clone())? != GiveMonResult::Party {
            return Err("Party insertion integration failed".into());
        }
    }
    if state.give_mon(&data, mon)?
        != (GiveMonResult::Pc {
            box_id: 0,
            box_position: 0,
        })
    {
        return Err("PC storage routing integration failed".into());
    }
    let potion = data.id("ITEM_POTION")?;
    let berry = data.id("ITEM_ORAN_BERRY")?;
    if !state.bag.add(&data, &mut state.events, potion, 1)
        || !state.bag.add(&data, &mut state.events, berry, 1)
        || !state.bag.has(&data, data.id("ITEM_BERRY_POUCH")?, 1)
        || !state.events.flag_get(0x847)
    {
        return Err("Inventory integration failed".into());
    }
    state.events.var_set(0x4001, 7)?;
    state.events.flag_set(0x230);
    state.events.flag_set(0x4000);
    let path = std::env::current_dir()
        .map_err(|e| e.to_string())?
        .join(format!("firered-core-check-{}.json", std::process::id()));
    let result: Result<(), String> = (|| {
        state.save(&path)?;
        state.save(&path)?;
        let loaded = State::load(&data, &path)?;
        if loaded.party.len() != 6
            || loaded.storage.boxes[0][0].is_none()
            || !loaded.bag.has(&data, potion, 1)
            || loaded.events.var_get(0x4001)? != 7
            || !loaded.events.flag_get(0x230)
            || loaded.events.flag_get(0x4000)
            || loaded.rng.state != state.rng.state
        {
            return Err("Native persistence integration failed".into());
        }
        Ok(())
    })();
    let _ = std::fs::remove_file(path);
    result?;
    let field = firered_native_prototype::field::Data::source();
    let mut fresh = State::new(&data, 42, 1234);
    let mut reset =
        firered_native_prototype::field::Vm::start(&field, "EventScript_ResetAllMapFlags")?;
    if reset.run(&field, &data, &mut fresh, 512)? != firered_native_prototype::field::Step::End {
        return Err("Original map initialization did not finish".into());
    }
    for pass in 0..2 {
        let mut script =
            firered_native_prototype::field::Vm::start(&field, "Route1_EventScript_MartClerk")?;
        let mut ended = false;
        let mut messages = 0;
        for _ in 0..128 {
            match script.run(&field, &data, &mut fresh, 256)? {
                firered_native_prototype::field::Step::End => {
                    ended = true;
                    break;
                }
                firered_native_prototype::field::Step::Host(
                    firered_native_prototype::field::Host::Message(address),
                ) => {
                    field.text(address)?;
                    messages += 1;
                }
                firered_native_prototype::field::Step::Host(
                    firered_native_prototype::field::Host::Special { name, .. },
                ) => {
                    return Err(format!("Unported integration dependency {name}"));
                }
                _ => {}
            }
        }
        if !ended
            || !fresh.events.flag_get(0x230)
            || !fresh.bag.has(&data, potion, 1)
            || (pass == 1 && messages != 1)
        {
            return Err("Original clerk script integration failed".into());
        }
    }
    println!(
        "Original field data/VM startup passed: reset-map script and both clerk branches. New behaviour unverified against original C."
    );
    println!(
        "Core startup/integration passed: {} species entries, {} moves, {} item definitions. New behaviour unverified against original C.",
        data.species.len(),
        data.moves.len(),
        data.items.len()
    );
    Ok(())
}
