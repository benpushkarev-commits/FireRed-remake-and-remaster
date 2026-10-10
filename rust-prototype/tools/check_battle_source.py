"""Compile ORIGINAL C AI handlers and the physical damage branch as an oracle.

Host adapters provide only this fixture's state (Normal moves, no held items,
badges, weather, status, switches or doubles). This is not a whole-ROM oracle.
The original AI selection/processing functions and reached handlers are copied
verbatim at authoring time. Pointers encode offsets into the original-sized AI
bytecode so 64-bit hosts retain the GBA script operand widths.
"""
import hashlib
import json
import re
import subprocess
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / 'reference/pokefirered'
OUT = ROOT / 'target/battle-oracle'
OUT.mkdir(parents=True, exist_ok=True)
D = json.loads((ROOT / 'data/battle.json').read_text())
assert subprocess.check_output(['git','-C',str(REF),'rev-parse','HEAD'],text=True).strip()==D['revision']
AI = (REF / 'src/battle_ai_script_commands.c').read_text()
POKE = (REF / 'src/pokemon.c').read_text()
SCRIPT = (REF / 'src/battle_script_commands.c').read_text()

def braced(text, start):
    begin = text.index('{', start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def function(text, name):
    m = re.search(r'^(?:static (?:inline )?)?\w+ ' + name + r'\([^;]*?\)\s*\{', text, re.M)
    assert m, name
    return braced(text, m.start())

constants = {}
for file in list((REF / 'include/constants').glob('*.h')) + list((REF / 'include').glob('*.h')):
    for key, value in re.findall(r'^#define[ \t]+(\w+)[ \t]+([^\n]+)', file.read_text(), re.M):
        value = re.sub(r'/\*.*?\*/', '', value.split('//')[0]).strip()
        constants[key] = value
constants.update(dict(AI_ACTION_DONE='1', AI_ACTION_FLEE='2', AI_ACTION_WATCH='4', AI_ACTION_DO_NOT_ATTACK='8',
    AI_ACTION_UNK5='16', AI_ACTION_UNK6='32', AI_ACTION_UNK7='64', AI_ACTION_UNK8='128',
    MAX_MON_MOVES='4', TRUE='1', FALSE='0', AIState_SettingUp='0', AIState_Processing='1',
    AIState_FinishedProcessing='2', AIState_DoNotProcess='3', STAT_ATK='1', STAT_DEF='2', DEFAULT_STAT_STAGE='6'))
def number(value):
    if value in constants: return number(constants[value])
    if re.fullmatch(r'[A-Z][A-Za-z_0-9]*', value): raise ValueError('Unknown constant: ' + value)
    expression = re.sub(r'\b[A-Z][A-Za-z_0-9]*\b', lambda m: str(number(m[0])), value)
    expression = re.sub(r'(?<=\d)[uUlL]+\b', '', expression)
    if not re.fullmatch(r'[\dxa-fA-F() <>|&+*/~\-]+', expression): raise ValueError(value)
    return int(eval(expression, {'__builtins__': {}}, {}))

# Encodings are the widths consumed by the original C handlers.
ops = {
    'get_how_powerful_move_is': ('get_how_powerful_move_is', ''),
    'get_ability': ('get_ability', 'b'), 'if_equal': ('if_equal', 'bp'),
    'if_not_equal': ('if_not_equal', 'bp'), 'if_type_effectiveness': ('if_type_effectiveness', 'bp'),
    'if_effect': ('if_effect', 'bp'), 'if_not_effect': ('if_not_effect', 'bp'),
    'if_move': ('if_move', 'hp'), 'score': ('score', 'b'), 'goto': ('goto', 'p'), 'end': ('end', ''),
    'if_stat_level_equal': ('if_stat_level_equal', 'bbbp'),
    'if_stat_level_more_than': ('if_stat_level_more_than', 'bbbp'),
    'if_stat_level_less_than': ('if_stat_level_less_than', 'bbbp'),
    'if_hp_more_than': ('if_hp_more_than', 'bbp'), 'if_hp_less_than': ('if_hp_less_than', 'bbp'),
    'if_random_less_than': ('if_random_less_than', 'bp'), 'if_can_faint': ('if_can_faint', 'p'),
}
ids = {op: i for i, op in enumerate(ops)}
offsets, pos = [], 0
for op, args in D['instructions']:
    offsets.append(pos)
    pos += 1 + sum({'b':1, 'h':2, 'p':4}[x] for x in ops.get(op, ('', ''))[1])
code = bytearray()
for op, args in D['instructions']:
    code.append(ids.get(op, 255))
    if op in ops:
        for kind, arg in zip(ops[op][1], args):
            n = offsets[D['labels'][arg]] if kind == 'p' else number(arg)
            code += (n & ((1 << (8 * {'b':1,'h':2,'p':4}[kind])) - 1)).to_bytes({'b':1,'h':2,'p':4}[kind], 'little')

selected = ['BattleAI_ChooseMoveOrAction', 'BattleAI_DoAIProcessing'] + ['Cmd_' + name for name, _ in ops.values()]
functions = '\n\n'.join(function(AI, name) for name in selected)
checks = '\n\n'.join(function(SCRIPT,name) for name in ['Cmd_accuracycheck','Cmd_critcalc'])
accuracy_ratios=braced(SCRIPT,SCRIPT.index('static const struct StatFractions sAccuracyStageRatios'))+';'
stat_functions=function(POKE,'ModifyStatByNature')+'\n'+function(POKE,'CalculateMonStats')
nature_table=braced(POKE,POKE.index('static const s8 sNatureStatTable'))+';'
calc_macro=POKE[POKE.index('#define CALC_STAT'):POKE.index('void CalculateMonStats')]
discouraged = braced(AI, AI.index('static const u16 sDiscouragedPowerfulMoveEffects[]')) + ';'
ratios = braced(POKE, POKE.index('const u8 gStatStageRatios')) + ';'
apply_macro = POKE[POKE.index('#define APPLY_STAT_MOD'):POKE.index('// Own function in pokeemerald')]
damage_function = function(POKE, 'CalculateBaseDamage')
physical = braced(damage_function, damage_function.index('if (IS_TYPE_PHYSICAL(type))\n    {'))
damage = '''static u32 original_damage(struct Mon *attacker, struct Mon *defender, u32 power, u32 crit) {
    s32 damage=0, damageHelper; u16 attack=attacker->attack, defense=defender->defense;
    u16 move=0, sideStatus=0; gBattleMovePower=power; gCritMultiplier=crit;
''' + physical + '\nreturn (damage+2)*crit;\n}\n'
# The fixture has none of the branches that these identifiers represent.
needed = set(re.findall(r'\b[A-Z][A-Za-z_0-9]+\b', functions + discouraged + ratios + damage + checks + stat_functions + nature_table + calc_macro))
needed.update(['ABILITY_BLAZE','ABILITY_TORRENT','MOVE_TACKLE','MOVE_TAIL_WHIP'])
special = {'AI_THINKING_STRUCT', 'BATTLE_HISTORY', 'T1_READ_PTR', 'T1_READ_16','T2_READ_16','GET_MOVE_TYPE','WEATHER_HAS_EFFECT', 'GET_BATTLER_SIDE',
           'IS_TYPE_PHYSICAL', 'APPLY_STAT_MOD', 'MAX_STAT_STAGE'}
defines = '\n'.join(f'#define {key} {number(key)}' for key in sorted(needed-special) if key in constants)
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;
struct Mon { u16 hp,maxHP,attack,defense,spAttack,spDefense,moves[4]; u8 pp[4],statStages[8],level,ability; u32 status1,status2; u16 species,item; } gBattleMons[2];
struct Move { u16 power; u8 effect,type,target,accuracy; } gBattleMoves[400];
struct Info { u8 abilities[2],baseHP,baseAttack,baseDefense,baseSpeed,baseSpAttack,baseSpDefense; } gSpeciesInfo[10];
struct Pokemon {u32 data[100],personality;};
static u32 GetMonData(struct Pokemon *mon,int field,...){return mon->data[field];}
static void SetMonData(struct Pokemon *mon,int field,void *value){memcpy(&mon->data[field],value,4);}
static u8 GetNature(struct Pokemon *mon){return mon->personality%25;}
static u8 GetLevelFromMonExp(struct Pokemon *mon);
struct Thinking { s8 score[4]; u8 simulatedRNG[4]; u16 moveConsidered; u32 funcResult,aiFlags; u8 movesetIndex,aiLogicId,aiState,aiAction; } thinking;
struct History { u8 abilities[2]; } history;
struct { u8 dynamicMoveType; } battleStruct, *gBattleStruct=&battleStruct;
struct { u8 dmgMultiplier; s32 levelUpHP; } gBattleScripting;
static u8 script[65536]; static const u8 *sAIScriptPtr; static u8 *gBattleAI_ScriptsTable[3];
static u32 gRngValue, rngCalls; static u16 gCurrentMove,gDynamicBasePower,gBattleMovePower,gCritMultiplier;
static u8 gBattlerAttacker=1,gBattlerTarget=0,gMoveResultFlags;
static u32 gBattleTypeFlags; static s32 gBattleMoveDamage;
struct StatFractions {u16 dividend,divisor;};
static u8 battleCode[16],tutorialFlags,gPotentialItemEffectBattler,gBattleCommunication[8];
static const u8 *gBattlescriptCurrInstr;
static u32 gStatuses3[2],gBattleWeather;
struct {u8 battlerWithSureHit;} gDisableStructs[2];
struct {u8 holdEffect,holdEffectParam;} gEnigmaBerries[2];
static const u16 sCriticalHitChance[]={16,8,4,3,2};
#define ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define T2_READ_16(p) T1_READ_16(p)
#define GET_MOVE_TYPE(move,type) (type=gBattleMoves[move].type)
#define WEATHER_HAS_EFFECT 0
static u8 BtlCtrl_OakOldMan_TestState2Flag(u8 f){return tutorialFlags&f;}
static void JumpIfMoveFailed(u8 adder,u16 move){gBattlescriptCurrInstr+=adder;}
static u8 JumpIfMoveAffectedByProtect(u16 move){return 0;}
static u8 AccuracyCalcHelper(u16 move){return 0;}
static u8 ItemId_GetHoldEffect(u16 item){return 0;}
static u8 ItemId_GetHoldEffectParam(u16 item){return 0;}
static void CheckWonderGuardAndLevitate(void){}
#define AI_THINKING_STRUCT (&thinking)
#define BATTLE_HISTORY (&history)
#define MAX_STAT_STAGE 12
#define GET_BATTLER_SIDE(id) (id)
#define IS_TYPE_PHYSICAL(type) 1
#define T1_READ_PTR(p) (script + read32(p))
#define T1_READ_16(p) ((u16)((p)[0] | ((p)[1]<<8)))
static u32 read32(const u8 *p) { return p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u8 GetBattlerSide(u8 id) { return id; }
static void RecordLastUsedMoveByTarget(void) {}
static u8 AIStackPop(void) { return 0; }
static int CountAliveMonsInBattle(int c) { return 1; }
static void unsupported(void) { fprintf(stderr,"Unsupported source AI opcode %u\n",*sAIScriptPtr); exit(2); }
'''
random = function((REF / 'src/random.c').read_text(), 'Random')
prefix += '#define ISO_RANDOMIZE1(val) (1103515245u*(val)+24691u)\n' + random.replace('return gRngValue >> 16;', 'rngCalls++; return gRngValue >> 16;')
prototypes = '\n'.join(re.match(r'[^\{]+', function(AI, name))[0].strip() + ';' for name in selected)
stubs = '''
static void AI_CalcDmg(u8 atk,u8 def) { gBattleMoveDamage=original_damage(&gBattleMons[atk],&gBattleMons[def],gBattleMoves[gCurrentMove].power,1); }
static void TypeCalc(u16 move,u8 atk,u8 def) { /* Normal against these starters: no STAB/resistance. */ }
'''
table = 'static void (*sBattleAICmdTable[256])(void);\n'
init = '\n'.join(f'sBattleAICmdTable[{ids[op]}]=Cmd_{name};' for op,(name,_) in ops.items())
array = ','.join(map(str,code))
roots = '\n'.join(f'gBattleAI_ScriptsTable[{i}]=script+{offsets[D["labels"][root]]};' for i,root in enumerate(D['ai_roots']))
move_init = '\n'.join(f'gBattleMoves[{number(name)}]=(struct Move){{{m["power"]},{number(m["effect"])},0,0,{m["accuracy"]}}};' for name,m in D['moves'].items())
species_init='\n'.join(f'gSpeciesInfo[{index}]=(struct Info){{{{{number("ABILITY_BLAZE" if index==4 else "ABILITY_TORRENT")},0}},{",".join(map(str,D["species"][i]["base"]))}}};' for i,index in enumerate([4,7]))
main = f'''
int main(void) {{
    u8 bytes[]={{{array}}}; memcpy(script,bytes,sizeof(bytes));
    for(int i=0;i<256;i++)sBattleAICmdTable[i]=unsupported;
    {init}
    {roots}
    {move_init}
    {species_init}
    gSpeciesInfo[4].abilities[0]=ABILITY_BLAZE; gSpeciesInfo[7].abilities[0]=ABILITY_TORRENT;
    puts("{{\\\"ai\\\":["); int comma=0;
    for(int hp=1;hp<=20;hp++)for(int defense=0;defense<=6;defense++)for(int attack=0;attack<=6;attack++) {{
        memset(&thinking,0,sizeof(thinking)); memset(gBattleMons,0,sizeof(gBattleMons));
        gBattleMons[0]=(struct Mon){{.hp=hp,.maxHP=20,.attack=11,.defense=10,.level=5,.species=4,.ability=ABILITY_BLAZE}};
        gBattleMons[1]=(struct Mon){{.hp=19,.maxHP=19,.attack=9,.defense=9,.level=5,.species=7,.ability=ABILITY_TORRENT}};
        gBattleMons[0].statStages[1]=6; gBattleMons[0].statStages[2]=defense;
        gBattleMons[1].statStages[1]=attack; gBattleMons[1].statStages[2]=6;
        gBattleMons[1].moves[0]=MOVE_TACKLE; gBattleMons[1].moves[1]=MOVE_TAIL_WHIP;
        gBattleMons[1].pp[0]=35;gBattleMons[1].pp[1]=30;
        gRngValue=hp*73+defense*9+attack; rngCalls=0;
        for(int i=0;i<4;i++){{thinking.score[i]=i<2?100:0;thinking.simulatedRNG[i]=100-Random()%16;}}
        thinking.aiFlags=7;
        int slot=BattleAI_ChooseMoveOrAction();
        printf("%s[%d,%d,%d,%d,%d,%d,%u,%u]",comma?",":"",hp,defense,attack,slot,thinking.score[0],thinking.score[1],gRngValue,rngCalls);comma=1;
    }}
    puts("],\\\"damage\\\":[");comma=0;
    for(int attack=1;attack<=35;attack++)for(int defense=1;defense<=35;defense++)for(int crit=1;crit<=2;crit++)for(int st=0;st<=6;st++){{
        struct Mon a={{.attack=attack,.level=5}},d={{.defense=defense}};
        a.statStages[1]=st;d.statStages[2]=st;
        int value=original_damage(&a,&d,40,crit);
        printf("%s[%d,%d,%d,%d,%d]",comma?",":"",attack,defense,crit,st,value);comma=1;
    }}
    puts("],\\\"checks\\\":[");comma=0;
    gBattleTypeFlags=BATTLE_TYPE_FIRST_BATTLE;
    for(int seed=0;seed<128;seed++)for(int flags=0;flags<4;flags++)for(int actor=0;actor<2;actor++)for(int slot=0;slot<2;slot++){{
        memset(gBattleMons,0,sizeof(gBattleMons));
        gBattlerAttacker=actor;gBattlerTarget=actor^1;tutorialFlags=flags;
        gBattleMons[0].ability=ABILITY_BLAZE;gBattleMons[1].ability=ABILITY_TORRENT;
        for(int i=0;i<2;i++)for(int j=0;j<8;j++)gBattleMons[i].statStages[j]=6;
        gCurrentMove=actor?(slot?{number('MOVE_TAIL_WHIP')}:{number('MOVE_TACKLE')}):(slot?{number('MOVE_GROWL')}:{number('MOVE_SCRATCH')});
        gRngValue=seed;rngCalls=0;gMoveResultFlags=0;gBattlescriptCurrInstr=battleCode;
        Cmd_accuracycheck();int hit=!(gMoveResultFlags&MOVE_RESULT_MISSED),crit=0;
        if(hit&&gBattleMoves[gCurrentMove].power){{Cmd_critcalc();crit=gCritMultiplier==2;}}
        printf("%s[%d,%d,%d,%d,%d,%d,%u,%u]",comma?",":"",seed,flags,actor,slot,hit,crit,gRngValue,rngCalls);comma=1;
    }}
    puts("],\\\"stats\\\":[");comma=0;
    for(int species=0;species<2;species++)for(int level=5;level<=6;level++)for(int iv=0;iv<=31;iv+=31)for(int ev=0;ev<=252;ev+=252)for(int nature=0;nature<25;nature++){{
        struct Pokemon mon={{0}};mon.personality=nature;
        mon.data[MON_DATA_SPECIES]=species?7:4;mon.data[MON_DATA_LEVEL]=level;
        int iv_fields[]={{MON_DATA_HP_IV,MON_DATA_ATK_IV,MON_DATA_DEF_IV,MON_DATA_SPEED_IV,MON_DATA_SPATK_IV,MON_DATA_SPDEF_IV}};
        int ev_fields[]={{MON_DATA_HP_EV,MON_DATA_ATK_EV,MON_DATA_DEF_EV,MON_DATA_SPEED_EV,MON_DATA_SPATK_EV,MON_DATA_SPDEF_EV}};
        for(int i=0;i<6;i++){{mon.data[iv_fields[i]]=iv;mon.data[ev_fields[i]]=ev;}}
        CalculateMonStats(&mon);
        printf("%s[%d,%d,%d,%d,%d,%u,%u,%u,%u,%u,%u]",comma?",":"",species,level,iv,ev,nature,
            mon.data[MON_DATA_MAX_HP],mon.data[MON_DATA_ATK],mon.data[MON_DATA_DEF],mon.data[MON_DATA_SPEED],mon.data[MON_DATA_SPATK],mon.data[MON_DATA_SPDEF]);comma=1;
    }}
    puts("]}}");return 0;
}}
'''
# Tiny integer stats can round defense to zero; GBA starters never do. Limit
# oracle damage vectors to stats >= 5 to keep the supported domain valid.
main = main.replace('attack=1;', 'attack=5;').replace('defense=1;', 'defense=5;')
source = '\n'.join([prefix,defines,'static u8 GetLevelFromMonExp(struct Pokemon *mon){return mon->data[MON_DATA_LEVEL];}',nature_table,calc_macro,stat_functions,ratios,accuracy_ratios,apply_macro,damage,stubs,prototypes,table,discouraged,functions,checks,main])
(OUT / 'oracle.c').write_text(source)
env = dict(os.environ); env['PATH'] = 'C:/msys64/mingw64/bin;' + env['PATH']
subprocess.run(['C:/msys64/mingw64/bin/gcc.exe','-std=c17','-O2',str(OUT/'oracle.c'),'-o',str(OUT/'oracle.exe')], check=True, env=env)
result = subprocess.run([str(OUT/'oracle.exe')],check=True,capture_output=True,text=True,env=env)
vectors = json.loads(result.stdout)
vectors['scope'] = 'Original C AI handlers/selection/processing, physical damage branch, accuracy/critical commands and stat calculation; bounded fixture, not complete-ROM timing parity'
vectors['source_sha256'] = {name: hashlib.sha256((REF/name).read_bytes()).hexdigest() for name in ['src/pokemon.c','src/battle_ai_script_commands.c','src/random.c','src/battle_script_commands.c']}
(ROOT/'data/battle-reference.json').write_text(json.dumps(vectors,separators=(',',':'))+'\n')
print(f'Original C oracle: {len(vectors["ai"])} AI, {len(vectors["damage"])} damage, {len(vectors["checks"])} accuracy/critical, {len(vectors["stats"])} stat vectors.')
