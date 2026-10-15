// AutoPickup.cpp - auto pickup of gold, runes and gems for Diablo II LoD (single player, via PlugY)
//
// STATUS: all version-specific hooks are filled in for 1.13c from D2BS, but this has NOT been
// compiled or run. Items marked TODO(VERIFY) are from memory and must be checked in game.
//
// Build: 32-bit (x86) DLL, e.g.  cl /LD /O2 /MT AutoPickup.cpp   (use the x86 developer prompt)
// Load:  PlugY.ini -> DllToLoad=AutoPickup.dll   (put the DLL in the D2 folder)
//
// Use offline / single player only. On Battle.net this would be flagged by Warden.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>

// ---------------- settings ----------------
static const int   POLL_MS      = 150;   // how often to scan
static const int   RADIUS       = 2;     // max tile distance for a pickup. If the character still walks
                                         // toward items, lower it; if items are missed, raise it by 1.
static const bool  ONLY_WHEN_IDLE = false; // only pick up while standing still (not walking/attacking/casting)
static const bool  PICK_GOLD    = true;
static const bool  PICK_RUNES   = true;
static const bool  ONLY_HIGH_RUNES = true;  // true: only runes from MIN_RUNE_NUMBER up are picked up, lower runes stay on the ground
static const int   MIN_RUNE_NUMBER = 23;    // 23 = Mal (r23). Runes run r01 (El) .. r33 (Zod)
static const bool  PICK_GEMS    = true;


// ---------------- debug log (writes AutoPickup.log next to Game.exe) ----------------
static const bool DEBUG_LOG = false;   // set true to write AutoPickup.log again
static void Log(const char* fmt, ...)
{
    if (!DEBUG_LOG) return;
    FILE* f = fopen("AutoPickup.log", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---------------- item filter (pure logic, version independent) ----------------
// Item codes are 3 letters (+ trailing space in the txt files, not in memory lookups).
static bool IsGold(const char* c) { return !strcmp(c, "gld"); }

static bool IsRune(const char* c)               // r01 .. r33
{
    return c[0] == 'r' && c[1] >= '0' && c[1] <= '3' && c[2] >= '0' && c[2] <= '9' &&
           atoi(c + 1) >= 1 && atoi(c + 1) <= 33;
}

static bool IsGem(const char* c)                // gc?, gf?, gs?, gl?, gz?, gp?  and sk[cfulz]
{
    const char* grades = "cfslzp";
    const char* colors = "vybgrw";
    if (c[0] == 'g' && strchr(grades, c[1]) && strchr(colors, c[2])) return true;
    if (c[0] == 's' && c[1] == 'k' && strchr("cfulz", c[2])) return true;
    return false;
}

static int RuneNumber(const char* c) { return atoi(c + 1); }   // "r23" -> 23 (only call if IsRune)

static bool WantItem(const char* code)
{
    if (PICK_GOLD && IsGold(code)) return true;
    if (PICK_RUNES && IsRune(code))
        return !ONLY_HIGH_RUNES || RuneNumber(code) >= MIN_RUNE_NUMBER;
    if (PICK_GEMS && IsGem(code)) return true;
    return false;
}

// ---------------- game structures (from D2BS D2Structs.h, 1.13c) ----------------
#pragma pack(push, 1)
struct UnitAny {
    DWORD    dwType;        // 0x00  0 = player, 4 = item
    DWORD    dwTxtFileNo;   // 0x04  index into the item txt (armor/weapon/misc)
    DWORD    _1;            // 0x08
    DWORD    dwUnitId;      // 0x0C
    DWORD    dwMode;        // 0x10  items: 3 = on ground (verify), 4 = cursor
    void*    pData;         // 0x14  PlayerData* / ItemData* / ...
    DWORD    dwAct;         // 0x18
    void*    pAct;          // 0x1C
    DWORD    dwSeed[2];     // 0x20
    DWORD    _2;            // 0x28
    void*    pPath;         // 0x2C  Path* (players) / ItemPath* (items)
    DWORD    _3[5];         // 0x30
    DWORD    dwGfxFrame;    // 0x44
    DWORD    dwFrameRemain; // 0x48
    WORD     wFrameRate;    // 0x4C
    WORD     _4;            // 0x4E
    BYTE*    pGfxUnk;       // 0x50
    DWORD*   pGfxInfo;      // 0x54
    DWORD    _5;            // 0x58
    void*    pStats;        // 0x5C
    void*    pInventory;    // 0x60
    void*    ptLight;       // 0x64
    DWORD    _6[9];         // 0x68
    WORD     wX;            // 0x8C
    WORD     wY;            // 0x8E
    DWORD    _7;            // 0x90
    DWORD    dwOwnerType;   // 0x94
    DWORD    dwOwnerId;     // 0x98
    DWORD    _8[2];         // 0x9C
    void*    pOMsg;         // 0xA4
    void*    pInfo;         // 0xA8
    DWORD    _9[6];         // 0xAC
    DWORD    dwFlags;       // 0xC4
    DWORD    dwFlags2;      // 0xC8
    DWORD    _10[5];        // 0xCC
    UnitAny* pChangedNext;  // 0xE0
    UnitAny* pRoomNext;     // 0xE4
    UnitAny* pListNext;     // 0xE8  (header comment also says "-> 0xD8", see note below)
};
#pragma pack(pop)

// Path (used by players/monsters). Ground items use ItemPath instead - not added yet.
#pragma pack(push, 1)
struct Path {
    WORD  xOffset;      // 0x00
    WORD  xPos;         // 0x02  tile X
    WORD  yOffset;      // 0x04
    WORD  yPos;         // 0x06  tile Y
    DWORD _1[2];        // 0x08
    WORD  xTarget;      // 0x10
    WORD  yTarget;      // 0x12
};
#pragma pack(pop)
static_assert(offsetof(Path, xPos) == 0x02, "Path layout");
static_assert(offsetof(Path, yPos) == 0x06, "Path layout");

// ItemPath: position of an item lying on the ground (use Path for everything after 0x10).
// ItemData: extra item info. NOTE: ItemData relies on the compiler's NATURAL alignment (the
// WORD at 0x48 has a padding byte before it) - do not wrap it in #pragma pack(1).
// These offsets also assume a 32-bit build (pointers are 4 bytes).
static_assert(sizeof(void*) == 4, "Build this DLL as 32-bit (x86)");
struct ItemPath {
    DWORD _1[3];        // 0x00
    DWORD dwPosX;       // 0x0C
    DWORD dwPosY;       // 0x10
};

struct ItemData {
    DWORD dwQuality;    // 0x00
    DWORD _1[2];        // 0x04
    DWORD dwItemFlags;  // 0x0C  1 = owned by player, 0xFFFFFFFF = not owned
    DWORD _2[2];        // 0x10
    DWORD dwFlags;      // 0x18
    DWORD _3[3];        // 0x1C
    DWORD dwQuality2;   // 0x28
    DWORD dwItemLevel;  // 0x2C
    DWORD _4[2];        // 0x30
    WORD  wPrefix;      // 0x38
    WORD  _5[2];        // 0x3A
    WORD  wSuffix;      // 0x3E
    DWORD _6;           // 0x40
    BYTE  BodyLocation; // 0x44
    BYTE  ItemLocation; // 0x45  non-body/belt location (body/belt == 0xFF)
    BYTE  _7;           // 0x46
    WORD  _8;           // 0x47
    DWORD _9[4];        // 0x48
    void* pOwnerInventory; // 0x5C
    DWORD _10;          // 0x60
    void* pNextInvItem; // 0x64
    BYTE  _11;          // 0x68
    BYTE  NodePage;     // 0x69  actual location, most reliable
    WORD  _12;          // 0x6A
    DWORD _13[6];       // 0x6C
    void* pOwner;       // 0x84
};
static_assert(offsetof(ItemPath, dwPosX) == 0x0C, "ItemPath layout");
static_assert(offsetof(ItemPath, dwPosY) == 0x10, "ItemPath layout");
static_assert(offsetof(ItemData, NodePage) == 0x69, "ItemData layout");
static_assert(offsetof(ItemData, pOwner) == 0x84, "ItemData layout");

static_assert(offsetof(UnitAny, dwUnitId)   == 0x0C, "UnitAny layout");
static_assert(offsetof(UnitAny, dwMode)     == 0x10, "UnitAny layout");
static_assert(offsetof(UnitAny, pPath)      == 0x2C, "UnitAny layout");
static_assert(offsetof(UnitAny, pInventory) == 0x60, "UnitAny layout");
static_assert(offsetof(UnitAny, wX)         == 0x8C, "UnitAny layout");
static_assert(offsetof(UnitAny, pListNext)  == 0xE8, "UnitAny layout");

// Inventory (UnitAny::pInventory).
#pragma pack(push, 1)
struct Inventory {
    DWORD    dwSignature;   // 0x00
    BYTE*    bGame1C;       // 0x04
    UnitAny* pOwner;        // 0x08
    UnitAny* pFirstItem;    // 0x0C
    UnitAny* pLastItem;     // 0x10
    DWORD    _1[2];         // 0x14
    DWORD    dwLeftItemUid; // 0x1C
    UnitAny* pCursorItem;   // 0x20
    DWORD    dwOwnerId;     // 0x24
    DWORD    dwItemCount;   // 0x28
};
#pragma pack(pop)
static_assert(offsetof(Inventory, pFirstItem)  == 0x0C, "Inventory layout");
static_assert(offsetof(Inventory, pCursorItem) == 0x20, "Inventory layout");

// ---------------- game access (VERSION SPECIFIC - fill in) ----------------
struct Pos { int x, y; };

// Return the local player's unit pointer, or nullptr when not in a game.
// 1.13c: D2CLIENT GetPlayerUnit, UnitAny* __stdcall (void), D2Client.dll + 0xA4D60 (from D2BS D2Ptrs.h)
static void* GetPlayerUnit()
{
    HMODULE client = GetModuleHandleA("D2Client.dll");
    if (!client) return nullptr;
    typedef void* (__stdcall *GetPlayerUnit_t)(void);
    GetPlayerUnit_t fn = reinterpret_cast<GetPlayerUnit_t>(
        reinterpret_cast<uintptr_t>(client) + 0xA4D60);
    return fn();    // nullptr when not in a game
}

// Player's current tile position.
// Players use Path (xPos/yPos); ground items are handled by GetItemPos (ItemPath).
static bool GetUnitPos(void* unit, Pos& out)
{
    UnitAny* u = static_cast<UnitAny*>(unit);
    if (!u || u->dwType != 0 || !u->pPath) return false;   // player only for now
    Path* p = static_cast<Path*>(u->pPath);
    out.x = p->xPos;
    out.y = p->yPos;
    return true;
}

// Position of a ground item (ItemPath, not Path).
static bool GetItemPos(UnitAny* it, Pos& out)
{
    if (!it || it->dwType != 4 || !it->pPath) return false;
    ItemPath* p = static_cast<ItemPath*>(it->pPath);
    out.x = static_cast<int>(p->dwPosX);
    out.y = static_cast<int>(p->dwPosY);
    return true;
}

// Walk the client's unit hash table and call cb for every ground item
// (UnitAny type 4, item mode 3 = on ground).
//
// 1.13c: the D2BS "ClientSideUnitHashTables" address (0x109A08) did NOT contain the items; the
// block 0xC00 further on (0x10A608) did. See UNIT_TABLE_OFFSET below.
// The block holds one 128-bucket table per unit type (0..5, back to back): items = index 4*128.
// Every unit is re-checked for dwType == 4, so a wrong assumption skips units rather than crashing.
struct GroundItem { uint32_t unitId; Pos pos; char code[4]; };
typedef void (*ItemCb)(const GroundItem&, void*);

// Item txt number -> 3-letter code ("gld", "r01", "gpv" ...).
// 1.13c: D2COMMON GetItemText, ItemTxt* __stdcall (DWORD dwItemNo), D2Common.dll ordinal 10695.
// ItemTxt code offset: I believe 0x80 (a 0x40-wchar name buffer comes first). NOT read from your
// header - please compare with the ItemTxt struct in D2Structs.h.
static const size_t ITEMTXT_CODE_OFFSET = 0x80;

static bool GetItemCode(DWORD txtFileNo, char out[4])
{
    typedef void* (__stdcall *GetItemText_t)(DWORD);
    static GetItemText_t fn = nullptr;
    if (!fn)
    {
        HMODULE common = GetModuleHandleA("D2Common.dll");
        if (!common) return false;
        fn = reinterpret_cast<GetItemText_t>(GetProcAddress(common, MAKEINTRESOURCEA(10695)));
        if (!fn) return false;
    }
    const char* txt = static_cast<const char*>(fn(txtFileNo));
    if (!txt) return false;
    memcpy(out, txt + ITEMTXT_CODE_OFFSET, 3);
    out[3] = 0;                       // file codes are 3 chars + trailing space
    return true;
}

// Probe result (1.13c): ground items are found in the block at D2Client.dll + 0x10A608
// (= 0x109A08 + 0xC00), 128 buckets per unit type, chained through the pointer at +0xE4.
static const uintptr_t UNIT_TABLE_OFFSET = 0x10A608;
static const size_t    UNIT_NEXT_OFFSET  = 0xE4;
static UnitAny* NextUnit(UnitAny* u)
{
    return *reinterpret_cast<UnitAny**>(reinterpret_cast<uintptr_t>(u) + UNIT_NEXT_OFFSET);
}

static const int ITEM_TYPE        = 4;
static const int ITEM_MODE_GROUND = 3;   // TODO(VERIFY) on 1.13c
static const int BUCKETS          = 128;

static void ForEachGroundItem(ItemCb cb, void* ctx)
{
    HMODULE client = GetModuleHandleA("D2Client.dll");
    if (!client) return;
    UnitAny** tables = reinterpret_cast<UnitAny**>(reinterpret_cast<uintptr_t>(client) + UNIT_TABLE_OFFSET);
    UnitAny** items  = tables + ITEM_TYPE * BUCKETS;

    __try
    {
        for (int b = 0; b < BUCKETS; ++b)
        {
            int guard = 0;
            for (UnitAny* u = items[b]; u && guard < 2000; u = NextUnit(u), ++guard)
            {
                if (u->dwType != ITEM_TYPE || u->dwMode != ITEM_MODE_GROUND) continue;

                GroundItem gi = {};
                gi.unitId = u->dwUnitId;
                if (!GetItemPos(u, gi.pos)) continue;
                if (!GetItemCode(u->dwTxtFileNo, gi.code)) continue;
                cb(gi, ctx);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // bad pointer: skip this scan; try again next tick
    }
}

// Send a packet from client to server.
// 1.13c: D2NET SendPacket, void __stdcall (size_t aLen, DWORD arg1, BYTE* aPacket), D2Net.dll ordinal 10024
static void SendPacket(const uint8_t* data, size_t len)
{
    typedef void (__stdcall *SendPacket_t)(size_t, DWORD, BYTE*);
    static SendPacket_t fn = nullptr;
    if (!fn)
    {
        HMODULE net = GetModuleHandleA("D2Net.dll");
        if (!net) return;
        fn = reinterpret_cast<SendPacket_t>(GetProcAddress(net, MAKEINTRESOURCEA(10024)));
        if (!fn) return;
    }
    fn(len, 1, const_cast<BYTE*>(data));   // arg1: D2BS passes 1 (TODO(VERIFY) in D2BS source)
}

// ---------------- pickup ----------------
// Client->server 0x16 = "pick up ground item". Layout I believe is:
//   byte 0x16, dword unitType (=4 for item), dword itemUnitId, dword 0
// Gold goes straight into your gold total. Runes/gems normally land on the CURSOR.
// TODO(VERIFY): confirm the packet layout for your version.
static void PickUp(uint32_t unitId)
{
    uint8_t p[13] = {0};
    p[0] = 0x16;
    Log("PickUp: sending 0x16 for unitId=%u", unitId);
    *reinterpret_cast<uint32_t*>(p + 1) = 4;
    *reinterpret_cast<uint32_t*>(p + 5) = unitId;
    SendPacket(p, sizeof p);
}

// ---------------- placing the cursor item into the inventory ----------------
// Runes/gems/skulls are all 1x1. We build an occupancy grid of the 10x4 inventory from the items
// already in it, pick the first free cell, then tell the server to put the cursor item there.
//
// ItemTxt inventory size. The D2BS header is self-inconsistent here (comments say 0xFC/0xFD, the
// field sizes add up to 0x10F/0x110); my recollection of the game is 0x11C/0x11D. If the sanity
// check below keeps failing, try 0x10F/0x110.
static const size_t ITEMTXT_XSIZE_OFFSET = 0x11C;
static const size_t ITEMTXT_YSIZE_OFFSET = 0x11D;

static const int INV_W = 10, INV_H = 4;
static const int NODEPAGE_INVENTORY = 1;   // TODO(VERIFY): ItemData::NodePage value for the main inventory

// Raw ItemTxt pointer for an item txt number (same D2Common ordinal 10695 as GetItemCode).
static const char* GetItemTxtRaw(DWORD txtFileNo)
{
    typedef void* (__stdcall *GetItemText_t)(DWORD);
    static GetItemText_t fn = nullptr;
    if (!fn)
    {
        HMODULE common = GetModuleHandleA("D2Common.dll");
        if (!common) return nullptr;
        fn = reinterpret_cast<GetItemText_t>(GetProcAddress(common, MAKEINTRESOURCEA(10695)));
        if (!fn) return nullptr;
    }
    return static_cast<const char*>(fn(txtFileNo));
}

// Find the first free 1x1 cell. Returns false if there is none OR if the grid looks inconsistent
// (item out of bounds / overlapping), which means an offset above is wrong - we then do nothing.
static bool FindFreeCell(void* player, int& outX, int& outY)
{
    UnitAny* pl = static_cast<UnitAny*>(player);
    if (!pl || !pl->pInventory) return false;
    Inventory* inv = static_cast<Inventory*>(pl->pInventory);

    bool grid[INV_H][INV_W] = {};
    __try
    {
        int guard = 0;
        for (UnitAny* it = inv->pFirstItem; it && guard < 200; ++guard)
        {
            ItemData* d = static_cast<ItemData*>(it->pData);
            if (!d) break;
            if (d->NodePage == NODEPAGE_INVENTORY && it->dwType == ITEM_TYPE && it->pPath)
            {
                ItemPath* ip = static_cast<ItemPath*>(it->pPath);
                const char* txt = GetItemTxtRaw(it->dwTxtFileNo);
                if (!txt) return false;
                int w = static_cast<BYTE>(txt[ITEMTXT_XSIZE_OFFSET]);
                int h = static_cast<BYTE>(txt[ITEMTXT_YSIZE_OFFSET]);
                int x = static_cast<int>(ip->dwPosX), y = static_cast<int>(ip->dwPosY);

                if (w < 1 || w > 2 || h < 1 || h > 4) return false;                  // sanity
                if (x < 0 || y < 0 || x + w > INV_W || y + h > INV_H) return false;  // sanity
                for (int yy = y; yy < y + h; ++yy)
                    for (int xx = x; xx < x + w; ++xx)
                    {
                        if (grid[yy][xx]) return false;                             // overlap => bad data
                        grid[yy][xx] = true;
                    }
            }
            it = static_cast<UnitAny*>(d->pNextInvItem);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    for (int y = 0; y < INV_H; ++y)
        for (int x = 0; x < INV_W; ++x)
            if (!grid[y][x]) { outX = x; outY = y; return true; }
    return false;   // inventory full
}

// Client->server 0x18 = "put cursor item into a container". Layout I believe is:
//   byte 0x18, dword itemUnitId, dword x, dword y, dword container
// TODO(VERIFY): both the layout and the container value for the inventory are from memory.
// Left OFF by default: with it off, picked-up runes/gems wait on the cursor for you to place them.
static const bool ENABLE_AUTO_PLACE   = false;
static const DWORD PLACE_CONTAINER_INV = 0;   // TODO(VERIFY)

static void PlaceCursorItem(void* player)
{
    if (!ENABLE_AUTO_PLACE) return;
    UnitAny* pl = static_cast<UnitAny*>(player);
    if (!pl || !pl->pInventory) return;
    UnitAny* cur = static_cast<Inventory*>(pl->pInventory)->pCursorItem;
    if (!cur) return;

    int x, y;
    if (!FindFreeCell(player, x, y)) return;

    uint8_t p[17] = {0};
    p[0] = 0x18;
    *reinterpret_cast<uint32_t*>(p + 1)  = cur->dwUnitId;
    *reinterpret_cast<uint32_t*>(p + 5)  = static_cast<uint32_t>(x);
    *reinterpret_cast<uint32_t*>(p + 9)  = static_cast<uint32_t>(y);
    *reinterpret_cast<uint32_t*>(p + 13) = PLACE_CONTAINER_INV;
    SendPacket(p, sizeof p);
}

// True while the player is holding an item on the mouse cursor.
static bool HasCursorItem(void* player)
{
    UnitAny* u = static_cast<UnitAny*>(player);
    if (!u || !u->pInventory) return false;
    return static_cast<Inventory*>(u->pInventory)->pCursorItem != nullptr;
}

// ---------------- main loop ----------------
struct ScanCtx { Pos me; uint32_t best; int bestDist; };

static void OnItem(const GroundItem& it, void* v)
{
    ScanCtx* c = static_cast<ScanCtx*>(v);
    if (!WantItem(it.code)) return;
    int dx = it.pos.x - c->me.x, dy = it.pos.y - c->me.y;
    int d = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (d <= RADIUS && d < c->bestDist) { c->best = it.unitId; c->bestDist = d; }
}

// Diagnostic matrix: try both table blocks (client 0x109A08, and +0xC00 = 0x10A608) with several
// candidate "next" offsets, with cycle protection, and summarise unit types per table.
// Reading: "t4:3(4:3)" = table 4 has 3 units, all of dwType 4. "CYC" = a chain hit the step cap.
static void DumpProbe()
{
    HMODULE client = GetModuleHandleA("D2Client.dll");
    if (!client) return;
    const uintptr_t blocks[2]  = { 0x109A08, 0x10A608 };
    const char*     bname[2]   = { "client", "server?" };
    const size_t    nextOff[5] = { 0xD8, 0xE0, 0xE4, 0xE8, 0xEC };

    for (int bi = 0; bi < 2; ++bi)
        for (int ni = 0; ni < 5; ++ni)
        {
            char line[400]; int len = 0;
            len += _snprintf(line + len, sizeof(line) - len, "  probe %s next=0x%X :", bname[bi], (unsigned)nextOff[ni]);
            __try
            {
                UnitAny** base = reinterpret_cast<UnitAny**>(reinterpret_cast<uintptr_t>(client) + blocks[bi]);
                for (int t = 0; t < 6; ++t)
                {
                    int units = 0, types[8] = {}; bool cyc = false;
                    for (int b = 0; b < BUCKETS; ++b)
                    {
                        int steps = 0;
                        for (UnitAny* u = base[t * BUCKETS + b]; u; ++steps)
                        {
                            if (steps >= 300) { cyc = true; break; }
                            ++units;
                            if (u->dwType < 8) ++types[u->dwType];
                            u = *reinterpret_cast<UnitAny**>(reinterpret_cast<uintptr_t>(u) + nextOff[ni]);
                        }
                    }
                    len += _snprintf(line + len, sizeof(line) - len, " t%d:%d(", t, units);
                    for (int k = 0; k < 6; ++k)
                        if (types[k]) len += _snprintf(line + len, sizeof(line) - len, "%d:%d,", k, types[k]);
                    len += _snprintf(line + len, sizeof(line) - len, ")%s", cyc ? "CYC" : "");
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { len += _snprintf(line + len, sizeof(line) - len, " EXCEPTION"); }
            line[sizeof(line) - 1] = 0;
            Log("%s", line);
        }
}

// Diagnostic: summarise all six unit hash tables, and check that the player is in table 0.
// If the player is NOT found, my assumption about the table location/layout is wrong.
static void DumpTables(void* player)
{
    HMODULE client = GetModuleHandleA("D2Client.dll");
    if (!client) return;
    UnitAny** base = reinterpret_cast<UnitAny**>(reinterpret_cast<uintptr_t>(client) + 0x109A08);
    UnitAny*  pl   = static_cast<UnitAny*>(player);
    __try
    {
        const DWORD* raw = reinterpret_cast<const DWORD*>(base);
        Log("  table base=%p first dwords: %08X %08X %08X %08X", base, raw[0], raw[1], raw[2], raw[3]);
        for (int t = 0; t < 6; ++t)
        {
            int buckets = 0, units = 0, types[8] = {};
            for (int b = 0; b < BUCKETS; ++b)
            {
                UnitAny* head = base[t * BUCKETS + b];
                if (head) ++buckets;
                int guard = 0;
                for (UnitAny* u = head; u && guard < 2000; u = u->pListNext, ++guard)
                {
                    ++units;
                    if (u->dwType < 8) ++types[u->dwType];
                }
            }
            Log("  table[%d]: nonEmptyBuckets=%d units=%d  dwType counts 0..5: %d %d %d %d %d %d",
                t, buckets, units, types[0], types[1], types[2], types[3], types[4], types[5]);
        }
        bool found = false;
        int guard = 0;
        for (UnitAny* u = base[pl->dwUnitId & 127]; u && guard < 2000; u = u->pListNext, ++guard)
            if (u == pl) { found = true; break; }
        Log("  player id=%u bucket=%u foundInTable0=%d  pListNext=%p pRoomNext=%p pChangedNext=%p",
            pl->dwUnitId, pl->dwUnitId & 127, found, pl->pListNext, pl->pRoomNext, pl->pChangedNext);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("  EXCEPTION in DumpTables"); }
}

// Diagnostic: log what the plugin can see. Runs every ~2 seconds while in a game.
static void DumpState(void* player)
{
    static int dumps = 0;
    if (++dumps > 150) return;          // stop logging after ~5 minutes
    DumpTables(player);
    {   // does the table block we actually use contain the same player unit that GetPlayerUnit returns?
        HMODULE client = GetModuleHandleA("D2Client.dll");
        if (client)
        {
            UnitAny** t0 = reinterpret_cast<UnitAny**>(reinterpret_cast<uintptr_t>(client) + UNIT_TABLE_OFFSET);
            __try
            {
                for (int b = 0; b < BUCKETS; ++b)
                    for (UnitAny* u = t0[b]; u; u = NextUnit(u))
                        Log("  used-block player unit=%p (GetPlayerUnit=%p) id=%u type=%u", u, player, u->dwUnitId, u->dwType);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { Log("  EXCEPTION reading used-block table 0"); }
        }
    }
    if (dumps == 1 || dumps % 4 == 0) DumpProbe();
    Pos me = {-1, -1};
    bool havePos = GetUnitPos(player, me);
    Log("tick: player=%p pos=%d,%d (ok=%d) cursor=%d", player, me.x, me.y, havePos, HasCursorItem(player));

    HMODULE client = GetModuleHandleA("D2Client.dll");
    if (!client) return;
    UnitAny** items = reinterpret_cast<UnitAny**>(reinterpret_cast<uintptr_t>(client) + UNIT_TABLE_OFFSET)
                      + ITEM_TYPE * BUCKETS;
    int total = 0, shown = 0;
    int modeCount[16] = {};
    __try
    {
        for (int b = 0; b < BUCKETS; ++b)
        {
            int guard = 0;
            for (UnitAny* u = items[b]; u && guard < 2000; u = NextUnit(u), ++guard)
            {
                if (u->dwType != ITEM_TYPE) continue;
                ++total;
                if (u->dwMode < 16) ++modeCount[u->dwMode];
                if (shown < 12 && u->dwMode != 0 && u->dwMode != 1)   // skip stored/equipped, show the rest
                {
                    Pos ip = {-1, -1};
                    bool okp = GetItemPos(u, ip);
                    char code[4] = {0};
                    bool okc = GetItemCode(u->dwTxtFileNo, code);
                    Log("  item id=%u txt=%u mode=%u pos=%d,%d(ok=%d) code='%s'(ok=%d)",
                        u->dwUnitId, u->dwTxtFileNo, u->dwMode, ip.x, ip.y, okp, okc ? code : "?", okc);
                    ++shown;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("  EXCEPTION while walking item table"); }
    Log("  item units seen=%d  by mode: 0=%d 1=%d 2=%d 3=%d 4=%d 5=%d 6=%d", total,
        modeCount[0], modeCount[1], modeCount[2], modeCount[3], modeCount[4], modeCount[5], modeCount[6]);
}

static DWORD WINAPI Worker(LPVOID)
{
    Log("Worker thread started");
    DWORD lastDump = 0;
    for (;;)
    {
        Sleep(POLL_MS);
        void* player = GetPlayerUnit();
        if (!player) continue;
        if (GetTickCount() - lastDump > 2000) { lastDump = GetTickCount(); DumpState(player); }
        if (HasCursorItem(player)) { PlaceCursorItem(player); continue; }

        if (ONLY_WHEN_IDLE)
        {
            // Player modes: 1 = neutral, 5 = town neutral. Walking (2/3/6), attacking (7/8), casting (10),
            // getting hit (4) etc. are skipped so a pickup never interrupts a fight or a run.
            DWORD mode = static_cast<UnitAny*>(player)->dwMode;
            if (mode != 1 && mode != 5) continue;
        }

        ScanCtx ctx = {};
        ctx.bestDist = RADIUS + 1;
        if (!GetUnitPos(player, ctx.me)) continue;

        ForEachGroundItem(OnItem, &ctx);
        if (ctx.best) PickUp(ctx.best);     // one item per tick
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(h);
        Log("---- AutoPickup.dll loaded ----");
        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }
    return TRUE;
}
