# BTLACT — battle-actor struct map

Reverse-engineered from `Decomp/FFV_FFD_compare/libjniproxy.so_new.c`. Ghidra emits
`BTLACT` as an empty placeholder, so the field meanings below come from
**`BattleClass::SetMemberStatus(BTLACT&, int)`** (c:83626 — copies a member's
`MEMBER_STATUS` into the BTLACT), **`SetBtlPlayerParam`** (c:88079) /
**`SetBtlEquipParam`** (c:83685 — fold in equipment), and how the fields are *used*
in **`CalcPhysicAttackDmg`** (c:91987) and **`CalcMagicDmg`** (c:93076).

`MEMBER_STATUS` lives at `GameClass + idx*0x4c4 + 0x1a180`; its battle-ready param
block (HP/MP/attack/defense/…) is at member `+0x1a370…`, written by `SetJobStatus`
from the decoded base stats (STR/SPD/VIT/INT/MND), level (← EXP), and equipment.

## Combat-critical fields

| BTLACT off | meaning | source (MEMBER_STATUS) | notes |
|-----------:|---------|------------------------|-------|
| `+0x08` | → MEMBER_STATUS ptr | `member + 0x1a180` | back-pointer |
| `+0x10` | party slot index | (SetBtlPlayerParam param) | |
| `+0x14` | action/attack type | | `0` = physical attack (CalcPhysicAttackDmg branch) |
| `+0x28` | **LEVEL** | `member[0x1a294]+1` (levelIdx+1) | **final damage multiplier** (`… × atk[0x28] >> 0xc`) |
| `+0x2c` | current HP | `member[0x1a370]` | HP-ratio attack bonus |
| `+0x30` | **max HP** | `member[0x1a298]` | |
| `+0x34` | current MP | `member[0x1a374]` | |
| `+0x3c` | **attack stat** (STR-derived) | `adj(member[0x1a378])` | physical base term (`iVar6`) |
| `+0x40` | secondary stat | `adj(member[0x1a380])` | (SPD/accuracy-class) |
| `+0x44` | secondary stat | `adj(member[0x1a384])` | (VIT/defense-class) |
| `+0x48` | secondary stat | `adj(member[0x1a388])` | (INT-class) |
| `+0x4c` | **weapon attack power** | `member[0x1a38c]` (+ `SetBtlEquipParam`) | main physical power term (`W<<6 × hits`) |
| `+0x54` | attack count / accuracy | `member[0x1a390]` | |
| `+0x58` | **DEFENSE** | `member[0x1a394]` (+ armor) | subtracted from the attack term |
| `+0x5c` | magic defense | `member[0x1a398]` | |
| `+0x60` | evade | `member[0x1a39c]` | |
| `+0x64` | magic evade | `member[0x1a3a0]` | |
| `+0x68` | stat (MND-class) | `adj(member[0x1a37c])` | |
| `+0x6c…0x74` | element-attack (4×i16) | `member[0x1a3c4]` | `CalcElementPoint` |
| `+0x84…0x8c` | element-resist (4×i16) | `member[0x1a3cc]` | |
| `+0xb8/0xc0/0xc1/0xc2` | status / flag bitfields | | crit = bit `0x4000000`; many modifiers |
| `+0x140` | **hit-count** (fixed-pt) | base `0x20` (SetBtlPlayerParam), equip-adjusted | `>>5` in the calc → 1 hit = `0x20` |
| `+0x144` | damage-taken multiplier | base `0x20` | defender side |
| `+0x148` | defense % | | `dmg × def[0x148] / 100` |
| `+0x158` | fixed damage | | returned when flag `+0x154` set |
| `+0x208/0x240/0x244` | crit / bonus values | | |
| `+0x25c` | damage % modifier | | |
| `+0x631` | "return maxHP−1" flag | | instant-kill style |
| `+0x648` | effect-actor ptr | | VFX |

## Physical damage formula (decoded core)

With attacker `L`=level(`0x28`), `A`=attack stat(`0x3c`), `W`=weapon power(`0x4c`),
`H`=hit-count(`0x140`) and defender `D`=defense(`0x58`):

```
iVar7    = A + jobMult·A/100                 # attack stat adjusted (≈ A)
powerFac = MAX(0x40, (iVar7·3 + L)·4)        # ≈ (3·A + L)·4   — scales with LEVEL
weapon   = W<<6                              # ×64 fixed-point
spread   = Rand(MAX(0x80, weapon/0x14))      # random ≈ 0 … W·3.2
attackRaw= (weapon·H)>>5 + spread            # = W·2·H + spread
afterDef = MAX(0, attackRaw − D·64…)         # defense SUBTRACTS (not def/2)
         × defense%(0x148) × element(iVar8) × race × crit(×2) × back-attack × revision
damage   = … × powerFac >> 0xc               # × (3A+L)·4 / 4096   (healing negates)
```

Magic (`CalcMagicDmg`/`CalcMagicHpDamage`) mirrors this with spell power + INT/MND
(`0x48`/`0x68`) vs magic-defense (`0x5c`).

**Status:** struct mapped; FFSmith implements the **core shape** (attack term + random
spread − full defense, scaling with STR/level). Exact-match still needs: the per-field
`SetJobStatus` derivation for `A`/`W`/`D` (so the *values* match), plus the
element/crit/race/status/hit-count modifiers above.
