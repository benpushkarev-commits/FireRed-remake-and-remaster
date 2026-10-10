//! Native field bytecode interpreter translated from script.c/scrcmd.c.
//! New behaviour UNVERIFIED. Unsupported commands fail at their exact boundary.
use crate::core::{Data as CoreData, State};
use serde::Deserialize;
use std::collections::HashMap;

#[derive(Deserialize)]
pub struct Data {
    pub revision: String,
    pub bytes: Vec<u8>,
    pub labels: HashMap<String, u32>,
    pub imports: HashMap<u32, String>,
    pub handlers: Vec<String>,
    pub std: Vec<u32>,
    pub texts: HashMap<u32, String>,
}
impl Data {
    pub fn source() -> Self {
        serde_json::from_str(include_str!("../data/field.json"))
            .expect("Original field script archive")
    }
    pub fn label(&self, name: &str) -> Result<u32, String> {
        self.labels
            .get(name)
            .copied()
            .ok_or_else(|| format!("Missing original script {name}"))
    }
    pub fn text(&self, address: u32) -> Result<&str, String> {
        self.texts.get(&address).map(String::as_str).ok_or_else(|| {
            format!(
                "Unsupported text pointer: {}",
                self.imports
                    .get(&address)
                    .map_or("unmapped", String::as_str)
            )
        })
    }
}
#[derive(Clone, Debug, PartialEq)]
pub enum Host {
    ApplyMovement {
        id: u16,
        address: u32,
        map: Option<[u8; 2]>,
    },
    WaitMovement {
        id: u16,
        map: Option<[u8; 2]>,
    },
    Object {
        command: String,
        id: u16,
        values: Vec<u16>,
        map: Option<[u8; 2]>,
    },
    PlayerGender,
    Lock(bool),
    Release(bool),
    FacePlayer,
    Message(u32),
    CloseMessage,
    WaitMessage,
    WaitButton,
    Sound(u16, bool),
    WaitSound(bool),
    Delay(u16),
    YesNo(u8, u8),
    Multichoice {
        left: u8,
        top: u8,
        multichoice_id: u8,
        ignore_b: bool,
    },
    Special {
        name: String,
        destination: Option<u16>,
    },
    WaitState,
    Warp {
        map: [u8; 2],
        warp_id: u8,
        x: u16,
        y: u16,
    },
    Door {
        open: bool,
        x: u16,
        y: u16,
    },
    ShowMonPic {
        species: u16,
        x: u8,
        y: u8,
    },
    HideMonPic,
    TrainerBattle {
        mode: u8,
        trainer: u16,
        flags: u16,
    },
    WildBattle,
}
#[derive(Clone, Debug, PartialEq)]
pub enum Step {
    End,
    Host(Host),
    Budget,
}
pub struct Vm {
    pub pc: Option<u32>,
    pub comparison: u8,
    pub locals: [u32; 4],
    pub strings: [String; 3],
    stack: Vec<u32>,
    pub fault: Option<String>,
    pub post_battle_pc: Option<u32>,
    pub battle_mode: u8,
    pub battle_trainer: u16,
    pub battle_flags: u16,
}
impl Vm {
    pub fn start(data: &Data, label: &str) -> Result<Self, String> {
        Ok(Self {
            pc: Some(data.label(label)?),
            comparison: 0,
            locals: [0; 4],
            strings: Default::default(),
            stack: Vec::new(),
            fault: None,
            post_battle_pc: None,
            battle_mode: 0,
            battle_trainer: 0,
            battle_flags: 0,
        })
    }
    pub fn at(data: &Data, address: u32) -> Result<Self, String> {
        if address as usize >= data.bytes.len() {
            return Err("Invalid scene address".into());
        }
        let mut vm = Self::start(data, "EventScript_Return")?;
        vm.pc = Some(address);
        Ok(vm)
    }
    pub fn peek_trainer(&self, data: &Data) -> Option<u16> {
        let pc = self.pc? as usize;
        let opcode = data.bytes.get(pc)?;
        let handler = data.handlers.get(*opcode as usize)?;
        let name = handler.strip_prefix("ScrCmd_").unwrap_or(handler);
        if name == "trainerbattle" {
            let trainer_low = data.bytes.get(pc + 2)?;
            let trainer_high = data.bytes.get(pc + 3)?;
            Some(u16::from(*trainer_low) | (u16::from(*trainer_high) << 8))
        } else {
            None
        }
    }
    fn read(&mut self, data: &Data, n: usize) -> Result<u32, String> {
        let p = self.pc.ok_or("Stopped script read")? as usize;
        let slice = data
            .bytes
            .get(p..p + n)
            .ok_or("Script read outside archive")?;
        let mut value = 0;
        for (i, &b) in slice.iter().enumerate() {
            value |= u32::from(b) << (8 * i);
        }
        self.pc = Some((p + n) as u32);
        Ok(value)
    }
    fn byte(&mut self, data: &Data) -> Result<u8, String> {
        Ok(self.read(data, 1)? as u8)
    }
    fn half(&mut self, data: &Data) -> Result<u16, String> {
        Ok(self.read(data, 2)? as u16)
    }
    fn word(&mut self, data: &Data) -> Result<u32, String> {
        self.read(data, 4)
    }
    fn value(&mut self, data: &Data, state: &State) -> Result<u16, String> {
        state.events.var_get(self.half(data)?)
    }
    fn variable(state: &State, id: u16) -> Result<u16, String> {
        if id < 0x4000 {
            return Err("Invalid variable pointer".into());
        }
        state.events.var_get(id)
    }
    fn write(state: &mut State, id: u16, value: u16) -> Result<(), String> {
        if !state.events.var_set(id, value)? {
            return Err("Invalid variable destination".into());
        }
        Ok(())
    }
    fn result(state: &mut State, value: u16) -> Result<(), String> {
        Self::write(state, 0x800d, value)
    }
    fn jump(&mut self, data: &Data, address: u32, call: bool) -> Result<(), String> {
        if address as usize >= data.bytes.len() {
            return Err(format!(
                "Unsupported script pointer: {}",
                data.imports
                    .get(&address)
                    .map_or("unmapped", String::as_str)
            ));
        }
        // ScriptPush silently refuses the 20th entry, but ScriptCall still jumps.
        if call && self.stack.len() + 1 < 20 {
            self.stack.push(self.pc.ok_or("Stopped script call")?);
        }
        self.pc = if address == 0 { None } else { Some(address) };
        Ok(())
    }
    fn condition(&self, n: u8) -> Result<bool, String> {
        match n {
            0 => Ok(self.comparison == 0),
            1 => Ok(self.comparison == 1),
            2 => Ok(self.comparison == 2),
            3 => Ok(self.comparison <= 1),
            4 => Ok(self.comparison >= 1),
            5 => Ok(self.comparison != 1),
            _ => Err("Invalid comparison condition".into()),
        }
    }
    fn compare(&mut self, a: u16, b: u16) {
        self.comparison = if a < b {
            0
        } else if a == b {
            1
        } else {
            2
        };
    }
    /// Runs source handlers until a host callback/yield/end or a scheduling budget.
    pub fn run(
        &mut self,
        data: &Data,
        core: &CoreData,
        state: &mut State,
        budget: usize,
    ) -> Result<Step, String> {
        if let Some(fault) = &self.fault {
            return Err(fault.clone());
        }
        for _ in 0..budget {
            let Some(pc) = self.pc else {
                return Ok(Step::End);
            };
            let result = self.command(data, core, state);
            match result {
                Ok(Some(host)) => return Ok(Step::Host(host)),
                Ok(None) => {}
                Err(e) => {
                    let label = data
                        .labels
                        .iter()
                        .filter(|(_, a)| **a <= pc)
                        .max_by_key(|(_, a)| **a)
                        .map(|(name, _)| name.as_str())
                        .unwrap_or("unlabelled");
                    let error = format!("Field script stopped at {label}: {e}");
                    self.fault = Some(error.clone());
                    self.pc = None;
                    return Err(error);
                }
            }
        }
        if self.pc.is_none() {
            Ok(Step::End)
        } else {
            Ok(Step::Budget)
        }
    }
    fn command(
        &mut self,
        data: &Data,
        core: &CoreData,
        state: &mut State,
    ) -> Result<Option<Host>, String> {
        let opcode = self.byte(data)?;
        let Some(handler) = data.handlers.get(opcode as usize) else {
            self.pc = None;
            return Ok(None);
        };
        let name = handler.strip_prefix("ScrCmd_").unwrap_or(handler);
        match name {
            "nop" | "nop1" => {}
            "end" => self.pc = None,
            "return" => self.pc = self.stack.pop(),
            "goto" | "call" => {
                let address = self.word(data)?;
                self.jump(data, address, name == "call")?;
            }
            "goto_if" | "call_if" => {
                let c = self.byte(data)?;
                let address = self.word(data)?;
                if self.condition(c)? {
                    self.jump(data, address, name == "call_if")?;
                }
            }
            "gotostd" | "callstd" | "gotostd_if" | "callstd_if" => {
                let condition = if name.ends_with("_if") {
                    Some(self.byte(data)?)
                } else {
                    None
                };
                let index = self.byte(data)? as usize;
                if condition
                    .map(|c| self.condition(c))
                    .transpose()?
                    .unwrap_or(true)
                {
                    // Source ignores a standard-script index beyond gStdScriptsEnd.
                    if let Some(&address) = data.std.get(index) {
                        self.jump(data, address, name.starts_with("call"))?;
                    }
                }
            }
            "loadword" | "loadbyte" => {
                let i = self.byte(data)? as usize;
                let value = if name == "loadword" {
                    self.word(data)?
                } else {
                    u32::from(self.byte(data)?)
                };
                *self.locals.get_mut(i).ok_or("Invalid local index")? = value;
            }
            "copylocal" => {
                let a = self.byte(data)? as usize;
                let b = self.byte(data)? as usize;
                let v = *self.locals.get(b).ok_or("Invalid local index")?;
                *self.locals.get_mut(a).ok_or("Invalid local index")? = v;
            }
            "setvar" | "copyvar" | "setorcopyvar" | "addvar" | "subvar" => {
                let id = self.half(data)?;
                let operand = self.half(data)?;
                let value = match name {
                    "setvar" => operand,
                    "copyvar" => Self::variable(state, operand)?,
                    "setorcopyvar" => state.events.var_get(operand)?,
                    "addvar" => Self::variable(state, id)?.wrapping_add(operand),
                    _ => Self::variable(state, id)?.wrapping_sub(state.events.var_get(operand)?),
                };
                Self::write(state, id, value)?;
            }
            "compare_var_to_value" | "compare_var_to_var" => {
                let id = self.half(data)?;
                let b = self.half(data)?;
                let a = Self::variable(state, id)?;
                self.compare(
                    a,
                    if name.ends_with("_var") {
                        Self::variable(state, b)?
                    } else {
                        b
                    },
                );
            }
            "compare_local_to_local" | "compare_local_to_value" => {
                let i = self.byte(data)? as usize;
                let b = self.byte(data)?;
                let a = *self.locals.get(i).ok_or("Invalid local index")? as u8;
                let b = if name.ends_with("_local") {
                    *self.locals.get(b as usize).ok_or("Invalid local index")? as u8
                } else {
                    b
                };
                self.compare(u16::from(a), u16::from(b));
            }
            "setflag" | "clearflag" | "checkflag" => {
                let id = self.half(data)?;
                match name {
                    "setflag" => {
                        state.events.flag_set(id);
                    }
                    "clearflag" => {
                        state.events.flag_clear(id);
                    }
                    _ => self.comparison = u8::from(state.events.flag_get(id)),
                }
            }
            "settrainerflag" | "cleartrainerflag" | "checktrainerflag" => {
                let id = self.value(data, state)?.wrapping_add(0x500);
                match name {
                    "settrainerflag" => {
                        state.events.flag_set(id);
                    }
                    "cleartrainerflag" => {
                        state.events.flag_clear(id);
                    }
                    _ => self.comparison = u8::from(state.events.flag_get(id)),
                }
            }
            "random" => {
                let max = self.value(data, state)?;
                if max == 0 {
                    return Err("Random modulo zero".into());
                }
                let result = state.rng.next16() % max;
                Self::result(state, result)?;
            }
            "additem" | "removeitem" | "checkitem" | "checkitemspace" => {
                let item = self.value(data, state)?;
                let quantity = u16::from(self.value(data, state)? as u8);
                let result = match name {
                    "additem" => state.bag.add(core, &mut state.events, item, quantity),
                    "removeitem" => state.bag.remove(core, item, quantity),
                    "checkitem" => state.bag.has(core, item, quantity),
                    _ => state.bag.has_space(core, item, quantity),
                };
                Self::result(state, u16::from(result))?;
            }
            "checkitemtype" => {
                let item = self.value(data, state)?;
                let pocket = core.pocket(item).map_or(0, |p| (p as u16) + 1);
                Self::result(state, pocket)?;
            }
            "getpartysize" => Self::result(state, state.party.len() as u16)?,
            "addmoney" | "removemoney" | "checkmoney" => {
                let amount = self.word(data)?;
                let ignore = self.byte(data)?;
                if ignore == 0 {
                    match name {
                        "addmoney" => state.add_money(amount),
                        "removemoney" => state.remove_money(amount),
                        _ => Self::result(state, u16::from(state.money >= amount))?,
                    }
                }
            }
            "textcolor" => {
                let color = u16::from(self.byte(data)?);
                let old = state.events.var_get(0x8012)?;
                Self::write(state, 0x8013, old)?;
                Self::write(state, 0x8012, color)?;
            }
            "bufferitemname" => {
                let i = self.byte(data)? as usize;
                let id = self.value(data, state)?;
                let item = core.items.get(&id).ok_or("Unsupported item name")?;
                let text = item
                    .fields
                    .get("english")
                    .and_then(|v| v.as_str())
                    .ok_or("Missing item name")?;
                *self.strings.get_mut(i).ok_or("Invalid string buffer")? = text.into();
            }
            "bufferitemnameplural" => {
                let i = self.byte(data)? as usize;
                let id = self.value(data, state)?;
                let quantity = self.value(data, state)?;
                let item = core.items.get(&id).ok_or("Unsupported item name")?;
                let text = item
                    .fields
                    .get("english")
                    .and_then(|v| v.as_str())
                    .ok_or("Missing item name")?;
                let mut name = text.to_string();
                let poke_ball_id = core.ids.get("ITEM_POKE_BALL").copied().unwrap_or(4);
                if id == poke_ball_id && quantity >= 2 {
                    name.push('S');
                }
                *self.strings.get_mut(i).ok_or("Invalid string buffer")? = name;
            }
            "bufferstring" => {
                let i = self.byte(data)? as usize;
                let address = self.word(data)?;
                *self.strings.get_mut(i).ok_or("Invalid string buffer")? =
                    data.text(address)?.trim_end_matches('$').into();
            }
            "bufferstdstring" => {
                let i = self.byte(data)? as usize;
                let index = self.value(data, state)?;
                let text = match index {
                    0 => "COOL",
                    1 => "BEAUTY",
                    2 => "CUTE",
                    3 => "SMART",
                    4 => "TOUGH",
                    5 => "COOL",
                    6 => "BEAUTY",
                    7 => "CUTE",
                    8 => "SMART",
                    9 => "TOUGH",
                    10 => "ITEMS",
                    11 => "KEY ITEMS",
                    12 => "POKEMON BALLS",
                    13 => "TMs & HMs",
                    14 => "BERRIES",
                    15 => "BOULDERBADGE",
                    16 => "CASCADEBADGE",
                    17 => "THUNDERBADGE",
                    18 => "RAINBOWBADGE",
                    19 => "SOULBADGE",
                    20 => "MARSHBADGE",
                    21 => "VOLCANOBADGE",
                    22 => "EARTHBADGE",
                    23 => "COINS",
                    24 => "ITEMS POCKET",
                    25 => "KEY ITEMS POCKET",
                    26 => "POKE BALLS POCKET",
                    27 => "TM CASE",
                    28 => "BERRY POUCH",
                    _ => return Err(format!("Unknown std string {index}")),
                };
                *self.strings.get_mut(i).ok_or("Invalid string buffer")? = text.into();
            }
            "applymovement" | "applymovementat" => {
                let id = self.value(data, state)?;
                let address = self.word(data)?;
                let map = if name.ends_with("at") {
                    Some([self.byte(data)?, self.byte(data)?])
                } else {
                    None
                };
                return Ok(Some(Host::ApplyMovement { id, address, map }));
            }
            "waitmovement" | "waitmovementat" => {
                let id = self.value(data, state)?;
                let map = if name.ends_with("at") {
                    Some([self.byte(data)?, self.byte(data)?])
                } else {
                    None
                };
                return Ok(Some(Host::WaitMovement { id, map }));
            }
            "addobject"
            | "addobjectat"
            | "removeobject"
            | "removeobjectat"
            | "hideobjectat"
            | "showobjectat"
            | "setobjectxy"
            | "setobjectxyperm"
            | "copyobjectxytoperm"
            | "turnobject"
            | "setobjectmovementtype" => {
                let id = self.value(data, state)?;
                let mut values = Vec::new();
                if name == "setobjectxy" || name == "setobjectxyperm" {
                    values.push(self.value(data, state)?);
                    values.push(self.value(data, state)?);
                }
                if name == "turnobject" || name == "setobjectmovementtype" {
                    values.push(self.byte(data)? as u16);
                }
                let map = if name.ends_with("at") {
                    Some([self.byte(data)?, self.byte(data)?])
                } else {
                    None
                };
                return Ok(Some(Host::Object {
                    command: name.into(),
                    id,
                    values,
                    map,
                }));
            }
            "setworldmapflag" => {
                let id = self.half(data)?;
                state.events.flag_set(id);
            }
            "setrespawn" => {
                state.respawn = self.value(data, state)?;
            }
            "checkplayergender" => return Ok(Some(Host::PlayerGender)),
            "lock" | "lockall" => return Ok(Some(Host::Lock(name == "lockall"))),
            "release" | "releaseall" => return Ok(Some(Host::Release(name == "releaseall"))),
            "faceplayer" => return Ok(Some(Host::FacePlayer)),
            "message" => {
                let address = self.word(data)?;
                return Ok(Some(Host::Message(if address == 0 {
                    self.locals[0]
                } else {
                    address
                })));
            }
            "closemessage" => return Ok(Some(Host::CloseMessage)),
            "waitmessage" => return Ok(Some(Host::WaitMessage)),
            "waitbuttonpress" => return Ok(Some(Host::WaitButton)),
            "playse" | "playfanfare" => {
                let song = self.half(data)?;
                return Ok(Some(Host::Sound(song, name == "playfanfare")));
            }
            "waitse" | "waitfanfare" => return Ok(Some(Host::WaitSound(name == "waitfanfare"))),
            "delay" => {
                let frames = self.half(data)?;
                return Ok(Some(Host::Delay(frames)));
            }
            "yesnobox" => {
                let x = self.byte(data)?;
                let y = self.byte(data)?;
                return Ok(Some(Host::YesNo(x, y)));
            }
            "multichoice" => {
                let left = self.byte(data)?;
                let top = self.byte(data)?;
                let multichoice_id = self.byte(data)?;
                let ignore_b = self.byte(data)? != 0;
                return Ok(Some(Host::Multichoice {
                    left,
                    top,
                    multichoice_id,
                    ignore_b,
                }));
            }
            "incrementgamestat" => {
                let _stat = self.byte(data)?;
            }
            "dofieldeffect" => {
                let _effect = self.value(data, state)?;
            }
            "waitfieldeffect" => {
                let _effect = self.value(data, state)?;
            }
            "waitstate" => return Ok(Some(Host::WaitState)),
            "special" | "specialvar" => {
                let destination = if name == "specialvar" {
                    Some(self.half(data)?)
                } else {
                    None
                };
                let index = self.half(data)?;
                let table = data.label("gSpecials")? as usize + usize::from(index) * 4;
                let address = data
                    .bytes
                    .get(table..table + 4)
                    .ok_or("Invalid special index")?;
                let address = u32::from_le_bytes(address.try_into().unwrap());
                let name = data
                    .imports
                    .get(&address)
                    .ok_or("Unresolved special")?
                    .clone();
                return Ok(Some(Host::Special { name, destination }));
            }
            "givemon" => {
                let species = self.value(data, state)?;
                let level = self.byte(data)?;
                let item = self.value(data, state)?;
                let _unk1 = self.word(data)?;
                let _unk2 = self.word(data)?;
                let _unk3 = self.byte(data)?;
                let mut mon = crate::core::Pokemon::create(
                    core,
                    &mut state.rng,
                    species,
                    level,
                    32,
                    None,
                    crate::core::OtId::Player,
                    state.trainer_id,
                )?;
                if item != 0 {
                    mon.held_item = item;
                }
                let personality = mon.personality;
                let res = match state.give_mon(core, mon)? {
                    crate::core::GiveMonResult::Party => {
                        state.pokedex.set_caught(species, personality)?;
                        0
                    }
                    crate::core::GiveMonResult::Pc { .. } => {
                        state.pokedex.set_caught(species, personality)?;
                        1
                    }
                    crate::core::GiveMonResult::Full => 2,
                };
                Self::result(state, res)?;
            }
            "bufferspeciesname" => {
                let i = self.byte(data)? as usize;
                let species = self.value(data, state)?;
                let name = core
                    .ids
                    .iter()
                    .find(|(k, v)| **v == species && k.starts_with("SPECIES_"))
                    .map(|(k, _)| k.strip_prefix("SPECIES_").unwrap_or(k))
                    .ok_or_else(|| format!("Unknown species {species}"))?;
                *self.strings.get_mut(i).ok_or("Invalid string buffer")? = name.into();
            }
            "bufferleadmonspeciesname" => {
                let i = self.byte(data)? as usize;
                let species = state
                    .party
                    .first()
                    .map(|m| m.species)
                    .ok_or("Party empty")?;
                let name = core
                    .ids
                    .iter()
                    .find(|(k, v)| **v == species && k.starts_with("SPECIES_"))
                    .map(|(k, _)| k.strip_prefix("SPECIES_").unwrap_or(k))
                    .ok_or_else(|| format!("Unknown species {species}"))?;
                *self.strings.get_mut(i).ok_or("Invalid string buffer")? = name.into();
            }
            "buffernumberstring" => {
                let i = self.byte(data)? as usize;
                let num = self.value(data, state)?;
                *self.strings.get_mut(i).ok_or("Invalid string buffer")? = num.to_string();
            }
            "warp" | "warpsilent" => {
                let group = self.byte(data)?;
                let num = self.byte(data)?;
                let warp_id = self.byte(data)?;
                let x = self.value(data, state)?;
                let y = self.value(data, state)?;
                return Ok(Some(Host::Warp {
                    map: [group, num],
                    warp_id,
                    x,
                    y,
                }));
            }
            "opendoor" | "closedoor" => {
                let x = self.value(data, state)?;
                let y = self.value(data, state)?;
                return Ok(Some(Host::Door {
                    open: name == "opendoor",
                    x,
                    y,
                }));
            }
            "waitdooranim" => {}
            "setdooropen" | "setdoorclosed" => {
                let _x = self.value(data, state)?;
                let _y = self.value(data, state)?;
            }
            "showmonpic" => {
                let species = self.value(data, state)?;
                let x = self.byte(data)?;
                let y = self.byte(data)?;
                return Ok(Some(Host::ShowMonPic { species, x, y }));
            }
            "hidemonpic" => {
                return Ok(Some(Host::HideMonPic));
            }
            "playbgm" => {
                let song = self.half(data)?;
                let _save = self.byte(data)?;
                return Ok(Some(Host::Sound(song, false)));
            }
            "savebgm" => {
                let _song = self.half(data)?;
            }
            "fadedefaultbgm" => {}
            "fadenewbgm" => {
                let _song = self.half(data)?;
            }
            "fadeoutbgm" | "fadeinbgm" | "fadescreen" => {
                let _mode = self.byte(data)?;
            }
            "fadescreenspeed" => {
                let _mode = self.byte(data)?;
                let _speed = self.byte(data)?;
            }
            "trainerbattle" => {
                let mode = self.byte(data)?;
                let trainer = self.half(data)?;
                let flags = self.half(data)?;
                self.battle_mode = mode;
                match mode {
                    9 => {
                        let _defeat = self.word(data)?;
                        let _victory = self.word(data)?;
                        self.battle_trainer = trainer;
                        self.battle_flags = flags;
                        self.post_battle_pc = self.pc;
                        self.pc = Some(data.label("EventScript_DoNoIntroTrainerBattle")?);
                    }
                    0 | 4 => {
                        let _defeat = self.word(data)?;
                        let _extra = if mode == 0 { self.word(data)? } else { 0 };
                        self.battle_trainer = trainer;
                        self.battle_flags = flags;
                        self.post_battle_pc = self.pc;
                        self.pc = Some(data.label("EventScript_DoNoIntroTrainerBattle")?);
                    }
                    1 | 2 => {
                        let _defeat = self.word(data)?;
                        let _victory = self.word(data)?;
                        let post = self.word(data)?;
                        self.battle_trainer = trainer;
                        self.battle_flags = flags;
                        self.post_battle_pc = Some(post);
                        self.pc = Some(data.label("EventScript_DoNoIntroTrainerBattle")?);
                    }
                    _ => return Err(format!("Unported trainer battle mode {mode}")),
                }
            }
            "dotrainerbattle" => {
                return Ok(Some(Host::TrainerBattle {
                    mode: self.battle_mode,
                    trainer: self.battle_trainer,
                    flags: self.battle_flags,
                }));
            }
            "gotopostbattlescript" => {
                self.pc = self.post_battle_pc.take();
            }
            _ => return Err(format!("Unported original command {handler}")),
        }
        Ok(None)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::State;

    #[test]
    fn starter_choice_script_gives_mon_and_buffers_species() {
        let field = Data::source();
        let core = crate::core::Data::source();
        // (starter_num, player_species, rival_species, rival_ball, expected_rival_name)
        let choices = [
            (0u16, 1u16, 4u16, 7u16, "CHARMANDER"),
            (1u16, 7u16, 1u16, 5u16, "BULBASAUR"),
            (2u16, 4u16, 7u16, 6u16, "SQUIRTLE"),
        ];
        for (num, player_species, rival_species, rival_ball, expected_rival_name) in choices {
            let mut state = State::new(&core, 42, 1234);
            state.events.var_set(0x4001, num).unwrap();
            state.events.var_set(0x4002, player_species).unwrap();
            state.events.var_set(0x4003, rival_species).unwrap();
            state.events.var_set(0x4004, rival_ball).unwrap();
            let mut vm = Vm::start(
                &field,
                "PalletTown_ProfessorOaksLab_EventScript_ChoseStarter",
            )
            .unwrap();
            for _ in 0..128 {
                match vm.run(&field, &core, &mut state, 32).unwrap() {
                    Step::End => break,
                    _ => {}
                }
            }
            assert_eq!(state.party.len(), 1);
            assert_eq!(state.party[0].species, player_species);
            assert_eq!(state.party[0].level, 5);
            assert!(!state.party[0].moves.is_empty());
            assert!(state.pokedex.seen(player_species));
            assert!(state.pokedex.caught(player_species));
            assert_eq!(vm.strings[0], expected_rival_name);
            assert!(state.events.flag_get(0x828)); // FLAG_SYS_POKEMON_GET
        }
    }

    #[test]
    fn early_rival_battle_preserves_source_mode_and_flags() {
        let field = Data::source();
        let core = crate::core::Data::source();
        let mut state = State::new(&core, 42, 1234);
        let mut vm = Vm::start(
            &field,
            "PalletTown_ProfessorOaksLab_EventScript_RivalBattleSquirtle",
        )
        .unwrap();
        for _ in 0..32 {
            if let Step::Host(Host::TrainerBattle {
                mode,
                trainer,
                flags,
            }) = vm.run(&field, &core, &mut state, 32).unwrap()
            {
                assert_eq!(mode, 9); // TRAINER_BATTLE_EARLY_RIVAL
                assert_eq!(trainer, 326); // TRAINER_RIVAL_OAKS_LAB_SQUIRTLE
                assert_eq!(flags, 3); // RIVAL_BATTLE_TUTORIAL
                return;
            }
        }
        panic!("Rival script did not reach dotrainerbattle");
    }

    #[test]
    fn viridian_mart_parcel_scene_gives_oaks_parcel_and_sets_vars() {
        let field = Data::source();
        let core = crate::core::Data::source();
        let mut state = State::new(&core, 42, 1234);
        let mut vm = Vm::start(&field, "ViridianCity_Mart_EventScript_ParcelScene").unwrap();
        let mut steps = 0;
        loop {
            match vm.run(&field, &core, &mut state, 32).unwrap() {
                Step::End => break,
                Step::Budget => {}
                Step::Host(_host) => {}
            }
            steps += 1;
            assert!(steps < 200, "Script took too many steps");
        }
        let oaks_parcel = core.ids["ITEM_OAKS_PARCEL"];
        assert!(state.bag.has(&core, oaks_parcel, 1));
        assert_eq!(state.events.var_get(0x4057).unwrap(), 1); // VAR_MAP_SCENE_VIRIDIAN_CITY_MART
        assert_eq!(state.events.var_get(0x4055).unwrap(), 5); // VAR_MAP_SCENE_PALLET_TOWN_PROFESSOR_OAKS_LAB

        // Now run Oak's lab dex scene with the parcel delivered
        state.events.var_set(0x800c, 2).unwrap(); // VAR_FACING = DIR_NORTH
        let mut vm_oak =
            Vm::start(&field, "PalletTown_ProfessorOaksLab_EventScript_ProfOak").unwrap();
        let mut steps = 0;
        loop {
            match vm_oak.run(&field, &core, &mut state, 32).unwrap() {
                Step::End => break,
                Step::Budget => {}
                Step::Host(_host) => {}
            }
            steps += 1;
            assert!(steps < 500, "Oak script took too many steps");
        }
        assert!(!state.bag.has(&core, oaks_parcel, 1));
        assert!(state.events.flag_get(0x829)); // FLAG_SYS_POKEDEX_GET
        let poke_ball = core.ids["ITEM_POKE_BALL"];
        assert!(state.bag.has(&core, poke_ball, 5));
        assert_eq!(state.events.var_get(0x4055).unwrap(), 6); // VAR_MAP_SCENE_PALLET_TOWN_PROFESSOR_OAKS_LAB
        assert_eq!(state.events.var_get(0x4057).unwrap(), 2); // VAR_MAP_SCENE_VIRIDIAN_CITY_MART
        assert_eq!(state.events.var_get(0x4051).unwrap(), 1); // VAR_MAP_SCENE_VIRIDIAN_CITY_OLD_MAN
        assert_eq!(state.events.var_get(0x4058).unwrap(), 1); // VAR_MAP_SCENE_PALLET_TOWN_RIVALS_HOUSE
        assert_eq!(state.events.var_get(0x4054).unwrap(), 1); // VAR_MAP_SCENE_ROUTE22
    }

    #[test]
    fn viridian_pokemon_center_nurse_heals_party() {
        let field = Data::source();
        let core = crate::core::Data::source();
        let mut state = State::new(&core, 42, 1234);
        let charmander = core.ids["SPECIES_CHARMANDER"];
        let mut mon = crate::core::Pokemon::create(
            &core,
            &mut state.rng,
            charmander,
            5,
            32,
            None,
            crate::core::OtId::Player,
            state.trainer_id,
        )
        .unwrap();
        let max_hp = mon.hp;
        mon.hp = 1; // Injured
        mon.pp[0] = 0; // Depleted move PP
        state.add_party(mon).unwrap();

        let mut vm = Vm::start(&field, "ViridianCity_PokemonCenter_1F_EventScript_Nurse").unwrap();
        let mut steps = 0;
        loop {
            match vm.run(&field, &core, &mut state, 32).unwrap() {
                Step::End => break,
                Step::Budget => {}
                Step::Host(host) => {
                    match host {
                        Host::Multichoice { .. } => {
                            // Player selects "Yes" (choice index 0)
                            state.events.var_set(0x800d, 0).unwrap();
                        }
                        Host::Special { name, destination } => {
                            if name == "HealPlayerParty" {
                                state.heal_party(&core).unwrap();
                            } else if name == "IsPlayerNotInTrainerTowerLobby" {
                                let dest = destination.unwrap_or(0x800d);
                                state.events.var_set(dest, 1).unwrap();
                            } else if name == "BufferUnionRoomPlayerName" {
                                let dest = destination.unwrap_or(0x800d);
                                state.events.var_set(dest, 0).unwrap();
                            }
                        }
                        _ => {}
                    }
                }
            }
            steps += 1;
            assert!(steps < 200, "Nurse script took too many steps");
        }
        assert_eq!(state.party[0].hp, max_hp);
        assert!(state.party[0].pp[0] > 0);
    }
}
