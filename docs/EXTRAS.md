# [Start] and [Ending] settings (difficulty.ini)

New-game inventory and ending rewards, adjustable without recompiling. All keys are
optional; with none set the game behaves exactly as before. Put them in `[Start]` /
`[Ending]` (everywhere), or inside a `[ModeN]` section (only while that mode is chosen).
The file is re-read on the title screen. Log lines start with `[EXTRAS]`.

## [Start]
| Key | Meaning |
|---|---|
| `ChrisSlots=6..8` | Chris's inventory size (the 8-slot screen Jill uses). |
| `RebeccaSlots=6..8` | Rebecca's inventory size. |
| `ChrisItems=` `JillItems=` `RebeccaItems=` | Starting items, `NAME:qty, NAME:qty`. Names are the `ITEM_` names in `Types.h` without the prefix (`KNIFE`, `BERETTA`, `COLT_PYTHON_MAG`, `FIRST_AID_SPRAY`, `INGRAM`...) or a number (`0x0B`). `NONE` = empty list. Unset = the game's own list. |

When Chris or Rebecca is widened beyond 6, Rebecca gets her own 8-slot array (the original
stored her right after Chris's slots, so Chris's slots 7-8 would have been hers). It is saved in
the save file's spare tail. Saves made without the option load with Rebecca's inventory empty
in that mode. Jill is unchanged (8).

## [Ending]
| Key | Meaning |
|---|---|
| `Epilogue=0/1` | 0 skips the rocket-launcher scene; the game goes straight to the fade and the carry-over save. |
| `SpecialKey=0/1` | 0 = the special key is not handed over. |
| `RocketLauncher=AUTO/0/1` | The infinite rocket launcher (AUTO = the original rule). |
| `TimeLimitMinutes=` | Fast-clear limit for the timed reward. 0 = always. Default 240 (the original 4 hours). |
| `TimedReward=` | AUTO = Ingram (Jill) / Minimi (Chris); `NONE`; or any item name/number, e.g. `COLT_PYTHON_MAG`. |
| `TimedRewardQty=` | Quantity / rounds (default 1). |
| `TimedRewardInfinite=0/1` | 1 = a Colt Python reward refills itself when empty (works in every mode). |
| `ExtraItems=` | Items always granted at the ending. |

Rewards that do not fit (no free slot) are skipped and logged. The carry-over save is the same
one the game already writes for the next cycle (New Game +); nothing about it changed.

## Example: Chris with 8 slots and an infinite Python for a fast clear
```ini
[Start]
ChrisSlots=8

[Ending]
Epilogue=0
SpecialKey=0
TimedReward=COLT_PYTHON_MAG
TimedRewardQty=6
TimedRewardInfinite=1
```
