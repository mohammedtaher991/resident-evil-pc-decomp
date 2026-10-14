// Extras.cpp - [Start] / [Ending] settings from difficulty.ini (see docs/EXTRAS.md).
//
// The file is parsed here on its own (the same file Difficulty.cpp reads). Keys in
// [Start] and [Ending] are global; the same key inside a [ModeN] section applies
// only while the mode whose Name= matches the active difficulty is chosen.
#include "Extras.h"
#include "Difficulty.h"
#include "BioCard.h"
#include "../Globals.h"
#include "../DebugPrint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace {

struct Entry {
    int  sec;            // 0 = global ([Start]/[Ending]); n+1 = [ModeN]
    char key[32];
    char val[200];
};

const int MAX_ENTRIES = 160;
Entry s_entries[MAX_ENTRIES];
int   s_entryCount = 0;
char  s_modeName[DIFF_MAX_MODES + 1][DIFF_NAME_LEN];   // Name= of each [ModeN], upper-cased
bool  s_loaded = false;

// Rebecca's own 8-slot array, used when Chris or Rebecca is widened beyond 6 slots.
ItemSlot s_rebeccaExt[8];

struct ItemName { const char* name; int id; };
const ItemName kItemNames[] = {
#include "ExtrasItemNames.inc"
};

bool IEq(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        if (!*a) return true;
    }
}

char* Trim(char* s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

int ModeSection(const char* sec)
{
    if (tolower((unsigned char)sec[0]) != 'm' || tolower((unsigned char)sec[1]) != 'o' ||
        tolower((unsigned char)sec[2]) != 'd' || tolower((unsigned char)sec[3]) != 'e') return -1;
    const char* d = sec + 4;
    if (!*d) return -1;
    for (const char* p = d; *p; ++p) if (!isdigit((unsigned char)*p)) return -1;
    const int n = atoi(d);
    return (n >= 0 && n < DIFF_MAX_MODES) ? n : -1;
}

void EnsureLoaded(void)
{
    if (!s_loaded) Extras_Reload();
}

// Value for `key`: the active mode's own entry wins over the global one. NULL if unset.
const char* Get(const char* key)
{
    EnsureLoaded();
    const DifficultyMode* m = Difficulty_Active();
    const char* global = NULL;
    for (int i = 0; i < s_entryCount; ++i) {
        const Entry& e = s_entries[i];
        if (!IEq(e.key, key)) continue;
        if (e.sec == 0) { global = e.val; continue; }
        if (m && IEq(s_modeName[e.sec - 1], m->name)) return e.val;
    }
    return global;
}

int GetInt(const char* key, int def)
{
    const char* v = Get(key);
    if (!v || !*v) return def;
    return atoi(v);
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool IsRebeccaExtended(void)
{
    return Extras_CharSlotCount(0) > 6 || Extras_CharSlotCount(3) > 6;
}

// "KNIFE", "BERETTA", "0x0B", "11" -> item id, -1 on failure.
int ParseItemId(const char* tok)
{
    if (!*tok) return -1;
    if (isdigit((unsigned char)tok[0])) return (int)strtol(tok, NULL, 0);
    for (size_t i = 0; i < sizeof(kItemNames) / sizeof(kItemNames[0]); ++i)
        if (IEq(tok, kItemNames[i].name)) return kItemNames[i].id;
    return -1;
}

// Parses "KNIFE, BERETTA:15, 3:7, NONE". Returns the item count, or -1 if `key` is unset.
int ParseItemList(const char* key, unsigned char* ids, unsigned char* qtys, int maxItems)
{
    const char* v = Get(key);
    if (!v) return -1;
    char buf[200];
    strncpy(buf, v, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int n = 0;
    char* save = buf;
    while (*save) {
        char* tok = save;
        while (*save && *save != ',' && *save != ';') ++save;
        if (*save) *save++ = 0;
        tok = Trim(tok);
        if (!*tok || IEq(tok, "NONE")) continue;
        int qty = 1;
        char* colon = strchr(tok, ':');
        if (colon) { *colon = 0; qty = atoi(Trim(colon + 1)); }
        tok = Trim(tok);
        const int id = ParseItemId(tok);
        if (id <= 0 || id > 255) { dbg_printf("[EXTRAS] unknown item '%s' in %s\n", tok, key); continue; }
        if (n >= maxItems) { dbg_printf("[EXTRAS] %s lists more items than slots, extra ignored\n", key); break; }
        ids[n]  = (unsigned char)id;
        qtys[n] = (unsigned char)Clamp(qty, 0, 255);
        ++n;
    }
    return n;
}

void FillSlots(ItemSlot* slots, int slotCount, const unsigned char* ids, const unsigned char* qtys, int n)
{
    for (int i = 0; i < slotCount; ++i) {
        slots[i].Id  = (i < n) ? ids[i]  : 0;
        slots[i].qty = (i < n) ? qtys[i] : 0;
    }
}

} // namespace

// ---------------------------------------------------------------------------
void Extras_Reload(void)
{
    s_loaded = true;
    s_entryCount = 0;
    memset(s_modeName, 0, sizeof(s_modeName));

    const char* path = Difficulty_IniPath();
    FILE* f = (path && path[0]) ? fopen(path, "r") : NULL;
    if (!f) return;

    int section = -2;           // -2 ignored, 0 global, n+1 = [ModeN]
    char line[300];
    while (fgets(line, sizeof(line), f)) {
        char* p = Trim(line);
        if (!*p || *p == ';' || *p == '#') continue;
        if (*p == '[') {
            char* e = strchr(p, ']');
            if (!e) { section = -2; continue; }
            *e = 0;
            char* name = Trim(p + 1);
            if (IEq(name, "Start") || IEq(name, "Ending")) section = 0;
            else { const int m = ModeSection(name); section = (m >= 0) ? m + 1 : -2; }
            continue;
        }
        if (section == -2) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = Trim(p);
        char* val = Trim(eq + 1);
        if (section > 0 && IEq(key, "Name")) {
            for (int i = 0; i < DIFF_NAME_LEN - 1 && val[i]; ++i)
                s_modeName[section - 1][i] = (char)toupper((unsigned char)val[i]);
            continue;
        }
        if (s_entryCount >= MAX_ENTRIES) continue;
        // mode sections only matter for the keys we own; the rest are Difficulty.cpp's
        Entry& en = s_entries[s_entryCount];
        en.sec = section;
        strncpy(en.key, key, sizeof(en.key) - 1); en.key[sizeof(en.key) - 1] = 0;
        strncpy(en.val, val, sizeof(en.val) - 1); en.val[sizeof(en.val) - 1] = 0;
        ++s_entryCount;
    }
    fclose(f);
    dbg_printf("[EXTRAS] ChrisSlots=%d RebeccaSlots=%d (%d setting(s) read)\n",
               Extras_CharSlotCount(0), Extras_CharSlotCount(3), s_entryCount);
}

// ---------------------------------------------------------------------------
// Inventory size
// ---------------------------------------------------------------------------
int Extras_CharSlotCount(int charId)
{
    switch (charId & 3) {
    case 1:  return 8;                                           // Jill
    case 3:  return Clamp(GetInt("RebeccaSlots", 6), 6, 8);      // Rebecca
    default: return Clamp(GetInt("ChrisSlots", 6), 6, 8);        // Chris
    }
}

ItemSlot* Extras_RebeccaSlotsPtr(void)
{
    return IsRebeccaExtended() ? s_rebeccaExt : (ItemSlot*)g_BioCard.rebeccaItemsSlots;
}

int Extras_RebeccaStorage(void) { return IsRebeccaExtended() ? 8 : 6; }

int Extras_ActiveSlotCount(void)
{
    if (g_ItemSlotsPointer != NULL && g_ItemSlotsPointer == (void*)Extras_RebeccaSlotsPtr())
        return Extras_CharSlotCount(3);
    return Extras_CharSlotCount(g_playerEntity.id & 3);
}

// ---------------------------------------------------------------------------
// Starting items
// ---------------------------------------------------------------------------
void Extras_ApplyStartItems(void)
{
    if ((g_main_state_flags2 & MSF2_ATTRACT_DEMO) != 0) return;   // demo keeps the stock loadout

    unsigned char ids[8], qtys[8];
    const int ch = g_playerEntity.id & 3;
    const bool chris = (ch == CHAR_CHRIS);
    const int slotCount = Extras_CharSlotCount(ch);

    const int n = ParseItemList(chris ? "ChrisItems" : "JillItems", ids, qtys, slotCount);
    if (n >= 0) {
        FillSlots(g_ItemsSlots, slotCount, ids, qtys, n);
        for (int i = 0; i < n; ++i) g_ItemSlotIndices[i] = (unsigned char)i;
        g_TotalHeldItems = (unsigned char)n;
        g_ItemSlotsBitmask = n ? ((1u << n) - 1) : 0;
    } else if (chris && slotCount > 6) {
        // widened but no explicit list: clear the new slots (they overlap old Rebecca data)
        for (int i = (int)g_TotalHeldItems; i < slotCount; ++i) { g_ItemsSlots[i].Id = 0; g_ItemsSlots[i].qty = 0; }
    }

    if (chris) {
        const int rn = ParseItemList("RebeccaItems", ids, qtys, Extras_CharSlotCount(3));
        if (rn >= 0) FillSlots(Extras_RebeccaSlotsPtr(), Extras_CharSlotCount(3), ids, qtys, rn);
    }
    dbg_printf("[EXTRAS] start: %d slot(s), %d item(s)\n", slotCount, (int)g_TotalHeldItems);
}

// ---------------------------------------------------------------------------
// Ending
// ---------------------------------------------------------------------------
int Extras_EndingEpilogue(void)   { return GetInt("Epilogue", 1) != 0; }
int Extras_EndingSpecialKey(void) { return GetInt("SpecialKey", 1) != 0; }

int Extras_EndingRocket(void)
{
    const char* v = Get("RocketLauncher");
    if (!v || !*v || IEq(v, "auto")) return -1;
    return atoi(v) != 0;
}

int Extras_UnderTimeLimit(unsigned int timerTicks)
{
    const int minutes = GetInt("TimeLimitMinutes", 240);
    if (minutes <= 0) return 1;                                // 0 = always
    return timerTicks < (unsigned int)minutes * 60u * 30u;     // 30 ticks per second
}

int Extras_TimedRewardItem(int charId, int* qty)
{
    if (qty) *qty = Clamp(GetInt("TimedRewardQty", 1), 1, 255);
    const char* v = Get("TimedReward");
    if (!v || !*v || IEq(v, "auto")) return ((charId & 3) != CHAR_CHRIS) ? ITEM_INGRAM : ITEM_MINIMI;
    if (IEq(v, "none")) return 0;
    const int id = ParseItemId(v);
    return id > 0 ? id : 0;
}

int Extras_TimedRewardInfinite(void) { return GetInt("TimedRewardInfinite", 0) != 0; }

int Extras_EndingExtraItems(unsigned char* ids, unsigned char* qtys, int maxItems)
{
    const int n = ParseItemList("ExtraItems", ids, qtys, maxItems);
    return n < 0 ? 0 : n;
}

// ---------------------------------------------------------------------------
// Save-file tail: 'E' 'X' 'T' <ver> + Rebecca's 8 extended slots (16 bytes).
// ---------------------------------------------------------------------------
#define EXTRAS_SAVE_OFFSET 0x7C0

void Extras_WriteSave(char* fileBuffer)
{
    fileBuffer[EXTRAS_SAVE_OFFSET + 0] = 'E';
    fileBuffer[EXTRAS_SAVE_OFFSET + 1] = 'X';
    fileBuffer[EXTRAS_SAVE_OFFSET + 2] = 'T';
    fileBuffer[EXTRAS_SAVE_OFFSET + 3] = 1;
    memcpy(fileBuffer + EXTRAS_SAVE_OFFSET + 4, s_rebeccaExt, sizeof(s_rebeccaExt));
}

void Extras_ReadSave(const char* fileBuffer, int fileSize)
{
    memset(s_rebeccaExt, 0, sizeof(s_rebeccaExt));
    if (fileSize < EXTRAS_SAVE_OFFSET + 4 + (int)sizeof(s_rebeccaExt)) return;
    if (fileBuffer[EXTRAS_SAVE_OFFSET] == 'E' && fileBuffer[EXTRAS_SAVE_OFFSET + 1] == 'X' &&
        fileBuffer[EXTRAS_SAVE_OFFSET + 2] == 'T')
        memcpy(s_rebeccaExt, fileBuffer + EXTRAS_SAVE_OFFSET + 4, sizeof(s_rebeccaExt));
}
