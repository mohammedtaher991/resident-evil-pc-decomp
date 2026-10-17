#!/usr/bin/env python3
"""balance_tool.py - turn the damage numbers in the C++ sources into balance.xml.

The game reads balance.xml at run time (docs/BALANCE_XML.md), so damage can be tuned
without recompiling. This tool builds that file from the code:

  export      dump every weapon-damage table value and every enemy attack that is
              hooked with BAL_DMG(...) into one editable balance.xml
  export-ini  write the weapon damage table as a plain weapondamage.ini (one section per
              enemy, one line per weapon) - put it in Assets/Addons/<Name>/ to mod the
              damage of that add-on's modes only
  convert     compare REPLACEMENT .cpp files (e.g. an add-on's Assets/addons/<Name>/src)
              with the originals and write only the differences as XML rules, limited
              to a difficulty mode and/or add-on - so the .cpp copies are not needed
  instrument  wrap the player-damage numbers in given enemy .cpp files with
              BAL_DMG("<key>", value) so balance.xml can reach them (the repository's
              own enemy files are already done; use this on your own copies)
  list        print the hooks found in the given files

Examples (run from the project folder):
  python tools/balance_tool.py export --out balance.xml
  python tools/balance_tool.py convert --repl Assets/addons/Western/src --mode WESTERN --out Assets/Addons/Western/balance.xml
  python tools/balance_tool.py instrument path/to/MyZombie.cpp
  python tools/balance_tool.py list src/game/entities/Hunter.cpp
"""
import argparse
import os
import re
import sys
from xml.sax.saxutils import quoteattr

ENEMIES = ["ZOMBIE", "ZOMBIE_NAKED", "CERBERUS", "WEB_SPINNER", "BLACK_TIGER", "CROW",
           "HUNTER", "WASP", "PLANT42", "CHIMERA", "ADDER", "NEPTUNE", "TYRANT_1",
           "YAWN_1", "PLANT42_ROOTS", "MONSTER_PLANT", "TYRANT_2", "ZOMBIE_VARIANT",
           "YAWN_2", "SPIDER_WEB"]
WEAPONS = ["KNIFE", "HANDGUN", "SHOTGUN", "PYTHON_DUMDUM", "MAGNUM", "FLAMETHROWER",
           "GL_EXPLOSIVE", "GL_ACID", "GL_FLAME", "ROCKET"]

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_SRC = [os.path.join(ROOT, "src", "game", "entities"),
               os.path.join(ROOT, "src", "game", "PlayerAnimations.cpp")]
WEAPON_FILE = os.path.join(ROOT, "src", "game", "WeaponDamage.cpp")

# --- finding the damage sites ----------------------------------------------------
SITE_A = re.compile(r"g_playerEntity\.health\s*-=\s*")
SITE_B = re.compile(r"g_playerEntity\.health\s*=\s*\(short\)\(\s*g_playerEntity\.health\s*-\s*")
FUNC = re.compile(r"^[A-Za-z_][\w \t\*:&<>,]*?\b(\w+)\s*\([^;{}]*\)\s*(?:const)?\s*\{?\s*$")


def balanced_end(text, start, stop_on_semicolon):
    """Index of the end of the expression that starts at `start`."""
    depth = 0
    i = start
    n = len(text)
    in_str = None
    while i < n:
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == in_str:
                in_str = None
        elif c in "\"'":
            in_str = c
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return i
            depth -= 1
        elif c == ";" and depth == 0 and stop_on_semicolon:
            return i
        i += 1
    return -1


def function_at(text, pos):
    line_start = text.rfind("\n", 0, pos) + 1
    lines = text[:line_start].split("\n")
    for line in reversed(lines):
        if line and not line[0].isspace() and not line.startswith(("//", "#", "}", "{")):
            m = FUNC.match(line.rstrip())
            if m and m.group(1) not in ("if", "for", "while", "switch", "return"):
                return m.group(1)
    return "global"


def enemy_name(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    return "Player" if stem == "PlayerAnimations" else stem


def unwrap(expr):
    """Strip BAL_DMG("key", x) and WESTERN(new, old) down to the value that is in effect
    for a replacement file (WESTERN -> its first argument)."""
    expr = expr.strip()
    m = re.fullmatch(r'BAL_DMG\(\s*"[^"]*"\s*,\s*(.*)\)', expr, re.S)
    if m:
        expr = m.group(1).strip()
    m = re.fullmatch(r"WESTERN\(\s*([^,]+?)\s*,.*\)", expr, re.S)
    if m:
        expr = m.group(1).strip()
    return expr


def literal(expr):
    expr = unwrap(expr).strip().strip("()")
    if re.fullmatch(r"-?(0[xX][0-9a-fA-F]+|\d+)[uUlL]*", expr):
        return int(re.sub(r"[uUlL]+$", "", expr), 0)
    return None


def value_pair(expr):
    """(first_playthrough, second_playthrough) for 'x' or '(...) ? a : b', else None."""
    v = literal(expr)
    if v is not None:
        return (v, v)
    m = re.search(r"\?\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))\s*:\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))\s*\)*\s*$",
                  unwrap(expr))
    if m:
        return (int(m.group(1), 0), int(m.group(2), 0))
    return None


class Site:
    def __init__(self, enemy, func, n, start, end, expr, line=0, fname=""):
        self.enemy, self.func, self.n = enemy, func, n
        self.line, self.fname = line, fname
        self.start, self.end, self.expr = start, end, expr
        self.key = "%s:%s#%d" % (enemy, func, n)


def find_sites(path, text=None):
    if text is None:
        with open(path, encoding="utf-8", errors="replace", newline="") as f:
            text = f.read()
    found = []
    for rx, semi in ((SITE_A, True), (SITE_B, False)):
        for m in rx.finditer(text):
            end = balanced_end(text, m.end(), semi)
            if end < 0:
                continue
            found.append((m.end(), end, text[m.end():end]))
    found.sort()
    sites, counts = [], {}
    enemy = enemy_name(path)
    for start, end, expr in found:
        func = function_at(text, start)
        counts[func] = counts.get(func, 0) + 1
        sites.append(Site(enemy, func, counts[func], start, end, expr,
                          text.count("\n", 0, start) + 1, os.path.basename(path)))
    return sites, text


def source_files(paths):
    out = []
    for p in paths:
        if os.path.isdir(p):
            out += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith(".cpp"))
        elif os.path.isfile(p):
            out.append(p)
    return out


# --- weapon tables ---------------------------------------------------------------
def read_weapon_tables(path):
    """Returns {'first': [dmg]*200, 'second': [dmg]*200} from WeaponDamage.cpp."""
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    tables = {}
    for name, key, field in (("g_weaponHitRecordsFirstRun", "first", 3),
                             ("g_weaponHitRecordsSecondRun", "second", 0)):
        m = re.search(name + r"\[200\]\s*=\s*\{", text)
        if not m:
            continue
        end = text.index("\n};", m.end())
        body = re.sub(r"//[^\n]*", "", text[m.end():end])
        rows = re.findall(r"\{([^{}]*)\}", body)
        vals = []
        for r in rows:
            nums = [int(x.strip(), 0) for x in r.split(",") if x.strip()]
            if len(nums) > field:
                vals.append(nums[field])
        tables[key] = vals
    return tables


def weapon_rules(tables, base=None):
    """Yield (second, enemy_idx, weapon_idx, dmg) for entries that differ from `base`."""
    for key, second in (("first", 0), ("second", 1)):
        vals = (tables or {}).get(key, [])
        bvals = (base or {}).get(key, []) if base else None
        for i, v in enumerate(vals):
            if i // 10 >= len(ENEMIES):
                break
            if bvals is not None and i < len(bvals) and bvals[i] == v:
                continue
            yield second, i // 10, i % 10, v


# --- XML writers -----------------------------------------------------------------
def attrs(**kw):
    return "".join(" %s=%s" % (k, quoteattr(str(v))) for k, v in kw.items() if v not in (None, ""))


def emit_weapons(out, tables, base, mode, addon, indent="  "):
    rows = list(weapon_rules(tables, base))
    if not rows:
        return
    for second in (0, 1):
        sel = [r for r in rows if r[0] == second]
        if not sel:
            continue
        out.append("%s<weapons%s>" % (indent, attrs(second="true" if second else "false",
                                                    mode=mode, addon=addon)))
        cur = None
        for _, e, w, dmg in sel:
            if e != cur:
                if cur is not None:
                    out.append("%s  </enemy>" % indent)
                out.append('%s  <enemy id="%s">' % (indent, ENEMIES[e]))
                cur = e
            out.append('%s    <weapon id="%s" damage="%d"/>' % (indent, WEAPONS[w], dmg))
        out.append("%s  </enemy>" % indent)
        out.append("%s</weapons>" % indent)


def emit_hits(out, hits, mode, addon, indent="  "):
    if not hits:
        return
    out.append("%s<enemies%s>" % (indent, attrs(mode=mode, addon=addon)))
    last = None
    for key, val, note in hits:
        enemy = key.split(":")[0]
        if enemy != last:
            out.append("%s  <!-- %s -->" % (indent, enemy))
            last = enemy
        if isinstance(val, int):
            out.append('%s  <hit key="%s" damage="%d"/>%s' % (indent, key, val,
                                                              "  <!-- %s -->" % note if note else ""))
        else:
            out.append('%s  <!-- %s uses code: %s -->' % (indent, key, " ".join(str(val).split())))
    out.append("%s</enemies>" % indent)


HEADER = """<?xml version="1.0" encoding="UTF-8"?>
<!--
  balance.xml - damage tuning. Format: docs/BALANCE_XML.md
  Put it next to difficulty.ini (or in Assets/Addons/<Name>/ for an add-on).
  Leave a value out and the game keeps the code's own number.
  mode="NAME"  applies only while that difficulty mode is chosen (as in difficulty.ini)
  addon="Name" applies only while that add-on is on
-->
<balance>"""


# --- commands --------------------------------------------------------------------
def cmd_list(args):
    for p in source_files(args.files or DEFAULT_SRC):
        sites, _ = find_sites(p)
        for s in sites:
            v = literal(s.expr)
            print("%-40s %s" % (s.key, v if v is not None else "(%s)" % " ".join(s.expr.split())))


def cmd_export(args):
    out = [HEADER]
    tables = read_weapon_tables(args.weapons or WEAPON_FILE)
    out.append("  <!-- Damage the player's weapons deal. enemy ids: " + ", ".join(ENEMIES) + " -->")
    out.append("  <!-- weapon ids: " + ", ".join(WEAPONS) + ". 'damage' sets the value, 'scale' is a percent. -->")
    emit_weapons(out, tables, None, args.mode, args.addon)
    hits = []
    for p in source_files(args.src or DEFAULT_SRC):
        sites, _ = find_sites(p)
        for s in sites:
            vp = value_pair(s.expr)
            if vp is not None and vp[0] == vp[1]:
                val = vp[0]
            elif vp is not None:
                val = "%s (first playthrough %d, second %d)" % (" ".join(unwrap(s.expr).split()), vp[0], vp[1])
            else:
                val = s.expr
            hits.append((s.key, val, "%s:%d" % (s.fname, s.line)))
    out.append("  <!-- Damage each enemy attack deals to the player. -->")
    emit_hits(out, hits, args.mode, args.addon)
    out.append("</balance>\n")
    write(args.out, "\n".join(out))


def cmd_export_ini(args):
    tables = read_weapon_tables(args.weapons or WEAPON_FILE)
    out = ["; weapondamage.ini - damage the player's weapons deal (table of the game's own values).",
           "; Put it in Assets\\Addons\\<Name>\\ and it applies only while that add-on is on, or next to",
           "; difficulty.ini for every mode. A line may be a number (set the damage) or N% (percent).",
           "; Delete the lines you do not want to change. [Enemy:Second] = second playthrough only.",
           "; Weapons: " + ", ".join(WEAPONS) + "   (All = every weapon)", ""]
    for key, suffix in (("first", ""), ("second", ":Second")):
        vals = (tables or {}).get(key, [])
        for e, name in enumerate(ENEMIES):
            row = vals[e * 10:e * 10 + 10]
            if len(row) < 10:
                continue
            out.append("[%s%s]" % (name, suffix))
            for w, v in enumerate(row):
                out.append("%s=%d" % (WEAPONS[w], v))
            out.append("")
    write(args.out, "\n".join(out))


def cmd_convert(args):
    orig_dirs = args.orig or [os.path.join(ROOT, "src", "game", "entities"),
                              os.path.join(ROOT, "src", "game"),
                              os.path.join(ROOT, "src", "game", "dc")]
    orig = {}
    for d in orig_dirs:
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.endswith(".cpp") and f not in orig:
                    orig[f] = os.path.join(d, f)
    out = [HEADER]
    hits, warnings = [], []
    for rp in source_files([args.repl]):
        name = os.path.basename(rp)
        if name == "Example.cpp":
            continue
        if name == "WeaponDamage.cpp":
            rt = read_weapon_tables(rp)
            ot = read_weapon_tables(orig.get(name, WEAPON_FILE))
            if rt and ot:
                emit_weapons(out, rt, ot, args.mode, args.addon)
            continue
        if name not in orig:
            warnings.append("%s: no original with that name; skipped" % name)
            continue
        rs, _ = find_sites(rp)
        os_, _ = find_sites(orig[name])
        omap = {s.key: s for s in os_}
        for s in rs:
            o = omap.get(s.key)
            if o is None:
                warnings.append("%s: no matching site in the original (stale copy?)" % s.key)
                continue
            rv, ov = value_pair(s.expr), value_pair(o.expr)
            if rv is not None and ov is not None:
                if rv != ov:
                    if rv[0] == rv[1]:
                        hits.append((s.key, rv[0], "%s:%d" % (s.fname, s.line)))
                    else:
                        warnings.append("%s: the two playthroughs differ (%d / %d); set it by hand"
                                        % (s.key, rv[0], rv[1]))
            elif unwrap(s.expr).split() != unwrap(o.expr).split():
                warnings.append("%s: changed code (%s) can't be turned into a number"
                                % (s.key, " ".join(unwrap(s.expr).split())))
        if len(rs) != len(os_):
            warnings.append("%s: %d damage sites vs %d in the original" % (name, len(rs), len(os_)))
    emit_hits(out, hits, args.mode, args.addon)
    out.append("</balance>\n")
    write(args.out, "\n".join(out))
    for w in warnings:
        print("warning:", w, file=sys.stderr)


def cmd_instrument(args):
    for p in args.files:
        sites, text = find_sites(p)
        todo = [s for s in sites if not s.expr.strip().startswith("BAL_DMG(")]
        if not todo:
            print("%s: nothing to do (%d hooks already)" % (p, len(sites)))
            continue
        for s in sorted(todo, key=lambda x: x.start, reverse=True):
            text = text[:s.start] + 'BAL_DMG("%s", %s)' % (s.key, s.expr.strip()) + text[s.end:]
        if "Balance.h" not in text:
            inc = '#include "Balance.h"' if os.path.basename(os.path.dirname(os.path.abspath(p))) != "entities" \
                else '#include "../Balance.h"'
            m = list(re.finditer(r'^#include .*$', text, re.M))
            nl = "\r\n" if "\r\n" in text else "\n"
            if m:
                at = m[-1].end()
                text = text[:at] + nl + inc + "   // BAL_DMG - balance.xml" + text[at:]
            else:
                text = inc + nl + text
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        print("%s: %d hook(s) added" % (p, len(todo)))


def write(path, text):
    if path:
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote", path)
    else:
        sys.stdout.write(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list"); p.add_argument("files", nargs="*"); p.set_defaults(fn=cmd_list)

    p = sub.add_parser("export")
    p.add_argument("--src", nargs="*"); p.add_argument("--weapons")
    p.add_argument("--mode"); p.add_argument("--addon"); p.add_argument("--out")
    p.set_defaults(fn=cmd_export)

    p = sub.add_parser("export-ini")
    p.add_argument("--weapons"); p.add_argument("--out")
    p.set_defaults(fn=cmd_export_ini)

    p = sub.add_parser("convert")
    p.add_argument("--repl", required=True, help="folder with the replacement .cpp files")
    p.add_argument("--orig", nargs="*", help="folders with the original sources")
    p.add_argument("--mode"); p.add_argument("--addon"); p.add_argument("--out")
    p.set_defaults(fn=cmd_convert)

    p = sub.add_parser("instrument"); p.add_argument("files", nargs="+"); p.set_defaults(fn=cmd_instrument)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
