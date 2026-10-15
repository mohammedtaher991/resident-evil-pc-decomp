// Balance.cpp - reads balance.xml and answers damage queries (see Balance.h and
// docs/BALANCE_XML.md for the file format).
//
// Files read, lowest priority first (later rules win):
//   <asset base>[/<mode overlay>]/balance.xml, <asset root>balance.xml,
//   <exe dir>/balance.xml, ./balance.xml, then <asset base>/Addons/<Name>/balance.xml
//   for each add-on in config.ini / difficulty.ini (add-ons override the base game).
#include "Balance.h"
#include "Addon.h"
#include "Difficulty.h"
#include "../Globals.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"
#include "../DebugPrint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BAL_FILE "balance.xml"
#define BAL_MAX_RULES 1024
#define BAL_MAX_FILE  (512 * 1024)

namespace {

enum RuleKind { RK_WEAPON = 1, RK_HIT = 2, RK_ENEMY = 3 };

struct Rule {
    int  kind;
    char key[64];      // RK_HIT: "Enemy:site"; RK_ENEMY: enemy name
    char mode[24];     // difficulty mode name ("" = any)
    char addon[32];    // add-on name ("" = any)
    int  enemy;        // RK_WEAPON: enemy type, -1 = any
    int  weapon;       // RK_WEAPON: weapon slot, -1 = any
    int  second;       // RK_WEAPON: -1 any, 0 first playthrough, 1 second
    int  damage;       // absolute value, -1 = not set
    int  scale;        // percent, -1 = not set
};

Rule s_rules[BAL_MAX_RULES];
int  s_ruleCount = 0;
bool s_loaded    = false;

// ---------------------------------------------------------------------------
// name tables
// ---------------------------------------------------------------------------
const char* const kEnemyNames[] = {
    "ZOMBIE", "ZOMBIE_NAKED", "CERBERUS", "WEB_SPINNER", "BLACK_TIGER", "CROW",
    "HUNTER", "WASP", "PLANT42", "CHIMERA", "ADDER", "NEPTUNE", "TYRANT_1",
    "YAWN_1", "PLANT42_ROOTS", "MONSTER_PLANT", "TYRANT_2", "ZOMBIE_VARIANT",
    "YAWN_2", "SPIDER_WEB"
};
const int kEnemyCount = (int)(sizeof(kEnemyNames) / sizeof(kEnemyNames[0]));

const char* const kWeaponNames[] = {
    "KNIFE", "HANDGUN", "SHOTGUN", "PYTHON_DUMDUM", "MAGNUM",
    "FLAMETHROWER", "GL_EXPLOSIVE", "GL_ACID", "GL_FLAME", "ROCKET"
};
const int kWeaponCount = (int)(sizeof(kWeaponNames) / sizeof(kWeaponNames[0]));

bool IEq(const char* a, const char* b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// "any" / "*" / "" -> -1. A name from `names`, or a number (decimal or 0x..).
int ParseId(const char* v, const char* const* names, int count)
{
    if (!v || !*v || strcmp(v, "*") == 0 || IEq(v, "any")) return -1;
    for (int i = 0; i < count; ++i)
        if (IEq(v, names[i])) return i;
    if (isdigit((unsigned char)v[0]) || v[0] == '-') return (int)strtol(v, NULL, 0);
    return -2;   // unknown name: the rule is dropped
}

// ---------------------------------------------------------------------------
// tiny XML reader: elements, attributes, comments, <?..?>, no entities needed
// ---------------------------------------------------------------------------
struct Attr { char name[24]; char value[96]; };
struct Tag  { char name[24]; Attr attr[12]; int attrCount; bool closing; bool selfClose; };

const char* GetAttr(const Tag& t, const char* name)
{
    for (int i = 0; i < t.attrCount; ++i)
        if (IEq(t.attr[i].name, name)) return t.attr[i].value;
    return NULL;
}

// Parses one tag starting at *pp (which points just after '<'); advances *pp past '>'.
bool ParseTag(const char** pp, const char* end, Tag* t)
{
    const char* p = *pp;
    memset(t, 0, sizeof(*t));
    if (p < end && *p == '/') { t->closing = true; ++p; }
    int n = 0;
    while (p < end && !isspace((unsigned char)*p) && *p != '>' && *p != '/') {
        if (n < (int)sizeof(t->name) - 1) t->name[n++] = *p;
        ++p;
    }
    while (p < end && *p != '>') {
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p >= end || *p == '>') break;
        if (*p == '/') { t->selfClose = true; ++p; continue; }
        Attr a; memset(&a, 0, sizeof(a));
        int k = 0;
        while (p < end && *p != '=' && *p != '>' && *p != '/' && !isspace((unsigned char)*p)) {
            if (k < (int)sizeof(a.name) - 1) a.name[k++] = *p;
            ++p;
        }
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p < end && *p == '=') {
            ++p;
            while (p < end && isspace((unsigned char)*p)) ++p;
            if (p < end && (*p == '"' || *p == '\'')) {
                const char q = *p++;
                int v = 0;
                while (p < end && *p != q) {
                    if (v < (int)sizeof(a.value) - 1) a.value[v++] = *p;
                    ++p;
                }
                if (p < end) ++p;
            }
        }
        if (a.name[0] && t->attrCount < 12) t->attr[t->attrCount++] = a;
    }
    if (p < end && *p == '>') ++p;
    *pp = p;
    return t->name[0] != 0;
}

struct Ctx {
    char mode[24];
    char addon[32];
    int  enemy;       // inside <enemy> of <weapons>
    int  second;      // <weapons second="..."> (-1 any)
    int  section;     // 0 none, 1 weapons, 2 enemies
};

void CopyStr(char* dst, size_t n, const char* src)
{
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

int ParseSecond(const char* v)
{
    if (!v || !*v || IEq(v, "any") || strcmp(v, "*") == 0) return -1;
    if (IEq(v, "true") || IEq(v, "yes") || strcmp(v, "1") == 0 || IEq(v, "second")) return 1;
    return 0;
}

void AddRule(const Rule& r)
{
    if (s_ruleCount < BAL_MAX_RULES) s_rules[s_ruleCount++] = r;
    else dbg_printf("[BALANCE] too many rules, ignoring the rest\n");
}

void Apply(const Tag& t, Ctx* ctx)
{
    const char* v;
    if ((v = GetAttr(t, "mode")))  CopyStr(ctx->mode,  sizeof(ctx->mode),  v);
    if ((v = GetAttr(t, "addon"))) CopyStr(ctx->addon, sizeof(ctx->addon), v);
}

void ParseDocument(const char* text, size_t len, const char* defMode, const char* defAddon)
{
    const char* p   = text;
    const char* end = text + len;
    Ctx stack[8];
    int depth = 0;
    memset(stack, 0, sizeof(stack));
    stack[0].enemy = -1; stack[0].second = -1;
    if (defMode)  CopyStr(stack[0].mode,  sizeof(stack[0].mode),  defMode);
    if (defAddon) CopyStr(stack[0].addon, sizeof(stack[0].addon), defAddon);

    while (p < end) {
        const char* lt = (const char*)memchr(p, '<', (size_t)(end - p));
        if (!lt) break;
        p = lt + 1;
        if (end - p >= 3 && memcmp(p, "!--", 3) == 0) {         // comment
            const char* e = strstr(p + 3, "-->");
            if (!e) break;
            p = e + 3;
            continue;
        }
        if (p < end && (*p == '?' || *p == '!')) {              // <?xml ?> / <!DOCTYPE>
            const char* e = (const char*)memchr(p, '>', (size_t)(end - p));
            if (!e) break;
            p = e + 1;
            continue;
        }
        Tag t;
        if (!ParseTag(&p, end, &t)) continue;

        if (t.closing) { if (depth > 0) --depth; continue; }

        Ctx ctx = stack[depth];
        Apply(t, &ctx);

        const char* v;
        Rule r; memset(&r, 0, sizeof(r));
        r.enemy = -1; r.weapon = -1; r.second = -1; r.damage = -1; r.scale = -1;
        CopyStr(r.mode,  sizeof(r.mode),  ctx.mode);
        CopyStr(r.addon, sizeof(r.addon), ctx.addon);

        if (IEq(t.name, "weapons")) {
            ctx.section = 1;
            ctx.second  = ParseSecond(GetAttr(t, "second"));
            ctx.enemy   = -1;
        } else if (IEq(t.name, "enemies")) {
            ctx.section = 2;
        } else if (IEq(t.name, "enemy") && ctx.section == 1) {
            const int id = ParseId(GetAttr(t, "id"), kEnemyNames, kEnemyCount);
            if (id == -2) dbg_printf("[BALANCE] unknown enemy id '%s'\n", GetAttr(t, "id"));
            ctx.enemy = id;
        } else if (IEq(t.name, "weapon") && ctx.section == 1) {
            r.kind   = RK_WEAPON;
            r.weapon = ParseId(GetAttr(t, "id"), kWeaponNames, kWeaponCount);
            r.enemy  = ctx.enemy;
            r.second = ctx.second;
            const char* e = GetAttr(t, "enemy");
            if (e) r.enemy = ParseId(e, kEnemyNames, kEnemyCount);
            if ((v = GetAttr(t, "second"))) r.second = ParseSecond(v);
            if ((v = GetAttr(t, "damage"))) r.damage = atoi(v);
            if ((v = GetAttr(t, "scale")))  r.scale  = atoi(v);
            if (r.weapon == -2 || r.enemy == -2) {
                dbg_printf("[BALANCE] weapon rule with an unknown id ignored\n");
            } else if (r.damage >= 0 || r.scale >= 0) {
                AddRule(r);
            }
        } else if (IEq(t.name, "enemy") && ctx.section == 2) {
            r.kind = RK_ENEMY;
            CopyStr(r.key, sizeof(r.key), GetAttr(t, "name"));
            if ((v = GetAttr(t, "scale"))) r.scale = atoi(v);
            if (r.key[0] && r.scale >= 0) AddRule(r);
        } else if (IEq(t.name, "hit") && ctx.section == 2) {
            r.kind = RK_HIT;
            CopyStr(r.key, sizeof(r.key), GetAttr(t, "key"));
            if ((v = GetAttr(t, "damage"))) r.damage = atoi(v);
            if ((v = GetAttr(t, "scale")))  r.scale  = atoi(v);
            if (r.key[0] && (r.damage >= 0 || r.scale >= 0)) AddRule(r);
        }

        if (!t.selfClose && depth < 7) stack[++depth] = ctx;
    }
}

// ---------------------------------------------------------------------------
bool ReadWhole(const char* raw, char** outBuf, size_t* outLen)
{
    char norm[512];
    const char* p = plat_normalize_path(raw, norm, sizeof(norm));
    FILE* f = fopen(p ? p : raw, "rb");
    if (!f) return false;
    char* buf = (char*)malloc(BAL_MAX_FILE + 1);
    if (!buf) { fclose(f); return false; }
    const size_t n = fread(buf, 1, BAL_MAX_FILE, f);
    fclose(f);
    buf[n] = 0;
    *outBuf = buf;
    *outLen = n;
    dbg_printf("[BALANCE] reading %s\n", p ? p : raw);
    return true;
}

int s_filesRead = 0;

// defMode / defAddon: condition for a file named balance_<mode>.xml / balance_<addon>.xml
// (rules inside may still narrow it with their own mode= / addon=).
bool LoadOne(const char* path, const char* defMode = NULL, const char* defAddon = NULL)
{
    char*  buf = NULL;
    size_t len = 0;
    if (!ReadWhole(path, &buf, &len)) return false;
    ParseDocument(buf, len, defMode, defAddon);
    free(buf);
    ++s_filesRead;
    return true;
}

// balance.xml, then balance_<mode>.xml for every difficulty mode and
// balance_<addon>.xml for every add-on, all inside `dir`.
void LoadFolder(const char* dir)
{
    char cand[512];
    snprintf(cand, sizeof(cand), "%s/%s", dir, BAL_FILE);
    LoadOne(cand);
    for (int i = 0; i < Difficulty_Count(); ++i) {
        const DifficultyMode* m = Difficulty_Get(i);
        if (!m || !m->name[0]) continue;
        char lower[32];
        size_t k = 0;
        for (; m->name[k] && k < sizeof(lower) - 1; ++k) lower[k] = (char)tolower((unsigned char)m->name[k]);
        lower[k] = 0;
        snprintf(cand, sizeof(cand), "%s/balance_%s.xml", dir, lower);
        LoadOne(cand, m->name, NULL);
    }
    for (int i = 0; i < GetAddonCount(); ++i) {
        snprintf(cand, sizeof(cand), "%s/balance_%s.xml", dir, GetAddonName(i));
        LoadOne(cand, NULL, GetAddonName(i));
    }
}

// Does the rule's mode / add-on condition hold right now?
bool Active(const Rule& r)
{
    if (r.mode[0]) {
        const DifficultyMode* m = Difficulty_Active();
        if (!m || !IEq(m->name, r.mode)) return false;
    }
    if (r.addon[0] && !Addon_IsEnabled(r.addon)) return false;
    return true;
}

int Scale(int v, int pct)
{
    const int r = (int)(((long long)v * pct + 50) / 100);
    return r < 0 ? 0 : r;
}

} // namespace

// ---------------------------------------------------------------------------
void Balance_Reload(void)
{
    s_loaded    = true;
    s_ruleCount = 0;

    char cand[512];
    const char* base = GetAssetBase();
    const char* mode = GetAssetModeName();
    const char* root = GetAssetRoot();
    s_filesRead = 0;

    if (base && base[0]) {
        LoadFolder(base);
        if (mode && mode[0]) {
            snprintf(cand, sizeof(cand), "%s/%s", base, mode);
            LoadFolder(cand);
        }
    }
    if (root && root[0]) {
        snprintf(cand, sizeof(cand), "%s%s", root, BAL_FILE);
        LoadOne(cand);
    }
    char exeDir[240] = "";
    if (plat_exe_dir(exeDir, sizeof(exeDir)) && exeDir[0]) LoadFolder(exeDir);
    LoadFolder(".");

    // add-ons last, so they override the base game
    if (base && base[0]) {
        for (int i = 0; i < GetAddonCount(); ++i) {
            snprintf(cand, sizeof(cand), "%s/Addons/%s", base, GetAddonName(i));
            LoadFolder(cand);
        }
    }
    if (s_filesRead == 0)
        dbg_printf("[BALANCE] no balance.xml found - looked in %s (the file must be named "
                   "balance.xml, or balance_<mode>.xml / balance_<addon>.xml)\n",
                   (base && base[0]) ? base : "(no asset folder)");
    dbg_printf("[BALANCE] %d rule(s) loaded\n", s_ruleCount);
}

static void EnsureLoaded(void)
{
    if (!s_loaded) Balance_Reload();
}

int Balance_WeaponDamage(unsigned int enemyType, unsigned int weaponSlot,
                         int secondPlaythrough, int dmg)
{
    EnsureLoaded();
    int v = dmg;
    for (int i = 0; i < s_ruleCount; ++i) {
        const Rule& r = s_rules[i];
        if (r.kind != RK_WEAPON) continue;
        if (r.enemy  != -1 && r.enemy  != (int)enemyType)  continue;
        if (r.weapon != -1 && r.weapon != (int)weaponSlot) continue;
        if (r.second != -1 && r.second != (secondPlaythrough ? 1 : 0)) continue;
        if (!Active(r)) continue;
        if (r.damage >= 0) v = r.damage;
        if (r.scale  >= 0) v = Scale(v, r.scale);
    }
    return v;
}

int Balance_EnemyDamage(const char* key, int dmg)
{
    EnsureLoaded();
    if (!key) return dmg;
    int v = dmg;

    // enemy name = the part of the key before ':'
    char enemy[32];
    const char* colon = strchr(key, ':');
    const size_t n = colon ? (size_t)(colon - key) : strlen(key);
    CopyStr(enemy, sizeof(enemy), key);
    if (n < sizeof(enemy)) enemy[n] = 0;

    for (int i = 0; i < s_ruleCount; ++i) {
        const Rule& r = s_rules[i];
        if (r.kind != RK_HIT || !IEq(r.key, key) || !Active(r)) continue;
        if (r.damage >= 0) v = r.damage;
        if (r.scale  >= 0) v = Scale(v, r.scale);
    }
    for (int i = 0; i < s_ruleCount; ++i) {
        const Rule& r = s_rules[i];
        if (r.kind != RK_ENEMY || !IEq(r.key, enemy) || !Active(r)) continue;
        v = Scale(v, r.scale);
    }
    return v;
}
