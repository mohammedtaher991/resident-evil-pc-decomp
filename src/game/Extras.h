// Extras.h - new-game / ending tuning read from difficulty.ini ([Start] and [Ending]).
//
// See docs/EXTRAS.md. Every key can also be written inside a [ModeN] section to
// apply to that difficulty mode only. With no keys present nothing changes.
#pragma once

struct ItemSlot;

// (Re)read the [Start]/[Ending] settings. Called after Difficulty_Reload().
void Extras_Reload(void);

// ---- inventory size -------------------------------------------------------
// Slots the ACTIVE character (Chris, Jill, or Rebecca while she is swapped in) has.
int  Extras_ActiveSlotCount(void);
// Slots a given character has (0 = Chris, 1 = Jill, 3 = Rebecca).
int  Extras_CharSlotCount(int charId);
// Rebecca's slots. Normally the BioCard array; when Chris or Rebecca is widened
// beyond 6 slots Rebecca moves to her own 8-slot array (saved in the save file's
// spare tail) so she no longer shares memory with Chris's slots 7-8.
ItemSlot* Extras_RebeccaSlotsPtr(void);
int  Extras_RebeccaStorage(void);   // slots actually allocated for Rebecca (6, or 8 when extended)

// ---- starting items -------------------------------------------------------
// Called at the end of SetInitialItems(): applies ChrisItems= / JillItems= /
// RebeccaItems= when set.
void Extras_ApplyStartItems(void);

// ---- ending ---------------------------------------------------------------
int  Extras_EndingEpilogue(void);      // [Ending] Epilogue (1 = play the rocket-launcher scene)
int  Extras_EndingSpecialKey(void);    // [Ending] SpecialKey (1 = grant the special key)
int  Extras_EndingRocket(void);        // [Ending] RocketLauncher: -1 auto, 0 never, 1 always
int  Extras_UnderTimeLimit(unsigned int timerTicks);      // reward time limit test
// Item granted for beating the time limit for character `charId`
// (0 = nothing). Also reports its quantity.
int  Extras_TimedRewardItem(int charId, int* qty);
int  Extras_TimedRewardInfinite(void); // Colt Python reward never runs dry
// Extra items always granted at the ending (ExtraItems=). Returns count.
int  Extras_EndingExtraItems(unsigned char* ids, unsigned char* qtys, int maxItems);

// Save-file tail (spare bytes, see Difficulty.h): Rebecca's extended inventory.
void Extras_WriteSave(char* fileBuffer);
void Extras_ReadSave(const char* fileBuffer, int fileSize);
