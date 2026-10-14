# balance.xml - data-driven damage tuning

`balance.xml` changes damage **without recompiling**: what your weapons do to each enemy
(the table in `WeaponDamage.cpp`) and what each enemy attack does to you. It is optional;
with no file, or no matching rule, the game uses the code's own numbers. It is re-read
every time the title screen appears, so edit, return to the title, play.

## Where to put it
Later files win. Next to `difficulty.ini` is the usual place.

1. `<Assets>/balance.xml`
2. `<Assets>/<mode>/balance.xml`
3. the asset root, the exe folder, `./balance.xml`
4. `<Assets>/Addons/<Name>/balance.xml` for each active add-on

The log (`[BALANCE] reading ...`, `[BALANCE] N rule(s) loaded`) shows what was read.

## Format
```xml
<balance>
  <weapons [second="true|false"] [mode="WESTERN"] [addon="Western"]>
    <enemy id="ZOMBIE">                 <!-- name, 0x00 hex or number -->
      <weapon id="HANDGUN" damage="12"/>   <!-- set the value -->
      <weapon id="SHOTGUN" scale="150"/>   <!-- percent of the value -->
    </enemy>
  </weapons>
  <enemies [mode=".."] [addon=".."]>
    <enemy name="Hunter" scale="120"/>     <!-- every Hunter attack, percent -->
    <hit key="Hunter:hunter_roll_leap_fixed#2" damage="13"/>  <!-- one attack -->
  </enemies>
</balance>
```
* `mode` is a difficulty.ini mode name (case-insensitive); `addon` an add-on name. The rule
  applies only while it is active. Leave them out for "always".
* `second="true"` = second playthrough table, `false` (default) = first.
* Weapon rules apply in file order; `enemy`/`weapon` on a `<weapon>` element can override the
  enclosing ones. Enemy `hit` rules by key come first, then the `<enemy name>` percent.
* Enemy ids: ZOMBIE, ZOMBIE_NAKED, CERBERUS, WEB_SPINNER, BLACK_TIGER, CROW, HUNTER, WASP,
  PLANT42, CHIMERA, ADDER, NEPTUNE, TYRANT_1, YAWN_1, PLANT42_ROOTS, MONSTER_PLANT, TYRANT_2,
  ZOMBIE_VARIANT, YAWN_2, SPIDER_WEB. Weapon ids: KNIFE, HANDGUN, SHOTGUN, PYTHON_DUMDUM,
  MAGNUM, FLAMETHROWER, GL_EXPLOSIVE, GL_ACID, GL_FLAME, ROCKET.
* `<hit key>` names are `<Enemy>:<function>#<n>`; `tools/balance_tool.py list` prints them all
  (the exported file also notes file:line). A first/second-playthrough pair is two keys (#1/#2).
* Limits: 1024 rules, file up to 512 KB. `<!-- comments -->` are fine.

## Tool: tools/balance_tool.py (Python 3)
```
python tools/balance_tool.py export  --out balance.xml          # every value, to edit
python tools/balance_tool.py list src/game/entities/*.cpp       # attack sites
python tools/balance_tool.py convert --repl Assets/addons/Western/src --mode WESTERN \
       --out Assets/Addons/Western/balance.xml                  # replacement .cpp -> rules
python tools/balance_tool.py instrument MyCopy/Hunter.cpp       # add BAL_DMG hooks to your copy
```
`convert` diffs replacement `.cpp` files against the originals and writes only the differences,
limited to a mode/add-on, so the `.cpp` swap is no longer needed (set `Replace=0`).

## Not covered
* Health *thresholds* and reach/knockback tweaks (e.g. Cerberus reach, Tyrant knockback).
* Wasp sting (`health + dmg`) - use `EnemyDamage` in difficulty.ini.
* Replacement `.cpp` files that lack the `BAL_DMG` hooks - run `instrument` on them.
* `difficulty.ini` scaling still applies after balance.xml.

`samples/balance_western.xml` is the converted Western set; `samples/balance_full.xml` a full export.
