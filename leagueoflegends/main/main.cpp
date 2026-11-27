#define NOMINMAX
#include "../Memory/Memory.h"

#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
#include <intrin.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <gdiplus.h>
#include <urlmon.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "urlmon.lib")

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx11.h"
#include "../imgui/imgui_impl_win32.h"
#include "../Misc/InputDevice.h"
#include "../Memory/InputManager.h"
#include "xorstr.hpp"
#include "Auth.h"
#include "ChampionCombo.h"

// ── Compile-out logging ────────────────────────────────────────────
// VX_LOG_ENABLED=1 keeps printf diagnostics (debug builds).
// VX_LOG_ENABLED=0 (release) preprocesses every VX_LOG() away so the
// format-string literals never appear in the binary's .rdata section.
#ifndef VX_LOG_ENABLED
#  ifdef NDEBUG
#    define VX_LOG_ENABLED 0
#  else
#    define VX_LOG_ENABLED 1
#  endif
#endif

#if VX_LOG_ENABLED
#  define VX_LOG(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
#  define VX_LOG(fmt, ...)  ((void)0)
#endif

// ── XorShift32 PRNG (seeded from RDTSC) ───────────────────────────
static uint32_t g_xorState = 0;
static uint32_t xorshift32() {
    if (g_xorState == 0) g_xorState = (uint32_t)__rdtsc();
    g_xorState ^= g_xorState << 13;
    g_xorState ^= g_xorState >> 17;
    g_xorState ^= g_xorState << 5;
    return g_xorState;
}
static float xorFloat(float lo, float hi) {
    return lo + (float)(xorshift32() & 0xFFFF) / 65535.f * (hi - lo);
}

// Offsets - mutable for auto-scan, defaults from 2026-11-18
namespace Offsets {
    constexpr uintptr_t ImageBase = 0x7FF69A1B0000;
    namespace Globals {
        inline uintptr_t LocalPlayer      = 0x1D3CF30;
        inline uintptr_t HeroManager      = 0x1CFAA70;
        inline uintptr_t MinionManager    = 0x1CFE0B8;
        inline uintptr_t TurretManager    = 0x1D04798;
        inline uintptr_t ObjectManager    = 0x1CFA920;
        inline uintptr_t GameTime         = 0x1D08560;
        inline uintptr_t MissileManager   = 0x1CFE0D0;
        inline uintptr_t MissileContainer = 0x1CEE208;  // not in new dump, kept
        inline uintptr_t NavGrid          = 0x1CFE010;
        inline uintptr_t ViewPort         = 0x1CBBA50;  // not in new dump, kept
        inline uintptr_t MouseScreenVec2  = 0x1CBBA58;  // not in new dump, kept
        inline uintptr_t UnderMouseObj    = 0x1CBBC98;  // not in new dump, kept
        inline uintptr_t HudInstance      = 0x1CFAAE0;
        inline uintptr_t ChatManager      = 0x1CCDED0;  // not in new dump, kept
        inline uintptr_t NetClient        = 0x1CFA940;
    }
    namespace GameObject {
        constexpr uintptr_t IsDead              = 0x160; // IsDeadEncrypted in dump
        inline uintptr_t Team                   = 0x15C;
        constexpr uintptr_t Position            = 0x160;
        constexpr uintptr_t Visible             = 0x210;
        constexpr uintptr_t Visible2            = 0x218;
        constexpr uintptr_t Mana                = 0x240;
        constexpr uintptr_t MaxMana             = 0x270;
        constexpr uintptr_t BoundingRadius      = 0x554;   // not in new dump, kept
        constexpr uintptr_t ResourceSize        = 0x860;
        constexpr uintptr_t Health              = 0x938;
        constexpr uintptr_t MaxHealth           = 0x960;
        constexpr uintptr_t Shield              = 0x9D0;   // not in new dump, kept
        constexpr uintptr_t MagicShield         = 0xA20;   // not in new dump, kept
        constexpr uintptr_t Block               = 0xD58;
        constexpr uintptr_t BonusAD             = 0xEF8;
        constexpr uintptr_t AttackSpeedScale    = 0xF74;
        constexpr uintptr_t AttackSpeedMod      = 0xF98;
        constexpr uintptr_t BaseAD              = 0xF9C;
        constexpr uintptr_t BaseAP              = 0xFAC;
        constexpr uintptr_t Armor               = 0xFC4;
        constexpr uintptr_t MagicResist         = 0xFCC;
        constexpr uintptr_t MoveSpeed           = 0xFDC;
        constexpr uintptr_t AttackRange         = 0xFE4;
        constexpr uintptr_t IsTargetable        = 0x10E0;  // not in new dump, kept
        constexpr uintptr_t CharacterStats      = 0x1388;
        constexpr uintptr_t BuffManager         = 0x25E8;
        inline uintptr_t SpellBook              = 0x28B0;
        constexpr uintptr_t CharacterDataStack  = 0x3638;  // not in new dump, kept
        constexpr uintptr_t EncryptedWrapper    = 0x3788;
        inline uintptr_t Name                   = 0x3898;
        constexpr uintptr_t Level               = 0x4080;
        constexpr uintptr_t MinionType          = 0x4B39;  // not in new dump, kept
        constexpr uintptr_t NetworkID           = 0x78;     // not in new dump, kept
    }
    namespace SpellBook {
        constexpr uintptr_t ActiveSpellCast = 0x90;
        inline uintptr_t SpellSlots         = 0xAE0;
    }
    namespace SpellSlot {
        inline uintptr_t SpellSlotData      = 0x140;
        inline uintptr_t SpellInfoNamePtr   = 0x28;
        inline uintptr_t ReadyAt            = 0x38;
        inline uintptr_t TotalCD            = 0x74;
    }
    namespace SpellData {
        constexpr uintptr_t MissileSpeed    = 0x3F8;
        constexpr uintptr_t LineWidth       = 0x448;
        constexpr uintptr_t BaseCost        = 0x6D8;
        constexpr uintptr_t CooldownTime    = 0x708;
        constexpr uintptr_t CastRange       = 0x778;
        constexpr uintptr_t CastRadius      = 0x7CC;
    }
    namespace SpellCast {
        constexpr uintptr_t StartPos        = 0xE8;   // Vec3 — cast origin (was 0x80, +0x68)
        constexpr uintptr_t EndPos          = 0xF4;   // Vec3 — cast target/direction (was 0x8C, +0x68)
        constexpr uintptr_t ReleaseTime     = 0x1E0;  // (was 0x1B8, +0x28)
        constexpr uintptr_t SpellCastData   = 0x238;  // (was 0x1D0, +0x68)
        constexpr uintptr_t SpellData       = 0x240;  // (was 0x1D8, +0x68)
        constexpr uintptr_t CasterHandle    = 0x2E0;  // (was 0x278, +0x68)
    }
    namespace Buff {
        constexpr uintptr_t EntryExpire     = 0x1C;
        constexpr uintptr_t ScriptName      = 0x60;
        constexpr uintptr_t Lifecycle       = 0x8C;
        constexpr uintptr_t EntryCount      = 0x94;
    }
    namespace Waypoint {
        constexpr uintptr_t WrapperToPC     = 0x10;
        constexpr uintptr_t CurrentWaypoint = 0x308;
        constexpr uintptr_t RouteStart      = 0x318;
        constexpr uintptr_t EndPos          = 0x324;
        constexpr uintptr_t WaypointArray   = 0x330;
        constexpr uintptr_t WaypointCount   = 0x338;
        constexpr uintptr_t ServerPos       = 0x45C;
    }
    namespace ViewPort {
        constexpr uintptr_t ViewMatrix      = 0x1E4;
        constexpr uintptr_t ProjMatrix      = 0x224;
        constexpr uintptr_t CamEntOffset    = 0x308;
        constexpr uintptr_t ViewportRect    = 0x310;
    }
    namespace Manager {
        constexpr uintptr_t List  = 0x8;
        constexpr uintptr_t Count = 0x10;
    }
    namespace MissileClient {
        // ALL offsets discovered at runtime by ProbeMissileOffsets()
        inline uintptr_t VtablePrimary      = 0;   // filled by vtable probe
        inline uintptr_t VtableAlternate    = 0;   // filled by vtable probe
        inline uintptr_t CurrentPos         = 0;   // Vec3 — probed
        inline uintptr_t SpellInfo          = 0;   // ptr to SpellInfo struct — probed
        inline uintptr_t StartPos           = 0;   // Vec3 — probed
        inline uintptr_t EndPos             = 0;   // Vec3 — probed
    }
    namespace MissileSpellInfo {
        inline uintptr_t NamePointer        = 0;   // MSVC std::string SSO — probed
    }
    namespace FixedSpeed {
        constexpr uintptr_t Accelerating    = 0x18F4DC0;
        constexpr uintptr_t AcceleratingAlt = 0x18F4DF0;
        constexpr uintptr_t FixedTime       = 0x18F4E20;
        constexpr uintptr_t FixedSpeed      = 0x18F4E50;
    }
}

// ── Encrypted SpellSlot support (patch 26.21+) ──
// Riot encrypts SpellSlotData pointer with a 3-step chain parsed from vtable:
//   1. SUB: temp = encVal - constant1
//   2. ROR: temp = ROR64(temp, shift)
//   3. XOR: result = temp ^ constant2
// Constants are extracted from the getContainer function bytes at runtime.
static bool     g_spellSlotEncrypted = false;
static int      g_spellEncDataOff    = 0x140;   // offset in slot where encrypted value lives
static uint64_t g_spellEncConst1     = 0;       // SUB constant (64-bit, from MOV RCX,imm64)
static int      g_spellEncShift      = 0;       // ROR shift amount
static uint64_t g_spellEncConst2     = 0;       // XOR constant (32 or 64-bit, from MOV ECX/RCX)

static inline uint64_t ROR64(uint64_t val, int shift) {
    return (val >> shift) | (val << (64 - shift));
}

static inline uintptr_t DecryptSpellSlotData(uintptr_t slotPtr) {
    if (!g_spellSlotEncrypted || g_spellEncDataOff == 0) return 0;
    uint64_t encVal = TargetProcess.Read<uint64_t>(slotPtr + g_spellEncDataOff);
    uint64_t temp = encVal - g_spellEncConst1;       // step 1: SUB
    if (g_spellEncShift > 0)
        temp = ROR64(temp, g_spellEncShift);          // step 2: ROR
    temp ^= g_spellEncConst2;                         // step 3: XOR
    uintptr_t sdr = (uintptr_t)temp;
    if (sdr < 0x10000 || sdr > 0x7FFFFFFFFFFF) return 0;
    return sdr;
}

static inline uintptr_t ReadSpellSlotData(uintptr_t slotPtr) {
    if (g_spellSlotEncrypted) {
        uintptr_t sdr = DecryptSpellSlotData(slotPtr);
        if (sdr) return sdr;
    }
    uintptr_t info = TargetProcess.Read<uintptr_t>(slotPtr + Offsets::SpellSlot::SpellSlotData);
    if (info > 0x10000 && info < 0x7FFFFFFFFFFF) return info;
    return 0;
}

// SSO-aware spell name reader: handles MSVC std::string inline (<=15 chars) and heap pointer
static int ReadSpellNameSSO(uintptr_t infoPtr, uintptr_t nameOff, char* outName, int outSize) {
    if (infoPtr < 0x10000 || infoPtr > 0x7FFFFFFFFFFF) return 0;
    uint8_t ssoData[32] = {};
    TargetProcess.Read(infoPtr + nameOff, ssoData, 32);
    uint64_t capacity = *(uint64_t*)(ssoData + 24);
    memset(outName, 0, outSize);
    if (capacity <= 15) {
        // SSO inline: name lives in bytes 0-15
        int len = 0;
        while (len < 15 && len < outSize - 1 && ssoData[len] >= 0x20 && ssoData[len] <= 0x7E) len++;
        memcpy(outName, ssoData, len);
        outName[len] = '\0';
        return len;
    } else {
        // Heap allocated: bytes 0-7 are a pointer
        uintptr_t np = *(uintptr_t*)(ssoData);
        if (np < 0x10000 || np > 0x7FFFFFFFFFFF) return 0;
        TargetProcess.Read(np, outName, outSize - 1);
        int len = 0;
        while (len < outSize - 1 && outName[len] >= 0x20 && outName[len] <= 0x7E) len++;
        outName[len] = '\0';
        return len;
    }
}

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };
struct Matrix4x4 { float m[4][4]; };

static int randInt(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(xorshift32() % (uint32_t)(hi - lo + 1));
}
static float randFloat(float lo, float hi) { return xorFloat(lo, hi); }

static ID3D11Device*           g_device    = nullptr;
static ID3D11DeviceContext*    g_context   = nullptr;
static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv       = nullptr;

static void CreateRenderTarget()
{
    ID3D11Texture2D* bb = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (!bb) return;
    g_device->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
}
static void CleanupRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}
static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hWnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &level, &g_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context)   { g_context->Release();   g_context   = nullptr; }
    if (g_device)    { g_device->Release();     g_device    = nullptr; }
}

static std::vector<RECT> g_overlayMonitors;
static int g_monitorIndex   = 0;
static int g_appliedMonitor = -1;

static BOOL CALLBACK AddMonitor(HMONITOR, HDC, LPRECT rect, LPARAM list)
{
    reinterpret_cast<std::vector<RECT>*>(list)->push_back(*rect);
    return TRUE;
}
static void DetectMonitors()
{
    g_overlayMonitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&g_overlayMonitors));
    if (g_overlayMonitors.empty())
        g_overlayMonitors.push_back({ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) });
    g_appliedMonitor = -1;
}
static void ApplyOverlayMonitor(HWND hWnd)
{
    g_monitorIndex = std::clamp(g_monitorIndex, 0, (int)g_overlayMonitors.size() - 1);
    if (g_monitorIndex == g_appliedMonitor) return;
    const RECT& r = g_overlayMonitors[g_monitorIndex];
    SetWindowPos(hWnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
    g_appliedMonitor = g_monitorIndex;
    VX_LOG("[Overlay] monitor %d: %ldx%ld at %ld,%ld\n", g_monitorIndex + 1,
           r.right - r.left, r.bottom - r.top, r.left, r.top);
}

static uintptr_t g_base = 0;
static float     g_gameTime = 0.f;
static int       g_screenW = 1920, g_screenH = 1080;
static Matrix4x4 g_viewProjMatrix{};

struct CachedHero {
    uintptr_t addr;
    Vec3      pos;
    Vec3      lastPos;
    Vec3      velocity;      // units/sec estimated from pos deltas
    float     health, maxHealth;
    float     shield, magicShield;
    float     attackRange;
    float     boundingRadius;
    float     baseAD, bonusAD;
    float     attackSpeedMod;
    float     armor, magicResist;
    float     mana, maxMana;
    int32_t   level;
    uint8_t   team;
    bool      visible;
    bool      isDead;
    bool      wasVisible;
    bool      recallAlerted;
    float     lastSeenTime;
    char      name[64];
};
static std::vector<CachedHero> g_heroes;
static bool g_useEntityFilter = false;    // true when using combined entity list (no separate managers)
static uintptr_t g_entityListRVA = 0;     // RVA of combined entity list found by ProbeManagerLayout
static uintptr_t g_heroListRVA = 0;       // RVA of flat hero pointer array (direct entity ptrs at mgr+0x0, +0x8, etc.)
static uintptr_t g_heroListMgrAddr = 0;   // cached manager address for the flat hero list

struct RecallAlert {
    char name[64];
    float triggerTime;
};
static std::vector<RecallAlert> g_recallAlerts;
static float g_recallChannelTime = 8.0f;
static uint8_t   g_localTeam = 0;
static Vec3      g_localPos{};
static uintptr_t g_localAddr = 0;
static float     g_localAttackRange = 550.f;
static float     g_localBoundingRadius = 65.f;
static float     g_localBaseAD      = 0.f;
static float     g_localBonusAD     = 0.f;
static float     g_localASMod       = 1.f;
static float     g_localASScale     = 0.625f;
static int32_t   g_localLevel       = 1;
static float     g_localMana        = 0.f;
static float     g_localMaxMana     = 0.f;

struct CachedTurret {
    uintptr_t addr;
    Vec3      pos;
    float     health, maxHealth;
    uint8_t   team;
};
static std::vector<CachedTurret> g_turrets;

struct CachedMinion {
    uintptr_t addr;
    Vec3      pos;
    float     health, maxHealth;
    float     boundingRadius;
    uint8_t   team;
    bool      visible;
    bool      isDead;
    char      name[64];
};
static std::vector<CachedMinion> g_minions;

// ── Summoner spell tracking (per hero) ──
struct SummonerSpell {
    char  name[64];
    float readyAt;
    float totalCD;
    bool  ready;
};

struct TrackedHeroSpells {
    SummonerSpell d, f;
};
static TrackedHeroSpells g_heroSumms[12] = {};

// ── Skillshot Dodge Alert system ──
struct TrackedSkillshot {
    Vec3  startPos;
    Vec3  endPos;
    float missileSpeed;
    float lineWidth;
    float castRange;
    float startTime;
    float duration;
    float delayS;       // cast delay before missile launches (seconds)
    int   casterIdx;
    char  spellName[32];
};
static std::vector<TrackedSkillshot> g_skillshots;
static uintptr_t g_lastActiveCast[12] = {};
static bool  g_skillshotAlerts    = true;
static float g_skillshotMinSpeed  = 500.f;

// ── Missile Object Tracking (reads MissileClient from ObjectManager) ──
struct TrackedMissileObj {
    uintptr_t addr;
    Vec3  currentPos;
    Vec3  startPos;
    Vec3  endPos;
    char  spellName[64];
    float speed;
    float width;
    float range;
    float firstSeen;
    bool  matched;
};
static std::vector<TrackedMissileObj> g_trackedMissiles;
static bool g_missileTracking = true;
static bool g_missileVtableProbed = false;
static int  g_missileVtableProbeCount = 0;
static bool g_missileOffsetsProbed = false;
static int  g_missileOffsetProbeAttempts = 0;
static int  g_missileFrameCounter = 0;

// ── Evade Spell Lookup Table (from evade_spells.csv) ──
enum EvadeShape { EVADE_LINE = 0, EVADE_CIRCLE = 1, EVADE_CIRCLE_START = 2, EVADE_CONE = 3 };
struct EvadeSpellEntry {
    EvadeShape shape;
    float size;
    float range;
    float speed;
    float halfAngle;
    int   delayMs;
    int   activeMs;
};
static std::unordered_map<std::string, EvadeSpellEntry> g_evadeSpells;
static bool g_evadeSpellsLoaded = false;

struct ChampSpellRef {
    const char* spellName;
    const EvadeSpellEntry* entry;
};
static std::unordered_map<std::string, std::vector<ChampSpellRef>> g_champLineSpells;

static void InitEvadeSpells();

static const EvadeSpellEntry* LookupEvadeSpell(const char* spellName) {
    if (!spellName || !spellName[0] || !g_evadeSpellsLoaded) return nullptr;
    auto it = g_evadeSpells.find(spellName);
    if (it != g_evadeSpells.end()) return &it->second;
    return nullptr;
}

static const EvadeSpellEntry* LookupEvadeByChampion(const char* champName, float castDist, char* outSpellName, int outLen) {
    if (!champName || !champName[0] || !g_evadeSpellsLoaded) return nullptr;
    auto it = g_champLineSpells.find(champName);
    if (it == g_champLineSpells.end() || it->second.empty()) return nullptr;
    const EvadeSpellEntry* best = nullptr;
    const char* bestName = nullptr;
    float bestDiff = 99999.f;
    for (auto& ref : it->second) {
        float diff = fabsf(ref.entry->range - castDist);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = ref.entry;
            bestName = ref.spellName;
        }
    }
    if (best && bestName && outSpellName) {
        strncpy(outSpellName, bestName, outLen - 1);
        outSpellName[outLen - 1] = 0;
    }
    return best;
}

// ── Missile→CSV name matching (strip common suffixes to find base spell) ──
static std::string MissileStripSuffix(const std::string& raw) {
    std::string s = raw;
    // uppercase for comparison
    std::string upper = s;
    for (auto& c : upper) c = (char)toupper((unsigned char)c);

    // strip trailing digits (e.g. KaisaQLeftMissile1 → KaisaQLeftMissile)
    while (!upper.empty() && upper.back() >= '0' && upper.back() <= '9') {
        upper.pop_back();
        s.pop_back();
    }

    // strip known missile suffixes
    static const char* suffixes[] = {
        "MISSILE", "MISSILEMID", "MIS", "INTERNAL", "ATTACK", "SPELL",
        "TRAIL", "LEFT", "RIGHT", "OFFHAND", "DUMMY", "VFX", "SOUND",
        "INDICATOR", "WARNING", "EMPOWERED", "MEGA", nullptr
    };
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; suffixes[i]; i++) {
            size_t slen = strlen(suffixes[i]);
            if (upper.size() > slen) {
                if (upper.compare(upper.size() - slen, slen, suffixes[i]) == 0) {
                    upper.erase(upper.size() - slen);
                    s.erase(s.size() - slen);
                }
            }
        }
    }
    return s;
}

static const EvadeSpellEntry* LookupEvadeByMissileName(const char* missileName, char* outCastName, int outLen) {
    if (!missileName || !missileName[0] || !g_evadeSpellsLoaded) return nullptr;

    // direct hit first
    auto it = g_evadeSpells.find(missileName);
    if (it != g_evadeSpells.end()) {
        if (outCastName) { strncpy(outCastName, missileName, outLen - 1); outCastName[outLen - 1] = 0; }
        return &it->second;
    }

    // strip suffixes and search
    std::string stem = MissileStripSuffix(missileName);
    if (stem.empty()) return nullptr;

    // try the stripped name directly
    it = g_evadeSpells.find(stem);
    if (it != g_evadeSpells.end()) {
        if (outCastName) { strncpy(outCastName, stem.c_str(), outLen - 1); outCastName[outLen - 1] = 0; }
        return &it->second;
    }

    // fuzzy: check all CSV entries — if the entry name starts with our stem or vice versa
    std::string stemUpper = stem;
    for (auto& c : stemUpper) c = (char)toupper((unsigned char)c);

    const EvadeSpellEntry* best = nullptr;
    std::string bestName;
    size_t bestOverlap = 0;
    for (auto& kv : g_evadeSpells) {
        if (kv.second.shape != EVADE_LINE) continue;
        if (kv.second.speed < 10.f || kv.second.range < 50.f) continue;
        std::string csvUpper = kv.first;
        for (auto& c : csvUpper) c = (char)toupper((unsigned char)c);
        std::string csvStem = MissileStripSuffix(csvUpper);

        // check if stems match
        if (stemUpper == csvStem) {
            if (outCastName) { strncpy(outCastName, kv.first.c_str(), outLen - 1); outCastName[outLen - 1] = 0; }
            return &kv.second;
        }
        // partial: one contains the other
        size_t overlap = 0;
        if (stemUpper.size() >= 3 && csvStem.size() >= 3) {
            if (csvStem.find(stemUpper) != std::string::npos) overlap = stemUpper.size();
            else if (stemUpper.find(csvStem) != std::string::npos) overlap = csvStem.size();
        }
        // require substantial overlap — short champion name prefixes matching
        // unrelated spells caused auto attacks to draw as skillshots
        if (overlap > bestOverlap && overlap >= 6 && overlap >= stemUpper.size() / 2) {
            bestOverlap = overlap;
            best = &kv.second;
            bestName = kv.first;
        }
    }
    if (best && outCastName) {
        strncpy(outCastName, bestName.c_str(), outLen - 1);
        outCastName[outLen - 1] = 0;
    }
    return best;
}

// ── Runtime MissileClient Offset Probe ──
// Scans raw memory of objects from MissileManager to discover struct layout:
//   1. SpellInfo + NamePointer: scan offsets 0x200-0x400 for pointers that lead
//      to structs where offsets 0x18-0x50 contain a readable SSO string starting
//      with an uppercase letter that matches a known spell in our CSV
//   2. CurrentPos: scan offsets 0x80-0x300 for Vec3 with valid map coords (0-16000)
//   3. StartPos/EndPos: scan offsets 0x280-0x500 for Vec3 pairs with valid coords
static void ProbeMissileOffsets() {
    if (g_missileOffsetsProbed) return;
    if (g_missileOffsetProbeAttempts >= 100) return;
    // throttle: only probe once per ~60 frames (~1/sec) to avoid nuking FPS
    g_missileFrameCounter++;
    if (g_missileFrameCounter % 60 != 1) return;
    g_missileOffsetProbeAttempts++;

    uintptr_t mgrPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::MissileManager);
    if (!mgrPtr) return;

    uintptr_t listPtr = TargetProcess.Read<uintptr_t>(mgrPtr + Offsets::Manager::List);
    uint32_t  count   = TargetProcess.Read<uint32_t>(mgrPtr + Offsets::Manager::Count);
    if (!listPtr || count == 0 || count > 500) return;

    // read object pointers
    uint32_t scanCount = std::min(count, (uint32_t)50); // only need a handful
    std::vector<uintptr_t> objPtrs(scanCount);
    TargetProcess.Read(listPtr, objPtrs.data(), scanCount * sizeof(uintptr_t));

    VX_LOG("[MissileProbe] attempt %d — scanning %u objects for struct layout\n",
        g_missileOffsetProbeAttempts, scanCount);

    // For each candidate object, dump a chunk of its memory and scan for patterns
    for (uint32_t i = 0; i < scanCount; i++) {
        uintptr_t obj = objPtrs[i];
        if (obj < 0x10000 || obj > 0x7FFFFFFFFFFF) continue;

        // quick vtable sanity
        uintptr_t vtable = TargetProcess.Read<uintptr_t>(obj);
        if (vtable < 0x10000 || vtable > 0x7FFFFFFFFFFF) continue;

        // ── STEP 1: Find SpellInfo pointer and NamePointer ──
        // SpellInfo is a pointer at some offset in the object that leads to
        // a struct containing an MSVC std::string (SSO) with a spell name.
        // We scan offsets 0x200-0x400 (8-byte aligned) for valid pointers,
        // then at each candidate, scan sub-offsets 0x18-0x50 for a readable name.
        if (Offsets::MissileClient::SpellInfo == 0) {
            for (uintptr_t sOff = 0x200; sOff <= 0x400; sOff += 8) {
                uintptr_t candidate = TargetProcess.Read<uintptr_t>(obj + sOff);
                if (candidate < 0x10000 || candidate > 0x7FFFFFFFFFFF) continue;

                // try sub-offsets for the SSO string within the SpellInfo struct
                for (uintptr_t nOff = 0x18; nOff <= 0x50; nOff += 8) {
                    char testName[64] = {};
                    int len = ReadSpellNameSSO(candidate, nOff, testName, 64);
                    if (len < 3) continue;
                    // name should start with uppercase
                    if (testName[0] < 'A' || testName[0] > 'Z') continue;
                    // must contain only printable ASCII
                    bool valid = true;
                    for (int c = 0; c < len; c++) {
                        if (testName[c] < 0x20 || testName[c] > 0x7E) { valid = false; break; }
                    }
                    if (!valid) continue;

                    // try to match against our CSV spell database
                    char matchBuf[64] = {};
                    const EvadeSpellEntry* ev = LookupEvadeByMissileName(testName, matchBuf, 64);
                    if (ev) {
                        // FOUND IT — this SpellInfo offset + NamePointer offset are correct
                        Offsets::MissileClient::SpellInfo = sOff;
                        Offsets::MissileSpellInfo::NamePointer = nOff;
                        VX_LOG("[MissileProbe] ✓ SpellInfo=0x%llX NamePointer=0x%llX (name='%s' → csv='%s')\n",
                            (uint64_t)sOff, (uint64_t)nOff, testName, matchBuf);
                        goto spellinfo_done;
                    }

                    // even if not in CSV, if it looks like a valid spell name
                    // (uppercase start, >4 chars, no spaces) it's worth noting
                    // but we keep scanning for a CSV match first
                }
            }
            spellinfo_done:;
        }

        // ── STEP 2: Find CurrentPos (Vec3 with valid map coordinates) ──
        // The game map is roughly 0-16000 on X and Z axes, Y is height (-500 to 2000)
        // CurrentPos updates every frame (missile is moving), so it should be valid coords
        if (Offsets::MissileClient::CurrentPos == 0 && Offsets::MissileClient::SpellInfo != 0) {
            for (uintptr_t cOff = 0x80; cOff <= 0x300; cOff += 4) {
                Vec3 testPos{};
                TargetProcess.Read(obj + cOff, &testPos, sizeof(Vec3));
                if (testPos.x > 0.f && testPos.x < 16000.f &&
                    testPos.y > -500.f && testPos.y < 2000.f &&
                    testPos.z > 0.f && testPos.z < 16000.f) {
                    // candidate — verify it's not StartPos/EndPos by checking
                    // that it differs from values at nearby offsets
                    Offsets::MissileClient::CurrentPos = cOff;
                    VX_LOG("[MissileProbe] ✓ CurrentPos=0x%llX (%.0f, %.0f, %.0f)\n",
                        (uint64_t)cOff, testPos.x, testPos.y, testPos.z);
                    break;
                }
            }
        }

        // ── STEP 3: Find StartPos and EndPos (Vec3 pair with valid coordinates) ──
        // StartPos and EndPos are typically close together in the struct (12 bytes apart = sizeof(Vec3))
        // Both should have valid map coordinates. StartPos is where the missile spawned,
        // EndPos is where it's heading. They should NOT be identical.
        if (Offsets::MissileClient::StartPos == 0 && Offsets::MissileClient::SpellInfo != 0) {
            for (uintptr_t pOff = 0x280; pOff <= 0x500; pOff += 4) {
                Vec3 testStart{}, testEnd{};
                TargetProcess.Read(obj + pOff, &testStart, sizeof(Vec3));
                TargetProcess.Read(obj + pOff + 12, &testEnd, sizeof(Vec3));

                bool startOk = testStart.x > 0.f && testStart.x < 16000.f &&
                               testStart.y > -500.f && testStart.y < 2000.f &&
                               testStart.z > 0.f && testStart.z < 16000.f;
                bool endOk   = testEnd.x > 0.f && testEnd.x < 16000.f &&
                               testEnd.y > -500.f && testEnd.y < 2000.f &&
                               testEnd.z > 0.f && testEnd.z < 16000.f;

                if (!startOk || !endOk) continue;

                // start and end should be different (missile is traveling somewhere)
                float dx = testEnd.x - testStart.x;
                float dz = testEnd.z - testStart.z;
                float dist = sqrtf(dx * dx + dz * dz);
                if (dist < 50.f || dist > 3000.f) continue;

                // also make sure this isn't the same as CurrentPos offset
                if (pOff == Offsets::MissileClient::CurrentPos) continue;

                Offsets::MissileClient::StartPos = pOff;
                Offsets::MissileClient::EndPos   = pOff + 12; // Vec3 is 12 bytes (3 floats)
                VX_LOG("[MissileProbe] ✓ StartPos=0x%llX EndPos=0x%llX (start=%.0f,%.0f end=%.0f,%.0f dist=%.0f)\n",
                    (uint64_t)pOff, (uint64_t)(pOff + 12),
                    testStart.x, testStart.z, testEnd.x, testEnd.z, dist);
                break;
            }
        }

        // check if all offsets found
        if (Offsets::MissileClient::SpellInfo != 0 &&
            Offsets::MissileClient::CurrentPos != 0 &&
            Offsets::MissileClient::StartPos != 0 &&
            Offsets::MissileClient::EndPos != 0 &&
            Offsets::MissileSpellInfo::NamePointer != 0) {
            g_missileOffsetsProbed = true;
            VX_LOG("[MissileProbe] ★ ALL OFFSETS FOUND — SpellInfo=0x%llX Name=0x%llX Cur=0x%llX Start=0x%llX End=0x%llX\n",
                (uint64_t)Offsets::MissileClient::SpellInfo,
                (uint64_t)Offsets::MissileSpellInfo::NamePointer,
                (uint64_t)Offsets::MissileClient::CurrentPos,
                (uint64_t)Offsets::MissileClient::StartPos,
                (uint64_t)Offsets::MissileClient::EndPos);
            return;
        }
    }

    // partial results logging
    if (Offsets::MissileClient::SpellInfo != 0 || Offsets::MissileClient::CurrentPos != 0) {
        VX_LOG("[MissileProbe] partial — SpellInfo=%s Cur=%s Start=%s (attempt %d)\n",
            Offsets::MissileClient::SpellInfo ? "YES" : "no",
            Offsets::MissileClient::CurrentPos ? "YES" : "no",
            Offsets::MissileClient::StartPos ? "YES" : "no",
            g_missileOffsetProbeAttempts);
    }
}

// ── Missile Object Reader ──
static int g_missileReadThrottle = 0;
static void ReadMissileObjects() {
    if (!g_missileTracking || !g_skillshotAlerts) return;

    // run offset probe if not done yet
    if (!g_missileOffsetsProbed) {
        ProbeMissileOffsets();
        if (!g_missileOffsetsProbed) return;
    }

    // throttle: read missiles every 3 frames to save DMA bandwidth
    if (++g_missileReadThrottle % 3 != 0) return;

    // read MissileManager → list of missile object pointers
    uintptr_t mgrPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::MissileManager);
    if (!mgrPtr) return;

    uintptr_t listPtr = TargetProcess.Read<uintptr_t>(mgrPtr + Offsets::Manager::List);
    uint32_t  count   = TargetProcess.Read<uint32_t>(mgrPtr + Offsets::Manager::Count);
    if (!listPtr || count == 0 || count > 500) return;

    // read all object pointers
    std::vector<uintptr_t> objPtrs(count);
    TargetProcess.Read(listPtr, objPtrs.data(), count * sizeof(uintptr_t));

    // mark all existing tracked missiles as not-seen-this-frame
    for (auto& m : g_trackedMissiles) m.matched = false;

    // ── SCATTER PASS 1: batch-read vtable + SpellInfo + positions from ALL objects ──
    // One DMA transaction for everything instead of N individual reads
    struct MissileRaw {
        uintptr_t vtable;
        uintptr_t spellInfoPtr;
        Vec3 curPos, startPos, endPos;
        bool valid;
    };
    std::vector<MissileRaw> raw(count);
    {
        auto sc = TargetProcess.CreateScatterHandle();
        for (uint32_t i = 0; i < count; i++) {
            raw[i] = {};
            if (objPtrs[i] < 0x10000 || objPtrs[i] > 0x7FFFFFFFFFFF) continue;
            raw[i].valid = true;
            TargetProcess.AddScatterReadRequest(sc, objPtrs[i], &raw[i].vtable, sizeof(uintptr_t));
            TargetProcess.AddScatterReadRequest(sc, objPtrs[i] + Offsets::MissileClient::SpellInfo, &raw[i].spellInfoPtr, sizeof(uintptr_t));
            TargetProcess.AddScatterReadRequest(sc, objPtrs[i] + Offsets::MissileClient::CurrentPos, &raw[i].curPos, sizeof(Vec3));
            TargetProcess.AddScatterReadRequest(sc, objPtrs[i] + Offsets::MissileClient::StartPos, &raw[i].startPos, sizeof(Vec3));
            TargetProcess.AddScatterReadRequest(sc, objPtrs[i] + Offsets::MissileClient::EndPos, &raw[i].endPos, sizeof(Vec3));
        }
        TargetProcess.ExecuteReadScatter(sc);
        TargetProcess.CloseScatterHandle(sc);
    }

    // ── SCATTER PASS 2: batch-read SSO name data for objects that pass vtable filter ──
    struct SSOData { uint8_t data[32]; uintptr_t spellInfo; bool needsDeref; };
    std::vector<SSOData> ssoRaw(count);
    {
        auto sc2 = TargetProcess.CreateScatterHandle();
        for (uint32_t i = 0; i < count; i++) {
            ssoRaw[i] = {};
            if (!raw[i].valid) continue;
            if (raw[i].vtable < 0x10000 || raw[i].vtable > 0x7FFFFFFFFFFF) { raw[i].valid = false; continue; }
            // vtable fast-filter
            if (Offsets::MissileClient::VtablePrimary != 0 || Offsets::MissileClient::VtableAlternate != 0) {
                if (raw[i].vtable != Offsets::MissileClient::VtablePrimary &&
                    raw[i].vtable != Offsets::MissileClient::VtableAlternate) {
                    // during vtable learning phase, let unknowns through
                    if (g_missileVtableProbed || g_missileVtableProbeCount >= 200) { raw[i].valid = false; continue; }
                }
            }
            if (raw[i].spellInfoPtr < 0x10000 || raw[i].spellInfoPtr > 0x7FFFFFFFFFFF) { raw[i].valid = false; continue; }
            ssoRaw[i].spellInfo = raw[i].spellInfoPtr;
            TargetProcess.AddScatterReadRequest(sc2, raw[i].spellInfoPtr + Offsets::MissileSpellInfo::NamePointer, ssoRaw[i].data, 32);
        }
        TargetProcess.ExecuteReadScatter(sc2);
        TargetProcess.CloseScatterHandle(sc2);
    }

    // ── SCATTER PASS 3: deref heap SSO pointers (capacity > 15) ──
    struct NameBuf { char n[64]; };
    std::vector<NameBuf> namesBuf(count);
    {
        std::vector<uint32_t> derefIdx;
        for (uint32_t i = 0; i < count; i++) {
            memset(namesBuf[i].n, 0, 64);
            if (!raw[i].valid) continue;
            uint64_t capacity = *(uint64_t*)(ssoRaw[i].data + 24);
            if (capacity <= 15) {
                int len = 0;
                while (len < 15 && ssoRaw[i].data[len] >= 0x20 && ssoRaw[i].data[len] <= 0x7E) len++;
                memcpy(namesBuf[i].n, ssoRaw[i].data, len);
            } else {
                uintptr_t np = *(uintptr_t*)(ssoRaw[i].data);
                if (np > 0x10000 && np < 0x7FFFFFFFFFFF) {
                    ssoRaw[i].needsDeref = true;
                    derefIdx.push_back(i);
                } else { raw[i].valid = false; }
            }
        }
        if (!derefIdx.empty()) {
            auto sc3 = TargetProcess.CreateScatterHandle();
            for (uint32_t idx : derefIdx) {
                uintptr_t np = *(uintptr_t*)(ssoRaw[idx].data);
                TargetProcess.AddScatterReadRequest(sc3, np, namesBuf[idx].n, 63);
            }
            TargetProcess.ExecuteReadScatter(sc3);
            TargetProcess.CloseScatterHandle(sc3);
            for (uint32_t idx : derefIdx) {
                int len = 0;
                while (len < 63 && namesBuf[idx].n[len] >= 0x20 && namesBuf[idx].n[len] <= 0x7E) len++;
                namesBuf[idx].n[len] = 0;
            }
        }
    }

    // ── Process results (NO DMA in this loop) ──
    int newCount = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!raw[i].valid) continue;
        char* missileName = namesBuf[i].n;
        int nameLen = (int)strlen(missileName);
        if (nameLen < 2) continue;
        if (missileName[0] < 'A' || missileName[0] > 'Z') continue;

        // skip obvious non-skillshot missiles (auto attacks, targeted spells, etc.)
        {
            std::string upper = missileName;
            for (auto& c : upper) c = (char)toupper((unsigned char)c);
            // filter any missile with ATTACK in the name — covers BasicAttack,
            // ChampNameAttack, AttackMissile, etc.
            if (upper.find("ATTACK") != std::string::npos) continue;
            if (upper.find("YOURCUT") != std::string::npos) continue;
            if (upper.find("YOURIKU") != std::string::npos) continue;
            if (upper.find("TAR") == 0 && upper.find("TARGET") == 0) continue;
        }

        uintptr_t vtable = raw[i].vtable;
        bool vtableOk = (Offsets::MissileClient::VtablePrimary != 0 &&
                         (vtable == Offsets::MissileClient::VtablePrimary || vtable == Offsets::MissileClient::VtableAlternate));

        // vtable learning
        if (!vtableOk && !g_missileVtableProbed && g_missileVtableProbeCount < 200) {
            g_missileVtableProbeCount++;
            char matchName[64] = {};
            const EvadeSpellEntry* ev = LookupEvadeByMissileName(missileName, matchName, 64);
            if (ev && ev->shape == EVADE_LINE && ev->speed > 10.f) {
                if (Offsets::MissileClient::VtablePrimary == 0) {
                    Offsets::MissileClient::VtablePrimary = vtable;
                    VX_LOG("[MissileVtable] PRIMARY vtable discovered: 0x%llX from '%s'\n",
                        (uint64_t)vtable, missileName);
                } else if (vtable != Offsets::MissileClient::VtablePrimary &&
                           Offsets::MissileClient::VtableAlternate == 0) {
                    Offsets::MissileClient::VtableAlternate = vtable;
                    VX_LOG("[MissileVtable] ALTERNATE vtable discovered: 0x%llX from '%s'\n",
                        (uint64_t)vtable, missileName);
                    g_missileVtableProbed = true;
                }
                vtableOk = true;
            } else {
                continue;
            }
        }
        if (!vtableOk) continue;

        Vec3 curPos = raw[i].curPos, startP = raw[i].startPos, endP = raw[i].endPos;

        // sanity check positions
        if (curPos.x < -1000.f || curPos.x > 20000.f) continue;
        if (startP.x < -1000.f || startP.x > 20000.f) continue;
        if (endP.x < -1000.f || endP.x > 20000.f) continue;

        // check if already tracked by address
        uintptr_t obj = objPtrs[i];
        bool found = false;
        for (auto& m : g_trackedMissiles) {
            if (m.addr == obj) {
                m.currentPos = curPos;
                m.matched = true;
                found = true;
                break;
            }
        }
        if (found) continue;

        // new missile — look up in CSV
        char castName[64] = {};
        const EvadeSpellEntry* ev = LookupEvadeByMissileName(missileName, castName, 64);
        if (!ev || ev->shape != EVADE_LINE || ev->speed < 10.f || ev->range < 50.f) continue;

        float dx = endP.x - startP.x;
        float dz = endP.z - startP.z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist < 50.f) continue;

        TrackedMissileObj tm{};
        tm.addr       = obj;
        tm.currentPos = curPos;
        tm.startPos   = startP;
        tm.endPos     = endP;
        tm.speed      = ev->speed;
        tm.width      = ev->size;
        tm.range      = ev->range;
        tm.firstSeen  = g_gameTime;
        tm.matched    = true;
        strncpy(tm.spellName, missileName, 63);

        if (g_trackedMissiles.size() < 30) {
            g_trackedMissiles.push_back(tm);
            newCount++;
            VX_LOG("[MissileTrack] NEW '%s' → csv='%s' spd=%.0f wid=%.0f rng=%.0f start=(%.0f,%.0f) end=(%.0f,%.0f)\n",
                missileName, castName, ev->speed, ev->size, ev->range,
                startP.x, startP.z, endP.x, endP.z);
        }
    }

    // remove missiles that disappeared (object no longer in list)
    g_trackedMissiles.erase(
        std::remove_if(g_trackedMissiles.begin(), g_trackedMissiles.end(),
            [](const TrackedMissileObj& m) { return !m.matched; }),
        g_trackedMissiles.end());
}

// ── Push tracked missiles into g_skillshots for drawing ──
static void SyncMissilesToSkillshots() {
    if (!g_missileTracking || !g_skillshotAlerts) return;

    for (auto& m : g_trackedMissiles) {
        // check if already in g_skillshots by address match (store in casterIdx as tag)
        bool dup = false;
        for (auto& ss : g_skillshots) {
            if (ss.casterIdx == -99 && strcmp(ss.spellName, m.spellName) == 0 &&
                fabsf(ss.startPos.x - m.startPos.x) < 10.f &&
                fabsf(ss.startPos.z - m.startPos.z) < 10.f) {
                // update missile head position from live tracking
                float dx = m.endPos.x - m.startPos.x;
                float dz = m.endPos.z - m.startPos.z;
                float len = sqrtf(dx * dx + dz * dz);
                if (len > 1.f) {
                    dx /= len; dz /= len;
                    float traveled = sqrtf(
                        (m.currentPos.x - m.startPos.x) * (m.currentPos.x - m.startPos.x) +
                        (m.currentPos.z - m.startPos.z) * (m.currentPos.z - m.startPos.z));
                    // recalc startTime so the drawing code's elapsed * speed matches actual position
                    if (m.speed > 1.f) {
                        ss.startTime = g_gameTime - (traveled / m.speed);
                    }
                }
                dup = true;
                break;
            }
        }
        if (dup) continue;

        // skip if missile has already traveled past its range
        float traveledSoFar = sqrtf(
            (m.currentPos.x - m.startPos.x) * (m.currentPos.x - m.startPos.x) +
            (m.currentPos.z - m.startPos.z) * (m.currentPos.z - m.startPos.z));
        if (traveledSoFar > m.range + 100.f) continue;

        // push new skillshot from missile data
        if (m.speed >= g_skillshotMinSpeed && m.width > 0.f && m.range > 100.f) {
            // remove any cast-based entry that matches this missile (prevents double drawing)
            // e.g. cast "ThreshQ" should be replaced when missile "ThreshQMissile" appears
            {
                std::string missileStem = MissileStripSuffix(m.spellName);
                for (auto it2 = g_skillshots.begin(); it2 != g_skillshots.end(); ) {
                    if (it2->casterIdx != -99) {  // cast-based entry (not missile)
                        std::string castStem = MissileStripSuffix(it2->spellName);
                        if (castStem == missileStem ||
                            strcmp(it2->spellName, missileStem.c_str()) == 0 ||
                            strcmp(m.spellName, it2->spellName) == 0) {
                            it2 = g_skillshots.erase(it2);
                            continue;
                        }
                    }
                    ++it2;
                }
            }
            TrackedSkillshot ss{};
            ss.startPos     = m.startPos;
            ss.endPos       = m.endPos;
            ss.missileSpeed = m.speed;
            ss.lineWidth    = m.width;
            ss.castRange    = m.range;
            // set startTime based on how far the missile has already traveled
            ss.startTime    = (m.speed > 1.f) ? (g_gameTime - traveledSoFar / m.speed) : g_gameTime;
            ss.duration     = m.range / m.speed;
            ss.delayS       = 0.f;  // missile is already in flight, no delay
            ss.casterIdx    = -99;  // tag: this came from missile tracking, not cast detection
            strncpy(ss.spellName, m.spellName, 31);
            if (g_skillshots.size() < 30)
                g_skillshots.push_back(ss);
        }
    }
}

// ── Cast-based Cooldown Tracker (enemies) ──
struct CastCooldown {
    float castTime;
    float cooldownDuration;
    float readyAt;
    char  spellName[32];
    bool  valid;
};
struct EnemyCooldowns {
    CastCooldown slots[6];
};
static EnemyCooldowns g_enemyCDs[12] = {};
static uintptr_t g_lastEnemyCast[12] = {};
static bool g_castCDTracker = true;

// ── Cached slot names for name-based cast matching ──
struct CachedSlotNames {
    char names[6][64];   // 0-3=QWER, 4-5=D/F
    bool cached;
};
static CachedSlotNames g_heroSlotNames[12] = {};

// ── SpellCast SlotIndex offset (auto-discovered by probe) ──
static uintptr_t g_spellCastSlotIdxOff = 0;  // 0 = not yet found
static bool g_slotIdxProbeComplete = false;

// ── SpellSlot → ActiveCast pointer offset (auto-discovered) ──
static uintptr_t g_slotCastPtrOff = 0;  // offset on SpellSlot that holds active cast pointer

// ── Summoner spell CD lookup table ──
static float LookupSummonerCD(const char* name) {
    if (!name || !name[0]) return 0.f;
    if (strstr(name, "Flash"))     return 300.f;
    if (strstr(name, "Dot"))       return 180.f;   // Ignite = SummonerDot
    if (strstr(name, "Heal"))      return 240.f;
    if (strstr(name, "Exhaust"))   return 210.f;
    if (strstr(name, "Teleport"))  return 360.f;
    if (strstr(name, "Barrier"))   return 180.f;
    if (strstr(name, "Smite"))     return 15.f;
    if (strstr(name, "Cleanse"))   return 210.f;
    if (strstr(name, "Ghost"))     return 210.f;
    if (strstr(name, "Haste"))     return 210.f;   // Ghost alt name
    if (strstr(name, "Snowball"))  return 40.f;    // ARAM Mark
    return 240.f;  // safe default
}

// ── Tracked enemy summoner CDs (our own calculation) ──
struct TrackedSummCD {
    float castTime;
    float readyAt;
    float totalCD;
    bool  tracked;
};
static TrackedSummCD g_trackedSummD[12] = {};
static TrackedSummCD g_trackedSummF[12] = {};
// Previous readyAt values for change-detection (encrypted values still change on cast)
static float g_prevSummReadyAt[12][2] = {};
static bool  g_prevSummReadyAtValid[12][2] = {};

// ── Auto Smite ──
static bool  g_smiteEnabled       = false;
static int   g_smiteSlot          = -1;   // 4 or 5 (D or F), -1 if no smite
static float g_smiteReadyAt       = 0.f;
static bool  g_smiteReady         = false;
static char  g_smiteName[64]      = "";

static float SmiteDamage(int level) {
    // Season 14+ smite: 600 base at level 1, scales up. Upgraded (pets) = 900.
    // Simple table: 600 + (level-1)*30, capped at practical values
    if (level < 1) level = 1;
    if (level > 18) level = 18;
    return 600.f + (level - 1) * 30.f;
}

// ── Awareness / Tracker ──
static bool g_trackerEnabled      = true;
static bool g_trackerSumms        = true;
static bool g_trackerJungle       = true;

struct JungleCamp {
    const char* name;
    const char* display;
    float       respawnTime;
    float       killedAt;
    bool        alive;
    Vec3        pos;
    float       buffRespawnAt; // exact respawn time from buff reading (0 = unknown)
};
static JungleCamp g_jungleCamps[] = {
    {"SRU_Baron",       "Baron",    360.f, 0.f, true, {5007.f, 0.f, 10471.f}, 0.f},
    {"SRU_Dragon",      "Dragon",   300.f, 0.f, true, {9866.f, 0.f, 4414.f}, 0.f},
    {"SRU_RiftHerald",  "Herald",   360.f, 0.f, true, {5007.f, 0.f, 10471.f}, 0.f},
    {"SRU_Blue",        "Blue (T)",  300.f, 0.f, true, {3821.f, 0.f, 8101.f}, 0.f},
    {"SRU_Blue",        "Blue (B)",  300.f, 0.f, true, {11131.f, 0.f, 6990.f}, 0.f},
    {"SRU_Red",         "Red (T)",   300.f, 0.f, true, {7066.f, 0.f, 10975.f}, 0.f},
    {"SRU_Red",         "Red (B)",   300.f, 0.f, true, {7762.f, 0.f, 4011.f}, 0.f},
    {"SRU_Gromp",       "Gromp (T)", 135.f, 0.f, true, {2288.f, 0.f, 8448.f}, 0.f},
    {"SRU_Gromp",       "Gromp (B)", 135.f, 0.f, true, {12703.f, 0.f, 6443.f}, 0.f},
    {"SRU_Murkwolf",    "Wolves (T)",135.f, 0.f, true, {3783.f, 0.f, 6495.f}, 0.f},
    {"SRU_Murkwolf",    "Wolves (B)",135.f, 0.f, true, {11059.f, 0.f, 8419.f}, 0.f},
    {"SRU_Razorbeak",   "Raptors (T)",135.f,0.f, true, {7061.f, 0.f, 5325.f}, 0.f},
    {"SRU_Razorbeak",   "Raptors (B)",135.f,0.f, true, {7820.f, 0.f, 9644.f}, 0.f},
    {"SRU_Krug",        "Krugs (T)", 135.f, 0.f, true, {8394.f, 0.f, 2641.f}, 0.f},
    {"SRU_Krug",        "Krugs (B)", 135.f, 0.f, true, {6499.f, 0.f, 12287.f}, 0.f},
};
constexpr int NUM_CAMPS = sizeof(g_jungleCamps) / sizeof(g_jungleCamps[0]);

static bool g_menuOpen      = true;
static int  g_activeTab     = 0;
static bool g_fpsShow       = true;
static bool g_keyDisplayOpen = true;
static float g_bgColor[3] = { 0.f, 0.f, 0.f }; 
static bool  g_highContrast = true;  

static bool g_espEnabled       = true;
static bool g_espNames         = true;
static bool g_espHealth        = true;
static bool g_espBox           = true;
static bool g_espShields       = true;
static bool g_espAttackRange   = true;
static bool g_espVisStatus     = true;
static bool g_espLastPosition  = true;
static bool g_espEnemyOnly     = false;
static bool g_espMinions       = true;
static bool g_espTurretRange   = true;
static float g_turretRange     = 850.f;

static bool  g_minimapEnabled   = true;
static float g_minimapSize      = 0.f;  // 0 = auto-compute from resolution
static float g_minimapOpacity   = 0.85f;
static float g_minimapOffX      = -1.f; // -1 = auto-compute from resolution
static float g_minimapOffY      = -1.f;
static bool  g_minimapAutoPos   = true; // true until user manually adjusts

static void MinimapAutoDefaults() {
    if (g_minimapSize <= 0.f)  g_minimapSize = g_screenH * 0.235f;
    if (g_minimapOffX < 0.f)   g_minimapOffX = g_screenW * 0.868f;
    if (g_minimapOffY < 0.f)   g_minimapOffY = g_screenH * 0.757f;
}
static void MinimapResetToGame() {
    g_minimapSize = g_screenH * 0.235f;
    g_minimapOffX = g_screenW * 0.868f;
    g_minimapOffY = g_screenH * 0.757f;
}

static int  g_drawMode         = 0;  // 0=outlined, 1=filled
static bool g_drawGlow         = true;
static float g_espBoxColor[4]  = { 1.f, 0.3f, 0.3f, 1.f };
static float g_allyColor[4]    = { 0.1f, 0.5f, 1.f, 1.f };
static float g_enemyColor[4]   = { 1.f, 0.2f, 0.2f, 1.f };

// ── Enemy Jungler Tracker ──
static bool  g_junglerTracker     = true;
static bool  g_junglerDangerAlert = true;
static float g_junglerDangerRange = 3000.f;  // world units — flash alert when jungler spotted within this range
static int   g_enemyJunglerIdx    = -1;      // index into g_heroes, -1 = not found yet
static float g_junglerAlertTime   = 0.f;     // game time of last danger alert trigger
static float g_junglerAlertDuration = 3.f;   // how long alert stays on screen

static char g_makcuPort[32]    = "";
static int  g_makcuBaud        = 115200;
static bool g_makcuAutoConnect = false;

static int  g_inputDeviceType  = 1;  // 0=Makcu, 1=KMBox B+ (COM), 2=KMBox B+ (Network)
static char g_kmboxIP[64]      = "192.168.2.188";
static int  g_kmboxPort        = 16896;
static char g_kmboxUUID[64]    = "";

static bool      g_orbEnabled      = true;
static int       g_orbMode         = 0;
static float     g_orbAttackSpeed  = 0.625f; // manual fallback attacks/sec
static int       g_orbWindupPct    = 35;
static float     g_orbPlayerAD     = 70.f;   // manual fallback AD
static float     g_orbMoveDelay    = 30.f;
static bool      g_orbAutoStats    = true;   // read AD/AS from memory
static float     g_orbBaseAS       = 0.625f; // champion base AS (manual, multiplied by ASMod)
static float     g_orbPingMs       = 0.f;    // estimated round-trip ping in ms (read via DMA)
static float     g_orbTimingBias   = 0.f;    // ms bias on attack window (negative = early, 0 = neutral)
static bool      g_orbPingComp     = true;   // enable ping-compensated attack timing

static int       g_orbCursorPolicy = 0;      // 0=Free (no cursor moves, attack-move-click), 1=Aim+Restore via smooth path
static int       g_orbPathMinMs    = 35;     // cursor path duration min
static int       g_orbPathMaxMs    = 90;     // cursor path duration max
static int       g_orbReactMinMs   = 0;      // reaction floor after target/killable event (was 25 — too sluggish)
static int       g_orbReactJitterMs = 0;     // added reaction jitter (was 55 — killed last-hit responsiveness)
static bool      g_orbClickSelectInMode = true;
static bool      g_orbLastHitOnly  = true;  // only AA predicted killables where applicable
static int       g_orbLastHitBufferPct = 95; // predicted HP threshold = effectiveAD * pct/100
static int       g_orbSpellGateMs  = 130;   // generic spell gate
static bool      g_orbHumanize     = true;  // bounded timing/cadence variance
static std::unordered_map<std::string, int> g_orbWindupPctChamp;

struct KeyOption { const char* label; uint8_t hid; };
static const KeyOption g_keyOptions[] = {
    {"Q",0x14},{"W",0x1A},{"E",0x08},{"R",0x15},{"T",0x17},
    {"Y",0x1C},{"U",0x18},{"I",0x0C},{"O",0x12},{"P",0x13},
    {"A",0x04},{"S",0x16},{"D",0x07},{"F",0x09},{"G",0x0A},
    {"H",0x0B},{"J",0x0D},{"K",0x0E},{"L",0x0F},
    {"Z",0x1D},{"X",0x1B},{"C",0x06},{"V",0x19},{"B",0x05},
    {"N",0x11},{"M",0x10},
    {"1",0x1E},{"2",0x1F},{"3",0x20},{"4",0x21},{"5",0x22},
    {"6",0x23},{"7",0x24},{"8",0x25},{"9",0x26},{"0",0x27},
    {"F1",0x3A},{"F2",0x3B},{"F3",0x3C},{"F4",0x3D},
    {"F5",0x3E},{"F6",0x3F},{"F7",0x40},{"F8",0x41},
    {"F9",0x42},{"F10",0x43},{"F11",0x44},{"F12",0x45},
    {"Space",0x2C},{"Tab",0x2B},{"`",0x35},
    {"LMB",0xF0},{"RMB",0xF1},
};
static const int g_keyOptionCount = sizeof(g_keyOptions) / sizeof(g_keyOptions[0]);
static int FindKeyIdx(uint8_t hid) {
    for (int i = 0; i < g_keyOptionCount; i++)
        if (g_keyOptions[i].hid == hid) return i;
    return 0;
}
static void PressKey(uint8_t hid) {
    if (hid == 0xF0) { InputDevice::LeftDown(); InputDevice::LeftUp(); }
    else if (hid == 0xF1) { InputDevice::RightDown(); InputDevice::RightUp(); }
    else { InputDevice::KeyDown(hid); InputDevice::KeyUp(hid); }
}

static int g_orbAtkKeyIdx    = 10; // default A
static int g_orbSpellKeyIdx[4] = {0, 1, 2, 3}; // default Q, W, E, R

// Mode activation keys (configurable)
// defaults: Space=Combo, C=Harass, V=LaneClear, X=LastHit, Z=Flee
static int g_orbModeKeyIdx[5] = {48, 21, 22, 20, 19}; // indices into g_keyOptions: Space, C, V, X, Z
static bool g_orbModeEnabled[5] = {true, true, true, true, true}; // per-mode on/off: Combo, Harass, Clear, LastHit, Flee

static uint32_t HidToVk(uint8_t hid) {
    if (hid >= 0x04 && hid <= 0x1D) return 'A' + (hid - 0x04);
    if (hid >= 0x1E && hid <= 0x26) return '1' + (hid - 0x1E);
    if (hid == 0x27) return '0';
    if (hid >= 0x3A && hid <= 0x45) return VK_F1 + (hid - 0x3A);
    if (hid == 0x2C) return VK_SPACE;
    if (hid == 0x2B) return VK_TAB;
    if (hid == 0x35) return VK_OEM_3; // backtick
    if (hid == 0xF0) return VK_LBUTTON;
    if (hid == 0xF1) return VK_RBUTTON;
    return 0;
}

static ULONGLONG g_orbLastAttack   = 0;
static ULONGLONG g_orbLastMove     = 0;
static ULONGLONG g_orbLastActionEvent = 0; // target/killable change for reaction floor
static int       g_orbActionReactMs = 0;
static uintptr_t g_orbLastActionTarget = 0;
static int       g_orbTargetIdx    = -1;
static bool      g_orbTargetIsMinion = false;
static uintptr_t g_orbStickyAddr   = 0;
static ULONGLONG g_orbStickyUntil  = 0;
static float     g_cursorX = 960.f, g_cursorY = 540.f; 

static bool      g_orbDrawRange    = true;
static bool      g_orbDrawTarget   = true;
static bool      g_orbDrawLastHit  = true;

static bool      g_orbUseQ = true;
static bool      g_orbUseW = true;
static bool      g_orbUseE = true;
static bool      g_orbUseR = false;

static ULONGLONG g_orbLastSpell[4] = {};
static float     g_orbEffectiveAD  = 70.f;   // computed each tick for drawings
static bool      g_orbReadCooldowns = true;  // read spell CDs from memory

static int       g_orbTargetMode   = 1;
static int       g_orbPriorityMode = 0; // 0=closest to champion, 1=lowest HP, 2=most AD, 3=most AP

static int       g_orbSelectedHero = -1;
static uintptr_t g_orbSelectedAddr = 0;
static std::unordered_map<std::string, int> g_orbPriority;

static float     g_orbClickOffY    = 0.f;
static float     g_orbClickOffX    = 0.f;
static float     g_orbClickOffZ    = 0.f;
static uintptr_t g_orbClickTarget  = 0;
static int       g_orbClickSpread  = 70;

struct MinionHpSample { uintptr_t addr = 0; float hp = 0.f; float prevHp = 0.f; ULONGLONG t = 0; ULONGLONG prevT = 0; };
static MinionHpSample g_orbMinionHp[96];

static void OrbTrackMinionHp(uintptr_t addr, float hp, ULONGLONG now)
{
    if (!addr) return;
    int slot = -1, freeSlot = -1, oldest = 0; ULONGLONG oldestT = ~0ULL;
    for (int i = 0; i < 96; i++) {
        auto& s = g_orbMinionHp[i];
        if (s.addr == addr) { slot = i; break; }
        if (!s.addr && freeSlot < 0) freeSlot = i;
        if (s.t < oldestT) { oldestT = s.t; oldest = i; }
    }
    if (slot < 0) slot = (freeSlot >= 0) ? freeSlot : oldest;
    auto& s = g_orbMinionHp[slot];
    if (s.addr != addr) { s = {}; s.addr = addr; s.hp = s.prevHp = hp; s.t = s.prevT = now; return; }
    if (now - s.t >= 50 && fabsf(hp - s.hp) > 0.01f) { s.prevHp = s.hp; s.prevT = s.t; s.hp = hp; s.t = now; }
}

static float OrbPredictMinionHp(uintptr_t addr, float hp, float hitMs, ULONGLONG now)
{
    for (int i = 0; i < 96; i++) {
        auto& s = g_orbMinionHp[i];
        if (s.addr != addr || s.prevT == 0 || s.t <= s.prevT) continue;
        if (now - s.t > 800) break;
        float dt = (float)(s.t - s.prevT);
        float dps = (s.prevHp - s.hp) / std::max(dt, 1.f) * 1000.f;
        if (dps <= 0.f) break;
        float predict = hp - dps * (hitMs / 1000.f);
        return std::max(0.f, predict);
    }
    return hp;
}

static bool OrbCanReact(ULONGLONG now)
{
    if (g_orbReactMinMs <= 0 && g_orbReactJitterMs <= 0) return true;
    return (now - g_orbLastActionEvent) >= (ULONGLONG)std::max(0, g_orbActionReactMs);
}

static void OrbNoteActionEvent(uintptr_t targetAddr, ULONGLONG now)
{
    if (targetAddr != g_orbLastActionTarget) {
        g_orbLastActionTarget = targetAddr;
        g_orbLastActionEvent = now;
        g_orbActionReactMs = std::max(0, g_orbReactMinMs) + randInt(0, std::max(0, g_orbReactJitterMs));
    }
}

static void MoveCursorPath(float targetX, float targetY, int maxTotalMs)
{
    (void)maxTotalMs; // non-blocking: never Sleep in main tick
    float dx = targetX - g_cursorX;
    float dy = targetY - g_cursorY;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist < 1.5f) return;
    int steps = (dist < 18.f) ? 1 : ((dist < 80.f) ? 2 : 3);
    float nx = -dy / std::max(dist, 1.f), ny = dx / std::max(dist, 1.f);
    float arc = (g_orbHumanize && steps > 1) ? randFloat(-0.03f, 0.03f) * std::min(dist, 120.f) : 0.f;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float ease = t * t * (3.f - 2.f * t);
        float wobble = (steps > 1 && i < steps) ? sinf(t * 3.14159265f) * arc : 0.f;
        float px = (i == steps) ? targetX : (g_cursorX + dx * ease + nx * wobble);
        float py = (i == steps) ? targetY : (g_cursorY + dy * ease + ny * wobble);
        int mx = (int)(px - g_cursorX);
        int my = (int)(py - g_cursorY);
        if (mx || my) InputDevice::MoveTo(mx, my);
        g_cursorX += (float)mx;
        g_cursorY += (float)my;
    }
}

static void OrbAimAndRestore(const Vec2& aim, bool enabled, Vec2& restore, bool& moved)
{
    moved = false;
    if (!enabled) return;
    float dx = aim.x - g_cursorX, dy = aim.y - g_cursorY;
    if ((dx*dx + dy*dy) < 9.f) return;
    restore = { g_cursorX, g_cursorY };
    MoveCursorPath(aim.x, aim.y, 0);
    moved = true;
}

static void OrbRestoreCursor(const Vec2& restore, bool moved)
{
    if (!moved) return;
    float dx = restore.x - g_cursorX, dy = restore.y - g_cursorY;
    if ((dx*dx + dy*dy) < 9.f) return;
    // quick restore: single move to avoid visible hitch
    int mx = (int)(restore.x - g_cursorX);
    int my = (int)(restore.y - g_cursorY);
    if (mx || my) InputDevice::MoveTo(mx, my);
    g_cursorX += (float)mx;
    g_cursorY += (float)my;
}

static void IssueAttackKey(uint8_t atkHid)
{
    if (atkHid == 0xF0) { InputDevice::LeftDown(); InputDevice::LeftUp(); }
    else if (atkHid == 0xF1) { InputDevice::RightDown(); InputDevice::RightUp(); }
    else { InputDevice::KeyDown(atkHid); InputDevice::KeyUp(atkHid); InputDevice::LeftDown(); InputDevice::LeftUp(); }
}

static void RollClickOffset(uintptr_t targetAddr, float boundingRadius)
{
    if (targetAddr == g_orbClickTarget && g_orbClickOffY != 0.f) return;
    g_orbClickTarget = targetAddr;
    float r = (boundingRadius > 10.f && boundingRadius < 200.f) ? boundingRadius : 65.f;
    float spread = g_orbClickSpread / 100.f;
    g_orbClickOffY = randFloat(30.f, 140.f) * spread;
    float hSpread = r * 0.4f * spread;
    g_orbClickOffX = randFloat(-hSpread, hSpread);
    g_orbClickOffZ = randFloat(-hSpread, hSpread);
}

static Vec3 ApplyClickOffset(const Vec3& pos)
{
    return { pos.x + g_orbClickOffX, pos.y + g_orbClickOffY, pos.z + g_orbClickOffZ };
}

struct SpellCache {
    int   level;
    float readyAt;
    float totalCD;
    float range;
    bool  ready;
    char  name[64];
};
static SpellCache g_spells[4] = {};

static char  g_localChampName[64] = "";
static float g_localHealth    = 0.f;
static float g_localMaxHealth = 0.f;

static float g_activeCastEnd    = 0.f;
static float g_activeAttackEnd  = 0.f;
static bool  g_activeIsAuto     = false;

static ChampionCombo::Settings g_comboSettings;
static ULONGLONG g_comboRetryCooldown[4] = {};
static bool g_comboEnabled = true;

static char g_configDir[MAX_PATH];
static char g_logDir[MAX_PATH];
static char g_configName[64]   = "default";
static char g_configStatus[128] = "";
static char g_licenseKey[128]  = "";

static char g_iconCacheDir[MAX_PATH];

static void InitPaths() {
    strncpy(g_configDir,    xorstr_("C:\\VeoxDMA\\Configs"), MAX_PATH - 1);
    strncpy(g_logDir,       xorstr_("C:\\VeoxDMA\\Logs"),    MAX_PATH - 1);
    strncpy(g_iconCacheDir, xorstr_("C:\\VeoxDMA\\Cache"),   MAX_PATH - 1);
}
static std::unordered_map<std::string, ID3D11ShaderResourceView*> g_iconCache;
static std::unordered_map<std::string, bool> g_iconRequested;
static char g_ddVersion[32] = "15.1.1";
static ULONG_PTR g_gdipToken = 0;

static void EnsureDirs() {
    std::error_code ec;
    std::filesystem::create_directories(xorstr_("C:\\VeoxDMA"), ec);
    std::filesystem::create_directories(g_configDir, ec);
    std::filesystem::create_directories(g_logDir, ec);
    std::filesystem::create_directories(g_iconCacheDir, ec);
    std::filesystem::create_directories(xorstr_("C:\\VeoxDMA\\Scripts"), ec);
}

static void FetchDDVersion() {
    wchar_t tmpPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPath);
    wcscat_s(tmpPath, L"dd_versions.json");
    if (SUCCEEDED(URLDownloadToFileW(nullptr, L"https://ddragon.leagueoflegends.com/api/versions.json", tmpPath, 0, nullptr))) {
        FILE* f = _wfopen(tmpPath, L"r");
        if (f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = 0;
            fclose(f);
            char* start = strchr(buf, '"');
            if (start) {
                start++;
                char* end = strchr(start, '"');
                if (end) { *end = 0; strncpy(g_ddVersion, start, sizeof(g_ddVersion) - 1); }
            }
            DeleteFileW(tmpPath);
        }
    }
    VX_LOG("[Icons] Data Dragon version: %s\n", g_ddVersion);
}

static void DownloadChampIconThread(std::string champName) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    char cachePath[MAX_PATH];
    snprintf(cachePath, sizeof(cachePath), "%s\\%s.png", g_iconCacheDir, champName.c_str());
    if (!std::filesystem::exists(cachePath)) {
        char url[512];
        snprintf(url, sizeof(url), xorstr_("https://ddragon.leagueoflegends.com/cdn/%s/img/champion/%s.png"), g_ddVersion, champName.c_str());
        wchar_t wUrl[512], wPath[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wUrl, 512);
        MultiByteToWideChar(CP_UTF8, 0, cachePath, -1, wPath, MAX_PATH);
        HRESULT hr = URLDownloadToFileW(nullptr, wUrl, wPath, 0, nullptr);
        VX_LOG("[Icons] Download %s: %s\n", champName.c_str(), SUCCEEDED(hr) ? "OK" : "FAILED");
    }
    CoUninitialize();
}

static ID3D11ShaderResourceView* LoadTextureFromPNG(const wchar_t* path) {
    if (!g_device) return nullptr;
    Gdiplus::Bitmap bmp(path);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return nullptr;
    UINT w = bmp.GetWidth(), h = bmp.GetHeight();
    if (w == 0 || h == 0) return nullptr;

    Gdiplus::BitmapData data;
    Gdiplus::Rect rect(0, 0, (INT)w, (INT)h);
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = data.Scan0;
    initData.SysMemPitch = data.Stride;

    ID3D11Texture2D* tex = nullptr;
    g_device->CreateTexture2D(&desc, &initData, &tex);
    bmp.UnlockBits(&data);
    if (!tex) return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    g_device->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    return srv;
}

static ID3D11ShaderResourceView* GetChampIcon(const char* name) {
    if (!name[0]) return nullptr;
    auto it = g_iconCache.find(name);
    if (it != g_iconCache.end()) return it->second;

    char cachePath[MAX_PATH];
    snprintf(cachePath, sizeof(cachePath), "%s\\%s.png", g_iconCacheDir, name);
    if (std::filesystem::exists(cachePath)) {
        wchar_t wPath[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, cachePath, -1, wPath, MAX_PATH);
        auto* srv = LoadTextureFromPNG(wPath);
        g_iconCache[name] = srv;
        return srv;
    }

    if (g_iconRequested.find(name) == g_iconRequested.end()) {
        g_iconRequested[name] = true;
        std::thread(DownloadChampIconThread, std::string(name)).detach();
    }
    return nullptr;
}

static void SaveConfig(const char* name) {
    EnsureDirs();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s.cfg", g_configDir, name);
    FILE* f = fopen(path, "w");
    if (!f) { snprintf(g_configStatus, sizeof(g_configStatus), "Failed to save %s", name); return; }

    fprintf(f, xorstr_("# VeoxDMA Config: %s\n"), name);
    fprintf(f, xorstr_("esp_enabled=%d\n"), g_espEnabled);
    fprintf(f, xorstr_("esp_names=%d\n"), g_espNames);
    fprintf(f, xorstr_("esp_health=%d\n"), g_espHealth);
    fprintf(f, xorstr_("esp_box=%d\n"), g_espBox);
    fprintf(f, xorstr_("esp_shields=%d\n"), g_espShields);
    fprintf(f, xorstr_("esp_attack_range=%d\n"), g_espAttackRange);
    fprintf(f, xorstr_("esp_vis_status=%d\n"), g_espVisStatus);
    fprintf(f, xorstr_("esp_last_pos=%d\n"), g_espLastPosition);
    fprintf(f, xorstr_("esp_enemy_only=%d\n"), g_espEnemyOnly);
    fprintf(f, xorstr_("esp_minions=%d\n"), g_espMinions);
    fprintf(f, xorstr_("esp_turret_range=%d\n"), g_espTurretRange);
    fprintf(f, xorstr_("turret_range=%.0f\n"), g_turretRange);
    fprintf(f, xorstr_("draw_mode=%d\n"), g_drawMode);
    fprintf(f, xorstr_("draw_glow=%d\n"), g_drawGlow);
    fprintf(f, xorstr_("enemy_color=%.3f,%.3f,%.3f,%.3f\n"), g_enemyColor[0], g_enemyColor[1], g_enemyColor[2], g_enemyColor[3]);
    fprintf(f, xorstr_("ally_color=%.3f,%.3f,%.3f,%.3f\n"), g_allyColor[0], g_allyColor[1], g_allyColor[2], g_allyColor[3]);
    fprintf(f, xorstr_("bg_color=%.3f,%.3f,%.3f\n"), g_bgColor[0], g_bgColor[1], g_bgColor[2]);
    fprintf(f, xorstr_("jungler_tracker=%d\n"), g_junglerTracker);
    fprintf(f, xorstr_("jungler_danger=%d\n"), g_junglerDangerAlert);
    fprintf(f, xorstr_("jungler_danger_range=%.0f\n"), g_junglerDangerRange);
    fprintf(f, xorstr_("minimap=%d\n"), g_minimapEnabled);
    fprintf(f, xorstr_("minimap_size=%.0f\n"), g_minimapSize);
    fprintf(f, xorstr_("minimap_opacity=%.2f\n"), g_minimapOpacity);
    fprintf(f, xorstr_("minimap_x=%.0f\n"), g_minimapOffX);
    fprintf(f, xorstr_("minimap_y=%.0f\n"), g_minimapOffY);
    fprintf(f, xorstr_("monitor=%d\n"), g_monitorIndex);
    fprintf(f, xorstr_("show_fps=%d\n"), g_fpsShow);
    fprintf(f, xorstr_("high_contrast=%d\n"), g_highContrast);
    fprintf(f, xorstr_("orb_enabled=%d\n"), g_orbEnabled);
    fprintf(f, xorstr_("orb_auto_stats=%d\n"), g_orbAutoStats);
    fprintf(f, xorstr_("orb_base_as=%.3f\n"), g_orbBaseAS);
    fprintf(f, xorstr_("orb_attack_speed=%.3f\n"), g_orbAttackSpeed);
    fprintf(f, xorstr_("orb_player_ad=%.0f\n"), g_orbPlayerAD);
    fprintf(f, xorstr_("orb_windup=%d\n"), g_orbWindupPct);
    fprintf(f, xorstr_("orb_move_delay=%.0f\n"), g_orbMoveDelay);
    fprintf(f, xorstr_("orb_path_min=%d\n"), g_orbPathMinMs);
    fprintf(f, xorstr_("orb_path_max=%d\n"), g_orbPathMaxMs);
    fprintf(f, xorstr_("orb_cursor_policy=%d\n"), g_orbCursorPolicy);
    fprintf(f, xorstr_("orb_react_min=%d\n"), g_orbReactMinMs);
    fprintf(f, xorstr_("orb_react_jitter=%d\n"), g_orbReactJitterMs);
    fprintf(f, xorstr_("orb_humanize=%d\n"), g_orbHumanize ? 1 : 0);
    fprintf(f, xorstr_("orb_click_select_mode=%d\n"), g_orbClickSelectInMode ? 1 : 0);
    fprintf(f, xorstr_("orb_lasthit_only=%d\n"), g_orbLastHitOnly ? 1 : 0);
    fprintf(f, xorstr_("orb_lasthit_buffer=%d\n"), g_orbLastHitBufferPct);
    fprintf(f, xorstr_("orb_spell_gate=%d\n"), g_orbSpellGateMs);
    for (auto& [name, pct] : g_orbWindupPctChamp)
        fprintf(f, xorstr_("orb_windup_champ_%s=%d\n"), name.c_str(), pct);
    fprintf(f, xorstr_("orb_click_spread=%d\n"), g_orbClickSpread);
    fprintf(f, xorstr_("orb_ping_comp=%d\n"), g_orbPingComp);
    fprintf(f, xorstr_("orb_timing_bias=%.0f\n"), g_orbTimingBias);
    fprintf(f, xorstr_("orb_atk_key_hid=%d\n"), g_keyOptions[g_orbAtkKeyIdx].hid);
    fprintf(f, xorstr_("orb_spell_q_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[0]].hid);
    fprintf(f, xorstr_("orb_spell_w_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[1]].hid);
    fprintf(f, xorstr_("orb_spell_e_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[2]].hid);
    fprintf(f, xorstr_("orb_spell_r_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[3]].hid);
    fprintf(f, xorstr_("orb_mode_combo_hid=%d\n"), g_keyOptions[g_orbModeKeyIdx[0]].hid);
    fprintf(f, xorstr_("orb_mode_harass_hid=%d\n"), g_keyOptions[g_orbModeKeyIdx[1]].hid);
    fprintf(f, xorstr_("orb_mode_clear_hid=%d\n"), g_keyOptions[g_orbModeKeyIdx[2]].hid);
    fprintf(f, xorstr_("orb_mode_lasthit_hid=%d\n"), g_keyOptions[g_orbModeKeyIdx[3]].hid);
    fprintf(f, xorstr_("orb_mode_flee_hid=%d\n"), g_keyOptions[g_orbModeKeyIdx[4]].hid);
    fprintf(f, xorstr_("orb_mode_combo_on=%d\n"), g_orbModeEnabled[0]);
    fprintf(f, xorstr_("orb_mode_harass_on=%d\n"), g_orbModeEnabled[1]);
    fprintf(f, xorstr_("orb_mode_clear_on=%d\n"), g_orbModeEnabled[2]);
    fprintf(f, xorstr_("orb_mode_lasthit_on=%d\n"), g_orbModeEnabled[3]);
    fprintf(f, xorstr_("orb_mode_flee_on=%d\n"), g_orbModeEnabled[4]);
    fprintf(f, xorstr_("orb_read_cooldowns=%d\n"), g_orbReadCooldowns);
    fprintf(f, xorstr_("orb_use_q=%d\n"), g_orbUseQ);
    fprintf(f, xorstr_("orb_use_w=%d\n"), g_orbUseW);
    fprintf(f, xorstr_("orb_use_e=%d\n"), g_orbUseE);
    fprintf(f, xorstr_("orb_use_r=%d\n"), g_orbUseR);
    fprintf(f, xorstr_("orb_draw_range=%d\n"), g_orbDrawRange);
    fprintf(f, xorstr_("orb_draw_target=%d\n"), g_orbDrawTarget);
    fprintf(f, xorstr_("orb_draw_lasthit=%d\n"), g_orbDrawLastHit);
    fprintf(f, xorstr_("orb_target_mode=%d\n"), g_orbTargetMode);
    fprintf(f, xorstr_("orb_priority_mode=%d\n"), g_orbPriorityMode);
    for (auto& [name, prio] : g_orbPriority)
        fprintf(f, xorstr_("orb_prio_%s=%d\n"), name.c_str(), prio);
        fprintf(f, xorstr_("makcu_port=%s\n"), g_makcuPort);
    fprintf(f, xorstr_("makcu_baud=%d\n"), g_makcuBaud);
    fprintf(f, xorstr_("input_device=%d\n"), g_inputDeviceType);
    fprintf(f, xorstr_("kmbox_ip=%s\n"), g_kmboxIP);
    fprintf(f, xorstr_("kmbox_port=%d\n"), g_kmboxPort);
    fprintf(f, xorstr_("kmbox_uuid=%s\n"), g_kmboxUUID);

    fclose(f);
    snprintf(g_configStatus, sizeof(g_configStatus), "Saved: %s", name);
    VX_LOG("[Config] Saved %s\n", path);
}

static void LoadConfig(const char* name) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s.cfg", g_configDir, name);
    FILE* f = fopen(path, "r");
    if (!f) { snprintf(g_configStatus, sizeof(g_configStatus), "No config: %s", name); return; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* val = eq + 1;
        char* nl = strchr(val, '\n'); if (nl) *nl = 0;
        char* cr = strchr(val, '\r'); if (cr) *cr = 0;

            if (!strcmp(line, xorstr_("esp_enabled")))       g_espEnabled = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_names")))    g_espNames = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_health")))   g_espHealth = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_box")))      g_espBox = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_shields")))  g_espShields = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_attack_range"))) g_espAttackRange = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_vis_status")))   g_espVisStatus = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_last_pos")))     g_espLastPosition = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_enemy_only")))   g_espEnemyOnly = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_minions")))      g_espMinions = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_turret_range"))) g_espTurretRange = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("turret_range")))     g_turretRange = (float)atof(val);
        else if (!strcmp(line, xorstr_("draw_quality")))     {} // legacy, ignored
        else if (!strcmp(line, xorstr_("draw_mode")))        g_drawMode = std::clamp(atoi(val), 0, 1);
        else if (!strcmp(line, xorstr_("draw_glow")))        g_drawGlow = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("enemy_color")))      sscanf(val, "%f,%f,%f,%f", &g_enemyColor[0], &g_enemyColor[1], &g_enemyColor[2], &g_enemyColor[3]);
        else if (!strcmp(line, xorstr_("ally_color")))       sscanf(val, "%f,%f,%f,%f", &g_allyColor[0], &g_allyColor[1], &g_allyColor[2], &g_allyColor[3]);
        else if (!strcmp(line, xorstr_("bg_color")))         sscanf(val, "%f,%f,%f", &g_bgColor[0], &g_bgColor[1], &g_bgColor[2]);
        else if (!strcmp(line, xorstr_("sidebar")))          {} // removed
        else if (!strcmp(line, xorstr_("sidebar_side")))     {} // removed
        else if (!strcmp(line, xorstr_("jungler_tracker")))  g_junglerTracker = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("jungler_danger")))   g_junglerDangerAlert = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("jungler_danger_range"))) g_junglerDangerRange = std::clamp((float)atof(val), 1000.f, 8000.f);
        else if (!strcmp(line, xorstr_("minimap")))          g_minimapEnabled = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("minimap_size")))     g_minimapSize = std::clamp((float)atof(val), 60.f, 600.f);
        else if (!strcmp(line, xorstr_("minimap_opacity")))  g_minimapOpacity = std::clamp((float)atof(val), 0.3f, 1.f);
        else if (!strcmp(line, xorstr_("minimap_x")))        g_minimapOffX = std::max(0.f, (float)atof(val));
        else if (!strcmp(line, xorstr_("minimap_y")))        g_minimapOffY = std::max(0.f, (float)atof(val));
            else if (!strcmp(line, xorstr_("monitor")))          g_monitorIndex = atoi(val);
        else if (!strcmp(line, xorstr_("show_fps")))         g_fpsShow = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("high_contrast")))    g_highContrast = atoi(val) != 0;

        else if (!strcmp(line, xorstr_("orb_enabled")))      g_orbEnabled = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_auto_stats")))   g_orbAutoStats = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_base_as")))      g_orbBaseAS = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_attack_speed"))) g_orbAttackSpeed = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_player_ad")))    g_orbPlayerAD = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_windup")))       g_orbWindupPct = atoi(val);
        else if (!strcmp(line, xorstr_("orb_move_delay")))   g_orbMoveDelay = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_settle_min")))   g_orbPathMinMs = std::clamp(atoi(val), 0, 500); // legacy -> path min
        else if (!strcmp(line, xorstr_("orb_settle_max")))   g_orbPathMaxMs = std::clamp(atoi(val), 0, 500); // legacy -> path max
        else if (!strcmp(line, xorstr_("orb_path_min")))     g_orbPathMinMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_path_max")))     g_orbPathMaxMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_cursor_policy"))) g_orbCursorPolicy = std::clamp(atoi(val), 0, 1);
        else if (!strcmp(line, xorstr_("orb_react_min")))    g_orbReactMinMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_react_jitter"))) g_orbReactJitterMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_humanize")))     g_orbHumanize = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_click_select_mode"))) g_orbClickSelectInMode = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_lasthit_only"))) g_orbLastHitOnly = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_lasthit_buffer"))) g_orbLastHitBufferPct = std::clamp(atoi(val), 50, 100);
        else if (!strcmp(line, xorstr_("orb_spell_gate")))   g_orbSpellGateMs = std::clamp(atoi(val), 0, 2000);
        else if (!strncmp(line, xorstr_("orb_windup_champ_"), 17)) {
            g_orbWindupPctChamp[line + 17] = std::clamp(atoi(val), 1, 100);
        }
        else if (!strcmp(line, xorstr_("orb_click_spread"))) g_orbClickSpread = std::clamp(atoi(val), 0, 100);
        else if (!strcmp(line, xorstr_("orb_ping_comp")))    g_orbPingComp = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_timing_bias")))  g_orbTimingBias = std::clamp((float)atof(val), -100.f, 100.f);
        else if (!strcmp(line, xorstr_("orb_atk_key_hid")))   g_orbAtkKeyIdx = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_q_hid")))  g_orbSpellKeyIdx[0] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_w_hid")))  g_orbSpellKeyIdx[1] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_e_hid")))  g_orbSpellKeyIdx[2] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_r_hid")))  g_orbSpellKeyIdx[3] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_mode_combo_hid")))   g_orbModeKeyIdx[0] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_mode_harass_hid")))  g_orbModeKeyIdx[1] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_mode_clear_hid")))   g_orbModeKeyIdx[2] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_mode_lasthit_hid"))) g_orbModeKeyIdx[3] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_mode_flee_hid")))    g_orbModeKeyIdx[4] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_mode_combo_on")))   g_orbModeEnabled[0] = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_mode_harass_on")))  g_orbModeEnabled[1] = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_mode_clear_on")))   g_orbModeEnabled[2] = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_mode_lasthit_on"))) g_orbModeEnabled[3] = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_mode_flee_on")))    g_orbModeEnabled[4] = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_atk_move_key"))) {} // legacy, ignored
        else if (!strcmp(line, xorstr_("orb_read_cooldowns"))) g_orbReadCooldowns = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_q")))        g_orbUseQ = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_w")))        g_orbUseW = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_e")))        g_orbUseE = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_r")))        g_orbUseR = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_spell_cd")))     g_orbSpellGateMs = std::clamp((int)atof(val), 0, 2000); // legacy
        else if (!strcmp(line, xorstr_("orb_draw_range")))   g_orbDrawRange = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_draw_target")))  g_orbDrawTarget = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_draw_lasthit"))) g_orbDrawLastHit = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_target_mode")))  g_orbTargetMode = std::clamp(atoi(val), 0, 1);
        else if (!strcmp(line, xorstr_("orb_priority_mode"))) g_orbPriorityMode = std::clamp(atoi(val), 0, 3);
        else if (strncmp(line, xorstr_("orb_prio_"), 9) == 0) {
            g_orbPriority[line + 9] = std::clamp(atoi(val), 1, 5);
        }
                else if (!strcmp(line, xorstr_("makcu_port")))       strncpy(g_makcuPort, val, sizeof(g_makcuPort) - 1);
        else if (!strcmp(line, xorstr_("makcu_baud")))       g_makcuBaud = atoi(val);
        else if (!strcmp(line, xorstr_("input_device")))     { g_inputDeviceType = std::clamp(atoi(val), 0, 2); InputDevice::g_deviceType = (InputDevice::DeviceType)g_inputDeviceType; }
        else if (!strcmp(line, xorstr_("kmbox_ip")))         strncpy(g_kmboxIP, val, sizeof(g_kmboxIP) - 1);
        else if (!strcmp(line, xorstr_("kmbox_port")))       g_kmboxPort = atoi(val);
        else if (!strcmp(line, xorstr_("kmbox_uuid")))       strncpy(g_kmboxUUID, val, sizeof(g_kmboxUUID) - 1);
    }
    fclose(f);
    snprintf(g_configStatus, sizeof(g_configStatus), "Loaded: %s", name);
    VX_LOG("[Config] Loaded %s\n", path);
}

static std::vector<std::string> ListConfigs() {
    std::vector<std::string> names;
    EnsureDirs();
    for (auto& entry : std::filesystem::directory_iterator(g_configDir)) {
        if (entry.path().extension() == ".cfg")
            names.push_back(entry.path().stem().string());
    }
    return names;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    EnsureDirs();
    SYSTEMTIME st; GetLocalTime(&st);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\crash_%04d%02d%02d_%02d%02d%02d.log",
        g_logDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    FILE* f = fopen(path, "w");
    if (f) {
        fprintf(f, xorstr_("Crash Report\n"));
        fprintf(f, xorstr_("=============================\n"));
        fprintf(f, xorstr_("Time: %04d-%02d-%02d %02d:%02d:%02d\n\n"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fprintf(f, xorstr_("Exception Code:    0x%08lX\n"), ep->ExceptionRecord->ExceptionCode);
        fprintf(f, xorstr_("Exception Address: 0x%p\n"), ep->ExceptionRecord->ExceptionAddress);
        fprintf(f, xorstr_("Exception Flags:   0x%08lX\n\n"), ep->ExceptionRecord->ExceptionFlags);

        auto* ctx = ep->ContextRecord;
        fprintf(f, xorstr_("Registers:\n"));
        fprintf(f, xorstr_("  RAX = 0x%016llX  RBX = 0x%016llX\n"), ctx->Rax, ctx->Rbx);
        fprintf(f, xorstr_("  RCX = 0x%016llX  RDX = 0x%016llX\n"), ctx->Rcx, ctx->Rdx);
        fprintf(f, xorstr_("  RSI = 0x%016llX  RDI = 0x%016llX\n"), ctx->Rsi, ctx->Rdi);
        fprintf(f, xorstr_("  RBP = 0x%016llX  RSP = 0x%016llX\n"), ctx->Rbp, ctx->Rsp);
        fprintf(f, xorstr_("  R8  = 0x%016llX  R9  = 0x%016llX\n"), ctx->R8, ctx->R9);
        fprintf(f, xorstr_("  R10 = 0x%016llX  R11 = 0x%016llX\n"), ctx->R10, ctx->R11);
        fprintf(f, xorstr_("  R12 = 0x%016llX  R13 = 0x%016llX\n"), ctx->R12, ctx->R13);
        fprintf(f, xorstr_("  R14 = 0x%016llX  R15 = 0x%016llX\n"), ctx->R14, ctx->R15);
        fprintf(f, xorstr_("  RIP = 0x%016llX\n\n"), ctx->Rip);

        fprintf(f, xorstr_("Stack (top 256 bytes):\n"));
        uint8_t stack[256];
        SIZE_T copied = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (void*)ctx->Rsp, stack, sizeof(stack), &copied)) {
            for (SIZE_T i = 0; i < copied; i += 16) {
                fprintf(f, xorstr_("  %016llX: "), ctx->Rsp + i);
                for (SIZE_T j = 0; j < 16 && (i + j) < copied; j++)
                    fprintf(f, xorstr_("%02X "), stack[i + j]);
                fprintf(f, "\n");
            }
        }
        fprintf(f, xorstr_("\nGame State:\n"));
        fprintf(f, xorstr_("  Base: 0x%llX  GameTime: %.1f\n"), (uint64_t)g_base, g_gameTime);
        fprintf(f, xorstr_("  Heroes: %d  Minions: %d  Turrets: %d\n"),
            (int)g_heroes.size(), (int)g_minions.size(), (int)g_turrets.size());

        fclose(f);
        VX_LOG("\n[CRASH] Log saved to %s\n", path);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static bool WorldToScreen(const Vec3& world, Vec2& screen)
{
    const auto& m = g_viewProjMatrix.m;
    float w = m[0][3] * world.x + m[1][3] * world.y + m[2][3] * world.z + m[3][3];
    if (w < 0.001f) return false;

    float x = m[0][0] * world.x + m[1][0] * world.y + m[2][0] * world.z + m[3][0];
    float y = m[0][1] * world.x + m[1][1] * world.y + m[2][1] * world.z + m[3][1];

    float invW = 1.0f / w;
    screen.x = (g_screenW * 0.5f) + (g_screenW * 0.5f) * x * invW;
    screen.y = (g_screenH * 0.5f) - (g_screenH * 0.5f) * y * invW;
    return true;
}

static float WorldRadiusToScreen(const Vec3& center, float worldRadius)
{
    Vec2 cp, ep;
    if (!WorldToScreen(center, cp)) return 0.f;
    Vec3 edge = { center.x + worldRadius, center.y, center.z };
    if (!WorldToScreen(edge, ep)) return 0.f;
    float dx = ep.x - cp.x, dy = ep.y - cp.y;
    return sqrtf(dx * dx + dy * dy);
}

static float ScreenScale(const Vec3& worldPos)
{
    float sr = WorldRadiusToScreen(worldPos, 100.f);
    return std::clamp(sr / 30.f, 0.3f, 3.f);
}

static uintptr_t g_scannedViewPort = 0;
static uintptr_t g_scannedVPMatrix = 0;
static bool g_viewPortScanned = false;
static bool g_globalsScanned  = false;

// ── Auto-offset sig scanner ──────────────────────────────────────────
// Scans League's binary over DMA for byte patterns that reference global
// variables via RIP-relative addressing.
//
// Each pattern is a full byte sequence with 0xFF as wildcard.
// The disp32 always starts at byte index `dispOff` in the pattern.
// RVA = (offset_in_module + dispOff + 4) + disp32
//
// Falls back to hardcoded defaults if a pattern is not found.

struct SigEntry {
    const char*   name;
    uintptr_t*    target;       // pointer to Offsets::Globals::xxx
    uintptr_t     fallback;     // hardcoded default
    const uint8_t* pattern;     // full byte pattern (0xFF = wildcard)
    uint8_t       patLen;       // total pattern length
    uint8_t       dispOff;      // byte index where the 4-byte disp32 starts
};

static void SigScanGlobals()
{
    g_globalsScanned = true;

    // ── v3 Patterns — dual-pattern multi-match with .data-range filtering ──
    // 0xFF bytes are wildcards. dispOff marks where the disp32 starts.
    // Each global has up to 2 patterns (from IDA plugin + LOLDumper).
    // Short patterns + multi-match collector + .data range filter = accuracy.
    // Different tools use different instruction sites for the same global,
    // so having 2 patterns per global doubles our chances across patches.

    // ── GameTime ──
    // Pattern A (IDA plugin): F3 0F 11 3D ?? ?? ?? ?? 48 8B 49
    // movss [rip+disp32],xmm7 ; mov rcx,[rcx+...]
    static const uint8_t pat_gt_a[] = {
        0xF3,0x0F,0x11,0x3D, 0xFF,0xFF,0xFF,0xFF, 0x48,0x8B,0x49
    };
    // Pattern B (LOLDumper): F3 0F 5C 35 ?? ?? ?? ?? 0F 28 F8
    // subss xmm6,[rip+disp32] ; movaps xmm7,xmm0
    static const uint8_t pat_gt_b[] = {
        0xF3,0x0F,0x5C,0x35, 0xFF,0xFF,0xFF,0xFF, 0x0F,0x28,0xF8
    };

    // ── LocalPlayer ──
    // Pattern A (IDA plugin): 48 8B 05 ?? ?? ?? ?? F3 0F 10 40
    // mov rax,[rip+disp32] ; movss xmm0,[rax+?]
    static const uint8_t pat_lp_a[] = {
        0x48,0x8B,0x05, 0xFF,0xFF,0xFF,0xFF, 0xF3,0x0F,0x10,0x40
    };
    // Pattern B (LOLDumper): 48 8B 0D ?? ?? ?? ?? 4C ?? ?? 74 ?? 49
    // mov rcx,[rip+disp32] ; ??? ; jz short ; ???
    static const uint8_t pat_lp_b[] = {
        0x48,0x8B,0x0D, 0xFF,0xFF,0xFF,0xFF, 0x4C, 0xFF,0xFF, 0x74, 0xFF, 0x49
    };

    // ── HeroManager ──
    // Pattern A (IDA plugin): 48 8B 05 ?? ?? ?? ?? 48 8B 78 ?? 8B 40 ?? 48 8D 2C C7
    // 18 bytes, very unique
    static const uint8_t pat_hm_a[] = {
        0x48,0x8B,0x05, 0xFF,0xFF,0xFF,0xFF,
        0x48,0x8B,0x78, 0xFF, 0x8B,0x40, 0xFF,
        0x48,0x8D,0x2C,0xC7
    };

    // ── MinionManager ──
    // Pattern A (IDA plugin): 48 8B 0D ?? ?? ?? ?? 8B 54 90
    // mov rcx,[rip+disp32] ; mov edx,[rax+rdx*4]
    static const uint8_t pat_mm_a[] = {
        0x48,0x8B,0x0D, 0xFF,0xFF,0xFF,0xFF, 0x8B,0x54,0x90
    };
    // Pattern B (LOLDumper): 48 8B 05 ?? ?? ?? ?? F3 ?? ?? ?? ?? 45
    // mov rax,[rip+disp32] ; (movss/comiss) ; (REX.WRB prefix)
    static const uint8_t pat_mm_b[] = {
        0x48,0x8B,0x05, 0xFF,0xFF,0xFF,0xFF, 0xF3, 0xFF,0xFF,0xFF,0xFF, 0x45
    };

    // ── TurretManager ──
    // Pattern A (IDA plugin): 4C 8B 35 ?? ?? ?? ?? 49 8B 56
    // mov r14,[rip+disp32] ; mov rdx,[r14+...]
    static const uint8_t pat_tm_a[] = {
        0x4C,0x8B,0x35, 0xFF,0xFF,0xFF,0xFF, 0x49,0x8B,0x56
    };

    // ── ObjectManager ──
    // Pattern A (IDA plugin): 48 8B 05 ?? ?? ?? ?? 45 8B F0
    // mov rax,[rip+disp32] ; mov r14d,r8d
    static const uint8_t pat_om_a[] = {
        0x48,0x8B,0x05, 0xFF,0xFF,0xFF,0xFF, 0x45,0x8B,0xF0
    };
    // Pattern B (LOLDumper): 48 8B 0D ?? ?? ?? ?? 8B 10 E8
    // mov rcx,[rip+disp32] ; mov edx,[rax] ; call
    static const uint8_t pat_om_b[] = {
        0x48,0x8B,0x0D, 0xFF,0xFF,0xFF,0xFF, 0x8B,0x10,0xE8
    };

    // ── HudInstance ──
    // Pattern A (IDA plugin): 48 8B 0D ?? ?? ?? ?? C7 85
    // mov rcx,[rip+disp32] ; mov dword [rbp+?],imm32
    static const uint8_t pat_hi_a[] = {
        0x48,0x8B,0x0D, 0xFF,0xFF,0xFF,0xFF, 0xC7,0x85
    };
    // Pattern B (LOLDumper): 48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 49 ?? 48 8D
    // mov rcx,[rip+disp32] ; test rcx,rcx ; jz ; mov rcx,[rcx+?] ; lea
    static const uint8_t pat_hi_b[] = {
        0x48,0x8B,0x0D, 0xFF,0xFF,0xFF,0xFF,
        0x48,0x85,0xC9, 0x74, 0xFF, 0x48,0x8B,0x49, 0xFF, 0x48,0x8D
    };

    // ── NetClient ──
    // Pattern A (IDA plugin): 48 8B 0D ?? ?? ?? ?? 4D 89 7B
    // mov rcx,[rip+disp32] ; mov [r11+?],r15
    static const uint8_t pat_nc_a[] = {
        0x48,0x8B,0x0D, 0xFF,0xFF,0xFF,0xFF, 0x4D,0x89,0x7B
    };

    // ── UnderMouseObj ──
    // Pattern A (IDA plugin): 48 8B 3D ?? ?? ?? ?? 48 8B 47
    // mov rdi,[rip+disp32] ; mov rax,[rdi+?]
    static const uint8_t pat_um_a[] = {
        0x48,0x8B,0x3D, 0xFF,0xFF,0xFF,0xFF, 0x48,0x8B,0x47
    };
    // Pattern B (LOLDumper): 48 89 0D ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 01 33 D2
    // mov [rip+disp32],rcx ; lea rax,[rip+?] ; mov [rcx],rax ; xor edx,edx
    static const uint8_t pat_um_b[] = {
        0x48,0x89,0x0D, 0xFF,0xFF,0xFF,0xFF,
        0x48,0x8D,0x05, 0xFF,0xFF,0xFF,0xFF,
        0x48,0x89,0x01, 0x33,0xD2
    };

    // ── MissileManager ──
    // Pattern A (IDA plugin): 48 89 0D ?? ?? ?? ?? 83 78
    // mov [rip+disp32],rcx ; cmp dword [rax+?]
    static const uint8_t pat_msm_a[] = {
        0x48,0x89,0x0D, 0xFF,0xFF,0xFF,0xFF, 0x83,0x78
    };

    // ── ChatManager ──
    // Pattern A (IDA plugin): 48 8B 35 ?? ?? ?? ?? 48 83 7E
    // mov rsi,[rip+disp32] ; cmp qword [rsi+?]
    static const uint8_t pat_cm_a[] = {
        0x48,0x8B,0x35, 0xFF,0xFF,0xFF,0xFF, 0x48,0x83,0x7E
    };

    // ── NavGrid ──
    // Pattern A (IDA plugin): 48 8B 35 ?? ?? ?? ?? 48 8B 56
    // mov rsi,[rip+disp32] ; mov rdx,[rsi+?]
    static const uint8_t pat_ng_a[] = {
        0x48,0x8B,0x35, 0xFF,0xFF,0xFF,0xFF, 0x48,0x8B,0x56
    };
    // Pattern B (LOLDumper): 48 8B 05 ?? ?? ?? ?? 0F 28 DA
    // mov rax,[rip+disp32] ; movaps xmm3,xmm2
    static const uint8_t pat_ng_b[] = {
        0x48,0x8B,0x05, 0xFF,0xFF,0xFF,0xFF, 0x0F,0x28,0xDA
    };

    // ── ViewPort ──
    // Pattern A: 4C 8B 3D ?? ?? ?? ?? 48 03 ?? 49
    // mov r15,[rip+disp32] ; add ?,? ; ???
    static const uint8_t pat_vp_a[] = {
        0x4C,0x8B,0x3D, 0xFF,0xFF,0xFF,0xFF, 0x48,0x03, 0xFF, 0x49
    };
    // Pattern B: 48 8B 15 ?? ?? ?? ?? 85 DB 78
    // mov rdx,[rip+disp32] ; test ebx,ebx ; js
    static const uint8_t pat_vp_b[] = {
        0x48,0x8B,0x15, 0xFF,0xFF,0xFF,0xFF, 0x85,0xDB,0x78
    };

    // ViewProjMatrices: 48 8D 0D ?? ?? ?? ?? 0F 10 00
    // lea rcx,[rip+disp32] ; movups xmm0,[rax]
    static const uint8_t pat_vpm[] = {
        0x48,0x8D,0x0D, 0xFF,0xFF,0xFF,0xFF, 0x0F,0x10,0x00
    };

    // Multiple patterns per global — scan all, collect candidates from both
    SigEntry entries[] = {
        // GameTime — 2 patterns
        { "GameTime",       &Offsets::Globals::GameTime,         0x1D08560, pat_gt_a,  sizeof(pat_gt_a),  4 },
        { "GameTime",       &Offsets::Globals::GameTime,         0x1D08560, pat_gt_b,  sizeof(pat_gt_b),  4 },
        // LocalPlayer — 2 patterns
        { "LocalPlayer",    &Offsets::Globals::LocalPlayer,      0x1D3CF30, pat_lp_a,  sizeof(pat_lp_a),  3 },
        { "LocalPlayer",    &Offsets::Globals::LocalPlayer,      0x1D3CF30, pat_lp_b,  sizeof(pat_lp_b),  3 },
        // HeroManager — 1 pattern (already unique)
        { "HeroManager",    &Offsets::Globals::HeroManager,      0x1CFAA70, pat_hm_a,  sizeof(pat_hm_a),  3 },
        // MinionManager — 2 patterns
        { "MinionManager",  &Offsets::Globals::MinionManager,    0x1CFE0B8, pat_mm_a,  sizeof(pat_mm_a),  3 },
        { "MinionManager",  &Offsets::Globals::MinionManager,    0x1CFE0B8, pat_mm_b,  sizeof(pat_mm_b),  3 },
        // TurretManager — 1 pattern
        { "TurretManager",  &Offsets::Globals::TurretManager,    0x1D04798, pat_tm_a,  sizeof(pat_tm_a),  3 },
        // ObjectManager — 2 patterns
        { "ObjectManager",  &Offsets::Globals::ObjectManager,    0x1CFA920, pat_om_a,  sizeof(pat_om_a),  3 },
        { "ObjectManager",  &Offsets::Globals::ObjectManager,    0x1CFA920, pat_om_b,  sizeof(pat_om_b),  3 },
        // HudInstance — 2 patterns
        { "HudInstance",    &Offsets::Globals::HudInstance,      0x1CFAAE0, pat_hi_a,  sizeof(pat_hi_a),  3 },
        { "HudInstance",    &Offsets::Globals::HudInstance,      0x1CFAAE0, pat_hi_b,  sizeof(pat_hi_b),  3 },
        // NetClient — 1 pattern
        { "NetClient",      &Offsets::Globals::NetClient,        0x1CFA940, pat_nc_a,  sizeof(pat_nc_a),  3 },
        // UnderMouseObj — 2 patterns
        { "UnderMouseObj",  &Offsets::Globals::UnderMouseObj,    0x1CBBC98, pat_um_a,  sizeof(pat_um_a),  3 },
        { "UnderMouseObj",  &Offsets::Globals::UnderMouseObj,    0x1CBBC98, pat_um_b,  sizeof(pat_um_b),  3 },
        // MissileManager — 1 pattern
        { "MissileManager", &Offsets::Globals::MissileManager,   0x1CFE0D0, pat_msm_a, sizeof(pat_msm_a), 3 },
        // ChatManager — 1 pattern
        { "ChatManager",    &Offsets::Globals::ChatManager,      0x1CCDED0, pat_cm_a,  sizeof(pat_cm_a),  3 },
        // NavGrid — 2 patterns
        { "NavGrid",        &Offsets::Globals::NavGrid,          0x1CBB9D0, pat_ng_a,  sizeof(pat_ng_a),  3 },
        { "NavGrid",        &Offsets::Globals::NavGrid,          0x1CBB9D0, pat_ng_b,  sizeof(pat_ng_b),  3 },
        // ViewPort — 2 patterns
        { "ViewPort",       &Offsets::Globals::ViewPort,         0x1CBBA50, pat_vp_a,  sizeof(pat_vp_a),  3 },
        { "ViewPort",       &Offsets::Globals::ViewPort,         0x1CBBA50, pat_vp_b,  sizeof(pat_vp_b),  3 },
    };
    constexpr int NUM_ENTRIES = sizeof(entries) / sizeof(entries[0]);

    // Build a list of unique globals (entries may have multiple patterns per global)
    struct GlobalInfo {
        const char* name;
        uintptr_t*  target;
        uintptr_t   fallback;
    };
    GlobalInfo globals[NUM_ENTRIES];
    int numGlobals = 0;
    for (int e = 0; e < NUM_ENTRIES; e++) {
        bool dup = false;
        for (int g = 0; g < numGlobals; g++) {
            if (globals[g].target == entries[e].target) { dup = true; break; }
        }
        if (!dup) {
            globals[numGlobals] = { entries[e].name, entries[e].target, entries[e].fallback };
            numGlobals++;
        }
    }

    // ── Multi-match collection ──
    struct Candidate { int64_t rva; uint64_t instrOff; int patIdx; };
    static constexpr int MAX_CANDS = 16;
    Candidate candidates[64][MAX_CANDS] = {};  // indexed by global idx
    int candCount[64] = {};

    uint64_t moduleSize = 0x3000000;
    PVMMDLL_MAP_MODULEENTRY mod = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, TargetProcess.current_process.PID,
        (LPSTR)xorstr_("League of Legends.exe"), &mod, VMMDLL_MODULE_FLAG_NORMAL)) {
        moduleSize = mod->cbImageSize;
        VMMDLL_MemFree(mod);
    }
    VX_LOG("[SigScan] v3 Globals: scanning 0x%llX bytes for %d patterns (%d globals)...\n",
        (uint64_t)moduleSize, NUM_ENTRIES, numGlobals);

    // .data section range — globals live here (RVA ~0x1C84000 to ~0x1DA2000)
    constexpr int64_t DATA_LO = 0x1B00000;
    constexpr int64_t DATA_HI = 0x1F00000;

    constexpr uint64_t CHUNK = 0x10000;
    constexpr uint32_t PAD   = 32;
    std::vector<uint8_t> buf(CHUNK + PAD);

    for (uint64_t off = 0; off < moduleSize; off += CHUNK) {
        DWORD cb = 0;
        DWORD toRead = (DWORD)(std::min)((uint64_t)(CHUNK + PAD), moduleSize - off);
        if (!VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
            g_base + off, buf.data(), toRead, &cb, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
            continue;
        if (cb < 10) continue;

        for (DWORD i = 0; i < cb; i++) {
            for (int e = 0; e < NUM_ENTRIES; e++) {
                const SigEntry& s = entries[e];
                if (i + s.patLen > cb) continue;

                // Find which global this entry belongs to
                int gIdx = -1;
                for (int g = 0; g < numGlobals; g++) {
                    if (globals[g].target == s.target) { gIdx = g; break; }
                }
                if (gIdx < 0 || candCount[gIdx] >= MAX_CANDS) continue;

                bool match = true;
                for (uint8_t p = 0; p < s.patLen && match; p++) {
                    if (s.pattern[p] == 0xFF) continue;
                    if (buf[i + p] != s.pattern[p]) match = false;
                }
                if (!match) continue;

                int32_t disp;
                memcpy(&disp, &buf[i + s.dispOff], 4);
                int64_t rva = (int64_t)(off + i + s.dispOff + 4) + (int64_t)disp;

                if (rva < 0x100000 || rva > (int64_t)moduleSize + 0x1000000) continue;

                auto& c = candidates[gIdx][candCount[gIdx]];
                c.rva = rva;
                c.instrOff = off + i;
                c.patIdx = e;
                candCount[gIdx]++;
                VX_LOG("[SigScan]   %s cand[%d] pat#%d: RVA=0x%llX (@ 0x%llX)\n",
                    s.name, candCount[gIdx]-1, e, (uint64_t)rva, (uint64_t)(off + i));
            }
        }
    }

    // ── Selection: prefer candidates in .data range, then most common RVA ──
    bool found[64] = {};
    int foundCount = 0;

    for (int g = 0; g < numGlobals; g++) {
        if (candCount[g] == 0) continue;

        // Count how many candidates land in .data for each unique RVA
        int64_t bestRva = -1;
        int bestScore = -1;
        for (int c = 0; c < candCount[g]; c++) {
            int64_t rva = candidates[g][c].rva;
            bool inData = (rva >= DATA_LO && rva <= DATA_HI);
            // Count how many other candidates agree on this RVA
            int agreement = 0;
            for (int c2 = 0; c2 < candCount[g]; c2++) {
                if (candidates[g][c2].rva == rva) agreement++;
            }
            int score = agreement + (inData ? 100 : 0);
            if (score > bestScore) {
                bestScore = score;
                bestRva = rva;
            }
        }

        if (bestRva >= 0) {
            *globals[g].target = (uintptr_t)bestRva;
            found[g] = true;
            foundCount++;
            VX_LOG("[SigScan] %s = 0x%llX (%d candidates, score %d%s)\n",
                globals[g].name, (uint64_t)bestRva, candCount[g], bestScore,
                (bestRva >= DATA_LO && bestRva <= DATA_HI) ? " [.data]" : " [!data]");
        }
    }

    // ── Duplicate detection (different globals pointing to same RVA) ──
    // When two globals collide, prefer the one whose FALLBACK matches the RVA
    // (it was correct in the old build and likely still is)
    for (int a = 0; a < numGlobals; a++) {
        if (!found[a]) continue;
        for (int b = a + 1; b < numGlobals; b++) {
            if (!found[b]) continue;
            if (*globals[a].target == *globals[b].target) {
                uintptr_t collisionRva = *globals[a].target;
                VX_LOG("[SigScan] WARNING: %s and %s both resolved to 0x%llX!\n",
                    globals[a].name, globals[b].name, (uint64_t)collisionRva);

                bool aMatchesFallback = (globals[a].fallback == collisionRva);
                bool bMatchesFallback = (globals[b].fallback == collisionRva);

                if (aMatchesFallback && !bMatchesFallback) {
                    // A's fallback matches — A is probably correct, revert B
                    *globals[b].target = globals[b].fallback;
                    found[b] = false; foundCount--;
                    VX_LOG("[SigScan]   -> %s fallback matches, %s reverted\n", globals[a].name, globals[b].name);
                } else if (bMatchesFallback && !aMatchesFallback) {
                    *globals[a].target = globals[a].fallback;
                    found[a] = false; foundCount--;
                    VX_LOG("[SigScan]   -> %s fallback matches, %s reverted\n", globals[b].name, globals[a].name);
                } else {
                    // Neither or both match fallback — revert the one with fewer candidates
                    if (candCount[a] >= candCount[b]) {
                        *globals[b].target = globals[b].fallback;
                        found[b] = false; foundCount--;
                        VX_LOG("[SigScan]   -> %s reverted (fewer candidates)\n", globals[b].name);
                    } else {
                        *globals[a].target = globals[a].fallback;
                        found[a] = false; foundCount--;
                        VX_LOG("[SigScan]   -> %s reverted (fewer candidates)\n", globals[a].name);
                    }
                }
            }
        }
    }

    // ── Generic fallback scan for unfound globals ──
    // For any global not yet found, brute-force scan for ALL mov reg,[rip+disp32]
    // variants and pick the one whose resolved RVA is closest to the fallback
    // (offsets shift slightly between patches but stay in the neighborhood)
    {
        int unfoundCount = 0;
        for (int g = 0; g < numGlobals; g++) {
            if (!found[g]) unfoundCount++;
        }
        if (unfoundCount > 0) {
            VX_LOG("[SigScan] Generic fallback scan for %d unfound globals...\n", unfoundCount);

            // All x86-64 opcodes for mov/lea reg,[rip+disp32]:
            // 48 8B 05/0D/15/1D/25/2D/35/3D = mov rax/rcx/rdx/rbx/rsp/rbp/rsi/rdi
            // 4C 8B 05/0D/15/1D/25/2D/35/3D = mov r8-r15
            // 48 8D 05/0D/15/1D/25/2D/35/3D = lea (same regs)
            // 48 89 05/0D/15/1D/25/2D/35/3D = mov [rip+disp32],reg (store)
            static const uint8_t prefixes[][3] = {
                {0x48,0x8B,0x05}, {0x48,0x8B,0x0D}, {0x48,0x8B,0x15}, {0x48,0x8B,0x1D},
                {0x48,0x8B,0x25}, {0x48,0x8B,0x2D}, {0x48,0x8B,0x35}, {0x48,0x8B,0x3D},
                {0x4C,0x8B,0x05}, {0x4C,0x8B,0x0D}, {0x4C,0x8B,0x15}, {0x4C,0x8B,0x1D},
                {0x4C,0x8B,0x25}, {0x4C,0x8B,0x2D}, {0x4C,0x8B,0x35}, {0x4C,0x8B,0x3D},
                {0x48,0x8D,0x05}, {0x48,0x8D,0x0D}, {0x48,0x8D,0x15}, {0x48,0x8D,0x1D},
                {0x48,0x89,0x05}, {0x48,0x89,0x0D}, {0x48,0x89,0x15}, {0x48,0x89,0x35},
                {0xF3,0x0F,0x11}, {0xF3,0x0F,0x5C},  // movss/subss [rip+disp]
            };
            constexpr int NUM_PREFIXES = sizeof(prefixes) / sizeof(prefixes[0]);
            constexpr int64_t PROXIMITY = 0x20000; // ±128KB from fallback

            // For each unfound global, collect nearby RVA hits
            struct GenCand { int64_t rva; int64_t dist; };
            static constexpr int MAX_GEN = 32;
            GenCand genCands[64][MAX_GEN] = {};
            int genCount[64] = {};

            std::vector<uint8_t> gbuf(CHUNK + PAD);
            for (uint64_t off = 0; off < moduleSize; off += CHUNK) {
                DWORD cb = 0;
                DWORD toRead = (DWORD)(std::min)((uint64_t)(CHUNK + PAD), moduleSize - off);
                if (!VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
                    g_base + off, gbuf.data(), toRead, &cb, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
                    continue;
                if (cb < 7) continue;

                for (DWORD i = 0; i + 7 <= cb; i++) {
                    // Check if this looks like any mov/lea reg,[rip+disp32]
                    bool isPrefixMatch = false;
                    int dispOffset = 3; // default: disp32 starts at byte 3
                    for (int p = 0; p < NUM_PREFIXES; p++) {
                        if (gbuf[i] == prefixes[p][0] && gbuf[i+1] == prefixes[p][1] && gbuf[i+2] == prefixes[p][2]) {
                            isPrefixMatch = true;
                            // For F3 0F xx, the ModRM byte follows at [3], disp at [4]
                            if (prefixes[p][0] == 0xF3) dispOffset = 4;
                            break;
                        }
                    }
                    if (!isPrefixMatch) continue;
                    if (i + dispOffset + 4 > cb) continue;

                    int32_t disp;
                    memcpy(&disp, &gbuf[i + dispOffset], 4);
                    int64_t rva = (int64_t)(off + i + dispOffset + 4) + (int64_t)disp;

                    if (rva < DATA_LO || rva > DATA_HI) continue;

                    // Check proximity to each unfound global's fallback
                    for (int g = 0; g < numGlobals; g++) {
                        if (found[g]) continue;
                        if (genCount[g] >= MAX_GEN) continue;
                        int64_t dist = rva - (int64_t)globals[g].fallback;
                        if (dist < 0) dist = -dist;
                        if (dist <= PROXIMITY) {
                            genCands[g][genCount[g]] = { rva, dist };
                            genCount[g]++;
                        }
                    }
                }
            }

            // For each unfound global, pick the RVA closest to fallback that validates
            for (int g = 0; g < numGlobals; g++) {
                if (found[g] || genCount[g] == 0) continue;

                VX_LOG("[SigScan] Generic: %s has %d candidates near fallback 0x%llX\n",
                    globals[g].name, genCount[g], (uint64_t)globals[g].fallback);

                // Sort by distance (simple selection sort, small N)
                for (int a = 0; a < genCount[g] - 1; a++) {
                    for (int b = a + 1; b < genCount[g]; b++) {
                        if (genCands[g][b].dist < genCands[g][a].dist) {
                            auto tmp = genCands[g][a];
                            genCands[g][a] = genCands[g][b];
                            genCands[g][b] = tmp;
                        }
                    }
                }

                // Try each candidate, closest first, validate by reading the pointer
                for (int c = 0; c < genCount[g]; c++) {
                    int64_t rva = genCands[g][c].rva;

                    // Skip RVAs already claimed by another global
                    bool claimed = false;
                    for (int g2 = 0; g2 < numGlobals; g2++) {
                        if (found[g2] && *globals[g2].target == (uintptr_t)rva) { claimed = true; break; }
                    }
                    if (claimed) continue;

                    // Validate — real managers are heap objects, NOT module-internal pointers
                    bool valid = false;
                    if (globals[g].target == &Offsets::Globals::GameTime) {
                        float gt = TargetProcess.Read<float>(g_base + (uintptr_t)rva);
                        valid = (gt >= 0.f && gt < 99999.f);
                    } else {
                        uintptr_t ptr = TargetProcess.Read<uintptr_t>(g_base + (uintptr_t)rva);
                        bool inRange = (ptr > 0x10000 && ptr < 0x7FFFFFFFFFFF);
                        bool inModule = (ptr >= g_base && ptr < g_base + moduleSize);
                        // Real heap pointers on 64-bit Windows are typically > 0x100000000
                        // Values like 0x4F5645 are ASCII junk, not real heap allocations
                        bool isRealHeap = (ptr > 0x100000000ULL);
                        valid = inRange && !inModule && isRealHeap;
                        // Extra validation for manager globals: check struct looks like a manager
                        // (has a list pointer at +0x8 and a count 1-500 at +0x10)
                        if (valid && (globals[g].target == &Offsets::Globals::HeroManager ||
                                      globals[g].target == &Offsets::Globals::MinionManager ||
                                      globals[g].target == &Offsets::Globals::TurretManager)) {
                            uintptr_t listPtr = TargetProcess.Read<uintptr_t>(ptr + 0x8);
                            int32_t count = TargetProcess.Read<int32_t>(ptr + 0x10);
                            if (listPtr < 0x100000000ULL || listPtr > 0x7FFFFFFFFFFF ||
                                count < 0 || count > 500) {
                                valid = false;
                            }
                        }
                    }

                    VX_LOG("[SigScan] Generic: %s try RVA=0x%llX (dist=%lld) ptr=0x%llX -> %s\n",
                        globals[g].name, (uint64_t)rva, (long long)genCands[g][c].dist,
                        (uint64_t)TargetProcess.Read<uintptr_t>(g_base + (uintptr_t)rva),
                        valid ? "VALID" : "invalid");

                    if (valid) {
                        *globals[g].target = (uintptr_t)rva;
                        found[g] = true;
                        foundCount++;
                        VX_LOG("[SigScan] Generic: %s = 0x%llX (proximity match)\n",
                            globals[g].name, (uint64_t)rva);
                        break;
                    }
                }
            }
        }
    }

    // ── Final validation pass ──
    int validated = 0;
    for (int g = 0; g < numGlobals; g++) {
        if (!found[g]) {
            VX_LOG("[SigScan] %s NOT FOUND — fallback 0x%llX\n", globals[g].name, (uint64_t)globals[g].fallback);
            *globals[g].target = globals[g].fallback;
            continue;
        }
        if (globals[g].target == &Offsets::Globals::GameTime) {
            float gt = TargetProcess.Read<float>(g_base + *globals[g].target);
            if (gt >= 0.f && gt < 99999.f) {
                validated++;
                VX_LOG("[SigScan] %s validated (%.1f)\n", globals[g].name, gt);
            } else {
                VX_LOG("[SigScan] %s FAILED validation (%.1f) — fallback\n", globals[g].name, gt);
                *globals[g].target = globals[g].fallback;
            }
        } else {
            uintptr_t ptr = TargetProcess.Read<uintptr_t>(g_base + *globals[g].target);
            bool inRange = (ptr > 0x10000 && ptr < 0x7FFFFFFFFFFF);
            bool inModule = (ptr >= g_base && ptr < g_base + moduleSize);
            if (inRange && !inModule) {
                validated++;
                VX_LOG("[SigScan] %s validated (ptr 0x%llX)\n", globals[g].name, (uint64_t)ptr);
            } else {
                VX_LOG("[SigScan] %s FAILED validation (ptr 0x%llX%s) — fallback\n",
                    globals[g].name, (uint64_t)ptr, inModule ? " MODULE-INTERNAL" : "");
                *globals[g].target = globals[g].fallback;
            }
        }
    }

    VX_LOG("[SigScan] v3 done: %d/%d found, %d/%d validated\n", foundCount, numGlobals, validated, foundCount);
}

// ── Post-SigScan manager fix ──
// When HeroManager/MinionManager/TurretManager pointers are invalid (ASCII junk,
// no real manager struct), scan the .data section broadly for real manager objects.
// Probe the LocalPlayer entity to find the correct Name offset
// Scans a range around the expected offset looking for a readable champion name
static void ProbeNameOffset()
{
    uintptr_t lpPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
    if (lpPtr < 0x100000000ULL || lpPtr > 0x7FFFFFFFFFFF) return;

    // Known champion name prefixes/patterns to validate against
    // We look for readable ASCII strings 3-20 chars at offsets around 0x3890
    struct NameProbe {
        uintptr_t offset;
        char name[33];
        int len;
    };

    std::vector<NameProbe> hits;
    // Scan from 0x3400 to 0x3C00 in steps of 8
    for (uintptr_t off = 0x3400; off <= 0x3C00; off += 0x8) {
        char buf[33] = {};
        TargetProcess.Read(lpPtr + off, buf, 32);
        buf[32] = 0;
        if (buf[0] == 0) continue;

        bool readable = true;
        int len = 0;
        for (int c = 0; c < 32 && buf[c]; c++) {
            if (buf[c] < 0x20 || buf[c] > 0x7E) { readable = false; break; }
            len++;
        }
        if (!readable || len < 3 || len > 20) continue;

        // Check if it looks like a champion name (starts with uppercase, contains only letters/digits)
        bool looksLikeName = (buf[0] >= 'A' && buf[0] <= 'Z');
        bool allAlnum = true;
        for (int c = 0; c < len; c++) {
            if (!((buf[c] >= 'A' && buf[c] <= 'Z') || (buf[c] >= 'a' && buf[c] <= 'z') ||
                  (buf[c] >= '0' && buf[c] <= '9') || buf[c] == '_' || buf[c] == ' ' || buf[c] == '\'')) {
                allAlnum = false; break;
            }
        }
        if (looksLikeName && allAlnum) {
            NameProbe np;
            np.offset = off;
            memcpy(np.name, buf, 33);
            np.len = len;
            hits.push_back(np);
        }
    }

    if (!hits.empty()) {
        VX_LOG("[NameProbe] Found %d name candidates on LocalPlayer (0x%llX):\n",
            (int)hits.size(), (uint64_t)lpPtr);
        for (auto& h : hits) {
            VX_LOG("[NameProbe]   offset=0x%llX name='%s' len=%d%s\n",
                (uint64_t)h.offset, h.name, h.len,
                h.offset == Offsets::GameObject::Name ? " (CURRENT)" : "");
        }
        // Pick the best candidate: prefer one close to 0x3890, with a name 4+ chars
        uintptr_t bestOff = hits[0].offset;
        int bestScore = 0;
        for (auto& h : hits) {
            int score = 0;
            if (h.len >= 4 && h.len <= 16) score += 100;
            // Prefer offsets near the known range
            int dist = (int)(h.offset > 0x3898 ? h.offset - 0x3898 : 0x3898 - h.offset);
            if (dist < 0x100) score += 50;
            if (dist == 0) score += 200;
            if (score > bestScore) { bestScore = score; bestOff = h.offset; }
        }
        if (bestOff != Offsets::GameObject::Name) {
            VX_LOG("[NameProbe] *** Name offset CHANGED: 0x%llX -> 0x%llX ***\n",
                (uint64_t)Offsets::GameObject::Name, (uint64_t)bestOff);
            Offsets::GameObject::Name = bestOff;
        } else {
            VX_LOG("[NameProbe] Name offset confirmed at 0x%llX\n", (uint64_t)bestOff);
        }
    } else {
        VX_LOG("[NameProbe] No name candidates found! Name offset may be very different.\n");
        // Dump a hex region around the expected offset for manual inspection
        VX_LOG("[NameProbe] Hex dump around LocalPlayer+0x3898:\n");
        for (uintptr_t doff = 0x3800; doff < 0x3900; doff += 0x20) {
            uint8_t dmp[0x20] = {};
            TargetProcess.Read(lpPtr + doff, dmp, 0x20);
            VX_LOG("[NameProbe]   +0x%04X: ", (unsigned)doff);
            for (int b = 0; b < 0x20; b++) {
                if (b == 0x10) VX_LOG(" ");
                VX_LOG("%02X ", dmp[b]);
            }
            // ASCII
            VX_LOG(" |");
            for (int b = 0; b < 0x20; b++) {
                VX_LOG("%c", (dmp[b] >= 0x20 && dmp[b] <= 0x7E) ? dmp[b] : '.');
            }
            VX_LOG("|\n");
        }
    }
}

// Probe the actual manager struct layout using ObjectManager (which we know is valid)
// Then scan the .data neighborhood around known globals for real managers
static void ProbeTeamOffset()
{
    uintptr_t localPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
    if (localPtr < 0x10000 || localPtr > 0x7FFFFFFFFFFF) return;

    uint8_t buf[0x80] = {};
    uintptr_t startOff = 0x140;
    TargetProcess.Read(localPtr + startOff, buf, sizeof(buf));

    VX_LOG("[TeamProbe] LocalPlayer=0x%llX scanning 0x%llX-0x%llX for team value...\n",
        (uint64_t)localPtr, (uint64_t)startOff, (uint64_t)(startOff + sizeof(buf)));
    for (int row = 0; row < 8; row++) {
        VX_LOG("[TeamProbe]   +0x%03X:", (int)(startOff + row * 0x10));
        for (int b = 0; b < 0x10; b++) VX_LOG(" %02X", buf[row*0x10+b]);
        VX_LOG("\n");
    }

    uintptr_t bestOff = 0;
    int bestVal = 0;
    // First pass: look for 100 or 200 as int32_t (Riot's standard team IDs)
    for (size_t i = 0; i + 4 <= sizeof(buf); i += 4) {
        int32_t val = *(int32_t*)(buf + i);
        if (val == 100 || val == 200) {
            bestOff = startOff + i;
            bestVal = val;
            VX_LOG("[TeamProbe] Found team=%d (int32) at offset 0x%llX\n", val, (uint64_t)bestOff);
            break;
        }
    }
    // Fallback: look for 1 or 2 as uint8 near original offset
    if (!bestOff) {
        for (size_t i = 0; i < sizeof(buf); i++) {
            if (buf[i] == 1 || buf[i] == 2) {
                uintptr_t off = startOff + i;
                if (off >= 0x150 && off <= 0x180) {
                    bestOff = off;
                    bestVal = buf[i];
                    VX_LOG("[TeamProbe] Found team=%d (uint8) at offset 0x%llX\n", bestVal, (uint64_t)bestOff);
                    break;
                }
            }
        }
    }

    if (bestOff && bestOff != Offsets::GameObject::Team) {
        VX_LOG("[TeamProbe] Team offset SHIFTED: 0x%llX -> 0x%llX\n",
            (uint64_t)Offsets::GameObject::Team, (uint64_t)bestOff);
        Offsets::GameObject::Team = bestOff;
    } else if (!bestOff) {
        VX_LOG("[TeamProbe] WARNING: No team value found!\n");
    } else {
        VX_LOG("[TeamProbe] Team offset OK at 0x%llX (value=%d)\n",
            (uint64_t)bestOff, bestVal);
    }
}

static void ProbeManagerLayout()
{
    uint64_t moduleSize = 0x3000000;
    PVMMDLL_MAP_MODULEENTRY mod = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, TargetProcess.current_process.PID,
        (LPSTR)"League of Legends.exe", &mod, VMMDLL_MODULE_FLAG_NORMAL)) {
        moduleSize = mod->cbImageSize;
        VMMDLL_MemFree(mod);
    }

    // Dump ObjectManager struct to find the real list/count offsets
    uintptr_t omPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::ObjectManager);
    if (omPtr > 0x100000000ULL && omPtr < 0x7FFFFFFFFFFF) {
        uint8_t omDump[0x40] = {};
        TargetProcess.Read(omPtr, omDump, sizeof(omDump));
        VX_LOG("[MgrProbe] ObjectManager ptr=0x%llX dump:\n", (uint64_t)omPtr);
        for (int row = 0; row < 4; row++) {
            VX_LOG("[MgrProbe]   +0x%02X:", row * 0x10);
            for (int b = 0; b < 0x10; b++) VX_LOG(" %02X", omDump[row*0x10+b]);
            VX_LOG("  |");
            for (int b = 0; b < 0x10; b++) {
                uint8_t c = omDump[row*0x10+b];
                VX_LOG("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
            }
            VX_LOG("|\n");
        }

        // Try to find list/count at various offsets
        for (int listOff = 0x0; listOff <= 0x30; listOff += 0x8) {
            uintptr_t lp = *(uintptr_t*)(omDump + listOff);
            if (lp < 0x100000000ULL || lp > 0x7FFFFFFFFFFF) continue;
            if (lp >= g_base && lp < g_base + moduleSize) continue;
            // Try count at listOff+4, listOff+8
            for (int cntRel = 4; cntRel <= 8; cntRel += 4) {
                if (listOff + cntRel + 4 > 0x40) continue;
                int32_t cnt = *(int32_t*)(omDump + listOff + cntRel);
                if (cnt >= 1 && cnt <= 500) {
                    // Try reading first entity from list
                    uintptr_t fe = TargetProcess.Read<uintptr_t>(lp);
                    if (fe > 0x100000000ULL && fe < 0x7FFFFFFFFFFF) {
                        uintptr_t vt = TargetProcess.Read<uintptr_t>(fe);
                        bool vtInMod = (vt >= g_base && vt < g_base + moduleSize);
                        char en[33] = {};
                        TargetProcess.Read(fe + Offsets::GameObject::Name, en, 32);
                        en[32] = 0;
                        VX_LOG("[MgrProbe]   list=+0x%X(0x%llX) count=+0x%X(%d) ent=0x%llX vt=%s name='%.16s'\n",
                            listOff, (uint64_t)lp, listOff+cntRel, cnt,
                            (uint64_t)fe, vtInMod ? "MOD" : "EXT", en);
                    }
                }
            }
        }
    }

    // Now scan the .data neighborhood around known globals
    // Known good RVAs: ObjectManager=0x1CFA920, NetClient=0x1CFA940, HeroManager=0x1CFAA70, HudInstance=0x1CFAAE0
    // Scan from -0x1000 to +0x1000 around ObjectManager's RVA
    uintptr_t centerRVA = Offsets::Globals::ObjectManager;
    uintptr_t scanFrom = (centerRVA > 0x1000) ? centerRVA - 0x1000 : 0;
    uintptr_t scanTo = centerRVA + 0x2000;
    if (scanTo > moduleSize) scanTo = moduleSize;

    VX_LOG("[MgrProbe] Scanning .data neighborhood RVA 0x%llX-0x%llX for manager-like ptrs...\n",
        (uint64_t)scanFrom, (uint64_t)scanTo);

    size_t toRead = (size_t)(scanTo - scanFrom);
    std::vector<uint8_t> nbuf(toRead, 0);
    DWORD cb = 0;
    if (VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
        g_base + scanFrom, nbuf.data(), (DWORD)toRead, &cb,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL) && cb > 0)
    {
        int found = 0;
        for (size_t i = 0; i + 8 <= cb; i += 8) {
            uintptr_t val = *(uintptr_t*)(nbuf.data() + i);
            uintptr_t rva = scanFrom + i;

            // Must be a valid heap pointer
            if (val < 0x100000000ULL || val > 0x7FFFFFFFFFFF) continue;
            if (val >= g_base && val < g_base + moduleSize) continue;

            // Skip known globals
            if (rva == Offsets::Globals::NetClient || rva == Offsets::Globals::ObjectManager ||
                rva == Offsets::Globals::HudInstance || rva == Offsets::Globals::LocalPlayer ||
                rva == Offsets::Globals::GameTime || rva == Offsets::Globals::UnderMouseObj ||
                rva == Offsets::Globals::MissileManager || rva == Offsets::Globals::NavGrid)
                continue;

            // Read 0x40 bytes at the target and try to find entity lists
            uint8_t objDump[0x40] = {};
            TargetProcess.Read(val, objDump, sizeof(objDump));

            // Try every combo of list offset (0x0-0x30) and count offset (list+4 or list+8)
            for (int lo = 0x0; lo <= 0x30; lo += 0x8) {
                uintptr_t lp = *(uintptr_t*)(objDump + lo);
                if (lp < 0x100000000ULL || lp > 0x7FFFFFFFFFFF) continue;
                if (lp >= g_base && lp < g_base + moduleSize) continue;

                for (int cr = 4; cr <= 8; cr += 4) {
                    if (lo + cr + 4 > 0x40) continue;
                    int32_t cnt = *(int32_t*)(objDump + lo + cr);
                    if (cnt < 1 || cnt > 500) continue;

                    uintptr_t fe = TargetProcess.Read<uintptr_t>(lp);
                    if (fe < 0x100000000ULL || fe > 0x7FFFFFFFFFFF) continue;

                    uintptr_t vt = TargetProcess.Read<uintptr_t>(fe);
                    if (vt < g_base || vt >= g_base + moduleSize) continue;

                    // Valid entity with module vtable! Read name and team
                    char en[33] = {};
                    TargetProcess.Read(fe + Offsets::GameObject::Name, en, 32);
                    en[32] = 0;
                    int team = TargetProcess.Read<int>(fe + Offsets::GameObject::Team) & 0xFF;
                    float ms = TargetProcess.Read<float>(fe + Offsets::GameObject::MoveSpeed);

                    VX_LOG("[MgrProbe] HIT RVA=0x%llX ptr=0x%llX list=+0x%X(0x%llX) cnt=+0x%X(%d) ent='%.20s' team=%d ms=%.0f\n",
                        (uint64_t)rva, (uint64_t)val, lo, (uint64_t)lp,
                        lo+cr, cnt, en, team, ms);

                    // Save best entity list RVA for FixInvalidManagers fallback
                    if (g_entityListRVA == 0 && lo == 0x8 && (lo+cr) == 0x10 && cnt >= 10) {
                        bool nameOK = (en[0] >= 'A' && en[0] <= 'Z');
                        for (int c2 = 0; c2 < 32 && en[c2]; c2++) {
                            if ((uint8_t)en[c2] < 0x20 || (uint8_t)en[c2] > 0x7E) { nameOK = false; break; }
                        }
                        if (nameOK && ms > 200.0f && ms < 600.0f) {
                            g_entityListRVA = rva;
                            VX_LOG("[MgrProbe] ** Saved entity list RVA=0x%llX (count=%d, first='%.20s') **\n",
                                (uint64_t)rva, cnt, en);
                        }
                    }

                    found++;
                    if (found >= 30) goto probe_done;
                }
            }
        }
        probe_done:
        VX_LOG("[MgrProbe] Total hits: %d\n", found);
    }
}

static void ProbeHeroList()
{
    uint64_t moduleSize = 0x3000000;
    PVMMDLL_MAP_MODULEENTRY mod = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, TargetProcess.current_process.PID,
        (LPSTR)"League of Legends.exe", &mod, VMMDLL_MODULE_FLAG_NORMAL)) {
        moduleSize = mod->cbImageSize;
        VMMDLL_MemFree(mod);
    }

    uintptr_t centerRVA = Offsets::Globals::ObjectManager;
    uintptr_t scanFrom = (centerRVA > 0x2000) ? centerRVA - 0x2000 : 0;
    uintptr_t scanTo = centerRVA + 0x3000;
    if (scanTo > moduleSize) scanTo = moduleSize;

    size_t toRead = (size_t)(scanTo - scanFrom);
    std::vector<uint8_t> nbuf(toRead, 0);
    DWORD cb = 0;
    if (!VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
        g_base + scanFrom, nbuf.data(), (DWORD)toRead, &cb,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL) || cb == 0)
    {
        VX_LOG("[HeroProbe] Failed to read .data section\n");
        return;
    }

    VX_LOG("[HeroProbe] Scanning for flat hero arrays in RVA 0x%llX-0x%llX...\n",
        (uint64_t)scanFrom, (uint64_t)scanTo);

    int bestHeroCount = 0;
    uintptr_t bestRVA = 0;

    for (size_t i = 0; i + 8 <= cb; i += 8) {
        uintptr_t val = *(uintptr_t*)(nbuf.data() + i);
        uintptr_t rva = scanFrom + i;

        if (val < 0x100000000ULL || val > 0x7FFFFFFFFFFF) continue;
        if (val >= g_base && val < g_base + moduleSize) continue;

        uint8_t flat[160] = {};
        TargetProcess.Read(val, flat, sizeof(flat));

        int heroCount = 0;
        int validPtrCount = 0;
        for (int slot = 0; slot < 20; slot++) {
            uintptr_t ep = *(uintptr_t*)(flat + slot * 8);
            if (ep < 0x100000000ULL || ep > 0x7FFFFFFFFFFF) continue;
            if (ep >= g_base && ep < g_base + moduleSize) continue;
            validPtrCount++;

            uintptr_t vt = TargetProcess.Read<uintptr_t>(ep);
            if (vt < g_base || vt >= g_base + moduleSize) continue;

            uint8_t nameRaw[32] = {};
            TargetProcess.Read(ep + Offsets::GameObject::Name, nameRaw, 32);
            uint64_t cap = *(uint64_t*)(nameRaw + 24);
            char resolved[64] = {};
            if (cap <= 15) {
                memcpy(resolved, nameRaw, 16);
                resolved[15] = 0;
            } else {
                uintptr_t heapPtr = *(uintptr_t*)(nameRaw);
                if (heapPtr > 0x10000 && heapPtr < 0x7FFFFFFFFFFF)
                    TargetProcess.Read(heapPtr, resolved, 63);
            }
            resolved[63] = 0;

            float ms = TargetProcess.Read<float>(ep + Offsets::GameObject::MoveSpeed);
            uint8_t team = 0;
            TargetProcess.Read(ep + Offsets::GameObject::Team, &team, 1);

            if (resolved[0] >= 'A' && resolved[0] <= 'Z' && ms >= 100.0f && ms <= 800.0f) {
                int len = (int)strlen(resolved);
                bool bad = false;
                if (len < 2 || len > 20) bad = true;
                for (int c = 0; !bad && c < len; c++) {
                    if (resolved[c] == '_' || (resolved[c] >= '0' && resolved[c] <= '9')) bad = true;
                }
                if (!bad && (team == 1 || team == 2)) heroCount++;
            }

            if (rva == bestRVA || heroCount > 0) {
                VX_LOG("[HeroProbe]   RVA=0x%llX slot[%d] ep=0x%llX name='%.20s' ms=%.1f team=%d\n",
                    (uint64_t)rva, slot, (uint64_t)ep, resolved, ms, (int)team);
            }
        }

        if (heroCount >= 2 && heroCount > bestHeroCount) {
            bestHeroCount = heroCount;
            bestRVA = rva;
            VX_LOG("[HeroProbe] ** Candidate hero list RVA=0x%llX heroes=%d valid=%d **\n",
                (uint64_t)rva, heroCount, validPtrCount);
        }
    }

    if (bestRVA != 0 && bestHeroCount >= 2) {
        g_heroListRVA = bestRVA;
        uintptr_t mgrAddr = TargetProcess.Read<uintptr_t>(g_base + bestRVA);
        g_heroListMgrAddr = mgrAddr;
        VX_LOG("[HeroProbe] *** FOUND hero list! RVA=0x%llX mgr=0x%llX heroes=%d ***\n",
            (uint64_t)bestRVA, (uint64_t)mgrAddr, bestHeroCount);

        uint8_t flat[160] = {};
        TargetProcess.Read(mgrAddr, flat, sizeof(flat));
        VX_LOG("[HeroProbe] Full dump of hero list (20 slots):\n");
        for (int slot = 0; slot < 20; slot++) {
            uintptr_t ep = *(uintptr_t*)(flat + slot * 8);
            if (ep < 0x100000000ULL || ep > 0x7FFFFFFFFFFF) {
                VX_LOG("[HeroProbe]   [%d] 0x%llX (invalid)\n", slot, (uint64_t)ep);
                continue;
            }
            uint8_t nameRaw[32] = {};
            TargetProcess.Read(ep + Offsets::GameObject::Name, nameRaw, 32);
            uint64_t cap = *(uint64_t*)(nameRaw + 24);
            char resolved[64] = {};
            if (cap <= 15) { memcpy(resolved, nameRaw, 16); resolved[15] = 0; }
            else {
                uintptr_t hp = *(uintptr_t*)(nameRaw);
                if (hp > 0x10000 && hp < 0x7FFFFFFFFFFF) TargetProcess.Read(hp, resolved, 63);
            }
            resolved[63] = 0;
            float ms = TargetProcess.Read<float>(ep + Offsets::GameObject::MoveSpeed);
            uint8_t team = 0;
            TargetProcess.Read(ep + Offsets::GameObject::Team, &team, 1);
            VX_LOG("[HeroProbe]   [%d] 0x%llX '%s' ms=%.1f team=%d\n",
                slot, (uint64_t)ep, resolved, ms, (int)team);
        }
    } else {
        VX_LOG("[HeroProbe] No flat hero list found (best=%d heroes)\n", bestHeroCount);
    }
}

static void ProbeReadyAtOffset()
{
    if (g_gameTime < 2.0f) return;
    uintptr_t localPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
    if (!localPtr) return;

    uintptr_t sbBase = localPtr + Offsets::GameObject::SpellBook;

    VX_LOG("[ReadyAtProbe] Scanning SpellSlot for readyAt offset (GT=%.1f)\n", g_gameTime);

    uintptr_t slotPtrs[6] = {};
    for (int s = 0; s < 6; s++)
        slotPtrs[s] = TargetProcess.Read<uintptr_t>(sbBase + Offsets::SpellBook::SpellSlots + s * 0x8);

    uint8_t slotBuf[6][0x200] = {};
    for (int s = 0; s < 6; s++) {
        if (slotPtrs[s] > 0x10000 && slotPtrs[s] < 0x7FFFFFFFFFFF)
            TargetProcess.Read(slotPtrs[s], slotBuf[s], 0x200);
    }

    int bestOff = -1;
    int bestScore = 0;

    for (int off = 0; off <= 0x1FC; off += 4) {
        int zeroCount = 0;
        int validCount = 0;
        int slotCount = 0;
        float vals[6] = {};

        for (int s = 0; s < 6; s++) {
            if (slotPtrs[s] < 0x10000) continue;
            slotCount++;
            vals[s] = *(float*)(slotBuf[s] + off);
            if (vals[s] == 0.0f) { zeroCount++; validCount++; }
            else if (vals[s] >= 1.0f && vals[s] <= g_gameTime + 600.0f) validCount++;
        }

        if (slotCount < 4 || validCount < slotCount || zeroCount < 2) continue;

        int score = zeroCount * 10 + validCount;
        bool hasGameTimeVal = false;
        for (int s = 0; s < 6; s++) {
            if (vals[s] > g_gameTime * 0.3f && vals[s] <= g_gameTime + 600.0f)
                hasGameTimeVal = true;
        }
        if (hasGameTimeVal) score += 50;

        VX_LOG("[ReadyAtProbe] CANDIDATE +0x%03X (score=%d):", off, score);
        for (int s = 0; s < 6; s++) {
            if (slotPtrs[s] < 0x10000) continue;
            VX_LOG(" [%d]=%.1f", s, vals[s]);
        }
        VX_LOG("\n");

        if (score > bestScore) {
            bestScore = score;
            bestOff = off;
        }
    }

    if (bestOff >= 0 && bestOff != (int)Offsets::SpellSlot::ReadyAt) {
        VX_LOG("[ReadyAtProbe] *** READYAT CHANGED: 0x%X -> 0x%X ***\n",
            (int)Offsets::SpellSlot::ReadyAt, bestOff);
        Offsets::SpellSlot::ReadyAt = bestOff;
    } else if (bestOff == (int)Offsets::SpellSlot::ReadyAt) {
        VX_LOG("[ReadyAtProbe] ReadyAt confirmed at +0x%X\n", bestOff);
    } else {
        VX_LOG("[ReadyAtProbe] No valid readyAt offset found\n");
    }

    VX_LOG("[ReadyAtProbe] Scanning for totalCD:\n");
    int bestCDOff = -1;
    int bestCDScore = 0;
    for (int off = 0; off <= 0x1FC; off += 4) {
        int matchCount = 0;
        int slotCount = 0;
        bool hasFlashCD = false;
        float vals[6] = {};
        for (int s = 0; s < 6; s++) {
            if (slotPtrs[s] < 0x10000) continue;
            slotCount++;
            vals[s] = *(float*)(slotBuf[s] + off);
            if (vals[s] == 0.0f || (vals[s] >= 1.0f && vals[s] <= 600.0f)) matchCount++;
            if (s >= 4 && vals[s] >= 290.0f && vals[s] <= 310.0f) hasFlashCD = true;
        }
        if (matchCount >= slotCount - 1 && hasFlashCD) {
            int score = matchCount * 10 + (hasFlashCD ? 100 : 0);
            VX_LOG("[ReadyAtProbe] TOTALCD HIT +0x%03X (score=%d):", off, score);
            for (int s = 0; s < 6; s++) {
                if (slotPtrs[s] < 0x10000) continue;
                VX_LOG(" [%d]=%.1f", s, vals[s]);
            }
            VX_LOG("\n");
            if (score > bestCDScore) { bestCDScore = score; bestCDOff = off; }
        }
    }
    if (bestCDOff >= 0 && bestCDOff != (int)Offsets::SpellSlot::TotalCD) {
        VX_LOG("[ReadyAtProbe] *** TOTALCD CHANGED: 0x%X -> 0x%X ***\n",
            (int)Offsets::SpellSlot::TotalCD, bestCDOff);
        Offsets::SpellSlot::TotalCD = bestCDOff;
    }

    VX_LOG("[ReadyAtProbe] Final: ReadyAt=+0x%X TotalCD=+0x%X\n",
        (int)Offsets::SpellSlot::ReadyAt, (int)Offsets::SpellSlot::TotalCD);
}

static void FixInvalidManagers()
{
    uint64_t moduleSize = 0x3000000;
    PVMMDLL_MAP_MODULEENTRY mod = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, TargetProcess.current_process.PID,
        (LPSTR)"League of Legends.exe", &mod, VMMDLL_MODULE_FLAG_NORMAL)) {
        moduleSize = mod->cbImageSize;
        VMMDLL_MemFree(mod);
    }

    struct MgrFix {
        uintptr_t* global;
        const char* name;
        int expectedCountMin; // heroes 1-12, minions 1-500, turrets 1-50
        int expectedCountMax;
    };
    MgrFix fixes[] = {
        { &Offsets::Globals::HeroManager,   "HeroManager",   1, 12 },
        { &Offsets::Globals::MinionManager, "MinionManager", 1, 500 },
        { &Offsets::Globals::TurretManager, "TurretManager", 1, 50 },
    };

    for (auto& fix : fixes) {
        // Check if current pointer is a valid manager
        uintptr_t ptr = TargetProcess.Read<uintptr_t>(g_base + *fix.global);
        bool needsFix = false;
        if (ptr < 0x100000000ULL || ptr > 0x7FFFFFFFFFFF) {
            needsFix = true;
        } else {
            uintptr_t listPtr = TargetProcess.Read<uintptr_t>(ptr + 0x8);
            int32_t count = TargetProcess.Read<int32_t>(ptr + 0x10);
            if (listPtr < 0x100000000ULL || listPtr > 0x7FFFFFFFFFFF ||
                count < fix.expectedCountMin || count > fix.expectedCountMax) {
                needsFix = true;
            } else {
                // Check first entity has valid vtable in module
                uintptr_t firstEnt = TargetProcess.Read<uintptr_t>(listPtr);
                if (firstEnt < 0x100000000ULL || firstEnt > 0x7FFFFFFFFFFF) {
                    needsFix = true;
                } else {
                    uintptr_t entVtable = TargetProcess.Read<uintptr_t>(firstEnt);
                    if (entVtable < g_base || entVtable >= g_base + moduleSize) needsFix = true;
                }
            }
        }
        if (!needsFix) {
            VX_LOG("[MgrFix] %s OK at 0x%llX\n", fix.name, (uint64_t)*fix.global);
            continue;
        }

        VX_LOG("[MgrFix] %s INVALID (ptr=0x%llX), scanning .data section...\n",
            fix.name, (uint64_t)ptr);

        // Scan the entire .data section for manager-like structures
        // .data is typically in the range moduleSize*0.6 to moduleSize
        uintptr_t scanStart = (uintptr_t)(moduleSize * 0.55);
        uintptr_t scanEnd = moduleSize;
        constexpr size_t SCAN_CHUNK = 0x10000;

        struct MgrCand {
            uintptr_t rva;
            uintptr_t ptr;
            int32_t count;
            int score;
            char entName[33];
        };
        std::vector<MgrCand> cands;

        for (uintptr_t off = scanStart; off < scanEnd; off += SCAN_CHUNK) {
            size_t toRead = (size_t)std::min((uintptr_t)SCAN_CHUNK, scanEnd - off);
            std::vector<uint8_t> chunk(toRead, 0);
            DWORD cb = 0;
            if (!VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
                g_base + off, chunk.data(), (DWORD)toRead, &cb,
                VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
                continue;

            for (size_t i = 0; i + 8 <= cb; i += 8) {
                uintptr_t val = *(uintptr_t*)(chunk.data() + i);
                uintptr_t rva = off + i;

                // Must be a real heap pointer (not module-internal, not tiny)
                if (val < 0x100000000ULL || val > 0x7FFFFFFFFFFF) continue;
                if (val >= g_base && val < g_base + moduleSize) continue;

                // Skip RVAs already used by other globals
                if (rva == Offsets::Globals::NetClient || rva == Offsets::Globals::ObjectManager ||
                    rva == Offsets::Globals::HudInstance || rva == Offsets::Globals::LocalPlayer ||
                    rva == Offsets::Globals::GameTime || rva == Offsets::Globals::NavGrid ||
                    rva == Offsets::Globals::UnderMouseObj || rva == Offsets::Globals::MissileManager)
                    continue;

                // Read manager struct: list at +0x8, count at +0x10
                uint8_t mgrData[0x20] = {};
                TargetProcess.Read(val, mgrData, sizeof(mgrData));
                uintptr_t listPtr = *(uintptr_t*)(mgrData + 0x8);
                int32_t count = *(int32_t*)(mgrData + 0x10);

                if (listPtr < 0x100000000ULL || listPtr > 0x7FFFFFFFFFFF) continue;
                if (listPtr >= g_base && listPtr < g_base + moduleSize) continue;
                if (count < fix.expectedCountMin || count > fix.expectedCountMax) continue;

                // Read first entity
                uintptr_t firstEnt = TargetProcess.Read<uintptr_t>(listPtr);
                if (firstEnt < 0x100000000ULL || firstEnt > 0x7FFFFFFFFFFF) continue;
                if (firstEnt >= g_base && firstEnt < g_base + moduleSize) continue;

                // Entity vtable must be module-internal
                uintptr_t entVtable = TargetProcess.Read<uintptr_t>(firstEnt);
                if (entVtable < g_base || entVtable >= g_base + moduleSize) continue;

                // Passed struct validation! Log it
                VX_LOG("[MgrFix] %s struct-valid: RVA=0x%llX ptr=0x%llX list=0x%llX count=%d ent=0x%llX vt=0x%llX\n",
                    fix.name, (uint64_t)rva, (uint64_t)val, (uint64_t)listPtr,
                    count, (uint64_t)firstEnt, (uint64_t)entVtable);

                // Entity name — readable ASCII is a bonus, NOT a hard filter
                char eName[33] = {};
                TargetProcess.Read(firstEnt + Offsets::GameObject::Name, eName, 32);
                eName[32] = 0;
                bool readable = eName[0] != 0;
                for (int c = 0; c < 32 && eName[c]; c++) {
                    if (eName[c] < 0x20 || eName[c] > 0x7E) { readable = false; break; }
                }
                if (!readable) {
                    // Name unreadable — might mean offset shifted. Zero out for display but don't reject
                    eName[0] = '?'; eName[1] = 0;
                }

                // Score based on liveness
                int score = 50;
                if (readable) score += 150; // big bonus for readable name
                int entTeam = TargetProcess.Read<int>(firstEnt + Offsets::GameObject::Team) & 0xFF;
                if (entTeam == 1 || entTeam == 2 || entTeam == 100 || entTeam == 200) score += 200;
                float entMS = TargetProcess.Read<float>(firstEnt + Offsets::GameObject::MoveSpeed);
                if (entMS > 200.0f && entMS < 600.0f) score += 100;
                float px = TargetProcess.Read<float>(firstEnt + Offsets::GameObject::Position);
                float pz = TargetProcess.Read<float>(firstEnt + Offsets::GameObject::Position + 8);
                if (px > 100.f && px < 16000.f && pz > 100.f && pz < 16000.f) score += 50;
                // vtable bonus
                uintptr_t mgrVtable = *(uintptr_t*)(mgrData);
                if (mgrVtable >= g_base && mgrVtable < g_base + moduleSize) score += 20;

                MgrCand mc;
                mc.rva = rva; mc.ptr = val; mc.count = count; mc.score = score;
                memcpy(mc.entName, eName, 33);
                cands.push_back(mc);
                if (cands.size() >= 50) goto done_scan;
            }
        }
        done_scan:

        if (!cands.empty()) {
            std::sort(cands.begin(), cands.end(), [](const MgrCand& a, const MgrCand& b) {
                return a.score > b.score;
            });
            VX_LOG("[MgrFix] %s: %d candidates found\n", fix.name, (int)cands.size());
            for (int i = 0; i < std::min((int)cands.size(), 5); i++) {
                VX_LOG("[MgrFix]   [%d] RVA=0x%llX ptr=0x%llX count=%d name='%s' score=%d\n",
                    i, (uint64_t)cands[i].rva, (uint64_t)cands[i].ptr,
                    cands[i].count, cands[i].entName, cands[i].score);
            }
            *fix.global = cands[0].rva;
            VX_LOG("[MgrFix] %s FIXED: 0x%llX (count=%d, entity='%s', score=%d)\n",
                fix.name, (uint64_t)cands[0].rva, cands[0].count, cands[0].entName, cands[0].score);
        } else {
            VX_LOG("[MgrFix] %s: NO valid manager found in .data scan!\n", fix.name);
            // Fallback: use combined entity list from ProbeManagerLayout
            if (g_entityListRVA != 0) {
                *fix.global = g_entityListRVA;
                g_useEntityFilter = true;
                VX_LOG("[MgrFix] %s: FALLBACK to entity list RVA=0x%llX (will filter by entity type)\n",
                    fix.name, (uint64_t)g_entityListRVA);
            }
        }
    }
}

static void SigScanViewPort()
{
    g_viewPortScanned = true;
    g_scannedViewPort = 0;
    g_scannedVPMatrix = 0;

    uint64_t moduleSize = 0x3000000;
    PVMMDLL_MAP_MODULEENTRY mod = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, TargetProcess.current_process.PID,
        (LPSTR)xorstr_("League of Legends.exe"), &mod, VMMDLL_MODULE_FLAG_NORMAL)) {
        moduleSize = mod->cbImageSize;
        VMMDLL_MemFree(mod);
    }
    VX_LOG("[SigScan] Scanning 0x%llX bytes...\n", (uint64_t)moduleSize);

    constexpr uint64_t CHUNK = 0x10000;
    std::vector<uint8_t> buf(CHUNK + 16);

    for (uint64_t off = 0; off < moduleSize; off += CHUNK) {
        DWORD cb = 0;
        DWORD toRead = (DWORD)(std::min)((uint64_t)(CHUNK + 13), moduleSize - off);
        if (!VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
            g_base + off, buf.data(), toRead, &cb, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
            continue;
        if (cb < 13) continue;
        for (DWORD i = 0; i + 13 <= cb; i++) {
            // ViewPort: 48 8B 3D ?? ?? ?? ?? FF 90 20 01 00 00
            if (!g_scannedViewPort &&
                buf[i]==0x48 && buf[i+1]==0x8B && buf[i+2]==0x3D &&
                buf[i+7]==0xFF && buf[i+8]==0x90 &&
                buf[i+9]==0x20 && buf[i+10]==0x01 && buf[i+11]==0x00 && buf[i+12]==0x00) {
                int32_t rel; memcpy(&rel, &buf[i+3], 4);
                int64_t rva = (int64_t)(off + i + 7) + (int64_t)rel;
                if (rva > 0) { g_scannedViewPort = (uintptr_t)rva; VX_LOG("[SigScan] ViewPort RVA 0x%llX\n", (uint64_t)rva); }
            }
            // ViewProjMatrices: 48 8D 0D ?? ?? ?? ?? 0F 10 00
            if (!g_scannedVPMatrix && i + 10 <= cb &&
                buf[i]==0x48 && buf[i+1]==0x8D && buf[i+2]==0x0D &&
                buf[i+7]==0x0F && buf[i+8]==0x10 && buf[i+9]==0x00) {
                int32_t rel; memcpy(&rel, &buf[i+3], 4);
                int64_t rva = (int64_t)(off + i + 7) + (int64_t)rel;
                if (rva > 0) { g_scannedVPMatrix = (uintptr_t)rva; VX_LOG("[SigScan] ViewProjMatrices RVA 0x%llX\n", (uint64_t)rva); }
            }
            if (g_scannedViewPort && g_scannedVPMatrix) goto scan_done;
        }
    }
    scan_done:
    if (!g_scannedViewPort) VX_LOG("[SigScan] ViewPort not found\n");
    if (!g_scannedVPMatrix) VX_LOG("[SigScan] ViewProjMatrices not found\n");
}

static void ReadEntityList(uintptr_t mgrGlobal, std::vector<uintptr_t>& out, uint32_t cap = 200)
{
    out.clear();
    uintptr_t mgr = TargetProcess.Read<uintptr_t>(g_base + mgrGlobal);
    if (!mgr) return;
    uintptr_t listPtr = TargetProcess.Read<uintptr_t>(mgr + Offsets::Manager::List);
    uint32_t  count   = TargetProcess.Read<uint32_t>(mgr + Offsets::Manager::Count);
    count = std::min(count, cap);
    if (!listPtr || count == 0) return;
    out.resize(count);
    TargetProcess.Read(listPtr, out.data(), count * sizeof(uintptr_t));
}

static void CacheGameState()
{
    g_gameTime = TargetProcess.Read<float>(g_base + Offsets::Globals::GameTime);
    if (g_gameTime < 1.0f) {
        // If GameTime stays 0 for too many frames, the offset is probably wrong.
        // Try scanning nearby RVAs for a plausible game-time float.
        static int zeroCount = 0;
        zeroCount++;
        if (zeroCount >= 300) { // ~5 seconds at 60fps
            zeroCount = 0;
            VX_LOG("[SigScan] GameTime stuck at 0 — scanning nearby RVAs...\n");
            uintptr_t origGT = Offsets::Globals::GameTime;
            uint64_t moduleSize = 0x3000000;
            PVMMDLL_MAP_MODULEENTRY mod = nullptr;
            if (VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, TargetProcess.current_process.PID,
                (LPSTR)"League of Legends.exe", &mod, VMMDLL_MODULE_FLAG_NORMAL)) {
                moduleSize = mod->cbImageSize;
                VMMDLL_MemFree(mod);
            }
            // Scan all movss/subss [rip+disp32] instructions for a float that looks like game time
            static const uint8_t gtPrefixes[][4] = {
                {0xF3,0x0F,0x11,0x05}, {0xF3,0x0F,0x11,0x0D}, {0xF3,0x0F,0x11,0x15},
                {0xF3,0x0F,0x11,0x1D}, {0xF3,0x0F,0x11,0x25}, {0xF3,0x0F,0x11,0x2D},
                {0xF3,0x0F,0x11,0x35}, {0xF3,0x0F,0x11,0x3D}, // movss [rip+disp],xmmN
                {0xF3,0x0F,0x5C,0x05}, {0xF3,0x0F,0x5C,0x0D}, {0xF3,0x0F,0x5C,0x15},
                {0xF3,0x0F,0x5C,0x35}, // subss xmmN,[rip+disp]
                {0xF3,0x0F,0x10,0x05}, {0xF3,0x0F,0x10,0x0D}, {0xF3,0x0F,0x10,0x15},
                {0xF3,0x0F,0x10,0x35}, // movss xmmN,[rip+disp]
            };
            constexpr int NUM_GT_PFX = sizeof(gtPrefixes) / sizeof(gtPrefixes[0]);
            constexpr uint64_t CHUNK = 0x100000;
            constexpr uint64_t PAD   = 64;
            constexpr int64_t DATA_LO = 0x1B00000, DATA_HI = 0x1F00000;

            struct GTCand { int64_t rva; float val; };
            std::vector<GTCand> gtCands;
            std::vector<uint8_t> gbuf(CHUNK + PAD);

            for (uint64_t off = 0; off < moduleSize && gtCands.size() < 64; off += CHUNK) {
                DWORD cb = 0;
                DWORD toRead = (DWORD)(std::min)((uint64_t)(CHUNK + PAD), moduleSize - off);
                if (!VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
                    g_base + off, gbuf.data(), toRead, &cb, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
                    continue;
                if (cb < 8) continue;

                for (DWORD i = 0; i + 8 <= cb; i++) {
                    bool match = false;
                    for (int p = 0; p < NUM_GT_PFX; p++) {
                        if (gbuf[i]==gtPrefixes[p][0] && gbuf[i+1]==gtPrefixes[p][1] &&
                            gbuf[i+2]==gtPrefixes[p][2] && gbuf[i+3]==gtPrefixes[p][3]) {
                            match = true; break;
                        }
                    }
                    if (!match) continue;
                    if (i + 8 > cb) continue;

                    int32_t disp;
                    memcpy(&disp, &gbuf[i + 4], 4);
                    int64_t rva = (int64_t)(off + i + 8) + (int64_t)disp;
                    if (rva < DATA_LO || rva > DATA_HI) continue;

                    // Check proximity to old GameTime address
                    int64_t dist = rva - (int64_t)origGT;
                    if (dist < 0) dist = -dist;
                    if (dist > 0x20000) continue;

                    float val = TargetProcess.Read<float>(g_base + (uintptr_t)rva);
                    if (val > 1.0f && val < 99999.f) {
                        gtCands.push_back({ rva, val });
                        if (gtCands.size() >= 64) break;
                    }
                }
            }

            if (!gtCands.empty()) {
                // Pick candidate closest to old address
                int bestIdx = 0;
                int64_t bestDist = 0x7FFFFFFF;
                for (int c = 0; c < (int)gtCands.size(); c++) {
                    int64_t d = gtCands[c].rva - (int64_t)origGT;
                    if (d < 0) d = -d;
                    if (d < bestDist) { bestDist = d; bestIdx = c; }
                }
                Offsets::Globals::GameTime = (uintptr_t)gtCands[bestIdx].rva;
                g_gameTime = gtCands[bestIdx].val;
                VX_LOG("[SigScan] GameTime RE-FOUND at 0x%llX (val=%.1f, dist=%lld)\n",
                    (uint64_t)gtCands[bestIdx].rva, gtCands[bestIdx].val, (long long)bestDist);

                // Apply the same delta to ALL other globals
                int64_t delta = (int64_t)gtCands[bestIdx].rva - (int64_t)origGT;
                if (delta != 0) {
                    VX_LOG("[SigScan] Data section delta = %lld bytes, applying to all globals...\n", (long long)delta);
                    auto applyDelta = [&](uintptr_t& g, const char* name) {
                        uintptr_t oldVal = g;
                        g = (uintptr_t)((int64_t)g + delta);
                        // Validate the shifted pointer
                        uintptr_t ptr = TargetProcess.Read<uintptr_t>(g_base + g);
                        bool inRange = (ptr > 0x10000 && ptr < 0x7FFFFFFFFFFF);
                        bool inModule = (ptr >= g_base && ptr < g_base + moduleSize);
                        if (inRange && !inModule) {
                            VX_LOG("[SigScan]   %s: 0x%llX -> 0x%llX (ptr 0x%llX) OK\n",
                                name, (uint64_t)oldVal, (uint64_t)g, (uint64_t)ptr);
                        } else {
                            VX_LOG("[SigScan]   %s: 0x%llX -> 0x%llX (ptr 0x%llX) INVALID, reverted\n",
                                name, (uint64_t)oldVal, (uint64_t)g, (uint64_t)ptr);
                            g = oldVal; // revert if shifted address doesn't validate
                        }
                    };
                    applyDelta(Offsets::Globals::LocalPlayer,      "LocalPlayer");
                    applyDelta(Offsets::Globals::HeroManager,      "HeroManager");
                    applyDelta(Offsets::Globals::MinionManager,    "MinionManager");
                    applyDelta(Offsets::Globals::TurretManager,    "TurretManager");
                    applyDelta(Offsets::Globals::ObjectManager,    "ObjectManager");
                    applyDelta(Offsets::Globals::MissileManager,   "MissileManager");
                    applyDelta(Offsets::Globals::MissileContainer, "MissileContainer");
                    applyDelta(Offsets::Globals::NavGrid,          "NavGrid");
                    applyDelta(Offsets::Globals::ViewPort,         "ViewPort");
                    applyDelta(Offsets::Globals::MouseScreenVec2,  "MouseScreenVec2");
                    applyDelta(Offsets::Globals::UnderMouseObj,    "UnderMouseObj");
                    applyDelta(Offsets::Globals::HudInstance,      "HudInstance");
                    applyDelta(Offsets::Globals::ChatManager,      "ChatManager");
                    applyDelta(Offsets::Globals::NetClient,        "NetClient");
                }

                // Diagnostic: dump manager internals to verify struct offsets
                VX_LOG("[Diag] === Manager Structure Dump ===\n");
                auto dumpManager = [&](uintptr_t rva, const char* name) {
                    uintptr_t ptr = TargetProcess.Read<uintptr_t>(g_base + rva);
                    if (ptr < 0x10000 || ptr > 0x7FFFFFFFFFFF) {
                        VX_LOG("[Diag] %s: ptr=0x%llX (INVALID)\n", name, (uint64_t)ptr);
                        return;
                    }
                    // Read first 0x20 bytes of manager object
                    uint8_t mgrBytes[0x20] = {};
                    TargetProcess.Read(ptr, mgrBytes, sizeof(mgrBytes));
                    VX_LOG("[Diag] %s ptr=0x%llX raw:", name, (uint64_t)ptr);
                    for (int b = 0; b < 0x20; b++) VX_LOG(" %02X", mgrBytes[b]);
                    VX_LOG("\n");

                    uintptr_t listPtr = *(uintptr_t*)(mgrBytes + 0x8);
                    int32_t count = *(int32_t*)(mgrBytes + 0x10);
                    VX_LOG("[Diag] %s list=0x%llX count=%d\n", name, (uint64_t)listPtr, count);

                    if (listPtr > 0x10000 && listPtr < 0x7FFFFFFFFFFF && count > 0 && count < 500) {
                        uintptr_t firstEnt = TargetProcess.Read<uintptr_t>(listPtr);
                        VX_LOG("[Diag] %s firstEntity=0x%llX\n", name, (uint64_t)firstEnt);
                        if (firstEnt > 0x10000 && firstEnt < 0x7FFFFFFFFFFF) {
                            int team = TargetProcess.Read<int>(firstEnt + Offsets::GameObject::Team);
                            float hp = TargetProcess.Read<float>(firstEnt + Offsets::GameObject::Health);
                            float maxHp = TargetProcess.Read<float>(firstEnt + Offsets::GameObject::MaxHealth);
                            float posX = TargetProcess.Read<float>(firstEnt + Offsets::GameObject::Position);
                            float posZ = TargetProcess.Read<float>(firstEnt + Offsets::GameObject::Position + 8);
                            VX_LOG("[Diag] %s entity[0] team=%d(byte=%d) hp=%.1f/%.1f pos=(%.0f,%.0f)\n",
                                name, team, team & 0xFF, hp, maxHp, posX, posZ);
                            // Read name (first 32 chars at Name offset)
                            char nameStr[33] = {};
                            TargetProcess.Read(firstEnt + Offsets::GameObject::Name, nameStr, 32);
                            nameStr[32] = 0;
                            // Check if readable ASCII
                            bool readable = true;
                            for (int c = 0; c < 32 && nameStr[c]; c++) {
                                if (nameStr[c] < 0x20 || nameStr[c] > 0x7E) { readable = false; break; }
                            }
                            if (readable && nameStr[0])
                                VX_LOG("[Diag] %s entity[0] name='%s'\n", name, nameStr);
                            else
                                VX_LOG("[Diag] %s entity[0] name=<unreadable>\n", name);
                        }
                    }
                };
                dumpManager(Offsets::Globals::HeroManager, "HeroManager");
                dumpManager(Offsets::Globals::MinionManager, "MinionManager");
                dumpManager(Offsets::Globals::ObjectManager, "ObjectManager");
                // Also dump LocalPlayer
                {
                    uintptr_t lpPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
                    VX_LOG("[Diag] LocalPlayer ptr=0x%llX\n", (uint64_t)lpPtr);
                    if (lpPtr > 0x10000 && lpPtr < 0x7FFFFFFFFFFF) {
                        int team = TargetProcess.Read<int>(lpPtr + Offsets::GameObject::Team);
                        float hp = TargetProcess.Read<float>(lpPtr + Offsets::GameObject::Health);
                        float maxHp = TargetProcess.Read<float>(lpPtr + Offsets::GameObject::MaxHealth);
                        float posX = TargetProcess.Read<float>(lpPtr + Offsets::GameObject::Position);
                        float posZ = TargetProcess.Read<float>(lpPtr + Offsets::GameObject::Position + 8);
                        float moveSpeed = TargetProcess.Read<float>(lpPtr + Offsets::GameObject::MoveSpeed);
                        VX_LOG("[Diag] LocalPlayer team=%d(byte=%d) hp=%.1f/%.1f pos=(%.0f,%.0f) ms=%.0f\n",
                            team, team & 0xFF, hp, maxHp, posX, posZ, moveSpeed);
                        char nameStr[33] = {};
                        TargetProcess.Read(lpPtr + Offsets::GameObject::Name, nameStr, 32);
                        nameStr[32] = 0;
                        bool readable = true;
                        for (int c = 0; c < 32 && nameStr[c]; c++) {
                            if (nameStr[c] < 0x20 || nameStr[c] > 0x7E) { readable = false; break; }
                        }
                        if (readable && nameStr[0])
                            VX_LOG("[Diag] LocalPlayer name='%s'\n", nameStr);
                        else
                            VX_LOG("[Diag] LocalPlayer name=<unreadable>\n", nameStr);
                    }
                }
                VX_LOG("[Diag] === End Dump ===\n");

                // ---- STRUCT OFFSET PROBE ----
                // Read entity from LocalPlayer and probe around broken offsets
                // to find where Team (100/200), Position (map coords), MoveSpeed (~325) actually are
                {
                    uintptr_t lpPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
                    if (lpPtr > 0x10000 && lpPtr < 0x7FFFFFFFFFFF) {
                        VX_LOG("[Probe] Scanning entity 0x%llX for shifted struct offsets...\n", (uint64_t)lpPtr);

                        // Probe 1: Team region (old=0x164). Dump 0x150-0x1B0 as int32s looking for 100 or 200
                        {
                            uint8_t buf[0x60] = {};
                            TargetProcess.Read(lpPtr + 0x150, buf, sizeof(buf));
                            VX_LOG("[Probe] Team region 0x150-0x1AF hex:\n");
                            for (int row = 0; row < 6; row++) {
                                int off = row * 16;
                                VX_LOG("[Probe]  +0x%03X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                    0x150 + off,
                                    buf[off+0], buf[off+1], buf[off+2], buf[off+3],
                                    buf[off+4], buf[off+5], buf[off+6], buf[off+7],
                                    buf[off+8], buf[off+9], buf[off+10], buf[off+11],
                                    buf[off+12], buf[off+13], buf[off+14], buf[off+15]);
                            }
                            // Search for team value: new encoding low byte 1=blue, 2=red
                            for (int i = 0; i + 4 <= (int)sizeof(buf); i += 4) {
                                int32_t val = *(int32_t*)(buf + i);
                                int lowByte = val & 0xFF;
                                if (val == 100 || val == 200) {
                                    VX_LOG("[Probe] TEAM FOUND (old): value=%d at offset 0x%X (shift=%+d from 0x164)\n",
                                        val, 0x150 + i, (0x150 + i) - 0x164);
                                }
                                if ((lowByte == 1 || lowByte == 2) && i >= 0x10 && i <= 0x1C) {
                                    VX_LOG("[Probe] TEAM FOUND (new): raw=0x%X byte=%d at offset 0x%X (shift=%+d from 0x164)\n",
                                        (unsigned)val, lowByte, 0x150 + i, (0x150 + i) - 0x164);
                                }
                            }
                        }

                        // Probe 2: Position region. Dump 0x168-0x1A0 as floats looking for map coords (100-15000)
                        {
                            uint8_t buf[0x60] = {};
                            TargetProcess.Read(lpPtr + 0x150, buf, sizeof(buf));
                            VX_LOG("[Probe] Position scan (floats in 0x150-0x1AF):\n");
                            for (int i = 0; i + 4 <= (int)sizeof(buf); i += 4) {
                                float val = *(float*)(buf + i);
                                if (val > 50.0f && val < 16000.0f) {
                                    VX_LOG("[Probe]   +0x%03X = %.1f (possible map coord)\n", 0x150 + i, val);
                                }
                            }
                        }

                        // Probe 3: MoveSpeed region (old=0xFE4). Dump 0xFC0-0x1020 looking for ~325
                        {
                            uint8_t buf[0x60] = {};
                            TargetProcess.Read(lpPtr + 0xFC0, buf, sizeof(buf));
                            VX_LOG("[Probe] MoveSpeed region 0xFC0-0x101F hex:\n");
                            for (int row = 0; row < 6; row++) {
                                int off = row * 16;
                                VX_LOG("[Probe]  +0x%03X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                    0xFC0 + off,
                                    buf[off+0], buf[off+1], buf[off+2], buf[off+3],
                                    buf[off+4], buf[off+5], buf[off+6], buf[off+7],
                                    buf[off+8], buf[off+9], buf[off+10], buf[off+11],
                                    buf[off+12], buf[off+13], buf[off+14], buf[off+15]);
                            }
                            // Search for float values in MoveSpeed range (200-500)
                            for (int i = 0; i + 4 <= (int)sizeof(buf); i += 4) {
                                float val = *(float*)(buf + i);
                                if (val > 200.0f && val < 600.0f) {
                                    VX_LOG("[Probe]   +0x%03X = %.1f (possible MoveSpeed)\n", 0xFC0 + i, val);
                                }
                            }
                        }

                        // Probe 4: Wider scan for Team value across larger range
                        // Scan 0x100-0x300 for new team encoding (low byte 1 or 2)
                        {
                            uint8_t buf[0x200] = {};
                            TargetProcess.Read(lpPtr + 0x100, buf, sizeof(buf));
                            for (int i = 0; i + 4 <= (int)sizeof(buf); i += 4) {
                                int32_t val = *(int32_t*)(buf + i);
                                if (val == 100 || val == 200) {
                                    VX_LOG("[Probe] TEAM candidate (old): value=%d at offset 0x%X\n", val, 0x100 + i);
                                }
                            }
                            // Check byte at 0x164 specifically for new encoding
                            int32_t teamAt164 = *(int32_t*)(buf + (0x164 - 0x100));
                            VX_LOG("[Probe] Team at 0x164: raw=0x%X byte=%d\n", (unsigned)teamAt164, teamAt164 & 0xFF);
                        }

                        // Probe 5: Wider MoveSpeed scan 0xE00-0x1100
                        {
                            uint8_t buf[0x300] = {};
                            TargetProcess.Read(lpPtr + 0xE00, buf, sizeof(buf));
                            for (int i = 0; i + 4 <= (int)sizeof(buf); i += 4) {
                                float val = *(float*)(buf + i);
                                if (val > 300.0f && val < 500.0f) {
                                    VX_LOG("[Probe] MS candidate: %.1f at offset 0x%X\n", val, 0xE00 + i);
                                }
                            }
                        }

                        // Probe 6: Check IsDead region (old=0x160) and nearby
                        {
                            uint8_t buf[0x20] = {};
                            TargetProcess.Read(lpPtr + 0x150, buf, sizeof(buf));
                            VX_LOG("[Probe] IsDead region as int32s:\n");
                            for (int i = 0; i < 0x20; i += 4) {
                                int32_t val = *(int32_t*)(buf + i);
                                VX_LOG("[Probe]   +0x%03X = %d (0x%08X)\n", 0x150 + i, val, (uint32_t)val);
                            }
                        }
                    }
                }

                // ---- MANAGER NEIGHBORHOOD SCAN ----
                // The generic scanner found config string table entries, not real managers.
                // Scan .data section around the FALLBACK addresses for real manager structures.
                {
                    VX_LOG("[MgrScan] Scanning .data neighborhood for real managers...\n");

                    struct MgrTarget {
                        uintptr_t* global;
                        const char* name;
                        uintptr_t fallback;
                    };
                    // Use actual sig-found addresses as center when available (not old hardcoded fallbacks)
                    // This handles the case where globals shifted far from old fallback positions
                    uintptr_t hmCenter = Offsets::Globals::HeroManager;
                    // If HeroManager was reverted to its hardcoded default due to collision,
                    // scan around NetClient's address instead (they were at the same RVA)
                    if (hmCenter == 0x1CFAA70 && Offsets::Globals::NetClient != 0x1CFA940) {
                        hmCenter = Offsets::Globals::NetClient; // scan near collision area
                        VX_LOG("[MgrScan] HeroManager using NetClient vicinity 0x%llX as scan center\n",
                            (uint64_t)hmCenter);
                    }

                    MgrTarget targets[] = {
                        { &Offsets::Globals::HeroManager, "HeroManager", hmCenter },
                        { &Offsets::Globals::MinionManager, "MinionManager", 0x1CFE0B8 },
                        { &Offsets::Globals::TurretManager, "TurretManager", 0x1D04798 },
                        { &Offsets::Globals::NetClient, "NetClient", 0x1CFA940 },
                        { &Offsets::Globals::ChatManager, "ChatManager", 0x1CCDED0 },
                    };

                    for (auto& tgt : targets) {
                        uintptr_t center = tgt.fallback;
                        const int64_t RANGE = 0x8000; // wider range to handle patch shifts
                        uintptr_t scanStart = center > (uintptr_t)RANGE ? center - RANGE : 0x1B00000;
                        uintptr_t scanEnd = center + RANGE;
                        size_t scanLen = (size_t)(scanEnd - scanStart);
                        std::vector<uint8_t> chunk(scanLen, 0);
                        TargetProcess.Read(g_base + scanStart, chunk.data(), scanLen);

                        struct MgrCand {
                            uintptr_t rva;
                            uintptr_t ptr;
                            uintptr_t listPtr;
                            int32_t count;
                            int score;
                            char entName[33];
                        };
                        std::vector<MgrCand> cands;

                        for (size_t off = 0; off + 8 <= scanLen; off += 8) {
                            uintptr_t val = *(uintptr_t*)(chunk.data() + off);
                            uintptr_t rva = scanStart + off;

                            // Skip null, module-internal, or tiny pointers
                            if (val < 0x10000 || val > 0x7FFFFFFFFFFF) continue;
                            if (val >= g_base && val < g_base + moduleSize) continue;

                            // Read manager structure: vtable at +0, list at +8, count at +10
                            uint8_t mgrData[0x20] = {};
                            TargetProcess.Read(val, mgrData, sizeof(mgrData));
                            uintptr_t vtable = *(uintptr_t*)(mgrData);
                            uintptr_t listPtr = *(uintptr_t*)(mgrData + 0x8);
                            int32_t count = *(int32_t*)(mgrData + 0x10);

                            // Manager validation: list must be heap pointer, count 1-500
                            if (listPtr < 0x10000 || listPtr > 0x7FFFFFFFFFFF) continue;
                            if (listPtr >= g_base && listPtr < g_base + moduleSize) continue;
                            if (count < 1 || count > 500) continue;

                            // Read first entity from list
                            uintptr_t firstEnt = TargetProcess.Read<uintptr_t>(listPtr);
                            if (firstEnt < 0x10000 || firstEnt > 0x7FFFFFFFFFFF) continue;
                            if (firstEnt >= g_base && firstEnt < g_base + moduleSize) continue;

                            // Entity vtable must be module-internal (real C++ object)
                            uintptr_t entVtable = TargetProcess.Read<uintptr_t>(firstEnt);
                            if (entVtable < g_base || entVtable >= g_base + moduleSize) continue;

                            // Entity must have readable name at Name offset
                            char eName[33] = {};
                            TargetProcess.Read(firstEnt + 0x3890, eName, 32);
                            eName[32] = 0;
                            bool readable = true;
                            for (int c = 0; c < 32 && eName[c]; c++) {
                                if (eName[c] < 0x20 || eName[c] > 0x7E) { readable = false; break; }
                            }
                            if (!readable || !eName[0]) continue;

                            // Score this candidate
                            int score = 100;
                            // Proximity to fallback
                            int64_t dist = (int64_t)rva - (int64_t)center;
                            if (dist < 0) dist = -dist;
                            score += std::max(0, (int)(50 - (int)(dist / 64)));
                            // Manager vtable being module-internal is bonus
                            if (vtable >= g_base && vtable < g_base + moduleSize) score += 20;
                            // Entity-liveness check: valid Team = live entity
                            // Team encoding changed: low byte 1=blue, 2=red (was 100/200)
                            int entTeamRaw = TargetProcess.Read<int>(firstEnt + 0x164);
                            int entTeam = entTeamRaw & 0xFF;
                            if (entTeam == 1 || entTeam == 2) score += 200;
                            // Valid MoveSpeed (200-600) = live entity
                            float entMS = TargetProcess.Read<float>(firstEnt + 0xFE4);
                            if (entMS > 200.0f && entMS < 600.0f) score += 100;
                            else if (entMS < 1.0f && count > 1) {
                                // entity[0] has ms=0 — check entity[1] for staleness
                                uintptr_t secEnt = TargetProcess.Read<uintptr_t>(listPtr + 8);
                                if (secEnt > 0x10000 && secEnt < 0x7FFFFFFFFFFF) {
                                    float secMS = TargetProcess.Read<float>(secEnt + 0xFE4);
                                    if (secMS > 200.0f && secMS < 600.0f) {
                                        // entity[0] dead/loading but entity[1] has valid ms
                                        // = stale manager from previous game session
                                        score -= 150;
                                        VX_LOG("[MgrScan]   STALE penalty: entity[0] ms=0 but entity[1] ms=%.0f\n", secMS);
                                    }
                                }
                            }
                            // Valid position (LoL map coords 0-16000)
                            float entPosX = TargetProcess.Read<float>(firstEnt + 0x168);
                            float entPosZ = TargetProcess.Read<float>(firstEnt + 0x170);
                            if (entPosX > 100.0f && entPosX < 16000.0f && entPosZ > 100.0f && entPosZ < 16000.0f) score += 50;

                            MgrCand mc;
                            mc.rva = rva;
                            mc.ptr = val;
                            mc.listPtr = listPtr;
                            mc.count = count;
                            mc.score = score;
                            memcpy(mc.entName, eName, 33);
                            cands.push_back(mc);
                        }

                        if (!cands.empty()) {
                            std::sort(cands.begin(), cands.end(), [](const MgrCand& a, const MgrCand& b) {
                                return a.score > b.score;
                            });

                            VX_LOG("[MgrScan] %s: %d candidates found\n", tgt.name, (int)cands.size());
                            for (int i = 0; i < std::min((int)cands.size(), 5); i++) {
                                VX_LOG("[MgrScan]   [%d] RVA=0x%llX ptr=0x%llX list=0x%llX count=%d name='%s' score=%d\n",
                                    i, (uint64_t)cands[i].rva, (uint64_t)cands[i].ptr,
                                    (uint64_t)cands[i].listPtr, cands[i].count, cands[i].entName, cands[i].score);
                            }

                            // Pick best candidate (highest score wins — liveness bonus makes current beat stale)
                            auto& best = cands[0];
                            if (best.rva != *tgt.global || best.score > 200) {
                                *tgt.global = best.rva;
                                VX_LOG("[MgrScan] %s UPDATED: 0x%llX (count=%d, entity='%s', score=%d)\n",
                                    tgt.name, (uint64_t)best.rva, best.count, best.entName, best.score);
                            } else {
                                VX_LOG("[MgrScan] %s keeping 0x%llX (best score=%d)\n",
                                    tgt.name, (uint64_t)*tgt.global, best.score);
                            }
                        } else {
                            VX_LOG("[MgrScan] %s: no valid manager candidates in ±0x%X of 0x%llX\n",
                                tgt.name, (int)RANGE, (uint64_t)center);
                        }
                    }
                    VX_LOG("[MgrScan] === Done ===\n");

                    // After finding correct HeroManager, also scan for LocalPlayer
                    // Read the updated HeroManager to find the current player entity
                    {
                        uintptr_t hmPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::HeroManager);
                        if (hmPtr > 0x10000 && hmPtr < 0x7FFFFFFFFFFF) {
                            uint8_t mgrData[0x18] = {};
                            TargetProcess.Read(hmPtr, mgrData, sizeof(mgrData));
                            uintptr_t listPtr = *(uintptr_t*)(mgrData + 0x8);
                            int32_t count = *(int32_t*)(mgrData + 0x10);
                            if (listPtr > 0x10000 && count > 0 && count < 500) {
                                // Read all entities and find the one with valid Team
                                VX_LOG("[MgrScan] Probing HeroManager entities (count=%d)...\n", count);
                                int validCount = 0;
                                for (int ei = 0; ei < std::min(count, (int32_t)50); ei++) {
                                    uintptr_t entAddr = TargetProcess.Read<uintptr_t>(listPtr + ei * 8);
                                    if (entAddr < 0x10000 || entAddr > 0x7FFFFFFFFFFF) continue;
                                    char eName[33] = {};
                                    TargetProcess.Read(entAddr + 0x3890, eName, 32);
                                    eName[32] = 0;
                                    int team = TargetProcess.Read<int>(entAddr + 0x164);
                                    float hp = TargetProcess.Read<float>(entAddr + 0x938);
                                    float ms = TargetProcess.Read<float>(entAddr + 0xFE4);
                                    float px = TargetProcess.Read<float>(entAddr + 0x168);
                                    float pz = TargetProcess.Read<float>(entAddr + 0x170);
                                    bool nameOk = false;
                                    for (int c = 0; c < 32 && eName[c]; c++) {
                                        if (eName[c] < 0x20 || eName[c] > 0x7E) { nameOk = false; break; }
                                        nameOk = true;
                                    }
                                    if (nameOk) {
                                        VX_LOG("[MgrScan]   entity[%d] 0x%llX '%s' team=%d(byte=%d) hp=%.1f ms=%.0f pos=(%.0f,%.0f)\n",
                                            ei, (uint64_t)entAddr, eName, team, team & 0xFF, hp, ms, px, pz);
                                        if ((team & 0xFF) == 1 || (team & 0xFF) == 2) validCount++;
                                    }
                                }
                                VX_LOG("[MgrScan] HeroManager: %d/%d entities with valid Team\n", validCount, count);

                                // Also probe the alternative manager at 0x1CB8480 to compare
                                {
                                    uintptr_t altRVA = (Offsets::Globals::HeroManager == 0x1CFAA70) ? 0x1CFA9A0 : 0x1CFAA70;
                                    uintptr_t altPtr = TargetProcess.Read<uintptr_t>(g_base + altRVA);
                                    if (altPtr > 0x10000 && altPtr < 0x7FFFFFFFFFFF) {
                                        uint8_t altData[0x18] = {};
                                        TargetProcess.Read(altPtr, altData, sizeof(altData));
                                        uintptr_t altList = *(uintptr_t*)(altData + 0x8);
                                        int32_t altCount = *(int32_t*)(altData + 0x10);
                                        VX_LOG("[MgrScan] Alt manager at 0x%llX: list=0x%llX count=%d\n",
                                            (uint64_t)altRVA, (uint64_t)altList, altCount);
                                        if (altList > 0x10000 && altCount > 0 && altCount < 500) {
                                            int altLive = 0;
                                            for (int ei = 0; ei < std::min(altCount, (int32_t)15); ei++) {
                                                uintptr_t ea = TargetProcess.Read<uintptr_t>(altList + ei * 8);
                                                if (ea < 0x10000 || ea > 0x7FFFFFFFFFFF) continue;
                                                char en[33] = {};
                                                TargetProcess.Read(ea + 0x3890, en, 32); en[32] = 0;
                                                bool ok = false;
                                                for (int c = 0; c < 32 && en[c]; c++) {
                                                    if (en[c] < 0x20 || en[c] > 0x7E) { ok = false; break; }
                                                    ok = true;
                                                }
                                                if (!ok) continue;
                                                float ams = TargetProcess.Read<float>(ea + 0xFE4);
                                                int at = TargetProcess.Read<int>(ea + 0x164);
                                                float ahp = TargetProcess.Read<float>(ea + 0x938);
                                                float apx = TargetProcess.Read<float>(ea + 0x168);
                                                float apz = TargetProcess.Read<float>(ea + 0x170);
                                                if ((at & 0xFF) == 1 || (at & 0xFF) == 2) altLive++;
                                                VX_LOG("[MgrScan]   alt[%d] 0x%llX '%s' team=%d(byte=%d) hp=%.1f ms=%.0f pos=(%.0f,%.0f)\n",
                                                    ei, (uint64_t)ea, en, at, at & 0xFF, ahp, ams, apx, apz);
                                            }
                                            VX_LOG("[MgrScan] Alt manager: %d entities with valid team (of %d probed)\n",
                                                altLive, std::min(altCount, (int32_t)15));
                                        }
                                    }
                                }

                                // Update LocalPlayer: scan near fallback for entity with valid team
                                uintptr_t lpCenter = 0x1CFA7B0;
                                const int64_t lpRange = 0x2000;
                                uintptr_t lpStart = lpCenter > (uintptr_t)lpRange ? lpCenter - lpRange : 0x1B00000;
                                size_t lpLen = (size_t)(lpRange * 2);
                                std::vector<uint8_t> lpChunk(lpLen, 0);
                                TargetProcess.Read(g_base + lpStart, lpChunk.data(), lpLen);

                                struct LpCand { uintptr_t rva; uintptr_t ptr; int team; float hp; int score; char name[33]; };
                                std::vector<LpCand> lpCands;

                                for (size_t off = 0; off + 8 <= lpLen; off += 8) {
                                    uintptr_t val = *(uintptr_t*)(lpChunk.data() + off);
                                    uintptr_t rva = lpStart + off;
                                    if (val < 0x10000 || val > 0x7FFFFFFFFFFF) continue;
                                    if (val >= g_base && val < g_base + moduleSize) continue;

                                    // Check vtable
                                    uintptr_t vt = TargetProcess.Read<uintptr_t>(val);
                                    if (vt < g_base || vt >= g_base + moduleSize) continue;

                                    // Check name
                                    char eName[33] = {};
                                    TargetProcess.Read(val + 0x3890, eName, 32);
                                    eName[32] = 0;
                                    bool nameOk = false;
                                    for (int c = 0; c < 32 && eName[c]; c++) {
                                        if (eName[c] < 0x20 || eName[c] > 0x7E) { nameOk = false; break; }
                                        nameOk = true;
                                    }
                                    if (!nameOk) continue;

                                    int team = TargetProcess.Read<int>(val + 0x164);
                                    float hp = TargetProcess.Read<float>(val + 0x938);

                                    int score = 100;
                                    int64_t d = (int64_t)rva - (int64_t)lpCenter;
                                    if (d < 0) d = -d;
                                    score += std::max(0, (int)(50 - (int)(d / 64)));
                                    if ((team & 0xFF) == 1 || (team & 0xFF) == 2) score += 200;
                                    float ms = TargetProcess.Read<float>(val + 0xFE4);
                                    if (ms > 200.0f && ms < 600.0f) score += 100;

                                    LpCand lc;
                                    lc.rva = rva;
                                    lc.ptr = val;
                                    lc.team = team;
                                    lc.hp = hp;
                                    lc.score = score;
                                    memcpy(lc.name, eName, 33);
                                    lpCands.push_back(lc);
                                }

                                if (!lpCands.empty()) {
                                    std::sort(lpCands.begin(), lpCands.end(), [](const LpCand& a, const LpCand& b) {
                                        return a.score > b.score;
                                    });
                                    VX_LOG("[MgrScan] LocalPlayer candidates:\n");
                                    for (int i = 0; i < std::min((int)lpCands.size(), 5); i++) {
                                        VX_LOG("[MgrScan]   [%d] RVA=0x%llX ptr=0x%llX '%s' team=%d hp=%.1f score=%d\n",
                                            i, (uint64_t)lpCands[i].rva, (uint64_t)lpCands[i].ptr,
                                            lpCands[i].name, lpCands[i].team, lpCands[i].hp, lpCands[i].score);
                                    }
                                    auto& best = lpCands[0];
                                    Offsets::Globals::LocalPlayer = best.rva;
                                    VX_LOG("[MgrScan] LocalPlayer UPDATED: 0x%llX (ptr=0x%llX '%s' team=%d score=%d)\n",
                                        (uint64_t)best.rva, (uint64_t)best.ptr, best.name, best.team, best.score);
                                } else {
                                    VX_LOG("[MgrScan] LocalPlayer: no candidates found\n");
                                }
                            }
                        }
                    }
                }

                // ── Auto SpellBook offset finder ──
                // Scan the local player's spellbook for the slot array and spell data offsets
                {
                    uintptr_t lpAddr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
                    if (lpAddr > 0x10000 && lpAddr < 0x7FFFFFFFFFFF) {
                        VX_LOG("[SpellScan] Scanning spellbook offsets on LocalPlayer 0x%llX...\n", (uint64_t)lpAddr);
                        // Step 1: Verify SpellBook base offset (0x28B8)
                        // Try a range around the known offset
                        uintptr_t bestSBOff = 0;
                        int bestSlotCount = 0;
                        uintptr_t bestSlotsOff = 0;
                        uintptr_t bestDataOff = 0;
                        for (int sbDelta = -0x40; sbDelta <= 0x40; sbDelta += 8) {
                            uintptr_t sbOff = 0x28B8 + sbDelta;
                            uintptr_t sb = lpAddr + sbOff;
                            // Step 2: Find SpellSlots offset within spellbook
                            // Look for 4+ consecutive valid heap pointers (Q/W/E/R slots)
                            for (int slOff = 0xA00; slOff <= 0xB00; slOff += 8) {
                                int consecutive = 0;
                                for (int s = 0; s < 6; s++) {
                                    uintptr_t sp = TargetProcess.Read<uintptr_t>(sb + slOff + s * 8);
                                    if (sp < 0x10000 || sp > 0x7FFFFFFFFFFF) break;
                                    // Check if slot has valid level (0-6 at +0x28)
                                    int lv = TargetProcess.Read<int>(sp + 0x28);
                                    if (lv < 0 || lv > 6) break;
                                    consecutive++;
                                }
                                if (consecutive >= 4 && consecutive > bestSlotCount) {
                                    bestSlotCount = consecutive;
                                    bestSBOff = sbOff;
                                    bestSlotsOff = slOff;
                                }
                            }
                        }
                        if (bestSlotCount >= 4) {
                            VX_LOG("[SpellScan] Found slot array: entity+0x%llX + 0x%llX (%d consecutive slots)\n",
                                (uint64_t)bestSBOff, (uint64_t)bestSlotsOff, bestSlotCount);
                            // Step 3: Find SpellSlotData offset (where spell info ptr is)
                            // Try 0x120-0x160 for the spell info pointer
                            uintptr_t sb = lpAddr + bestSBOff;
                            uintptr_t slot0 = TargetProcess.Read<uintptr_t>(sb + bestSlotsOff);
                            if (slot0 > 0x10000 && slot0 < 0x7FFFFFFFFFFF) {
                                for (int dataOff = 0x100; dataOff <= 0x1A0; dataOff += 8) {
                                    uintptr_t info = TargetProcess.Read<uintptr_t>(slot0 + dataOff);
                                    if (info < 0x10000 || info > 0x7FFFFFFFFFFF) continue;
                                    for (int nameOff = 0x18; nameOff <= 0x40; nameOff += 8) {
                                        uintptr_t np = TargetProcess.Read<uintptr_t>(info + nameOff);
                                        if (np < 0x10000 || np > 0x7FFFFFFFFFFF) continue;
                                        char spName[64] = {};
                                        TargetProcess.Read(np, spName, 63);
                                        bool ok = false;
                                        for (int c = 0; c < 60 && spName[c]; c++) {
                                            if (spName[c] < 0x20 || spName[c] > 0x7E) { ok = false; break; }
                                            ok = true;
                                        }
                                        if (ok && spName[0]) {
                                            VX_LOG("[SpellScan]   slot0 +0x%X -> info+0x%X -> '%s'\n",
                                                dataOff, nameOff, spName);
                                            if (!bestDataOff) bestDataOff = dataOff;
                                            break;
                                        }
                                    }
                                }
                            }
                            // Dump all 6 slots with the found offsets
                            VX_LOG("[SpellScan] Dumping slots with SB=+0x%llX Slots=+0x%llX Data=+0x%llX:\n",
                                (uint64_t)bestSBOff, (uint64_t)bestSlotsOff, (uint64_t)(bestDataOff ? bestDataOff : 0x140));
                            uintptr_t useData = bestDataOff ? bestDataOff : 0x140;
                            for (int s = 0; s < 6; s++) {
                                uintptr_t sp = TargetProcess.Read<uintptr_t>(sb + bestSlotsOff + s * 8);
                                if (sp < 0x10000 || sp > 0x7FFFFFFFFFFF) { VX_LOG("    [%d] null\n", s); continue; }
                                int lv = TargetProcess.Read<int>(sp + 0x28);
                                float rdy = TargetProcess.Read<float>(sp + 0x38);
                                float cd = TargetProcess.Read<float>(sp + 0x74);
                                uintptr_t info = TargetProcess.Read<uintptr_t>(sp + useData);
                                char nm[64] = {};
                                if (info > 0x10000 && info < 0x7FFFFFFFFFFF) {
                                    uintptr_t np = TargetProcess.Read<uintptr_t>(info + 0x28);
                                    if (np > 0x10000 && np < 0x7FFFFFFFFFFF) TargetProcess.Read(np, nm, 63);
                                }
                                VX_LOG("    [%d] lv=%d rdy=%.1f cd=%.1f '%s'\n", s, lv, rdy, cd, nm);
                            }
                            // Apply corrections if offsets differ
                            if (bestSBOff != Offsets::GameObject::SpellBook) {
                                VX_LOG("[SpellScan] *** SpellBook offset CHANGED: 0x%llX -> 0x%llX ***\n",
                                    (uint64_t)Offsets::GameObject::SpellBook, (uint64_t)bestSBOff);
                                Offsets::GameObject::SpellBook = bestSBOff;
                            }
                            if (bestSlotsOff != Offsets::SpellBook::SpellSlots) {
                                VX_LOG("[SpellScan] *** SpellSlots offset CHANGED: 0x%llX -> 0x%llX ***\n",
                                    (uint64_t)Offsets::SpellBook::SpellSlots, (uint64_t)bestSlotsOff);
                                Offsets::SpellBook::SpellSlots = bestSlotsOff;
                            }
                            if (bestDataOff && bestDataOff != Offsets::SpellSlot::SpellSlotData) {
                                VX_LOG("[SpellScan] *** SpellSlotData offset CHANGED: 0x%llX -> 0x%llX ***\n",
                                    (uint64_t)Offsets::SpellSlot::SpellSlotData, (uint64_t)bestDataOff);
                                Offsets::SpellSlot::SpellSlotData = bestDataOff;
                            }
                        } else {
                            VX_LOG("[SpellScan] No slot array found in spellbook region\n");
                        }
                    }
                }

            } else {
                VX_LOG("[SigScan] GameTime re-scan found nothing\n");
            }
        }
        if (g_gameTime < 1.0f) return;
    }

    if (!g_viewPortScanned) SigScanViewPort();

    // ── Deferred full SpellBook re-scan (runs once when game is live) ──
    // This is the ONLY spell offset scanner that matters — it runs with live game data.
    // The initial SpellScan inside GameTime re-find may or may not have run depending
    // on whether we attached mid-game or from lobby.
    static bool g_spellDataRescanDone = false;
    if (!g_spellDataRescanDone && g_gameTime > 5.0f) {
        g_spellDataRescanDone = true;
        uintptr_t lpAddr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
        if (lpAddr > 0x10000 && lpAddr < 0x7FFFFFFFFFFF) {
            VX_LOG("[SpellRescan] Full deferred scan on LocalPlayer 0x%llX (GT=%.1f)\n", (uint64_t)lpAddr, g_gameTime);

            // Spell name validator: real spell names are PascalCase alpha-only strings
            // e.g. "SummonerFlash", "KogMawBioArcaneBarrage", "Recall"
            // Rejects: "kColorFactor" (lowercase k prefix), "m_data" (underscore),
            //          "0x1234" (hex), short junk, non-alpha chars
            auto isSpellName = [](const char* s, int len) -> bool {
                if (len < 4 || len > 50) return false;
                if (s[0] < 'A' || s[0] > 'Z') return false;
                int alphaCount = 0;
                int upperCount = 0;
                for (int i = 0; i < len; i++) {
                    char c = s[i];
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                        alphaCount++;
                        if (c >= 'A' && c <= 'Z') upperCount++;
                    } else {
                        return false;
                    }
                }
                if (alphaCount < 4) return false;
                if (upperCount < 1) return false;
                return true;
            };

            // Step 1: Find SpellBook base + SpellSlots array
            // Score candidates: 6 consecutive is better, summoner slots (4,5) at level=1 is a strong signal
            uintptr_t bestSBOff = 0;
            int bestScore = 0;
            uintptr_t bestSlotsOff = 0;
            for (int sbDelta = -0x60; sbDelta <= 0x60; sbDelta += 8) {
                uintptr_t sbOff = 0x28B8 + sbDelta;
                uintptr_t sbAddr = lpAddr + sbOff;
                for (int slOff = 0xA00; slOff <= 0xB40; slOff += 8) {
                    uintptr_t firstSlot = TargetProcess.Read<uintptr_t>(sbAddr + slOff);
                    if (firstSlot < 0x10000 || firstSlot > 0x7FFFFFFFFFFF) continue;
                    int consecutive = 0;
                    bool summonerLevelsOk = true;
                    int abilityWithLevel = 0;
                    for (int s = 0; s < 6; s++) {
                        uintptr_t sp = TargetProcess.Read<uintptr_t>(sbAddr + slOff + s * 8);
                        if (sp < 0x10000 || sp > 0x7FFFFFFFFFFF) break;
                        int lv = TargetProcess.Read<int>(sp + 0x28);
                        if (lv < 0 || lv > 6) break;
                        consecutive++;
                        if (s == 4 || s == 5) {
                            if (lv != 1) summonerLevelsOk = false;
                        }
                        if (s < 4 && lv >= 1) abilityWithLevel++;
                    }
                    if (consecutive < 6) continue;
                    int score = consecutive;
                    if (summonerLevelsOk) score += 50;
                    if (abilityWithLevel >= 2) score += 20;
                    if (score > bestScore) {
                        bestScore = score;
                        bestSBOff = sbOff;
                        bestSlotsOff = slOff;
                    }
                }
            }
            if (bestScore >= 6) {
                VX_LOG("[SpellRescan] Found slot array: entity+0x%llX + 0x%llX (score=%d)\n",
                    (uint64_t)bestSBOff, (uint64_t)bestSlotsOff, bestScore);
                if (bestSBOff != Offsets::GameObject::SpellBook) {
                    VX_LOG("[SpellRescan] *** SpellBook CHANGED: 0x%llX -> 0x%llX ***\n",
                        (uint64_t)Offsets::GameObject::SpellBook, (uint64_t)bestSBOff);
                    Offsets::GameObject::SpellBook = bestSBOff;
                }
                if (bestSlotsOff != Offsets::SpellBook::SpellSlots) {
                    VX_LOG("[SpellRescan] *** SpellSlots CHANGED: 0x%llX -> 0x%llX ***\n",
                        (uint64_t)Offsets::SpellBook::SpellSlots, (uint64_t)bestSlotsOff);
                    Offsets::SpellBook::SpellSlots = bestSlotsOff;
                }

                // Step 2: Detect encrypted SpellSlot (patch 26.21+) and find name chain
                // Riot added vtable-based encryption: slot→vtable→getContainer→container+0x78→ROR64→SpellSlotData
                // Over DMA we parse the getContainer function bytes to find the container offset
                uintptr_t sb = lpAddr + bestSBOff;
                bool found = false;

                // Helper: read spell name from SpellSlotData ptr + nameOff
                auto readNameFromInfo = [&](uintptr_t infoPtr, int nameOff, char* outName, int outSize) -> int {
                    return ReadSpellNameSSO(infoPtr, (uintptr_t)nameOff, outName, outSize);
                };

                // --- Try encrypted path first ---
                // Read vtable from slot 4 (summoner D), parse getContainer to find container offset
                uintptr_t testSlot4 = TargetProcess.Read<uintptr_t>(sb + bestSlotsOff + 4 * 8);
                if (testSlot4 > 0x10000 && testSlot4 < 0x7FFFFFFFFFFF) {
                    uintptr_t vtable = TargetProcess.Read<uintptr_t>(testSlot4);
                    if (vtable > 0x10000 && vtable < 0x7FFFFFFFFFFF) {
                        // getContainer is vtable[3] (vtable + 0x18)
                        uintptr_t getContainerAddr = TargetProcess.Read<uintptr_t>(vtable + 0x18);
                        if (getContainerAddr > 0x10000 && getContainerAddr < 0x7FFFFFFFFFFF) {
                            // Read 64 bytes of function to parse full decryption algorithm
                            uint8_t fb[64] = {};
                            TargetProcess.Read(getContainerAddr, fb, 64);
                            VX_LOG("[SpellRescan] getContainer @0x%llX bytes:\n", (uint64_t)getContainerAddr);
                            VX_LOG("  +00: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                fb[0],fb[1],fb[2],fb[3],fb[4],fb[5],fb[6],fb[7],
                                fb[8],fb[9],fb[10],fb[11],fb[12],fb[13],fb[14],fb[15]);
                            VX_LOG("  +10: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                fb[16],fb[17],fb[18],fb[19],fb[20],fb[21],fb[22],fb[23],
                                fb[24],fb[25],fb[26],fb[27],fb[28],fb[29],fb[30],fb[31]);
                            VX_LOG("  +20: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                fb[32],fb[33],fb[34],fb[35],fb[36],fb[37],fb[38],fb[39],
                                fb[40],fb[41],fb[42],fb[43],fb[44],fb[45],fb[46],fb[47]);
                            VX_LOG("  +30: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                fb[48],fb[49],fb[50],fb[51],fb[52],fb[53],fb[54],fb[55],
                                fb[56],fb[57],fb[58],fb[59],fb[60],fb[61],fb[62],fb[63]);

                            int cOff = -1;
                            bool isLEA = false;
                            // 48 8B 41 XX = mov rax, [rcx+byte]
                            if (fb[0] == 0x48 && fb[1] == 0x8B && fb[2] == 0x41) {
                                cOff = (int)(int8_t)fb[3];
                            }
                            // 48 8B 81 XX XX XX XX = mov rax, [rcx+dword]
                            else if (fb[0] == 0x48 && fb[1] == 0x8B && fb[2] == 0x81) {
                                cOff = *(int32_t*)&fb[3];
                            }
                            // 48 8D 41 XX = lea rax, [rcx+byte]
                            else if (fb[0] == 0x48 && fb[1] == 0x8D && fb[2] == 0x41) {
                                cOff = (int)(int8_t)fb[3]; isLEA = true;
                            }
                            // 48 8D 81 XX XX XX XX = lea rax, [rcx+dword]
                            else if (fb[0] == 0x48 && fb[1] == 0x8D && fb[2] == 0x81) {
                                cOff = *(int32_t*)&fb[3]; isLEA = true;
                            }

                            if (cOff >= 0) {
                                VX_LOG("[SpellRescan] Parsed data offset: %s [rcx+0x%X]\n", isLEA ? "LEA" : "MOV", cOff);

                                // Extract the decryption constant from the function bytes
                                // After MOV RAX,[RCX+off] (7 bytes), look for MOV RCX, imm64 (48 B9 xx xx xx xx xx xx xx xx)
                                uint64_t funcConstant = 0;
                                bool hasConstant = false;
                                int parsePos = (fb[2] == 0x41 || fb[2] == 0x8D) ? 4 : 7; // byte or dword offset
                                if (fb[2] == 0x81 || fb[2] == 0x8D) parsePos = 7; // dword variants
                                if (fb[2] == 0x41) parsePos = 4; // byte variants

                                // Scan remaining bytes for MOV RCX, imm64 (48 B9) or MOV RDX, imm64 (48 BA)
                                for (int pi = parsePos; pi < 54; pi++) {
                                    if (fb[pi] == 0x48 && (fb[pi+1] == 0xB9 || fb[pi+1] == 0xBA)) {
                                        funcConstant = *(uint64_t*)&fb[pi+2];
                                        hasConstant = true;
                                        VX_LOG("[SpellRescan] Found constant in function: 0x%llX (at +%d)\n",
                                            funcConstant, pi);
                                        break;
                                    }
                                }

                                // Read the encrypted value directly from the slot
                                uint64_t encVal = TargetProcess.Read<uint64_t>(testSlot4 + cOff);
                                VX_LOG("[SpellRescan] slot4=0x%llX encVal=0x%llX (from slot4+0x%X)\n",
                                    (uint64_t)testSlot4, encVal, cOff);

                                // Also scan for ROR/ROL instruction to find shift amount
                                int funcShift = -1;
                                for (int pi = parsePos; pi < 60; pi++) {
                                    // 48 C1 C8 XX = ror rax, imm8
                                    if (fb[pi] == 0x48 && fb[pi+1] == 0xC1 && fb[pi+2] == 0xC8) {
                                        funcShift = (int)fb[pi+3];
                                        VX_LOG("[SpellRescan] Found ROR shift in function: %d (at +%d)\n", funcShift, pi);
                                        break;
                                    }
                                    // 48 C1 C0 XX = rol rax, imm8
                                    if (fb[pi] == 0x48 && fb[pi+1] == 0xC1 && fb[pi+2] == 0xC0) {
                                        funcShift = -(int)fb[pi+3]; // negative = ROL
                                        VX_LOG("[SpellRescan] Found ROL shift in function: %d (at +%d)\n", -funcShift, pi);
                                        break;
                                    }
                                }

                                // --- Try decrypting directly: encVal is the encrypted SpellSlotData ---
                                // Build set of constants to try (function constant + forum constant)
                                uint64_t tryConstants[3];
                                int numConst = 0;
                                if (hasConstant) tryConstants[numConst++] = funcConstant;
                                tryConstants[numConst++] = 0x80104765ULL; // forum post constant
                                if (hasConstant && funcConstant != 0x80104765ULL) tryConstants[numConst++] = 0;

                                // --- Parse full 3-step chain from function bytes ---
                                // Look for: SUB (48 2B C1), XOR (48 33 C1), and second constant
                                // Second constant can be MOV ECX,imm32 (B9 xx xx xx xx) WITHOUT 48 REX prefix
                                bool hasSUB = false, hasXOR = false;
                                uint64_t funcConstant2 = 0;
                                bool hasConstant2 = false;
                                for (int pi = parsePos; pi < 60; pi++) {
                                    if (pi + 2 < 64 && fb[pi] == 0x48 && fb[pi+1] == 0x2B && fb[pi+2] == 0xC1) {
                                        hasSUB = true;
                                        VX_LOG("[SpellRescan] Found SUB RAX,RCX at +%d\n", pi);
                                    }
                                    if (pi + 2 < 64 && fb[pi] == 0x48 && fb[pi+1] == 0x33 && fb[pi+2] == 0xC1) {
                                        hasXOR = true;
                                        VX_LOG("[SpellRescan] Found XOR RAX,RCX at +%d\n", pi);
                                    }
                                    // MOV ECX, imm32 (B9 without 48 REX prefix) = second constant
                                    if (fb[pi] == 0xB9 && (pi == 0 || fb[pi-1] != 0x48)) {
                                        if (pi + 4 < 64) {
                                            funcConstant2 = *(uint32_t*)&fb[pi+1];
                                            hasConstant2 = true;
                                            VX_LOG("[SpellRescan] Found constant2 (MOV ECX,imm32): 0x%llX at +%d\n",
                                                funcConstant2, pi);
                                        }
                                    }
                                }

                                // Try the full 3-step chain: SUB const1 -> ROR shift -> XOR const2
                                if (hasConstant && hasSUB && hasXOR && funcShift > 0) {
                                    uint64_t c1 = funcConstant;
                                    uint64_t c2 = hasConstant2 ? funcConstant2 : 0;
                                    VX_LOG("[SpellRescan] Trying 3-step chain: SUB 0x%llX -> ROR %d -> XOR 0x%llX\n",
                                        c1, funcShift, c2);
                                    uint64_t temp = encVal - c1;
                                    temp = ROR64(temp, funcShift);
                                    temp ^= c2;
                                    uintptr_t sdr = (uintptr_t)temp;
                                    VX_LOG("[SpellRescan] 3-step result: 0x%llX\n", (uint64_t)sdr);
                                    if (sdr > 0x10000 && sdr < 0x7FFFFFFFFFFF) {
                                        for (int nameOff = 0x08; nameOff <= 0x80 && !found; nameOff += 8) {
                                            char spName[64] = {};
                                            int len = readNameFromInfo(sdr, nameOff, spName, 64);
                                            if (len >= 4 && isSpellName(spName, len)) {
                                                VX_LOG("[SpellRescan] *** 3-STEP DECRYPTED! SUB 0x%llX ROR %d XOR 0x%llX -> 0x%llX -> +0x%X -> '%s' ***\n",
                                                    c1, funcShift, c2, (uint64_t)sdr, nameOff, spName);
                                                g_spellSlotEncrypted = true;
                                                g_spellEncDataOff = cOff;
                                                g_spellEncConst1 = c1;
                                                g_spellEncShift = funcShift;
                                                g_spellEncConst2 = c2;
                                                Offsets::SpellSlot::SpellInfoNamePtr = nameOff;
                                                found = true;
                                            }
                                        }
                                    }
                                }

                                // Fallback: try single-op brute force if 3-step didn't work
                                if (!found && hasConstant) {
                                    VX_LOG("[SpellRescan] 3-step failed, trying single-op fallback...\n");
                                    uint64_t tryC[] = { funcConstant };
                                    const char* opNames[] = { "ADD", "XOR", "SUB" };
                                    for (int op = 0; op < 3 && !found; op++) {
                                        uint64_t transformed;
                                        if (op == 0) transformed = encVal + tryC[0];
                                        else if (op == 1) transformed = encVal ^ tryC[0];
                                        else transformed = encVal - tryC[0];
                                        int shS = (funcShift > 0) ? funcShift : 1;
                                        int shE = (funcShift > 0) ? funcShift : 63;
                                        for (int sh = shS; sh <= shE && !found; sh++) {
                                            uintptr_t sdr = (uintptr_t)ROR64(transformed, sh);
                                            if (sdr < 0x10000 || sdr > 0x7FFFFFFFFFFF) continue;
                                            for (int nameOff = 0x08; nameOff <= 0x80 && !found; nameOff += 8) {
                                                char spName[64] = {};
                                                int len = readNameFromInfo(sdr, nameOff, spName, 64);
                                                if (len >= 8 && strncmp(spName, "Summoner", 8) == 0 && isSpellName(spName, len)) {
                                                    VX_LOG("[SpellRescan] *** DECRYPTED (single-op)! %s(enc, 0x%llX) ROR %d -> '%s' ***\n",
                                                        opNames[op], tryC[0], sh, spName);
                                                    g_spellSlotEncrypted = true;
                                                    g_spellEncDataOff = cOff;
                                                    g_spellEncConst1 = (op == 2) ? tryC[0] : 0;
                                                    g_spellEncShift = sh;
                                                    g_spellEncConst2 = (op == 1) ? tryC[0] : 0;
                                                    Offsets::SpellSlot::SpellInfoNamePtr = nameOff;
                                                    found = true;
                                                }
                                            }
                                        }
                                    }
                                }

                                if (!found) {
                                    VX_LOG("[SpellRescan] Direct decrypt failed. Trying slot raw scan...\n");
                                    int rawHits = 0;
                                    for (int rawOff = 0x00; rawOff <= 0x300 && rawHits < 5; rawOff += 8) {
                                        uintptr_t maybePtr = TargetProcess.Read<uintptr_t>(testSlot4 + rawOff);
                                        if (maybePtr < 0x10000 || maybePtr > 0x7FFFFFFFFFFF) continue;
                                        for (int nameOff = 0x08; nameOff <= 0x80; nameOff += 8) {
                                            char spName[64] = {};
                                            int len = readNameFromInfo(maybePtr, nameOff, spName, 64);
                                            if (len >= 4 && isSpellName(spName, len)) {
                                                VX_LOG("[SpellRescan] RAW HIT: slot4+0x%X -> ptr 0x%llX -> +0x%X -> '%s'\n",
                                                    rawOff, (uint64_t)maybePtr, nameOff, spName);
                                                rawHits++;
                                                if (len >= 8 && strncmp(spName, "Summoner", 8) == 0) {
                                                    VX_LOG("[SpellRescan] *** SUMMONER VIA RAW: dataOff=0x%X nameOff=0x%X ***\n",
                                                        rawOff, nameOff);
                                                    Offsets::SpellSlot::SpellSlotData = rawOff;
                                                    Offsets::SpellSlot::SpellInfoNamePtr = nameOff;
                                                    g_spellSlotEncrypted = false;
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // --- Fallback: unencrypted path (old patches) ---
                if (!found) {
                    VX_LOG("[SpellRescan] Trying unencrypted path (wide scan)...\n");
                    auto tryNameChain = [&](uintptr_t slotPtr, int dataOff, int nameOff, char* outName, int outSize) -> int {
                        uintptr_t info = TargetProcess.Read<uintptr_t>(slotPtr + dataOff);
                        if (info < 0x10000 || info > 0x7FFFFFFFFFFF) return 0;
                        return readNameFromInfo(info, nameOff, outName, outSize);
                    };
                    for (int trySlot = 4; trySlot <= 5 && !found; trySlot++) {
                        uintptr_t testSlot = TargetProcess.Read<uintptr_t>(sb + bestSlotsOff + trySlot * 8);
                        if (testSlot < 0x10000 || testSlot > 0x7FFFFFFFFFFF) continue;
                        for (int dataOff = 0x60; dataOff <= 0x300 && !found; dataOff += 8) {
                            for (int nameOff = 0x08; nameOff <= 0x80 && !found; nameOff += 8) {
                                char spName[64] = {};
                                int len = tryNameChain(testSlot, dataOff, nameOff, spName, 64);
                                if (len >= 8 && strncmp(spName, "Summoner", 8) == 0 && isSpellName(spName, len)) {
                                    VX_LOG("[SpellRescan] UNENCRYPTED MATCH: slot%d+0x%X -> info+0x%X -> '%s'\n",
                                        trySlot, dataOff, nameOff, spName);
                                    Offsets::SpellSlot::SpellSlotData = dataOff;
                                    Offsets::SpellSlot::SpellInfoNamePtr = nameOff;
                                    g_spellSlotEncrypted = false;
                                    found = true;
                                }
                            }
                        }
                    }
                }
                if (!found) {
                    VX_LOG("[SpellRescan] Name chain not found (encrypted or unencrypted).\n");
                }
                // Dump all 6 slots with final offsets
                VX_LOG("[SpellRescan] Final dump (SB=+0x%llX Slots=+0x%llX encrypted=%d dataOff=0x%X const1=0x%llX shift=%d const2=0x%llX NamePtr=+0x%llX):\n",
                    (uint64_t)Offsets::GameObject::SpellBook,
                    (uint64_t)Offsets::SpellBook::SpellSlots,
                    g_spellSlotEncrypted, g_spellEncDataOff,
                    g_spellEncConst1, g_spellEncShift, g_spellEncConst2,
                    (uint64_t)Offsets::SpellSlot::SpellInfoNamePtr);
                for (int s = 0; s < 6; s++) {
                    uintptr_t sp = TargetProcess.Read<uintptr_t>(sb + bestSlotsOff + s * 8);
                    if (sp < 0x10000 || sp > 0x7FFFFFFFFFFF) { VX_LOG("    [%d] null\n", s); continue; }
                    int lv = TargetProcess.Read<int>(sp + 0x28);
                    float rdy = TargetProcess.Read<float>(sp + 0x38);
                    uintptr_t info = ReadSpellSlotData(sp);
                    char nm[64] = {};
                    if (info) {
                        ReadSpellNameSSO(info, Offsets::SpellSlot::SpellInfoNamePtr, nm, 64);
                    }
                    VX_LOG("    [%d] lv=%d rdy=%.1f '%s'\n", s, lv, rdy, nm);
                }
                if (!found) VX_LOG("[SpellRescan] WARNING: Could not find spell name chain!\n");
            } else {
                VX_LOG("[SpellRescan] No slot array found in spellbook region!\n");
            }
        }
        // Re-probe readyAt/totalCD now that SpellBook/SpellSlots offsets are correct
        VX_LOG("[SpellRescan] Re-probing readyAt with updated offsets...\n");
        ProbeReadyAtOffset();
    }

    static int g_vpMethod = -1;
    static bool g_vpFound = false;

    auto isValidMatrix = [](const Matrix4x4& t) -> bool {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (std::isnan(t.m[i][j]) || std::isinf(t.m[i][j]) || fabsf(t.m[i][j]) > 50000.f) return false;
        if (t.m[0][0] == 0.f && t.m[1][1] == 0.f && t.m[2][2] == 0.f) return false;
        if (fabsf(t.m[0][0]) > 50.f || fabsf(t.m[1][1]) > 50.f) return false;
        return true;
    };

    auto multiplyViewProj = [](const Matrix4x4& v, const Matrix4x4& p, Matrix4x4& out) {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                out.m[i][j] = v.m[i][0]*p.m[0][j] + v.m[i][1]*p.m[1][j]
                             + v.m[i][2]*p.m[2][j] + v.m[i][3]*p.m[3][j];
    };

    auto tryMethod = [&](int method) -> bool {
        Matrix4x4 test{};
        if (method <= 3 && g_scannedVPMatrix) {
            // 0: View(+0)*Proj(+0x40)  1: direct +0x80  2: direct +0x40  3: direct +0
            uintptr_t addr = g_base + g_scannedVPMatrix;
            if (method == 0) {
                Matrix4x4 viewMat{}, projMat{};
                TargetProcess.Read(addr, &viewMat, sizeof(Matrix4x4));
                TargetProcess.Read(addr + 0x40, &projMat, sizeof(Matrix4x4));
                multiplyViewProj(viewMat, projMat, test);
            } else if (method == 1) { TargetProcess.Read(addr + 0x80, &test, sizeof(Matrix4x4));
            } else if (method == 2) { TargetProcess.Read(addr + 0x40, &test, sizeof(Matrix4x4));
            } else { TargetProcess.Read(addr, &test, sizeof(Matrix4x4)); }
        } else {
            int base = g_scannedVPMatrix ? 4 : 0;
            int vpIdx = (method - base) / 2;
            bool multiply = ((method - base) % 2) == 0;
            const uintptr_t vpRVAs[] = { g_scannedViewPort, Offsets::Globals::ViewPort };
            if (vpIdx >= 2 || !vpRVAs[vpIdx]) return false;
            uintptr_t vp = TargetProcess.Read<uintptr_t>(g_base + vpRVAs[vpIdx]);
            if (!vp) return false;
            if (multiply) {
                Matrix4x4 viewMat{}, projMat{};
                TargetProcess.Read(vp + Offsets::ViewPort::ViewMatrix, &viewMat, sizeof(Matrix4x4));
                TargetProcess.Read(vp + Offsets::ViewPort::ProjMatrix, &projMat, sizeof(Matrix4x4));
                multiplyViewProj(viewMat, projMat, test);
            } else {
                TargetProcess.Read(vp + Offsets::ViewPort::ProjMatrix + 0x40, &test, sizeof(Matrix4x4));
            }
        }
        if (!isValidMatrix(test)) return false;
        g_viewProjMatrix = test;
        return true;
    };

    int maxMethods = (g_scannedVPMatrix ? 4 : 0) + (g_scannedViewPort ? 2 : 0) + 2;
    if (g_vpFound) {
        tryMethod(g_vpMethod);
    } else {
        const char* names[] = {
            "Sig View*Proj", "Sig +0x80", "Sig +0x40", "Sig +0x00",
            "SigVP V*P", "SigVP precomp", "Hardcoded V*P", "Hardcoded precomp",
            "LadyDarknes V*P", "LadyDarknes precomp"
        };
        for (int m = 0; m < maxMethods; m++) {
            if (tryMethod(m)) {
                g_vpMethod = m;
                g_vpFound = true;
                VX_LOG("[ViewProj] Method %d: %s  diag=(%.3f, %.3f, %.3f, %.3f)\n",
                    m, names[m], g_viewProjMatrix.m[0][0], g_viewProjMatrix.m[1][1],
                    g_viewProjMatrix.m[2][2], g_viewProjMatrix.m[3][3]);
                break;
            }
        }
        if (!g_vpFound) {
            static bool once = false;
            if (!once) {
                once = true;
                VX_LOG("[ViewProj] ALL methods failed. Diagnostics:\n");
                if (g_scannedVPMatrix) {
                    float diag[4][4];
                    for (int s = 0; s < 4; s++) {
                        Matrix4x4 tmp{};
                        TargetProcess.Read(g_base + g_scannedVPMatrix + s * 0x40, &tmp, sizeof(Matrix4x4));
                        VX_LOG("  VPMatrix+0x%X diag=(%.3f, %.3f, %.3f, %.3f)\n",
                            s*0x40, tmp.m[0][0], tmp.m[1][1], tmp.m[2][2], tmp.m[3][3]);
                    }
                }
                uintptr_t vp = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::ViewPort);
                VX_LOG("  Hardcoded VP ptr = 0x%llX\n", (uint64_t)vp);
                if (vp) {
                    for (uintptr_t off = 0x100; off <= 0x300; off += 0x40) {
                        Matrix4x4 tmp{};
                        TargetProcess.Read(vp + off, &tmp, sizeof(Matrix4x4));
                        VX_LOG("  VP+0x%llX diag=(%.3f, %.3f, %.3f, %.3f)\n",
                            (uint64_t)off, tmp.m[0][0], tmp.m[1][1], tmp.m[2][2], tmp.m[3][3]);
                    }
                }
            }
        }
    }

    // local player scatter read
    uintptr_t localPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
    if (localPtr) {
        g_localAddr = localPtr;
        auto ls = TargetProcess.CreateScatterHandle();
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Team,           &g_localTeam,        sizeof(uint8_t));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Position,       &g_localPos,         sizeof(Vec3));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::AttackRange,    &g_localAttackRange, sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::BoundingRadius, &g_localBoundingRadius, sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::BaseAD,         &g_localBaseAD,      sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::BonusAD,        &g_localBonusAD,     sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::AttackSpeedMod,   &g_localASMod,       sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::AttackSpeedScale, &g_localASScale,     sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Level,          &g_localLevel,       sizeof(int32_t));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Mana,           &g_localMana,        sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::MaxMana,        &g_localMaxMana,     sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Health,         &g_localHealth,      sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::MaxHealth,      &g_localMaxHealth,   sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Name,           &g_localChampName,   63);
        TargetProcess.ExecuteReadScatter(ls);
        TargetProcess.CloseScatterHandle(ls);
        g_localChampName[63] = 0;
        if (g_localAttackRange < 50.f || g_localAttackRange > 1500.f) g_localAttackRange = 550.f;
        if (g_localBoundingRadius < 10.f || g_localBoundingRadius > 200.f) g_localBoundingRadius = 65.f;

        if (g_orbPingComp) {
            uintptr_t netPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::NetClient);
            if (netPtr > 0x10000 && netPtr < 0x7FFFFFFFFFFF) {
                float rawPing = TargetProcess.Read<float>(netPtr + 0x3C);
                if (rawPing >= 0.f && rawPing < 2000.f) g_orbPingMs = rawPing;
            }
        }

        if (g_orbReadCooldowns) {

            uintptr_t spellBookBase = localPtr + Offsets::GameObject::SpellBook;

            // SCATTER: batch-read all 4 QWER slot pointers + activeCastPtr
            uintptr_t localSlotPtrs[4] = {};
            uintptr_t activeCastPtr = 0;
            {
                auto sc = TargetProcess.CreateScatterHandle();
                for (int s = 0; s < 4; s++)
                    TargetProcess.AddScatterReadRequest(sc, spellBookBase + Offsets::SpellBook::SpellSlots + s * 0x8, &localSlotPtrs[s], sizeof(uintptr_t));
                TargetProcess.AddScatterReadRequest(sc, spellBookBase + Offsets::SpellBook::ActiveSpellCast, &activeCastPtr, sizeof(uintptr_t));
                TargetProcess.ExecuteReadScatter(sc);
                TargetProcess.CloseScatterHandle(sc);
            }

            // SCATTER: batch-read level + readyAt + totalCD + SpellSlotData from all 4 slots + active cast data
            struct LocalSlotData { int level; float readyAt; float totalCD; uintptr_t spellSlotData; };
            LocalSlotData lsd[4] = {};
            float castEnd = 0.f, attackEnd = 0.f;
            uintptr_t castInfoPtr = 0;
            {
                auto sc = TargetProcess.CreateScatterHandle();
                for (int s = 0; s < 4; s++) {
                    if (localSlotPtrs[s] < 0x10000 || localSlotPtrs[s] > 0x7FFFFFFFFFFF) continue;
                    TargetProcess.AddScatterReadRequest(sc, localSlotPtrs[s] + 0x28, &lsd[s].level, sizeof(int));
                    TargetProcess.AddScatterReadRequest(sc, localSlotPtrs[s] + Offsets::SpellSlot::ReadyAt, &lsd[s].readyAt, sizeof(float));
                    TargetProcess.AddScatterReadRequest(sc, localSlotPtrs[s] + Offsets::SpellSlot::TotalCD, &lsd[s].totalCD, sizeof(float));
                    uintptr_t dataOff = g_spellSlotEncrypted ? g_spellEncDataOff : Offsets::SpellSlot::SpellSlotData;
                    if (dataOff > 0)
                        TargetProcess.AddScatterReadRequest(sc, localSlotPtrs[s] + dataOff, &lsd[s].spellSlotData, sizeof(uintptr_t));
                }
                if (activeCastPtr > 0x10000 && activeCastPtr < 0x7FFFFFFFFFFF) {
                    TargetProcess.AddScatterReadRequest(sc, activeCastPtr + 0x1A4, &castEnd, sizeof(float));
                    TargetProcess.AddScatterReadRequest(sc, activeCastPtr + 0x1A8, &attackEnd, sizeof(float));
                    TargetProcess.AddScatterReadRequest(sc, activeCastPtr + 0x08, &castInfoPtr, sizeof(uintptr_t));
                }
                TargetProcess.ExecuteReadScatter(sc);
                TargetProcess.CloseScatterHandle(sc);
            }

            // SCATTER: batch-read spell names + castRange from SpellSlotData + isAuto from castInfo
            static char localSpellNameCache[4][64] = {};
            static bool localSpellNameCached[4] = {};
            struct SpellNameSSO { uint8_t data[32]; };
            SpellNameSSO spellSSO[4] = {};
            float spellRange[4] = {};
            uint8_t isAutoRaw = 0;
            {
                auto sc = TargetProcess.CreateScatterHandle();
                for (int s = 0; s < 4; s++) {
                    if (localSlotPtrs[s] < 0x10000 || localSlotPtrs[s] > 0x7FFFFFFFFFFF) continue;
                    uintptr_t infoPtr = 0;
                    if (g_spellSlotEncrypted && lsd[s].spellSlotData) {
                        uint64_t temp = lsd[s].spellSlotData - g_spellEncConst1;
                        if (g_spellEncShift > 0) temp = ROR64(temp, g_spellEncShift);
                        temp ^= g_spellEncConst2;
                        infoPtr = (uintptr_t)temp;
                    } else {
                        infoPtr = lsd[s].spellSlotData;
                    }
                    if (infoPtr < 0x10000 || infoPtr > 0x7FFFFFFFFFFF) continue;
                    if (!localSpellNameCached[s])
                        TargetProcess.AddScatterReadRequest(sc, infoPtr + Offsets::SpellSlot::SpellInfoNamePtr, spellSSO[s].data, 32);
                    TargetProcess.AddScatterReadRequest(sc, infoPtr + Offsets::SpellData::CastRange, &spellRange[s], sizeof(float));
                }
                if (castInfoPtr > 0x10000 && castInfoPtr < 0x7FFFFFFFFFFF)
                    TargetProcess.AddScatterReadRequest(sc, castInfoPtr + 0x113, &isAutoRaw, sizeof(uint8_t));
                TargetProcess.ExecuteReadScatter(sc);
                TargetProcess.CloseScatterHandle(sc);
            }

            // Populate local spells from scattered data
            for (int s = 0; s < 4; s++) {
                if (localSlotPtrs[s] < 0x10000 || localSlotPtrs[s] > 0x7FFFFFFFFFFF) { g_spells[s] = {}; continue; }
                g_spells[s].level   = lsd[s].level;
                g_spells[s].readyAt = lsd[s].readyAt;
                g_spells[s].totalCD = lsd[s].totalCD;
                g_spells[s].ready   = (lsd[s].level > 0 && lsd[s].readyAt <= g_gameTime);
                g_spells[s].range   = (spellRange[s] > 0.f && spellRange[s] < 10000.f) ? spellRange[s] : 0.f;
                if (localSpellNameCached[s]) {
                    strncpy(g_spells[s].name, localSpellNameCache[s], 63);
                } else {
                    uint64_t cap = *(uint64_t*)(spellSSO[s].data + 24);
                    if (cap <= 15) {
                        int len = 0;
                        while (len < 15 && spellSSO[s].data[len] >= 0x20 && spellSSO[s].data[len] <= 0x7E) len++;
                        memcpy(g_spells[s].name, spellSSO[s].data, len);
                        g_spells[s].name[len] = 0;
                    } else {
                        uintptr_t np = *(uintptr_t*)(spellSSO[s].data);
                        if (np > 0x10000 && np < 0x7FFFFFFFFFFF) {
                            TargetProcess.Read(np, g_spells[s].name, 63);
                            int len = 0;
                            while (len < 63 && g_spells[s].name[len] >= 0x20 && g_spells[s].name[len] <= 0x7E) len++;
                            g_spells[s].name[len] = 0;
                        }
                    }
                    if (g_spells[s].name[0]) {
                        strncpy(localSpellNameCache[s], g_spells[s].name, 63);
                        localSpellNameCached[s] = true;
                    }
                }
                g_spells[s].name[63] = 0;
            }

            g_activeCastEnd = castEnd;
            g_activeAttackEnd = attackEnd;
            g_activeIsAuto = (isAutoRaw != 0);
        }
    }

    std::vector<uintptr_t> ptrs;

    // Try flat hero list first (direct entity pointer array at manager)
    // CACHED: heroes never change mid-game, only rescan every 120 frames (~2s)
    static std::vector<uintptr_t> s_cachedHeroPtrs;
    static int s_heroRescanCounter = 0;
    if (g_heroListRVA != 0) {
        bool needRescan = s_cachedHeroPtrs.empty() || (++s_heroRescanCounter >= 120);
        if (!needRescan) {
            ptrs = s_cachedHeroPtrs;
        } else {
            s_heroRescanCounter = 0;
            uintptr_t mgrAddr = TargetProcess.Read<uintptr_t>(g_base + g_heroListRVA);
            if (mgrAddr > 0x100000000ULL && mgrAddr < 0x7FFFFFFFFFFF) {
                g_heroListMgrAddr = mgrAddr;
                uint8_t flat[160] = {};
                TargetProcess.Read(mgrAddr, flat, sizeof(flat));

                // Collect valid entity pointers from flat array
                uintptr_t candidates[20] = {};
                int candCount = 0;
                for (int slot = 0; slot < 20; slot++) {
                    uintptr_t ep = *(uintptr_t*)(flat + slot * 8);
                    if (ep >= 0x100000000ULL && ep <= 0x7FFFFFFFFFFF)
                        candidates[candCount++] = ep;
                }

                // SCATTER PASS 1: batch-read Name(32) + MoveSpeed(4) + Team(1) for all candidates
                struct HeroScanData { uint8_t nameRaw[32]; float ms; uint8_t team; };
                HeroScanData hsd[20] = {};
                {
                    auto sc = TargetProcess.CreateScatterHandle();
                    for (int i = 0; i < candCount; i++) {
                        TargetProcess.AddScatterReadRequest(sc, candidates[i] + Offsets::GameObject::Name, hsd[i].nameRaw, 32);
                        TargetProcess.AddScatterReadRequest(sc, candidates[i] + Offsets::GameObject::MoveSpeed, &hsd[i].ms, sizeof(float));
                        TargetProcess.AddScatterReadRequest(sc, candidates[i] + Offsets::GameObject::Team, &hsd[i].team, 1);
                    }
                    TargetProcess.ExecuteReadScatter(sc);
                    TargetProcess.CloseScatterHandle(sc);
                }

                // SCATTER PASS 2: dereference heap name pointers for long strings
                char resolvedNames[20][64] = {};
                int needDeref[20] = {};
                int derefCount = 0;
                for (int i = 0; i < candCount; i++) {
                    uint64_t cap = *(uint64_t*)(hsd[i].nameRaw + 24);
                    if (cap <= 15) {
                        memcpy(resolvedNames[i], hsd[i].nameRaw, 16);
                        resolvedNames[i][15] = 0;
                    } else {
                        uintptr_t hp = *(uintptr_t*)(hsd[i].nameRaw);
                        if (hp > 0x10000 && hp < 0x7FFFFFFFFFFF)
                            needDeref[derefCount++] = i;
                    }
                }
                if (derefCount > 0) {
                    auto sc = TargetProcess.CreateScatterHandle();
                    for (int d = 0; d < derefCount; d++) {
                        int i = needDeref[d];
                        uintptr_t hp = *(uintptr_t*)(hsd[i].nameRaw);
                        TargetProcess.AddScatterReadRequest(sc, hp, resolvedNames[i], 63);
                    }
                    TargetProcess.ExecuteReadScatter(sc);
                    TargetProcess.CloseScatterHandle(sc);
                }

                // Filter for valid champion names
                for (int i = 0; i < candCount; i++) {
                    resolvedNames[i][63] = 0;
                    char* r = resolvedNames[i];
                    if (r[0] < 'A' || r[0] > 'Z') continue;
                    if (hsd[i].ms < 100.0f || hsd[i].ms > 800.0f) continue;
                    if (hsd[i].team != 1 && hsd[i].team != 2) continue;
                    int len = (int)strlen(r);
                    bool bad = (len < 2 || len > 20);
                    for (int c = 0; !bad && c < len; c++) {
                        if (r[c] == '_' || (r[c] >= '0' && r[c] <= '9')) bad = true;
                    }
                    if (!bad) {
                        bool dup = false;
                        for (auto& p : ptrs) { if (p == candidates[i]) { dup = true; break; } }
                        if (!dup) ptrs.push_back(candidates[i]);
                    }
                }

                if (ptrs.size() >= 2) s_cachedHeroPtrs = ptrs;

                static int heroListLog = 0;
                if (heroListLog < 5) {
                    VX_LOG("[HeroList] Flat hero list at RVA=0x%llX mgr=0x%llX found %d champions (scatter)\n",
                        (uint64_t)g_heroListRVA, (uint64_t)mgrAddr, (int)ptrs.size());
                    heroListLog++;
                }
            }
        }
    }

    // Fallback: combined entity list filter if flat hero list didn't find enough
    if (ptrs.size() < 2 && g_useEntityFilter) {
        ptrs.clear();
        // Combined entity list mode: read all entities and filter for champions
        std::vector<uintptr_t> allPtrs;
        ReadEntityList(Offsets::Globals::HeroManager, allPtrs, 300);

        // Scatter read Name region + MoveSpeed for classification
        // Read 32 bytes at Name offset to handle MSVC std::string SSO:
        //   if capacity (bytes 24-31) <= 15, name is inline at bytes 0-15
        //   if capacity > 15, bytes 0-7 are a pointer to heap-allocated name
        struct EntClass { uint8_t nameRaw[32]; float ms; char resolved[64]; };
        std::vector<EntClass> cl(allPtrs.size());
        auto cs = TargetProcess.CreateScatterHandle();
        for (uint32_t i = 0; i < allPtrs.size(); i++) {
            cl[i] = {};
            if (!allPtrs[i]) continue;
            TargetProcess.AddScatterReadRequest(cs, allPtrs[i] + Offsets::GameObject::Name,      cl[i].nameRaw, 32);
            TargetProcess.AddScatterReadRequest(cs, allPtrs[i] + Offsets::GameObject::MoveSpeed, &cl[i].ms,  sizeof(float));
        }
        TargetProcess.ExecuteReadScatter(cs);
        TargetProcess.CloseScatterHandle(cs);

        // Resolve names: check SSO vs pointer
        // Also try pointer dereference for entities where inline read is garbage
        std::vector<uint32_t> needDeref;
        for (uint32_t i = 0; i < allPtrs.size(); i++) {
            if (!allPtrs[i]) continue;
            uint64_t capacity = *(uint64_t*)(cl[i].nameRaw + 24);
            if (capacity <= 15) {
                // SSO: name is inline in first 16 bytes
                memcpy(cl[i].resolved, cl[i].nameRaw, 16);
                cl[i].resolved[15] = 0;
            } else {
                // Long string: bytes 0-7 are a pointer
                needDeref.push_back(i);
            }
        }

        // Scatter-dereference pointers for long strings
        if (!needDeref.empty()) {
            auto cs2 = TargetProcess.CreateScatterHandle();
            for (uint32_t idx : needDeref) {
                uint64_t namePtr = *(uint64_t*)(cl[idx].nameRaw);
                if (namePtr > 0x10000 && namePtr < 0x7FFFFFFFFFFF) {
                    TargetProcess.AddScatterReadRequest(cs2, (uintptr_t)namePtr, cl[idx].resolved, 63);
                }
            }
            TargetProcess.ExecuteReadScatter(cs2);
            TargetProcess.CloseScatterHandle(cs2);
        }

        for (uint32_t i = 0; i < allPtrs.size(); i++) {
            if (!allPtrs[i]) continue;
            cl[i].resolved[63] = 0;
            char* n = cl[i].resolved;
            float ms = cl[i].ms;
            if (n[0] < 'A' || n[0] > 'Z') continue;
            int len = (int)strlen(n);
            if (len < 2 || len > 20) continue;
            if (ms < 200.0f || ms > 800.0f) continue;
            bool bad = false;
            for (int c = 0; c < len; c++) {
                if (n[c] == '_' || (n[c] >= '0' && n[c] <= '9')) { bad = true; break; }
            }
            if (bad) continue;
            ptrs.push_back(allPtrs[i]);
            if (ptrs.size() >= 12) break;
        }

        static int heroFilterLog = 0;
        if (heroFilterLog < 3) {
            VX_LOG("[HeroFilter] Found %d champions from %d total entities\n",
                (int)ptrs.size(), (int)allPtrs.size());
            for (int i = 0; i < (int)ptrs.size(); i++) {
                char fn[64] = {};
                TargetProcess.Read(ptrs[i] + Offsets::GameObject::Name, fn, 63);
                fn[63] = 0;
                VX_LOG("[HeroFilter]   [%d] 0x%llX '%s'\n", i, (uint64_t)ptrs[i], fn);
            }
            // Diagnostic: show ALL entities with non-zero movespeed + first 5 zeros
            int zeroShown = 0;
            VX_LOG("[HeroDiag] Scanning ALL %d entities (nameOff=0x%X msOff=0x%X):\n",
                (int)allPtrs.size(), (uint32_t)Offsets::GameObject::Name, (uint32_t)Offsets::GameObject::MoveSpeed);
            for (int i = 0; i < (int)allPtrs.size(); i++) {
                if (!allPtrs[i]) continue;
                char* n = cl[i].resolved;
                float ms = cl[i].ms;
                uint64_t cap = *(uint64_t*)(cl[i].nameRaw + 24);
                // Skip zero-name zero-ms entities after first 5
                if (n[0] == 0 && ms == 0.0f) { if (++zeroShown > 5) continue; }
                const char* reason = "PASS";
                if (n[0] < 'A' || n[0] > 'Z') reason = "no-upper";
                else {
                    int len = (int)strlen(n);
                    if (len < 2 || len > 20) reason = "bad-len";
                    else if (ms < 200.0f || ms > 800.0f) reason = "bad-ms";
                    else {
                        for (int c = 0; c < len; c++) {
                            if (n[c] == '_') { reason = "has_under"; break; }
                            if (n[c] >= '0' && n[c] <= '9') { reason = "has_digit"; break; }
                        }
                    }
                }
                // Also read team value for interesting entities
                uint8_t team = 0;
                if (ms > 0.0f) TargetProcess.Read(allPtrs[i] + Offsets::GameObject::Team, &team, 1);
                VX_LOG("[HeroDiag]   [%d] 0x%llX n='%.20s' ms=%.1f cap=%llu team=%d reason=%s raw=%02X%02X%02X%02X%02X%02X%02X%02X\n",
                    i, (uint64_t)allPtrs[i], n, ms, (unsigned long long)cap, (int)team, reason,
                    cl[i].nameRaw[0],cl[i].nameRaw[1],cl[i].nameRaw[2],cl[i].nameRaw[3],
                    cl[i].nameRaw[4],cl[i].nameRaw[5],cl[i].nameRaw[6],cl[i].nameRaw[7]);
            }
            heroFilterLog++;
        }
    } else if (ptrs.empty()) {
        ReadEntityList(Offsets::Globals::HeroManager, ptrs, 12);
    }
    if (!ptrs.empty()) {
    CachedHero oldHeroes[12]{};
    int oldCount = (int)(std::min)(g_heroes.size(), (size_t)12);
    for (int i = 0; i < oldCount; i++) oldHeroes[i] = g_heroes[i];
    g_heroes.resize(ptrs.size());

    auto scatter = TargetProcess.CreateScatterHandle();
    for (uint32_t i = 0; i < ptrs.size(); i++)
    {
        g_heroes[i].addr = ptrs[i];
        g_heroes[i].name[0] = 0;
        if (!ptrs[i]) continue;
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Position,       &g_heroes[i].pos,            sizeof(Vec3));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Health,         &g_heroes[i].health,         sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MaxHealth,      &g_heroes[i].maxHealth,      sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Shield,         &g_heroes[i].shield,         sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MagicShield,    &g_heroes[i].magicShield,    sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::AttackRange,    &g_heroes[i].attackRange,    sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::BoundingRadius, &g_heroes[i].boundingRadius, sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::BaseAD,         &g_heroes[i].baseAD,         sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::BonusAD,        &g_heroes[i].bonusAD,        sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::AttackSpeedMod, &g_heroes[i].attackSpeedMod, sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Armor,          &g_heroes[i].armor,          sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MagicResist,    &g_heroes[i].magicResist,    sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Mana,           &g_heroes[i].mana,           sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MaxMana,        &g_heroes[i].maxMana,        sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Level,          &g_heroes[i].level,          sizeof(int32_t));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Team,           &g_heroes[i].team,           sizeof(uint8_t));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Visible,        &g_heroes[i].visible,        sizeof(bool));
        // IsDead offset is encrypted by Riot — derive from health instead
        // TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::IsDead, &g_heroes[i].isDead, sizeof(bool));
        // Read 32 bytes for SSO name resolution
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Name,           &g_heroes[i].name,           32);
    }
    TargetProcess.ExecuteReadScatter(scatter);
    TargetProcess.CloseScatterHandle(scatter);

    // Resolve SSO names: check capacity at nameRaw+24
    {
        std::vector<uint32_t> heroDeref;
        for (uint32_t i = 0; i < ptrs.size(); i++) {
            if (!ptrs[i]) continue;
            uint8_t* raw = (uint8_t*)g_heroes[i].name;
            uint64_t cap = *(uint64_t*)(raw + 24);
            if (cap <= 15) {
                // Inline SSO: first 16 bytes are the name, null-terminate
                char tmp[16];
                memcpy(tmp, raw, 16);
                tmp[15] = 0;
                memset(g_heroes[i].name, 0, 64);
                memcpy(g_heroes[i].name, tmp, 16);
            } else {
                // Pointer: bytes 0-7 are ptr to string
                heroDeref.push_back(i);
            }
        }
        if (!heroDeref.empty()) {
            auto ns = TargetProcess.CreateScatterHandle();
            for (uint32_t idx : heroDeref) {
                uint64_t namePtr = *(uint64_t*)g_heroes[idx].name;
                memset(g_heroes[idx].name, 0, 64);
                if (namePtr > 0x10000 && namePtr < 0x7FFFFFFFFFFF) {
                    TargetProcess.AddScatterReadRequest(ns, (uintptr_t)namePtr, g_heroes[idx].name, 63);
                }
            }
            TargetProcess.ExecuteReadScatter(ns);
            TargetProcess.CloseScatterHandle(ns);
        }
    }

    // Derive isDead from health (IsDead offset is encrypted by Riot)
    for (uint32_t i = 0; i < ptrs.size(); i++)
        g_heroes[i].isDead = (ptrs[i] && g_heroes[i].health <= 0.f);

    for (uint32_t i = 0; i < ptrs.size(); i++)
    {
        g_heroes[i].name[63] = 0;
        if (!ptrs[i]) { g_heroes[i].name[0] = 0; continue; }

        bool found = false;
        for (int j = 0; j < oldCount; j++) {
            auto& old = oldHeroes[j];
            if (old.addr == ptrs[i]) {
                g_heroes[i].wasVisible    = old.visible;
                g_heroes[i].lastPos       = old.pos;  // previous frame's position
                g_heroes[i].velocity      = old.velocity;
                g_heroes[i].lastSeenTime  = old.lastSeenTime;
                g_heroes[i].recallAlerted = old.recallAlerted;
                found = true;
                break;
            }
        }
        if (!found) {
            g_heroes[i].wasVisible    = false;
            g_heroes[i].recallAlerted = false;
            g_heroes[i].lastPos       = g_heroes[i].pos;
            g_heroes[i].velocity      = {0,0,0};
            g_heroes[i].lastSeenTime  = 0.f;
        }
        if (g_heroes[i].visible) {
            // compute velocity from position delta
            float dx = g_heroes[i].pos.x - g_heroes[i].lastPos.x;
            float dz = g_heroes[i].pos.z - g_heroes[i].lastPos.z;
            float dist = sqrtf(dx*dx + dz*dz);
            if (dist > 1.f && dist < 2000.f && g_heroes[i].lastSeenTime > 0.f) {
                float dt = g_gameTime - g_heroes[i].lastSeenTime;
                if (dt > 0.001f && dt < 1.f) {
                    float speed = dist / dt;
                    // smooth: 70% new, 30% old to reduce jitter
                    g_heroes[i].velocity.x = 0.7f * (dx / dt) + 0.3f * g_heroes[i].velocity.x;
                    g_heroes[i].velocity.z = 0.7f * (dz / dt) + 0.3f * g_heroes[i].velocity.z;
                    g_heroes[i].velocity.y = 0.f;
                    // Flash detection: enemy teleports 300+ units in <0.15s = instant blink
                    if (dist > 300.f && dt < 0.15f && g_heroes[i].team != g_localTeam) {
                        // find which summ slot is Flash and mark it
                        for (int s = 0; s < 2; s++) {
                            int slotIdx = (s == 0) ? 4 : 5;
                            const char* sname = g_heroSlotNames[i].cached ? g_heroSlotNames[i].names[slotIdx] : "";
                            if (strstr(sname, "Flash")) {
                                auto& tracked = (s == 0) ? g_trackedSummD[i] : g_trackedSummF[i];
                                if (!tracked.tracked || tracked.readyAt <= g_gameTime) {
                                    tracked.castTime = g_gameTime;
                                    tracked.totalCD  = 300.f;
                                    tracked.readyAt  = g_gameTime + 300.f;
                                    tracked.tracked  = true;
                                    VX_LOG("[FlashBlink] %s teleported %.0f units in %.3fs — Flash CD tracked\n",
                                        g_heroes[i].name, dist, dt);
                                }
                                break;
                            }
                        }
                    }
                }
            } else if (dist <= 1.f) {
                // standing still — decay velocity
                g_heroes[i].velocity.x *= 0.5f;
                g_heroes[i].velocity.z *= 0.5f;
            }
            g_heroes[i].lastPos       = g_heroes[i].pos;
            g_heroes[i].lastSeenTime  = g_gameTime;
            g_heroes[i].recallAlerted = false;
        }
    }

    // ── Read summoner spells (slots 4,5 = D,F) for all heroes — SCATTER ──
    {
        static char g_summNameCache[12][2][64] = {};
        static bool g_summNameCached[12][2] = {};

        uintptr_t summSlotPtrs[12][2] = {};
        // SCATTER PASS 1: batch-read slot 4+5 pointers for all heroes
        {
            auto sc = TargetProcess.CreateScatterHandle();
            for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
                if (!ptrs[i]) continue;
                uintptr_t sb = ptrs[i] + Offsets::GameObject::SpellBook;
                TargetProcess.AddScatterReadRequest(sc, sb + Offsets::SpellBook::SpellSlots + 4 * 0x8, &summSlotPtrs[i][0], sizeof(uintptr_t));
                TargetProcess.AddScatterReadRequest(sc, sb + Offsets::SpellBook::SpellSlots + 5 * 0x8, &summSlotPtrs[i][1], sizeof(uintptr_t));
            }
            TargetProcess.ExecuteReadScatter(sc);
            TargetProcess.CloseScatterHandle(sc);
        }

        // SCATTER PASS 2: batch-read readyAt + totalCD from each valid slot pointer
        struct SummTimers { float readyAt; float totalCD; };
        SummTimers summTimers[12][2] = {};
        {
            auto sc = TargetProcess.CreateScatterHandle();
            for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
                if (!ptrs[i]) continue;
                for (int s = 0; s < 2; s++) {
                    uintptr_t sp = summSlotPtrs[i][s];
                    if (sp < 0x10000 || sp > 0x7FFFFFFFFFFF) continue;
                    TargetProcess.AddScatterReadRequest(sc, sp + Offsets::SpellSlot::ReadyAt, &summTimers[i][s].readyAt, sizeof(float));
                    TargetProcess.AddScatterReadRequest(sc, sp + Offsets::SpellSlot::TotalCD, &summTimers[i][s].totalCD, sizeof(float));
                }
            }
            TargetProcess.ExecuteReadScatter(sc);
            TargetProcess.CloseScatterHandle(sc);
        }

        // Populate results using cached names + scattered timers
        for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
            for (int s = 0; s < 2; s++) {
                SummonerSpell& out = (s == 0) ? g_heroSumms[i].d : g_heroSumms[i].f;
                out = {};
                uintptr_t sp = summSlotPtrs[i][s];
                if (!ptrs[i] || sp < 0x10000 || sp > 0x7FFFFFFFFFFF) continue;
                out.readyAt = summTimers[i][s].readyAt;
                out.totalCD = summTimers[i][s].totalCD;
                out.ready   = (out.readyAt <= g_gameTime);
                int slotIdx = (s == 0) ? 4 : 5;
                if (g_heroSlotNames[i].cached && g_heroSlotNames[i].names[slotIdx][0]) {
                    strncpy(out.name, g_heroSlotNames[i].names[slotIdx], 63);
                } else if (g_summNameCached[i][s]) {
                    strncpy(out.name, g_summNameCache[i][s], 63);
                } else {
                    uintptr_t spellInfo = ReadSpellSlotData(sp);
                    if (spellInfo) {
                        ReadSpellNameSSO(spellInfo, Offsets::SpellSlot::SpellInfoNamePtr, out.name, 64);
                        if (out.name[0]) {
                            strncpy(g_summNameCache[i][s], out.name, 63);
                            g_summNameCached[i][s] = true;
                        }
                    }
                }
                out.name[63] = 0;
            }
        }
    }

    // ── Detect summoner cast via readyAt VALUE CHANGE (works even if encrypted) ──
    for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
        if (!ptrs[i] || g_heroes[i].team == g_localTeam) continue;
        for (int s = 0; s < 2; s++) {
            SummonerSpell& out = (s == 0) ? g_heroSumms[i].d : g_heroSumms[i].f;
            auto& tracked = (s == 0) ? g_trackedSummD[i] : g_trackedSummF[i];
            float curVal = out.readyAt;
            if (g_prevSummReadyAtValid[i][s]) {
                float prevVal = g_prevSummReadyAt[i][s];
                bool trackExpired = tracked.tracked && tracked.readyAt <= g_gameTime;
                if (curVal != prevVal && (!tracked.tracked || trackExpired)) {
                    float cd = LookupSummonerCD(out.name);
                    if (cd > 0.f) {
                        tracked.castTime = g_gameTime;
                        tracked.totalCD  = cd;
                        tracked.readyAt  = g_gameTime + cd;
                        tracked.tracked  = true;
                        VX_LOG("[SummChanged] %s %c='%s' readyAt changed %.1f→%.1f — tracking cd=%.0f\n",
                            g_heroes[i].name, s == 0 ? 'D' : 'F', out.name, prevVal, curVal, cd);
                    }
                }
            }
            g_prevSummReadyAt[i][s] = curVal;
            g_prevSummReadyAtValid[i][s] = true;
        }
    }

    // ── Override enemy summoner readyAt/totalCD with our cast-tracked values ──
    for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
        if (!ptrs[i] || g_heroes[i].team == g_localTeam) continue;
        if (g_trackedSummD[i].tracked) {
            g_heroSumms[i].d.readyAt = g_trackedSummD[i].readyAt;
            g_heroSumms[i].d.totalCD = g_trackedSummD[i].totalCD;
            g_heroSumms[i].d.ready   = (g_trackedSummD[i].readyAt <= g_gameTime);
        }
        if (g_trackedSummF[i].tracked) {
            g_heroSumms[i].f.readyAt = g_trackedSummF[i].readyAt;
            g_heroSumms[i].f.totalCD = g_trackedSummF[i].totalCD;
            g_heroSumms[i].f.ready   = (g_trackedSummF[i].readyAt <= g_gameTime);
        }
    }

    // ── Fix SpellSlots off-by-1: validate once per game ──
    {
        static bool g_spellSlotsCorrected = false;
        if (!g_spellSlotsCorrected) {
            // Find first valid enemy hero and read slot 0 + slot 5 names
            for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
                if (!ptrs[i] || g_heroes[i].isDead || g_heroes[i].team == g_localTeam) continue;
                uintptr_t sb = ptrs[i] + Offsets::GameObject::SpellBook;
                // Read slot 5 (should be F summoner spell)
                uintptr_t slot5 = TargetProcess.Read<uintptr_t>(sb + Offsets::SpellBook::SpellSlots + 5 * 0x8);
                if (slot5 < 0x10000 || slot5 > 0x7FFFFFFFFFFF) continue;
                uintptr_t info5 = ReadSpellSlotData(slot5);
                if (!info5) continue;
                char name5[64] = {};
                ReadSpellNameSSO(info5, Offsets::SpellSlot::SpellInfoNamePtr, name5, 64);
                // If slot 5 reads "BaseSpell" or similar non-summoner, SpellSlots is +0x8 too high
                if (name5[0] && (strcmp(name5, "BaseSpell") == 0 || strstr(name5, "Item") || strstr(name5, "Recall"))) {
                    VX_LOG("[SpellSlotsFix] slot5='%s' → SpellSlots is off by +0x8, correcting 0x%X → 0x%X\n",
                        name5, (uint32_t)Offsets::SpellBook::SpellSlots,
                        (uint32_t)(Offsets::SpellBook::SpellSlots - 0x8));
                    Offsets::SpellBook::SpellSlots -= 0x8;
                }
                // Also check if slot 0 reads a "W" name (shifted by 1)
                uintptr_t slot0 = TargetProcess.Read<uintptr_t>(sb + Offsets::SpellBook::SpellSlots + 0 * 0x8);
                if (slot0 >= 0x10000 && slot0 <= 0x7FFFFFFFFFFF) {
                    uintptr_t info0 = ReadSpellSlotData(slot0);
                    if (info0) {
                        char name0[64] = {};
                        ReadSpellNameSSO(info0, Offsets::SpellSlot::SpellInfoNamePtr, name0, 64);
                        VX_LOG("[SpellSlotsFix] After correction: slot0='%s' slot5='%s' SpellSlots=0x%X\n",
                            name0, name5, (uint32_t)Offsets::SpellBook::SpellSlots);
                    }
                }
                g_spellSlotsCorrected = true;
                break;
            }
        }
    }

    // ── Cache slot names for all enemy heroes (for name-based matching) ──
    for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
        if (!ptrs[i] || g_heroes[i].isDead) continue;
        if (g_heroes[i].team == g_localTeam) continue;
        if (g_heroSlotNames[i].cached) continue;
        uintptr_t sb = ptrs[i] + Offsets::GameObject::SpellBook;
        bool allRead = true;
        for (int s = 0; s < 6; s++) {
            uintptr_t slotPtr = TargetProcess.Read<uintptr_t>(sb + Offsets::SpellBook::SpellSlots + s * 0x8);
            if (slotPtr < 0x10000 || slotPtr > 0x7FFFFFFFFFFF) { allRead = false; continue; }
            uintptr_t info = ReadSpellSlotData(slotPtr);
            if (!info) { allRead = false; continue; }
            ReadSpellNameSSO(info, Offsets::SpellSlot::SpellInfoNamePtr, g_heroSlotNames[i].names[s], 64);
        }
        if (allRead) {
            g_heroSlotNames[i].cached = true;
            VX_LOG("[SlotCache] hero=%s Q='%s' W='%s' E='%s' R='%s' D='%s' F='%s'\n",
                g_heroes[i].name,
                g_heroSlotNames[i].names[0], g_heroSlotNames[i].names[1],
                g_heroSlotNames[i].names[2], g_heroSlotNames[i].names[3],
                g_heroSlotNames[i].names[4], g_heroSlotNames[i].names[5]);
        }
    }

    // ── Missile Object Tracking (reads live MissileClient objects) ──
    if (g_missileTracking && g_skillshotAlerts) {
        ReadMissileObjects();
        SyncMissilesToSkillshots();
    }

    // ── Skillshot Alert + Cast-based CD: poll ActiveSpellCast on all enemy heroes ──
    {
        g_skillshots.erase(
            std::remove_if(g_skillshots.begin(), g_skillshots.end(),
                [](const TrackedSkillshot& ss) { return g_gameTime - ss.startTime > ss.duration + 0.5f; }),
            g_skillshots.end());

        // SCATTER: batch-read ActiveSpellCast for all enemy heroes at once
        uintptr_t enemyCastPtrs[12] = {};
        {
            auto sc = TargetProcess.CreateScatterHandle();
            for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
                if (!ptrs[i] || g_heroes[i].isDead || g_heroes[i].team == g_localTeam) continue;
                uintptr_t sb = ptrs[i] + Offsets::GameObject::SpellBook;
                TargetProcess.AddScatterReadRequest(sc, sb + Offsets::SpellBook::ActiveSpellCast, &enemyCastPtrs[i], sizeof(uintptr_t));
            }
            TargetProcess.ExecuteReadScatter(sc);
            TargetProcess.CloseScatterHandle(sc);
        }

        for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
            if (!ptrs[i] || g_heroes[i].isDead) continue;
            if (g_heroes[i].team == g_localTeam) continue;

            uintptr_t castPtr = enemyCastPtrs[i];

            if (castPtr < 0x10000 || castPtr > 0x7FFFFFFFFFFF) {
                g_lastActiveCast[i] = 0;
                g_lastEnemyCast[i]  = 0;
                continue;
            }

            if (castPtr != g_lastActiveCast[i]) {
                g_lastActiveCast[i] = castPtr;
                g_lastEnemyCast[i]  = castPtr;

                Vec3 startPos{}, endPos{};
                TargetProcess.Read(castPtr + Offsets::SpellCast::StartPos, &startPos, sizeof(Vec3));
                TargetProcess.Read(castPtr + Offsets::SpellCast::EndPos,   &endPos,   sizeof(Vec3));

                // === NAME-BASED Cast Matching (readyAt is zeroed for enemies) ===
                float missileSpeed = 0.f, lineWidth = 0.f, castRange = 0.f, cooldownTime = 0.f;
                char castSpellName[64] = {};
                int matchedSlot = -1;

                // --- Step 1: Try SpellCast::SlotIndex (if we've found the offset) ---
                if (g_spellCastSlotIdxOff > 0) {
                    int32_t slotIdx = TargetProcess.Read<int32_t>(castPtr + g_spellCastSlotIdxOff);
                    if (slotIdx >= 0 && slotIdx <= 5 && g_heroSlotNames[i].cached) {
                        matchedSlot = slotIdx;
                        strncpy(castSpellName, g_heroSlotNames[i].names[slotIdx], 63);
                    }
                }

                // --- Step 2: SpellCast SlotIndex PROBE (auto-discover the offset) ---
                if (!g_slotIdxProbeComplete) {
                    static int probeCount = 0;
                    if (probeCount < 20) {
                        probeCount++;
                        VX_LOG("[SlotProbe] #%d hero=%s castPtr=0x%llX\n", probeCount, g_heroes[i].name, (uint64_t)castPtr);
                        // Scan candidate offsets for small integers 0-5
                        const uintptr_t probeOffs[] = {
                            0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30,
                            0x100, 0x104, 0x108, 0x10C, 0x110, 0x114, 0x118, 0x11C, 0x120
                        };
                        for (auto off : probeOffs) {
                            int32_t val = TargetProcess.Read<int32_t>(castPtr + off);
                            if (val >= 0 && val <= 5) {
                                VX_LOG("[SlotProbe]   +0x%03X = %d\n", (uint32_t)off, val);
                            }
                        }
                    }
                    if (probeCount >= 20) g_slotIdxProbeComplete = true;
                }

                // --- Step 3: Identify which slot cast this spell ---
                // REVERSE probe: scan SpellCast for a pointer that matches one of the 6 SpellSlot ptrs.
                // The SpellCast must reference which SpellSlot created it.
                if (matchedSlot < 0 && g_heroSlotNames[i].cached) {
                    uintptr_t sbAddr = ptrs[i] + Offsets::GameObject::SpellBook;

                    // Read all 6 SpellSlot pointers
                    uintptr_t slotPtrs[6] = {};
                    for (int s = 0; s < 6; s++) {
                        slotPtrs[s] = TargetProcess.Read<uintptr_t>(sbAddr + Offsets::SpellBook::SpellSlots + s * 0x8);
                    }

                    // 3a: Fast path — if we know the offset on SpellCast that holds the SlotPtr
                    static uintptr_t g_castSlotPtrOff = 0;
                    if (g_castSlotPtrOff > 0) {
                        uintptr_t val = TargetProcess.Read<uintptr_t>(castPtr + g_castSlotPtrOff);
                        for (int s = 0; s < 6; s++) {
                            if (slotPtrs[s] >= 0x10000 && val == slotPtrs[s]) {
                                matchedSlot = s;
                                strncpy(castSpellName, g_heroSlotNames[i].names[s], 63);
                                VX_LOG("[CastSlot] hero=%s slot %d '%s' (fast, castOff=0x%X)\n",
                                    g_heroes[i].name, s, castSpellName, (uint32_t)g_castSlotPtrOff);
                                break;
                            }
                        }
                    }

                    // 3b: PROBE — scan SpellCast offsets 0x08-0x300 for a pointer matching any SlotPtr
                    if (matchedSlot < 0 && g_castSlotPtrOff == 0) {
                        static int castSlotProbeCount = 0;
                        if (castSlotProbeCount < 15) {
                            castSlotProbeCount++;
                            VX_LOG("[CastSlotProbe] #%d hero=%s castPtr=0x%llX slots=[0x%llX,0x%llX,0x%llX,0x%llX,0x%llX,0x%llX]\n",
                                castSlotProbeCount, g_heroes[i].name, (uint64_t)castPtr,
                                (uint64_t)slotPtrs[0], (uint64_t)slotPtrs[1], (uint64_t)slotPtrs[2],
                                (uint64_t)slotPtrs[3], (uint64_t)slotPtrs[4], (uint64_t)slotPtrs[5]);
                            for (uintptr_t off = 0x08; off <= 0x300; off += 0x8) {
                                uintptr_t val = TargetProcess.Read<uintptr_t>(castPtr + off);
                                if (val < 0x10000) continue;
                                for (int s = 0; s < 6; s++) {
                                    if (val == slotPtrs[s]) {
                                        matchedSlot = s;
                                        strncpy(castSpellName, g_heroSlotNames[i].names[s], 63);
                                        g_castSlotPtrOff = off;
                                        VX_LOG("[CastSlotProbe] FOUND! castPtr+0x%X = slotPtr[%d] → '%s' — caching offset\n",
                                            (uint32_t)off, s, castSpellName);
                                        break;
                                    }
                                }
                                if (matchedSlot >= 0) break;
                            }
                            // If still no match, also try SpellSlotData ptrs (decrypted info pointers)
                            if (matchedSlot < 0) {
                                for (int s = 0; s < 6; s++) {
                                    if (slotPtrs[s] < 0x10000 || slotPtrs[s] > 0x7FFFFFFFFFFF) continue;
                                    uintptr_t infoPtr = ReadSpellSlotData(slotPtrs[s]);
                                    if (!infoPtr) continue;
                                    for (uintptr_t off = 0x08; off <= 0x300; off += 0x8) {
                                        uintptr_t val = TargetProcess.Read<uintptr_t>(castPtr + off);
                                        if (val == infoPtr) {
                                            matchedSlot = s;
                                            strncpy(castSpellName, g_heroSlotNames[i].names[s], 63);
                                            VX_LOG("[CastSlotProbe] FOUND via SpellSlotData! castPtr+0x%X = info[%d] → '%s'\n",
                                                (uint32_t)off, s, castSpellName);
                                            break;
                                        }
                                    }
                                    if (matchedSlot >= 0) break;
                                }
                            }
                        }
                    }
                }

                // --- Step 4: Handle matched cast ---
                if (matchedSlot >= 0) {
                    // Summoner spell cast (slot 4 or 5)
                    if (matchedSlot == 4 || matchedSlot == 5) {
                        float cd = LookupSummonerCD(castSpellName);
                        cooldownTime = cd;
                        auto& tracked = (matchedSlot == 4) ? g_trackedSummD[i] : g_trackedSummF[i];
                        tracked.castTime = g_gameTime;
                        tracked.totalCD  = cd;
                        tracked.readyAt  = g_gameTime + cd;
                        tracked.tracked  = true;
                        VX_LOG("[SummCast] %s %c='%s' cd=%.0f readyAt=%.1f\n",
                            g_heroes[i].name, matchedSlot == 4 ? 'D' : 'F', castSpellName, cd, tracked.readyAt);
                    }
                    // QWER cast (slot 0-3): read baseCooldown from SpellData
                    else {
                        const float defaultCDs[] = { 8.f, 12.f, 14.f, 100.f };
                        cooldownTime = defaultCDs[matchedSlot];

                        // Try to read actual baseCooldown from cast → SpellData → CooldownTime
                        uintptr_t castSpellData = TargetProcess.Read<uintptr_t>(castPtr + Offsets::SpellCast::SpellData);
                        if (castSpellData > 0x10000 && castSpellData < 0x7FFFFFFFFFFF) {
                            // CooldownTime is a float array indexed by spell level (0-based)
                            // Read spell level from the slot
                            uintptr_t sb = ptrs[i] + Offsets::GameObject::SpellBook;
                            uintptr_t slotP = TargetProcess.Read<uintptr_t>(sb + Offsets::SpellBook::SpellSlots + matchedSlot * 0x8);
                            int spellLevel = 1;
                            if (slotP > 0x10000 && slotP < 0x7FFFFFFFFFFF)
                                spellLevel = TargetProcess.Read<int>(slotP + 0x28);
                            if (spellLevel < 1) spellLevel = 1;
                            if (spellLevel > 5) spellLevel = 5;

                            float baseCooldown = TargetProcess.Read<float>(
                                castSpellData + Offsets::SpellData::CooldownTime + (spellLevel - 1) * sizeof(float));
                            if (baseCooldown > 1.f && baseCooldown < 600.f) {
                                cooldownTime = baseCooldown;
                                VX_LOG("[CastCD-SpellData] %s %c lv%d baseCooldown=%.1f (from memory)\n",
                                    g_heroes[i].name, "QWER"[matchedSlot], spellLevel, baseCooldown);
                            }
                        }
                    }
                }

                // ── CSV Evade Spell Lookup (primary source for spell data) ──
                if (castSpellName[0]) {
                    const EvadeSpellEntry* ev = LookupEvadeSpell(castSpellName);
                    if (ev && ev->shape == EVADE_LINE && ev->speed > 10.f && ev->range > 50.f) {
                        missileSpeed = ev->speed;
                        lineWidth    = ev->size;
                        castRange    = ev->range;
                        VX_LOG("[EvadeLookup] '%s' → spd=%.0f wid=%.0f rng=%.0f (from CSV)\n",
                            castSpellName, ev->speed, ev->size, ev->range);
                    }
                }
                // ── Champion Fallback: match by champion name + cast distance when slot matching failed ──
                if (missileSpeed < 1.f && g_heroes[i].name[0]) {
                    float dx = endPos.x - startPos.x;
                    float dz = endPos.z - startPos.z;
                    float castDist = sqrtf(dx * dx + dz * dz);
                    char fallbackName[32] = {};
                    const EvadeSpellEntry* ev = LookupEvadeByChampion(g_heroes[i].name, castDist, fallbackName, 32);
                    if (ev) {
                        missileSpeed = ev->speed;
                        lineWidth    = ev->size;
                        castRange    = ev->range;
                        if (!castSpellName[0]) strncpy(castSpellName, fallbackName, 31);
                        VX_LOG("[ChampFallback] %s cast dist=%.0f → '%s' spd=%.0f wid=%.0f rng=%.0f\n",
                            g_heroes[i].name, castDist, fallbackName, ev->speed, ev->size, ev->range);
                    }
                }

                VX_LOG("[CastDetect] hero=%s slot=%d cast='%s' speed=%.0f width=%.0f range=%.0f cd=%.1f start=(%.0f,%.0f,%.0f) end=(%.0f,%.0f,%.0f)\n",
                    g_heroes[i].name, matchedSlot, castSpellName, missileSpeed, lineWidth, castRange, cooldownTime,
                    startPos.x, startPos.y, startPos.z, endPos.x, endPos.y, endPos.z);

                if (g_skillshotAlerts && missileSpeed >= g_skillshotMinSpeed && lineWidth > 0.f && castRange > 100.f) {
                    // look up cast delay from CSV evade entry
                    float castDelayS = 0.f;
                    {
                        const EvadeSpellEntry* evDelay = LookupEvadeSpell(castSpellName);
                        if (evDelay && evDelay->delayMs > 0)
                            castDelayS = evDelay->delayMs / 1000.0f;
                    }
                    TrackedSkillshot ss{};
                    ss.startPos     = startPos;
                    ss.endPos       = endPos;
                    ss.missileSpeed = missileSpeed;
                    ss.lineWidth    = lineWidth;
                    ss.castRange    = castRange;
                    ss.startTime    = g_gameTime;
                    ss.duration     = castDelayS + castRange / missileSpeed;
                    ss.delayS       = castDelayS;
                    ss.casterIdx    = i;
                    strncpy(ss.spellName, castSpellName, 31);
                    if (g_skillshots.size() < 20)
                        g_skillshots.push_back(ss);
                }

                // Skip auto attacks — they spam ActiveSpellCast but aren't real ability casts
                bool isCastAutoAttack = (strstr(castSpellName, "Attack") != nullptr ||
                                         strstr(castSpellName, "BasicAttack") != nullptr);

                if (g_castCDTracker && matchedSlot >= 0 && matchedSlot <= 3 && castSpellName[0] && !isCastAutoAttack) {
                    auto& cd = g_enemyCDs[i].slots[matchedSlot];
                    cd.castTime         = g_gameTime;
                    cd.cooldownDuration  = cooldownTime;
                    cd.readyAt           = g_gameTime + cooldownTime;
                    strncpy(cd.spellName, castSpellName, 31);
                    cd.valid             = true;
                    VX_LOG("[CastCD] %s %c='%s' cd=%.1f readyAt=%.1f\n",
                        g_heroes[i].name, "QWER"[matchedSlot], castSpellName, cooldownTime, cd.readyAt);
                }
            } else {
                g_lastActiveCast[i] = castPtr;
                g_lastEnemyCast[i]  = castPtr;
            }
        }
    }

    // One-time diagnostic: dump readyAt for all hero summoner slots
    {
        static int summDiagLog = 0;
        if (summDiagLog < 3) {
            VX_LOG("[SummDiag] GT=%.1f ReadyAtOff=0x%X TotalCDOff=0x%X encrypted=%d\n",
                g_gameTime, (uint32_t)Offsets::SpellSlot::ReadyAt, (uint32_t)Offsets::SpellSlot::TotalCD, (int)g_spellSlotEncrypted);
            for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
                if (!ptrs[i]) continue;
                auto& d = g_heroSumms[i].d;
                auto& f = g_heroSumms[i].f;
                VX_LOG("[SummDiag]   [%d] 0x%llX D='%s' rdy=%.1f tot=%.1f ready=%d | F='%s' rdy=%.1f tot=%.1f ready=%d\n",
                    i, (uint64_t)ptrs[i], d.name, d.readyAt, d.totalCD, (int)d.ready, f.name, f.readyAt, f.totalCD, (int)f.ready);
                // Also dump raw bytes at slot+0x38 through slot+0x80 for enemy slot 4
                if (g_heroes[i].team != g_localTeam) {
                    uintptr_t sb = ptrs[i] + Offsets::GameObject::SpellBook;
                    uintptr_t sp4 = TargetProcess.Read<uintptr_t>(sb + Offsets::SpellBook::SpellSlots + 4 * 8);
                    if (sp4 > 0x10000 && sp4 < 0x7FFFFFFFFFFF) {
                        uint8_t raw[0x100] = {};
                        TargetProcess.Read(sp4, raw, 0x100);
                        VX_LOG("[SummDiag]     slot4 raw dump (key floats):\n");
                        for (int off = 0x30; off <= 0xA0; off += 4) {
                            float val = *(float*)(raw + off);
                            if (val != 0.0f)
                                VX_LOG("[SummDiag]       +0x%03X = %.2f\n", off, val);
                        }
                    }
                }
            }
            summDiagLog++;
        }
    }

    // ── Local player smite detection (uses scatter-read summoner data above) ──
    g_smiteSlot = -1;
    g_smiteReady = false;
    g_smiteName[0] = 0;
    if (localPtr) {
        int localIdx = -1;
        for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
            if (ptrs[i] == localPtr) { localIdx = i; break; }
        }
        if (localIdx >= 0) {
            SummonerSpell* slots[2] = { &g_heroSumms[localIdx].d, &g_heroSumms[localIdx].f };
            for (int s = 0; s < 2; s++) {
                if (strstr(slots[s]->name, "Smite") || strstr(slots[s]->name, "smite") ||
                    strstr(slots[s]->name, "SummonerSmite")) {
                    g_smiteSlot    = (s == 0) ? 4 : 5;
                    g_smiteReadyAt = slots[s]->readyAt;
                    g_smiteReady   = slots[s]->ready;
                    strncpy(g_smiteName, slots[s]->name, 63);
                    break;
                }
            }
        }
    }

    // ── Enemy jungler detection (find which enemy has Smite) ──
    if (g_junglerTracker || g_junglerDangerAlert) {
        if (g_enemyJunglerIdx < 0) {
            for (int i = 0; i < (int)g_heroes.size() && i < 12; i++) {
                if (!g_heroes[i].addr || g_heroes[i].team == g_localTeam || g_heroes[i].addr == g_localAddr || !g_heroes[i].name[0]) continue;
                auto& d = g_heroSumms[i].d;
                auto& f = g_heroSumms[i].f;
                if ((d.name[0] && (strstr(d.name, "Smite") || strstr(d.name, "smite") || strstr(d.name, "SummonerSmite"))) ||
                    (f.name[0] && (strstr(f.name, "Smite") || strstr(f.name, "smite") || strstr(f.name, "SummonerSmite")))) {
                    g_enemyJunglerIdx = i;
                    VX_LOG("[JGTracker] Enemy jungler detected: %s (idx=%d)\n", g_heroes[i].name, i);
                    break;
                }
            }
        }
        // danger alert: trigger when jungler becomes visible near local player
        if (g_junglerDangerAlert && g_enemyJunglerIdx >= 0 && g_enemyJunglerIdx < (int)g_heroes.size()) {
            auto& jg = g_heroes[g_enemyJunglerIdx];
            if (jg.visible && !jg.isDead) {
                float dx = jg.pos.x - g_localPos.x;
                float dz = jg.pos.z - g_localPos.z;
                float dist = sqrtf(dx * dx + dz * dz);
                if (dist < g_junglerDangerRange && (g_gameTime - g_junglerAlertTime) > g_junglerAlertDuration + 2.f) {
                    g_junglerAlertTime = g_gameTime;
                    VX_LOG("[JGTracker] DANGER! %s spotted %.0f units away!\n", jg.name, dist);
                }
            }
        }
    }

    g_recallAlerts.erase(
        std::remove_if(g_recallAlerts.begin(), g_recallAlerts.end(),
            [](const RecallAlert& a) { return g_gameTime - a.triggerTime > 5.f; }),
        g_recallAlerts.end());

    for (auto& hero : g_heroes) {
        if (!hero.addr || hero.isDead || hero.team == g_localTeam) continue;
        if (!hero.visible && hero.lastSeenTime > 0.f && !hero.recallAlerted) {
            float timeSince = g_gameTime - hero.lastSeenTime;
            if (timeSince >= g_recallChannelTime && timeSince < g_recallChannelTime + 1.5f) {
                hero.recallAlerted = true;
                RecallAlert alert{};
                strncpy(alert.name, hero.name, 63);
                alert.triggerTime = g_gameTime;
                g_recallAlerts.push_back(alert);
                VX_LOG("[Recall] %s possible recall (MIA %.1fs)\n", hero.name, timeSince);
            }
        }
    }
    }

    if (g_espTurretRange || g_orbEnabled) {
        std::vector<uintptr_t> tPtrs;
        if (g_useEntityFilter) {
            std::vector<uintptr_t> allPtrs;
            ReadEntityList(Offsets::Globals::TurretManager, allPtrs, 300);
            struct EntClass { uint8_t nameRaw[32]; char resolved[64]; };
            std::vector<EntClass> cl(allPtrs.size());
            auto cs = TargetProcess.CreateScatterHandle();
            for (uint32_t i = 0; i < allPtrs.size(); i++) {
                cl[i] = {};
                if (!allPtrs[i]) continue;
                TargetProcess.AddScatterReadRequest(cs, allPtrs[i] + Offsets::GameObject::Name, cl[i].nameRaw, 32);
            }
            TargetProcess.ExecuteReadScatter(cs);
            TargetProcess.CloseScatterHandle(cs);
            // Resolve SSO names
            std::vector<uint32_t> tDeref;
            for (uint32_t i = 0; i < allPtrs.size(); i++) {
                if (!allPtrs[i]) continue;
                uint64_t cap = *(uint64_t*)(cl[i].nameRaw + 24);
                if (cap <= 15) {
                    memcpy(cl[i].resolved, cl[i].nameRaw, 16);
                    cl[i].resolved[15] = 0;
                } else {
                    tDeref.push_back(i);
                }
            }
            if (!tDeref.empty()) {
                auto cs2 = TargetProcess.CreateScatterHandle();
                for (uint32_t idx : tDeref) {
                    uint64_t np = *(uint64_t*)(cl[idx].nameRaw);
                    if (np > 0x10000 && np < 0x7FFFFFFFFFFF)
                        TargetProcess.AddScatterReadRequest(cs2, (uintptr_t)np, cl[idx].resolved, 63);
                }
                TargetProcess.ExecuteReadScatter(cs2);
                TargetProcess.CloseScatterHandle(cs2);
            }
            for (uint32_t i = 0; i < allPtrs.size(); i++) {
                if (!allPtrs[i]) continue;
                cl[i].resolved[63] = 0;
                if (strncmp(cl[i].resolved, "Turret_", 7) == 0 || strncmp(cl[i].resolved, "Barracks_", 9) == 0 ||
                    strncmp(cl[i].resolved, "HQ_", 3) == 0) {
                    tPtrs.push_back(allPtrs[i]);
                    if (tPtrs.size() >= 30) break;
                }
            }
        } else {
            ReadEntityList(Offsets::Globals::TurretManager, tPtrs, 30);
        }
        if (!tPtrs.empty()) {
        g_turrets.resize(tPtrs.size());
        auto ts = TargetProcess.CreateScatterHandle();
        for (uint32_t i = 0; i < tPtrs.size(); i++) {
            g_turrets[i].addr = tPtrs[i];
            if (!tPtrs[i]) continue;
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::Position,  &g_turrets[i].pos,       sizeof(Vec3));
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::Health,    &g_turrets[i].health,    sizeof(float));
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::MaxHealth, &g_turrets[i].maxHealth, sizeof(float));
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::Team,      &g_turrets[i].team,      sizeof(uint8_t));
        }
        TargetProcess.ExecuteReadScatter(ts);
        TargetProcess.CloseScatterHandle(ts);
        }
    }

    if (g_espMinions || g_orbEnabled) {
        std::vector<uintptr_t> mPtrs;
        if (g_useEntityFilter) {
            std::vector<uintptr_t> allPtrs;
            ReadEntityList(Offsets::Globals::MinionManager, allPtrs, 300);
            struct EntClass { uint8_t nameRaw[32]; char resolved[64]; };
            std::vector<EntClass> cl(allPtrs.size());
            auto cs = TargetProcess.CreateScatterHandle();
            for (uint32_t i = 0; i < allPtrs.size(); i++) {
                cl[i] = {};
                if (!allPtrs[i]) continue;
                TargetProcess.AddScatterReadRequest(cs, allPtrs[i] + Offsets::GameObject::Name, cl[i].nameRaw, 32);
            }
            TargetProcess.ExecuteReadScatter(cs);
            TargetProcess.CloseScatterHandle(cs);
            // Resolve SSO names
            std::vector<uint32_t> mDeref;
            for (uint32_t i = 0; i < allPtrs.size(); i++) {
                if (!allPtrs[i]) continue;
                uint64_t cap = *(uint64_t*)(cl[i].nameRaw + 24);
                if (cap <= 15) {
                    memcpy(cl[i].resolved, cl[i].nameRaw, 16);
                    cl[i].resolved[15] = 0;
                } else {
                    mDeref.push_back(i);
                }
            }
            if (!mDeref.empty()) {
                auto cs2 = TargetProcess.CreateScatterHandle();
                for (uint32_t idx : mDeref) {
                    uint64_t np = *(uint64_t*)(cl[idx].nameRaw);
                    if (np > 0x10000 && np < 0x7FFFFFFFFFFF)
                        TargetProcess.AddScatterReadRequest(cs2, (uintptr_t)np, cl[idx].resolved, 63);
                }
                TargetProcess.ExecuteReadScatter(cs2);
                TargetProcess.CloseScatterHandle(cs2);
            }
            for (uint32_t i = 0; i < allPtrs.size(); i++) {
                if (!allPtrs[i]) continue;
                cl[i].resolved[63] = 0;
                if (strncmp(cl[i].resolved, "SRU_", 4) == 0 || strncmp(cl[i].resolved, "Minion_", 7) == 0 ||
                    strstr(cl[i].resolved, "MinionSuper") != nullptr || strstr(cl[i].resolved, "Siege") != nullptr ||
                    strstr(cl[i].resolved, "Crab") != nullptr) {
                    mPtrs.push_back(allPtrs[i]);
                    if (mPtrs.size() >= 200) break;
                }
            }
        } else {
            ReadEntityList(Offsets::Globals::MinionManager, mPtrs, 200);
        }
        if (!mPtrs.empty()) {
        g_minions.resize(mPtrs.size());
        auto ms = TargetProcess.CreateScatterHandle();
        for (uint32_t i = 0; i < mPtrs.size(); i++) {
            g_minions[i].addr = mPtrs[i];
            g_minions[i].name[0] = 0;
            if (!mPtrs[i]) continue;
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Position,  &g_minions[i].pos,       sizeof(Vec3));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Health,         &g_minions[i].health,         sizeof(float));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::MaxHealth,      &g_minions[i].maxHealth,      sizeof(float));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::BoundingRadius, &g_minions[i].boundingRadius, sizeof(float));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Team,           &g_minions[i].team,           sizeof(uint8_t));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Visible,   &g_minions[i].visible,   sizeof(bool));
            // IsDead offset is encrypted — derive from health instead
            // TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::IsDead, &g_minions[i].isDead, sizeof(bool));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Name,      &g_minions[i].name,      32);
        }
        TargetProcess.ExecuteReadScatter(ms);
        TargetProcess.CloseScatterHandle(ms);
        // Resolve SSO names for minions
        {
            std::vector<uint32_t> mnDeref;
            for (uint32_t i = 0; i < mPtrs.size(); i++) {
                if (!mPtrs[i]) continue;
                uint8_t* raw = (uint8_t*)g_minions[i].name;
                uint64_t cap = *(uint64_t*)(raw + 24);
                if (cap <= 15) {
                    char tmp[16]; memcpy(tmp, raw, 16); tmp[15] = 0;
                    memset(g_minions[i].name, 0, 64);
                    memcpy(g_minions[i].name, tmp, 16);
                } else {
                    mnDeref.push_back(i);
                }
            }
            if (!mnDeref.empty()) {
                auto ns = TargetProcess.CreateScatterHandle();
                for (uint32_t idx : mnDeref) {
                    uint64_t np = *(uint64_t*)g_minions[idx].name;
                    memset(g_minions[idx].name, 0, 64);
                    if (np > 0x10000 && np < 0x7FFFFFFFFFFF)
                        TargetProcess.AddScatterReadRequest(ns, (uintptr_t)np, g_minions[idx].name, 63);
                }
                TargetProcess.ExecuteReadScatter(ns);
                TargetProcess.CloseScatterHandle(ns);
            }
        }
        for (uint32_t i = 0; i < mPtrs.size(); i++) {
            g_minions[i].name[63] = 0;
            g_minions[i].isDead = (mPtrs[i] && g_minions[i].health <= 0.f);
        }
        }
    } else {
        g_minions.clear();
    }

    // ── Jungle camp tracking (vision + fog-of-war buff reading) ──
    if (g_trackerJungle && g_gameTime > 90.f) {
        // Vision-based: if we can see a live monster, camp is alive
        for (auto& camp : g_jungleCamps) {
            bool found = false;
            for (const auto& m : g_minions) {
                if (!m.addr || m.isDead || m.health <= 0.f) continue;
                if (m.name[0] == 0) continue;
                if (strstr(m.name, camp.name)) {
                    float dx = m.pos.x - camp.pos.x;
                    float dz = m.pos.z - camp.pos.z;
                    if (dx * dx + dz * dz < 2000.f * 2000.f) {
                        found = true;
                        break;
                    }
                }
            }
            if (found && !camp.alive) {
                camp.alive = true;
                camp.buffRespawnAt = 0.f;
            } else if (!found && camp.alive && camp.killedAt > 0.f) {
                // only mark dead from vision if we previously had vision (killedAt set)
            }
        }

        // Fog-of-war: scan minion objects near camp positions for respawn countdown buffs
        // ponytail: runs every 30 frames (~0.5s), 6 scatter passes; bump interval if DMA latency matters
        static int fogScanTick = 0;
        if (++fogScanTick >= 30) {
            fogScanTick = 0;

            // Collect all minion addrs near each camp position
            struct BufCand { uintptr_t addr; int campIdx; };
            std::vector<BufCand> cands;
            for (const auto& m : g_minions) {
                if (!m.addr) continue;
                for (int ci = 0; ci < NUM_CAMPS; ci++) {
                    float dx = m.pos.x - g_jungleCamps[ci].pos.x;
                    float dz = m.pos.z - g_jungleCamps[ci].pos.z;
                    if (dx * dx + dz * dz < 1500.f * 1500.f) {
                        if (strstr(m.name, "SRU_") && m.health > 0.f && !m.isDead) continue;
                        cands.push_back({m.addr, ci});
                    }
                }
            }

            if (!cands.empty()) {
                // Scatter 1: read BuffManager start/end ptrs
                struct BmPtrs { uintptr_t start = 0, end = 0; };
                std::vector<BmPtrs> bmP(cands.size());
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (size_t i = 0; i < cands.size(); i++) {
                        TargetProcess.AddScatterReadRequest(s, cands[i].addr + Offsets::GameObject::BuffManager,        &bmP[i].start, 8);
                        TargetProcess.AddScatterReadRequest(s, cands[i].addr + Offsets::GameObject::BuffManager + 0x10, &bmP[i].end,   8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 2: read buff entry pointers from each valid buff array
                struct EntInfo { int candIdx; uintptr_t entryPtr = 0; };
                std::vector<EntInfo> ents;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (size_t i = 0; i < cands.size(); i++) {
                        if (bmP[i].start < 0x10000 || bmP[i].end < 0x10000) continue;
                        if (bmP[i].start > 0x7FFFFFFFFFFF || bmP[i].end > 0x7FFFFFFFFFFF) continue;
                        int cnt = (int)((bmP[i].end - bmP[i].start) / 8);
                        if (cnt <= 0 || cnt > 128) continue;
                        for (int j = 0; j < cnt; j++) {
                            ents.push_back({(int)i, 0});
                            TargetProcess.AddScatterReadRequest(s, bmP[i].start + j * 8, &ents.back().entryPtr, 8);
                        }
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 3: entry -> buff pointer (entry + 0x10)
                struct BufInfo { int candIdx; uintptr_t buffPtr = 0; };
                std::vector<BufInfo> bufs;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& e : ents) {
                        if (e.entryPtr < 0x10000 || e.entryPtr > 0x7FFFFFFFFFFF) continue;
                        bufs.push_back({e.candIdx, 0});
                        TargetProcess.AddScatterReadRequest(s, e.entryPtr + 0x10, &bufs.back().buffPtr, 8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 4: read endTime + nameStructPtr from each buff
                struct BufData { int candIdx; float endTime = 0.f; uintptr_t namePtr = 0; };
                std::vector<BufData> bd;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& b : bufs) {
                        if (b.buffPtr < 0x10000 || b.buffPtr > 0x7FFFFFFFFFFF) continue;
                        bd.push_back({b.candIdx, 0.f, 0});
                        TargetProcess.AddScatterReadRequest(s, b.buffPtr + 0x1C, &bd.back().endTime,  4);
                        TargetProcess.AddScatterReadRequest(s, b.buffPtr + 0x10, &bd.back().namePtr,  8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 5: nameStructPtr + 0x8 -> char* pointer
                struct NmInfo { int candIdx; float endTime; uintptr_t charPtr = 0; };
                std::vector<NmInfo> nm;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& d : bd) {
                        if (d.namePtr < 0x10000 || d.namePtr > 0x7FFFFFFFFFFF) continue;
                        nm.push_back({d.candIdx, d.endTime, 0});
                        TargetProcess.AddScatterReadRequest(s, d.namePtr + 0x8, &nm.back().charPtr, 8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 6: read name strings
                struct NmResult { int candIdx; float endTime; char name[64] = {}; };
                std::vector<NmResult> nr;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& n : nm) {
                        if (n.charPtr < 0x10000 || n.charPtr > 0x7FFFFFFFFFFF) continue;
                        nr.push_back({n.candIdx, n.endTime, {}});
                        TargetProcess.AddScatterReadRequest(s, n.charPtr, nr.back().name, 63);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Match countdown buffs to camps
                for (auto& r : nr) {
                    r.name[63] = 0;
                    int ci = cands[r.candIdx].campIdx;
                    auto& camp = g_jungleCamps[ci];
                    if (strstr(r.name, "camprespawncountdownhidden")) {
                        camp.alive = false;
                        camp.buffRespawnAt = r.endTime + 60.f;
                        if (camp.killedAt <= 0.f)
                            camp.killedAt = camp.buffRespawnAt - camp.respawnTime;
                    } else if (strstr(r.name, "camprespawncountdownvisible")) {
                        camp.alive = false;
                        camp.buffRespawnAt = r.endTime;
                        if (camp.killedAt <= 0.f)
                            camp.killedAt = camp.buffRespawnAt - camp.respawnTime;
                    }
                }
            }

            // If buff says respawn time has passed, mark alive again
            for (auto& camp : g_jungleCamps) {
                if (!camp.alive && camp.buffRespawnAt > 0.f && g_gameTime >= camp.buffRespawnAt) {
                    camp.alive = true;
                    camp.killedAt = 0.f;
                    camp.buffRespawnAt = 0.f;
                }
            }
        }
    }

    // ── Auto smite ──
    if (g_smiteEnabled && g_smiteSlot >= 0 && g_smiteReady && g_gameTime > 90.f) {
        float smiteDmg = SmiteDamage(g_localLevel);
        int bestIdx = -1;
        float bestHP = 99999.f;
        for (int i = 0; i < (int)g_minions.size(); i++) {
            const auto& m = g_minions[i];
            if (!m.addr || m.isDead || m.health <= 0.f || !m.visible) continue;
            if (m.team == g_localTeam) continue;
            if (m.name[0] == 0) continue;
            bool isJungle = (strstr(m.name, "SRU_") != nullptr) ||
                            (strstr(m.name, "Sru_") != nullptr);
            if (!isJungle) continue;
            // skip small monsters (camps like mini raptors, mini krugs)
            if (m.maxHealth < 500.f) continue;
            float dx = m.pos.x - g_localPos.x;
            float dy = m.pos.y - g_localPos.y;
            float dz = m.pos.z - g_localPos.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist > 500.f) continue; // smite range ~500
            if (m.health <= smiteDmg && m.health < bestHP) {
                bestHP = m.health;
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) {
            // Cast smite via keyboard (D = slot 4, F = slot 5)
            unsigned int hidCode = (g_smiteSlot == 4) ? 0x07 : 0x09; // D=0x07, F=0x09 HID
            InputDevice::KeyPress(hidCode);
        }
    }
}

struct SinCosEntry { float s, c; };
static SinCosEntry g_sincos12[13], g_sincos20[21], g_sincos50[51];
static bool g_sincosInit = false;
static void InitSinCos() {
    if (g_sincosInit) return;
    g_sincosInit = true;
    auto fill = [](SinCosEntry* tbl, int n) {
        float step = 6.28318f / (float)n;
        for (int i = 0; i <= n; i++) { tbl[i].s = sinf(i * step); tbl[i].c = cosf(i * step); }
    };
    fill(g_sincos12, 12); fill(g_sincos20, 20); fill(g_sincos50, 50);
}

static void DrawWorldCircle(ImDrawList* dl, const Vec3& center, float radius, ImU32 color, float thickness, int segments)
{
    Vec2 centerSp;
    if (!WorldToScreen(center, centerSp)) return;
    float margin = radius * 4.f;
    if (centerSp.x < -margin || centerSp.x > g_screenW + margin ||
        centerSp.y < -margin || centerSp.y > g_screenH + margin) return;

    float screenR = WorldRadiusToScreen(center, radius);
    if (segments <= 0) {
        segments = (int)(screenR * 0.5f);
        segments = std::clamp(segments, 12, 100);
    } else {
        if (segments < 8) segments = 8;
        if (segments > 128) segments = 128;
    }

    const SinCosEntry* tbl = nullptr;
    if (segments == 12) tbl = g_sincos12;
    else if (segments == 20) tbl = g_sincos20;
    else if (segments == 50) tbl = g_sincos50;

    ImVec2 pts[129];
    int count = 0;
    for (int i = 0; i <= segments; i++) {
        float sc, cc;
        if (tbl) { sc = tbl[i].s; cc = tbl[i].c; }
        else { float a = i * 6.28318f / (float)segments; sc = sinf(a); cc = cosf(a); }
        Vec3 wp = { center.x + cc * radius, center.y, center.z + sc * radius };
        Vec2 sp;
        if (WorldToScreen(wp, sp))
            pts[count++] = ImVec2(sp.x, sp.y);
    }
    if (count < 2) return;

    if (g_drawGlow) {
        ImU32 glow = (color & 0x00FFFFFF) | 0x28000000;
        dl->AddPolyline(pts, count, glow, ImDrawFlags_None, thickness + 4.f);
    }

    if (g_drawMode == 1 && count >= 3)
        dl->AddConvexPolyFilled(pts, count, (color & 0x00FFFFFF) | 0x1A000000);

    dl->AddPolyline(pts, count, color, ImDrawFlags_None, thickness);
}

static void DrawESP()
{
    if (!g_espEnabled || g_gameTime < 1.0f) return;
    auto* dl = ImGui::GetBackgroundDrawList();

    static int espDiagCount = 0;
    bool espDiag = (espDiagCount < 10);
    if (espDiag) {
        espDiagCount++;
        VX_LOG("[ESPDiag] tick=%d heroes=%d espEnabled=%d GT=%.1f screenW=%d screenH=%d vpMatrix[0][0]=%.4f localTeam=%d\n",
            espDiagCount, (int)g_heroes.size(), (int)g_espEnabled, g_gameTime, g_screenW, g_screenH,
            g_viewProjMatrix.m[0][0], (int)g_localTeam);
    }

    ImU32 enemyCol = ImGui::ColorConvertFloat4ToU32(ImVec4(g_enemyColor[0], g_enemyColor[1], g_enemyColor[2], g_enemyColor[3]));
    ImU32 allyCol  = ImGui::ColorConvertFloat4ToU32(ImVec4(g_allyColor[0], g_allyColor[1], g_allyColor[2], g_allyColor[3]));

    for (int heroIdx = 0; heroIdx < (int)g_heroes.size(); heroIdx++)
    {
        const auto& hero = g_heroes[heroIdx];
        if (!hero.addr || hero.isDead) continue;
        if (hero.addr == g_localAddr) continue;  // never draw ESP on ourselves
        bool isAlly = (hero.team == g_localTeam);
        if (g_espEnemyOnly && isAlly) continue;
        bool isJungler = (g_junglerTracker && heroIdx == g_enemyJunglerIdx);

        if (espDiag) {
            Vec2 dbgSp;
            bool dbgW2S = WorldToScreen(hero.pos, dbgSp);
            VX_LOG("[ESPDiag]   hero '%s' addr=0x%llX team=%d dead=%d vis=%d pos=(%.0f,%.0f,%.0f) atkRng=%.0f W2S=%d screen=(%.0f,%.0f) espEO=%d isAlly=%d\n",
                hero.name, (uint64_t)hero.addr, hero.team, hero.isDead, hero.visible,
                hero.pos.x, hero.pos.y, hero.pos.z, hero.attackRange,
                dbgW2S, dbgSp.x, dbgSp.y, (int)g_espEnemyOnly, isAlly);
        }

        if (!hero.visible && !isAlly && (g_espLastPosition || isJungler) && hero.lastSeenTime > 0.f)
        {
            float timeSince = g_gameTime - hero.lastSeenTime;

            // jungler gets special purple/magenta, others keep yellow/orange/red
            ImU32 miaCol;
            if (isJungler) {
                float pulse = 0.7f + 0.3f * sinf(g_gameTime * 4.f);
                uint8_t mag = (uint8_t)(255 * pulse);
                miaCol = IM_COL32(mag, 0, 255, 255);
            } else {
                if (timeSince < 10.f)       miaCol = IM_COL32(255, 255, 0, 255);
                else if (timeSince < 30.f)  miaCol = IM_COL32(255, 160, 0, 255);
                else                        miaCol = IM_COL32(255, 40, 40, 255);
            }

            Vec2 lastSp;
            bool onScreen = WorldToScreen(hero.lastPos, lastSp) &&
                lastSp.x >= -50 && lastSp.x <= g_screenW + 50 &&
                lastSp.y >= -50 && lastSp.y <= g_screenH + 50;

            if (onScreen && lastSp.x >= 0 && lastSp.x <= g_screenW && lastSp.y >= 0 && lastSp.y <= g_screenH)
            {

                float pulse = 20.f + 5.f * sinf(g_gameTime * 3.f);
                dl->AddCircle(ImVec2(lastSp.x, lastSp.y), pulse, miaCol, 0, 2.f);
                dl->AddCircle(ImVec2(lastSp.x, lastSp.y), 10.f, miaCol, 0, 2.f);
                float cx = lastSp.x, cy = lastSp.y;
                dl->AddLine(ImVec2(cx - 7, cy - 7), ImVec2(cx + 7, cy + 7), miaCol, 2.f);
                dl->AddLine(ImVec2(cx + 7, cy - 7), ImVec2(cx - 7, cy + 7), miaCol, 2.f);

                char lastBuf[80];
                if (isJungler)
                    snprintf(lastBuf, sizeof(lastBuf), "[JG] %s [%.0fs]", hero.name, timeSince);
                else
                    snprintf(lastBuf, sizeof(lastBuf), "%s [%.0fs]", hero.name, timeSince);
                auto ts = ImGui::CalcTextSize(lastBuf);
                dl->AddRectFilled(ImVec2(cx - ts.x/2 - 2, cy - 28), ImVec2(cx + ts.x/2 + 2, cy - 12), IM_COL32(20,20,20,255), 3.f);
                dl->AddText(ImVec2(cx - ts.x/2, cy - 27), miaCol, lastBuf);
            }
            else
            {

                float margin = 40.f;
                float cx = std::clamp(lastSp.x, margin, (float)g_screenW - margin);
                float cy = std::clamp(lastSp.y, margin, (float)g_screenH - margin);

                float dx = lastSp.x - cx, dy = lastSp.y - cy;
                float len = sqrtf(dx*dx + dy*dy);
                if (len > 0.f) { dx /= len; dy /= len; }
                else { dx = 0; dy = -1; }

                float arrowLen = 14.f, arrowW = 8.f;
                ImVec2 tip(cx + dx * arrowLen, cy + dy * arrowLen);
                ImVec2 left(cx - dy * arrowW - dx * 4, cy + dx * arrowW - dy * 4);
                ImVec2 right(cx + dy * arrowW - dx * 4, cy - dx * arrowW - dy * 4);
                dl->AddTriangleFilled(tip, left, right, miaCol);

                char edgeBuf[80];
                if (isJungler)
                    snprintf(edgeBuf, sizeof(edgeBuf), "[JG] %s %.0fs", hero.name, timeSince);
                else
                    snprintf(edgeBuf, sizeof(edgeBuf), "%s %.0fs", hero.name, timeSince);
                auto es = ImGui::CalcTextSize(edgeBuf);
                float tx = cx - es.x/2, ty = cy - dy * 18 - es.y/2;
                tx = std::clamp(tx, 4.f, (float)g_screenW - es.x - 4.f);
                ty = std::clamp(ty, 4.f, (float)g_screenH - es.y - 4.f);
                dl->AddRectFilled(ImVec2(tx - 2, ty - 1), ImVec2(tx + es.x + 2, ty + es.y + 1), IM_COL32(20,20,20,255), 3.f);
                dl->AddText(ImVec2(tx, ty), miaCol, edgeBuf);
            }
            continue;
        }

        Vec2 screenPos;
        if (!WorldToScreen(hero.pos, screenPos)) continue;
        if (screenPos.x < 0 || screenPos.x > g_screenW || screenPos.y < 0 || screenPos.y > g_screenH) continue;

        ImU32 color = (isAlly ? allyCol : enemyCol) | 0xFF000000;
        if (!hero.visible && g_espVisStatus)
            color = IM_COL32(255, 180, 0, 255);

        float sc = ScreenScale(hero.pos);

        if (g_espBox) {
            float h = 80.f * sc, w = 40.f * sc;
            dl->AddRect(ImVec2(screenPos.x - w/2, screenPos.y - h),
                        ImVec2(screenPos.x + w/2, screenPos.y),
                        color, 0.f, 0, std::max(1.f, 2.f * sc));
        }

        if (g_espHealth && hero.maxHealth > 0.f) {
            float ratio = std::clamp(hero.health / hero.maxHealth, 0.f, 1.f);
            float barW = 50.f * sc, barH = std::max(3.f, 5.f * sc);
            float bx = screenPos.x - barW / 2;
            float by = screenPos.y + 5.f * sc;
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, by + barH), IM_COL32(40, 40, 40, 255));
            ImU32 hpCol = ratio > 0.5f ? IM_COL32(0, 255, 0, 255) : (ratio > 0.25f ? IM_COL32(255, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW * ratio, by + barH), hpCol);

            if (g_espShields) {
                float totalShield = hero.shield + hero.magicShield;
                if (totalShield > 1.f) {
                    float shieldRatio = std::clamp(totalShield / hero.maxHealth, 0.f, 1.f - ratio);
                    dl->AddRectFilled(ImVec2(bx + barW * ratio, by), ImVec2(bx + barW * (ratio + shieldRatio), by + barH), IM_COL32(255, 255, 255, 255));
                }
            }
        }

        if (g_espNames && hero.name[0]) {
            auto textSize = ImGui::CalcTextSize(hero.name);
            dl->AddText(ImVec2(screenPos.x - textSize.x/2, screenPos.y - 15.f * sc), color, hero.name);
        }

        // ── QWER cooldown indicators above head (enemies only) ──
        if (g_castCDTracker && !isAlly) {
            int heroIdx = (int)(&hero - &g_heroes[0]);
            if (heroIdx >= 0 && heroIdx < 12) {
                auto& cds = g_enemyCDs[heroIdx];
                const char* labels[] = { "Q", "W", "E", "R" };
                float boxW = 14.f * sc, boxH = 10.f * sc;
                float gap = 2.f * sc;
                float totalW = 4 * boxW + 3 * gap;
                float startX = screenPos.x - totalW / 2.f;
                float startY = screenPos.y + (g_espHealth ? 12.f : 5.f) * sc;

                for (int s = 0; s < 4; s++) {
                    float bx = startX + s * (boxW + gap);
                    auto& cd = cds.slots[s];
                    ImU32 bgCol, txtCol;
                    char txt[8];

                    if (!cd.valid) {
                        bgCol  = IM_COL32(50, 50, 55, 200);
                        txtCol = IM_COL32(120, 120, 130, 255);
                        snprintf(txt, sizeof(txt), "%s", labels[s]);
                    } else if (cd.readyAt <= g_gameTime) {
                        bgCol  = IM_COL32(20, 90, 30, 220);
                        txtCol = IM_COL32(80, 255, 100, 255);
                        snprintf(txt, sizeof(txt), "%s", labels[s]);
                    } else {
                        float rem = cd.readyAt - g_gameTime;
                        float cdPct = std::clamp(rem / std::max(cd.cooldownDuration, 1.f), 0.f, 1.f);
                        bgCol  = IM_COL32((uint8_t)(180 * cdPct), 20, 20, 220);
                        txtCol = IM_COL32(255, 255, 255, 255);
                        if (rem >= 10.f) snprintf(txt, sizeof(txt), "%.0f", rem);
                        else             snprintf(txt, sizeof(txt), "%.1f", rem);
                    }

                    dl->AddRectFilled(ImVec2(bx, startY), ImVec2(bx + boxW, startY + boxH), bgCol, 2.f);
                    auto tsz = ImGui::CalcTextSize(txt);
                    float tx = bx + (boxW - tsz.x) * 0.5f;
                    float ty = startY + (boxH - tsz.y) * 0.5f;
                    dl->AddText(ImVec2(tx, ty), txtCol, txt);
                }

                // ── Summoner Spell D/F indicators below QWER ──
                if (g_trackerSumms) {
                    float summY = startY + boxH + 2.f * sc;
                    float summBoxW = 2 * boxW + gap;
                    float summBoxH = boxH;
                    float summStartX = screenPos.x - (2 * summBoxW + gap) / 2.f;
                    const SummonerSpell* summs[2] = { &g_heroSumms[heroIdx].d, &g_heroSumms[heroIdx].f };
                    const char* summLabels[2] = { "D", "F" };
                    for (int s = 0; s < 2; s++) {
                        float sx = summStartX + s * (summBoxW + gap);
                        const auto& sp = *summs[s];
                        ImU32 sBg, sTx;
                        char sTxt[16];
                        if (!sp.name[0]) {
                            sBg  = IM_COL32(50, 50, 55, 200);
                            sTx  = IM_COL32(120, 120, 130, 255);
                            snprintf(sTxt, sizeof(sTxt), "%s", summLabels[s]);
                        } else if (sp.ready) {
                            sBg  = IM_COL32(20, 70, 90, 220);
                            sTx  = IM_COL32(80, 200, 255, 255);
                            // show short summoner name
                            const char* sn = sp.name;
                            if (strncmp(sn, "Summoner", 8) == 0) sn += 8;
                            snprintf(sTxt, sizeof(sTxt), "%s", sn);
                        } else {
                            float rem = sp.readyAt - g_gameTime; if (rem < 0) rem = 0;
                            float cdPct = std::clamp(rem / std::max(sp.totalCD, 1.f), 0.f, 1.f);
                            sBg  = IM_COL32((uint8_t)(140 * cdPct), 30, (uint8_t)(80 * (1.f - cdPct)), 220);
                            sTx  = IM_COL32(255, 255, 255, 255);
                            if (rem >= 10.f) snprintf(sTxt, sizeof(sTxt), "%.0fs", rem);
                            else             snprintf(sTxt, sizeof(sTxt), "%.1fs", rem);
                        }
                        dl->AddRectFilled(ImVec2(sx, summY), ImVec2(sx + summBoxW, summY + summBoxH), sBg, 2.f);
                        auto stsz = ImGui::CalcTextSize(sTxt);
                        dl->AddText(ImVec2(sx + (summBoxW - stsz.x) * 0.5f, summY + (summBoxH - stsz.y) * 0.5f), sTx, sTxt);
                    }
                }
            }
        }

        if (g_espVisStatus && !hero.visible) {
            dl->AddText(ImVec2(screenPos.x - 10.f, screenPos.y - 28.f * sc), IM_COL32(255, 180, 0, 255), "[FOG]");
        }

        if (g_espAttackRange && hero.attackRange > 50.f) {
            DrawWorldCircle(dl, hero.pos, hero.attackRange, color, 2.f, 0);
        }
    }

    // ── Practice Tool Dummies (draw like enemy champions) ──
    for (const auto& m : g_minions) {
        if (!m.addr || m.health <= 0.f || !m.visible) continue;
        if (m.team == g_localTeam) continue;
        if (m.maxHealth < 2000.f) continue; // dummies have high HP
        Vec2 sp;
        if (!WorldToScreen(m.pos, sp)) continue;
        if (sp.x < 0 || sp.x > g_screenW || sp.y < 0 || sp.y > g_screenH) continue;
        float sc = ScreenScale(m.pos);
        ImU32 dummyCol = IM_COL32(255, 100, 100, 255);
        if (g_espBox) {
            float h = 80.f * sc, w = 40.f * sc;
            dl->AddRect(ImVec2(sp.x - w/2, sp.y - h), ImVec2(sp.x + w/2, sp.y), dummyCol, 0.f, 0, std::max(1.f, 2.f * sc));
        }
        if (g_espHealth && m.maxHealth > 0.f) {
            float ratio = std::clamp(m.health / m.maxHealth, 0.f, 1.f);
            float barW = 50.f * sc, barH = std::max(3.f, 5.f * sc);
            float bx = sp.x - barW / 2;
            float by = sp.y + 5.f * sc;
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, by + barH), IM_COL32(40, 40, 40, 255));
            ImU32 hpCol = ratio > 0.5f ? IM_COL32(0, 255, 0, 255) : (ratio > 0.25f ? IM_COL32(255, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW * ratio, by + barH), hpCol);
        }
        if (g_espNames) {
            const char* tag = "Dummy";
            auto tsz = ImGui::CalcTextSize(tag);
            dl->AddText(ImVec2(sp.x - tsz.x/2, sp.y - 15.f * sc), dummyCol, tag);
        }
    }

    if (g_espTurretRange) {
        for (const auto& turret : g_turrets) {
            if (!turret.addr || turret.health <= 0.f) continue;
            bool isTAlly = (turret.team == g_localTeam);
            if (g_espEnemyOnly && isTAlly) continue;
            ImU32 tc = isTAlly ? IM_COL32(50, 120, 255, 255) : IM_COL32(255, 50, 50, 255);
            DrawWorldCircle(dl, turret.pos, g_turretRange, tc, 2.f, 0);

            Vec2 tsp;
            if (WorldToScreen(turret.pos, tsp) && turret.maxHealth > 0.f) {
                float tsc = ScreenScale(turret.pos);
                float tRatio = std::clamp(turret.health / turret.maxHealth, 0.f, 1.f);
                float tw = 60.f * tsc, th = std::max(3.f, 5.f * tsc);
                float tOff = 30.f * tsc;
                dl->AddRectFilled(ImVec2(tsp.x - tw/2, tsp.y + tOff), ImVec2(tsp.x + tw/2, tsp.y + tOff + th), IM_COL32(40, 40, 40, 255));
                dl->AddRectFilled(ImVec2(tsp.x - tw/2, tsp.y + tOff), ImVec2(tsp.x - tw/2 + tw * tRatio, tsp.y + tOff + th), tc);
            }
        }
    }

    if (g_espMinions) {
        for (const auto& m : g_minions) {
            if (!m.addr || m.health <= 0.f || !m.visible) continue;
            if (g_espEnemyOnly && m.team == g_localTeam) continue;
            Vec2 sp;
            if (!WorldToScreen(m.pos, sp)) continue;
            if (sp.x < 0 || sp.x > g_screenW || sp.y < 0 || sp.y > g_screenH) continue;

            bool isJungle = (m.team != g_localTeam && m.team != 1 && m.team != 2);
            ImU32 mc;
            if (isJungle)
                mc = IM_COL32(255, 200, 0, 255);
            else if (m.team == g_localTeam)
                mc = IM_COL32(0, 255, 0, 255);
            else
                mc = IM_COL32(255, 80, 80, 255);

            float circleRadius = isJungle ? 80.f : 50.f;
            DrawWorldCircle(dl, m.pos, circleRadius, mc, 2.f, 0);

            float msc = ScreenScale(m.pos);
            float ratio = std::clamp(m.health / std::max(m.maxHealth, 1.f), 0.f, 1.f);
            float bw = (isJungle ? 40.f : 30.f) * msc;
            float bh = std::max(2.f, (isJungle ? 4.f : 3.f) * msc);
            float bOff = 3.f * msc;
            dl->AddRectFilled(ImVec2(sp.x - bw/2, sp.y + bOff), ImVec2(sp.x + bw/2, sp.y + bOff + bh), IM_COL32(40, 40, 40, 255));
            dl->AddRectFilled(ImVec2(sp.x - bw/2, sp.y + bOff), ImVec2(sp.x - bw/2 + bw * ratio, sp.y + bOff + bh), mc);
        }
    }

    // ── Jungler Danger Alert (on-screen warning) ──
    if (g_junglerDangerAlert && g_junglerAlertTime > 0.f) {
        float alertElapsed = g_gameTime - g_junglerAlertTime;
        if (alertElapsed < g_junglerAlertDuration && g_enemyJunglerIdx >= 0 && g_enemyJunglerIdx < (int)g_heroes.size()) {
            float alpha = 1.f - (alertElapsed / g_junglerAlertDuration);
            alpha = std::clamp(alpha, 0.f, 1.f);
            uint8_t a = (uint8_t)(alpha * 255);
            float pulse = 0.5f + 0.5f * sinf(g_gameTime * 8.f);
            uint8_t r = (uint8_t)(200 + 55 * pulse);

            // red border flash
            float borderW = 6.f * alpha;
            dl->AddRect(ImVec2(0, 0), ImVec2((float)g_screenW, (float)g_screenH), IM_COL32(r, 0, 0, a / 2), 0.f, 0, borderW);
            dl->AddRect(ImVec2(borderW, borderW), ImVec2((float)g_screenW - borderW, (float)g_screenH - borderW), IM_COL32(r, 0, 0, a / 4), 0.f, 0, borderW / 2);

            // warning text at top center
            const auto& jg = g_heroes[g_enemyJunglerIdx];
            char alertBuf[128];
            float dist = sqrtf(
                (jg.pos.x - g_localPos.x) * (jg.pos.x - g_localPos.x) +
                (jg.pos.z - g_localPos.z) * (jg.pos.z - g_localPos.z));
            snprintf(alertBuf, sizeof(alertBuf), "!! ENEMY JG SPOTTED: %s (%.0f units) !!", jg.name, dist);
            auto ts = ImGui::CalcTextSize(alertBuf);
            float tx = ((float)g_screenW - ts.x) / 2.f;
            float ty = 60.f;
            dl->AddRectFilled(ImVec2(tx - 12, ty - 6), ImVec2(tx + ts.x + 12, ty + ts.y + 6), IM_COL32(20, 0, 0, (uint8_t)(200 * alpha)), 6.f);
            dl->AddRect(ImVec2(tx - 12, ty - 6), ImVec2(tx + ts.x + 12, ty + ts.y + 6), IM_COL32(r, 0, 0, a), 6.f, 0, 2.f);
            dl->AddText(ImVec2(tx, ty), IM_COL32(255, 50, 50, a), alertBuf);
        }
    }

    // ── Skillshot dodge alert lines ──
    if (g_skillshotAlerts && !g_skillshots.empty()) {
        for (const auto& ss : g_skillshots) {
            float elapsed = g_gameTime - ss.startTime;
            if (elapsed > ss.duration + 0.3f) continue;

            float dx = ss.endPos.x - ss.startPos.x;
            float dz = ss.endPos.z - ss.startPos.z;
            float len = sqrtf(dx * dx + dz * dz);
            if (len < 1.f) continue;
            dx /= len; dz /= len;

            float moveElapsed = (ss.delayS > 0.f) ? std::max(0.f, elapsed - ss.delayS) : elapsed;
            float traveled = ss.missileSpeed * moveElapsed;
            if (traveled > ss.castRange) traveled = ss.castRange;

            Vec3 missileHead = { ss.startPos.x + dx * traveled, ss.startPos.y, ss.startPos.z + dz * traveled };
            Vec3 lineEnd     = { ss.startPos.x + dx * ss.castRange, ss.startPos.y, ss.startPos.z + dz * ss.castRange };

            float px = -dz * ss.lineWidth * 0.5f;
            float pz =  dx * ss.lineWidth * 0.5f;

            Vec3 corners[4] = {
                { missileHead.x + px, missileHead.y, missileHead.z + pz },
                { missileHead.x - px, missileHead.y, missileHead.z - pz },
                { lineEnd.x - px, lineEnd.y, lineEnd.z - pz },
                { lineEnd.x + px, lineEnd.y, lineEnd.z + pz }
            };

            ImVec2 screenPts[4];
            int validPts = 0;
            for (int c = 0; c < 4; c++) {
                Vec2 sp;
                if (WorldToScreen(corners[c], sp))
                    screenPts[validPts++] = ImVec2(sp.x, sp.y);
            }

            if (validPts >= 3) {
                float alpha = 1.0f;
                if (elapsed > ss.duration * 0.8f)
                    alpha = 1.0f - (elapsed - ss.duration * 0.8f) / (ss.duration * 0.2f + 0.3f);
                if (alpha < 0.f) alpha = 0.f;
                uint8_t a = (uint8_t)(alpha * 180.f);

                float distToLocal = sqrtf(
                    (missileHead.x - g_localPos.x) * (missileHead.x - g_localPos.x) +
                    (missileHead.z - g_localPos.z) * (missileHead.z - g_localPos.z));

                ImU32 fillCol = IM_COL32(255, 40, 40, a / 6);
                ImU32 lineCol = IM_COL32(255, 60, 60, (uint8_t)(alpha * 140.f));
                if (distToLocal < ss.castRange * 1.2f) {
                    fillCol = IM_COL32(255, 20, 20, a / 4);
                    lineCol = IM_COL32(255, 40, 40, (uint8_t)(alpha * 200.f));
                }

                dl->AddConvexPolyFilled(screenPts, validPts, fillCol);
                dl->AddPolyline(screenPts, validPts, lineCol, ImDrawFlags_Closed, 2.f);

                Vec2 headSp;
                if (WorldToScreen(missileHead, headSp)) {
                    char label[48];
                    snprintf(label, sizeof(label), "! %s", ss.spellName);
                    auto tsz = ImGui::CalcTextSize(label);
                    dl->AddRectFilled(
                        ImVec2(headSp.x - tsz.x/2 - 3, headSp.y - tsz.y - 6),
                        ImVec2(headSp.x + tsz.x/2 + 3, headSp.y - 2),
                        IM_COL32(180, 20, 20, (uint8_t)(alpha * 140.f)));
                    dl->AddText(ImVec2(headSp.x - tsz.x/2, headSp.y - tsz.y - 4),
                        IM_COL32(255, 255, 255, (uint8_t)(alpha * 255.f)), label);
                }
            }
        }
    }

    if (!g_recallAlerts.empty()) {
        float alertY = 120.f;
        for (const auto& alert : g_recallAlerts) {
            float age = g_gameTime - alert.triggerTime;
            if (age > 5.f) continue;

            char alertTxt[128];
            snprintf(alertTxt, sizeof(alertTxt), "  RECALL  %s  ", alert.name);
            auto sz = ImGui::CalcTextSize(alertTxt);
            float cx = g_screenW / 2.f;

            dl->AddRectFilled(
                ImVec2(cx - sz.x/2 - 8, alertY - 2),
                ImVec2(cx + sz.x/2 + 8, alertY + sz.y + 4),
                IM_COL32(140, 20, 20, 255), 6.f);
            dl->AddRect(
                ImVec2(cx - sz.x/2 - 8, alertY - 2),
                ImVec2(cx + sz.x/2 + 8, alertY + sz.y + 4),
                IM_COL32(255, 60, 60, 255), 6.f, 0, 2.f);
            dl->AddText(ImVec2(cx - sz.x/2, alertY + 1), IM_COL32(255, 255, 255, 255), alertTxt);

            alertY += sz.y + 12.f;
        }
    }
}

static void DrawMinimap()
{
    if (!g_minimapEnabled || g_gameTime < 1.0f) return;
    MinimapAutoDefaults();
    auto* dl = ImGui::GetBackgroundDrawList();

    constexpr float MAP_MIN = -120.f;
    constexpr float MAP_MAX = 14870.f;
    constexpr float MAP_RANGE = MAP_MAX - MAP_MIN;

    float sz = g_minimapSize;
    float ox = g_minimapOffX;
    float oy = g_minimapOffY;

    dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + sz, oy + sz), IM_COL32(15, 20, 15, 255), 4.f);
    dl->AddRect(ImVec2(ox, oy), ImVec2(ox + sz, oy + sz), IM_COL32(80, 80, 80, 255), 4.f, 0, 1.5f);

    auto worldToMap = [&](const Vec3& w) -> ImVec2 {
        float mx = (w.x - MAP_MIN) / MAP_RANGE * sz;
        float my = (1.f - (w.z - MAP_MIN) / MAP_RANGE) * sz; // Z flipped: top of map = high Z
        return ImVec2(ox + std::clamp(mx, 1.f, sz - 1.f), oy + std::clamp(my, 1.f, sz - 1.f));
    };

    for (const auto& t : g_turrets) {
        if (!t.addr || t.health <= 0.f) continue;
        ImVec2 tp = worldToMap(t.pos);
        ImU32 tc = (t.team == g_localTeam) ? IM_COL32(60, 140, 255, 255) : IM_COL32(255, 60, 60, 255);
        dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 3), ImVec2(tp.x + 3, tp.y + 3), tc);
    }

    for (int mi = 0; mi < (int)g_heroes.size(); mi++) {
        const auto& h = g_heroes[mi];
        if (!h.addr) continue;
        bool isAlly = (h.team == g_localTeam);
        bool isLocal = (h.addr == g_localAddr);
        bool isJgMap = (g_junglerTracker && mi == g_enemyJunglerIdx);

        Vec3 drawPos = h.visible ? h.pos : h.lastPos;
        if (h.isDead && !isLocal) continue;

        ImVec2 mp = worldToMap(drawPos);
        float r = isLocal ? 5.f : (isJgMap ? 5.f : 4.f);

        ImU32 col;
        if (isLocal) {
            col = IM_COL32(0, 255, 120, 255);
        } else if (isAlly) {
            col = IM_COL32(60, 160, 255, 255);
        } else if (isJgMap) {
            float pulse = 0.7f + 0.3f * sinf(g_gameTime * 4.f);
            uint8_t mag = (uint8_t)(255 * pulse);
            col = IM_COL32(mag, 0, 255, 255);
        } else {

            if (h.visible) {
                col = IM_COL32(255, 60, 60, 255);
            } else if (h.lastSeenTime > 0.f) {
                float timeSince = g_gameTime - h.lastSeenTime;
                uint8_t fade = (uint8_t)(std::max(0.3f, 1.f - timeSince / 60.f) * 255);
                col = IM_COL32(fade, fade / 3, fade / 3, 255);
                r = 3.f;
            } else {
                continue;
            }

            if (h.addr == g_orbSelectedAddr) {
                dl->AddCircle(mp, r + 3, IM_COL32(255, 200, 0, 255), 0, 2.f);
            }
        }

        dl->AddCircleFilled(mp, r, col);

        if (isLocal)
            dl->AddCircle(mp, r, IM_COL32(255, 255, 255, 255), 0, 1.5f);
    }

    dl->AddText(ImVec2(ox + 4, oy + 2), IM_COL32(200, 200, 200, 255), "MAP");
}

static void MoveCursorTo(float targetX, float targetY)
{
    int dx = (int)(targetX - g_cursorX);
    int dy = (int)(targetY - g_cursorY);
    if (dx == 0 && dy == 0) return;
    InputDevice::MoveTo(dx, dy);
    g_cursorX = targetX;
    g_cursorY = targetY;
}

#include "LuaEngine.h"

static void OrbwalkerTick()
{
    if (!g_orbEnabled || !InputDevice::IsConnected() || !Keyboard::IsReady()) return;
    if (g_gameTime < 1.0f) return;

    int mode = 0;
    if      (g_orbModeEnabled[0] && Keyboard::IsKeyDown(HidToVk(g_keyOptions[g_orbModeKeyIdx[0]].hid))) mode = 1;
    else if (g_orbModeEnabled[1] && Keyboard::IsKeyDown(HidToVk(g_keyOptions[g_orbModeKeyIdx[1]].hid))) mode = 2;
    else if (g_orbModeEnabled[2] && Keyboard::IsKeyDown(HidToVk(g_keyOptions[g_orbModeKeyIdx[2]].hid))) mode = 3;
    else if (g_orbModeEnabled[3] && Keyboard::IsKeyDown(HidToVk(g_keyOptions[g_orbModeKeyIdx[3]].hid))) mode = 4;
    else if (g_orbModeEnabled[4] && Keyboard::IsKeyDown(HidToVk(g_keyOptions[g_orbModeKeyIdx[4]].hid))) mode = 5;
    g_orbMode = mode;
    if (mode == 0) { g_orbTargetIdx = -1; g_orbTargetIsMinion = false; g_orbStickyAddr = 0; g_orbStickyUntil = 0; g_orbLastActionTarget = 0; return; }

    ULONGLONG now = GetTickCount64();
    float effectiveAS = g_orbAttackSpeed;
    float effectiveAD = g_orbPlayerAD;
    if (g_orbAutoStats) {
        float totalAD = g_localBaseAD + g_localBonusAD;
        if (totalAD > 10.f && totalAD < 2000.f) effectiveAD = totalAD;
        float baseAS = (g_localASScale > 0.5f && g_localASScale < 0.85f) ? g_localASScale : g_orbBaseAS;
        float totalAS = baseAS * g_localASMod;
        if (totalAS > 0.2f && totalAS < 10.f) effectiveAS = std::clamp(totalAS, 0.2f, 2.5f);
    } else {
        effectiveAS = std::clamp(effectiveAS, 0.2f, 2.5f);
    }
    g_orbEffectiveAD = effectiveAD;
    float attackDelayMs = 1000.f / std::max(effectiveAS, 0.1f);
    // Windup: prefer game-state cast end time (auto-detected), fall back to manual percentage
    int windPct = g_orbWindupPct;
    if (g_localChampName[0]) {
        auto wit = g_orbWindupPctChamp.find(g_localChampName);
        if (wit != g_orbWindupPctChamp.end()) windPct = wit->second;
    }
    float windupMsFallback = std::max(60.f, attackDelayMs * (std::clamp(windPct, 1, 100) / 100.f));
    float postWindupMargin = (attackDelayMs < 500.f) ? 15.f : 10.f;
    if (windupMsFallback > attackDelayMs - postWindupMargin) windupMsFallback = std::max(60.f, attackDelayMs - postWindupMargin);

    static float attackJitter = 0.f;
    static float moveJitter   = 0.f;
    static float sessionDrift = 0.f;
    if (sessionDrift == 0.f) sessionDrift = g_orbHumanize ? xorFloat(-8.f, 8.f) : 0.f;
    // ping comp: use half-ping (DMA reads are separate from network RTT) + bias
    float pingComp = g_orbPingComp ? (0.5f * std::max(0.f, g_orbPingMs) + g_orbTimingBias) : 0.f;
    float humanJitter = g_orbHumanize ? (attackJitter + sessionDrift) : 0.f;
    ULONGLONG timeSinceAttack = now - g_orbLastAttack;
    // If we've been idle for over 2x the attack delay, we're definitely ready — no extra delay
    bool canAttack = (timeSinceAttack >= (ULONGLONG)(attackDelayMs * 2.f))
                   || (timeSinceAttack >= (ULONGLONG)(attackDelayMs + pingComp + humanJitter));

    // Use game-state windup detection: g_activeCastEnd is when the game says the cast/windup phase ends
    // This is the EXACT windup timing per champion, auto-detected — no manual percentage needed
    bool gameSaysWindup = false;
    if (g_activeIsAuto && g_gameTime > 1.f) {
        // g_activeCastEnd (0x1A4) = when the windup/cast ends (can move after this)
        if (g_activeCastEnd > g_gameTime + 0.001f && g_activeCastEnd < g_gameTime + 2.0f)
            gameSaysWindup = true;
        // also check g_activeAttackEnd as a secondary safety net
        else if (g_activeAttackEnd > g_gameTime + 0.001f && g_activeAttackEnd < g_gameTime + 2.0f)
            gameSaysWindup = true;
    }
    // canMove: game-state takes priority (auto-detected windup), timer is fallback safety
    bool timerSaysWindup = (now - g_orbLastAttack) < (ULONGLONG)(windupMsFallback + moveJitter);
    bool canMove = !gameSaysWindup && !timerSaysWindup;
    auto aimEnabled = [&]() { return g_orbCursorPolicy == 1; };

    auto dist3d = [](const Vec3& a, const Vec3& b) -> float {
        float dx = a.x - b.x, dz = a.z - b.z;
        return sqrtf(dx*dx + dz*dz);
    };
    auto safeRadius = [](float r) { return (r > 10.f && r < 200.f) ? r : 65.f; };
    float localRadius = safeRadius(g_localBoundingRadius);
    auto attackRangeTo = [&](float targetRadius) {
        return g_localAttackRange + localRadius + safeRadius(targetRadius) + 12.f;
    };
    auto getPrio = [](const char* name) -> int {
        auto it = g_orbPriority.find(name);
        return (it != g_orbPriority.end()) ? std::clamp(it->second, 1, 5) : 3;
    };
    auto orbDoMove = [&](int minGapMs) {
        if (!canMove) return;
        if ((now - g_orbLastMove) <= (ULONGLONG)(minGapMs + (g_orbHumanize ? randInt(0, 10) : 0))) return;
        InputDevice::RightDown();
        InputDevice::RightUp();
        g_orbLastMove = now;
    };

    // track visible minion HP for prediction
    for (const auto& m : g_minions) {
        if (m.addr && m.visible && m.health > 0.f) OrbTrackMinionHp(m.addr, m.health, now);
    }

    if (mode == 5) {
        orbDoMove(70);
        g_orbTargetIdx = -1; g_orbTargetIsMinion = false;
        return;
    }

    // mouse mode
    if (g_orbTargetMode == 0) {
        POINT cp; if (GetCursorPos(&cp)) { g_cursorX = (float)cp.x; g_cursorY = (float)cp.y; }
        Vec2 cursorSp = { g_cursorX, g_cursorY };
        bool cursorValid = (cursorSp.x > 0 && cursorSp.x < g_screenW && cursorSp.y > 0 && cursorSp.y < g_screenH);

        if ((mode == 1 || mode == 2) && cursorValid) {
            const bool* useSpell[] = {&g_orbUseQ, &g_orbUseW, &g_orbUseE, &g_orbUseR};
            for (int s = 0; s < 4; s++) {
                if (!*useSpell[s]) continue;
                if (g_orbReadCooldowns && g_spells[s].level > 0 && !g_spells[s].ready) continue;
                if ((now - g_orbLastSpell[s]) < (ULONGLONG)(g_orbSpellGateMs + (g_orbHumanize ? randInt(0, 10) : 0))) continue;
                PressKey(g_keyOptions[g_orbSpellKeyIdx[s]].hid);
                g_orbLastSpell[s] = now;
                break;
            }
        }

        bool didAttack = false;
        if (canAttack && cursorValid) {
            IssueAttackKey(g_keyOptions[g_orbAtkKeyIdx].hid);
            g_orbLastAttack = now;
            didAttack = true;
            attackJitter = g_orbHumanize ? xorFloat(2.f, 8.f) : 0.f;
            moveJitter   = g_orbHumanize ? xorFloat(1.f, 5.f) : 0.f;
            sessionDrift = g_orbHumanize ? std::clamp(sessionDrift + xorFloat(-1.f, 1.f), -4.f, 4.f) : 0.f;
        }
        if (!didAttack) orbDoMove(std::max(20, (int)g_orbMoveDelay));
        g_orbTargetIdx = -1; g_orbTargetIsMinion = false;
        return;
    }

    // target mode
    // click-select target (also in orbwalk modes when enabled)
    {
        static bool prevLMB = false;
        bool curLMB = Keyboard::IsKeyDown(VK_LBUTTON);
        bool allowSelect = (mode == 0) || (g_orbClickSelectInMode && g_orbTargetMode == 1);
        if (curLMB && !prevLMB && allowSelect) {
            POINT cp; if (GetCursorPos(&cp)) { g_cursorX = (float)cp.x; g_cursorY = (float)cp.y; }
            float bestDist = 60.f;
            int bestIdx = -1;
            for (int i = 0; i < (int)g_heroes.size(); i++) {
                const auto& h = g_heroes[i];
                if (!h.addr || h.team == g_localTeam || h.health <= 0.f) continue;
                Vec2 sp;
                if (!WorldToScreen(h.pos, sp)) continue;
                float dx = sp.x - g_cursorX, dy = sp.y - g_cursorY;
                float d = sqrtf(dx*dx + dy*dy);
                if (d < bestDist) { bestDist = d; bestIdx = i; }
            }
            if (bestIdx >= 0) {
                g_orbSelectedAddr = g_heroes[bestIdx].addr;
                g_orbSelectedHero = bestIdx;
                g_orbLastActionTarget = 0; g_orbLastActionEvent = now;
            } else if (mode == 0) {
                g_orbSelectedAddr = 0;
                g_orbSelectedHero = -1;
            }
        }
        prevLMB = curLMB;
    }

    if (g_orbSelectedAddr) {
        g_orbSelectedHero = -1;
        for (int i = 0; i < (int)g_heroes.size(); i++) {
            if (g_heroes[i].addr == g_orbSelectedAddr) { g_orbSelectedHero = i; break; }
        }
        if (g_orbSelectedHero >= 0) {
            const auto& sel = g_heroes[g_orbSelectedHero];
            if (sel.isDead || sel.health <= 0.f) { g_orbSelectedAddr = 0; g_orbSelectedHero = -1; }
        } else {
            g_orbSelectedAddr = 0;
        }
    }

    g_orbTargetIdx = -1;
    g_orbTargetIsMinion = false;
    Vec3 targetWorldPos{};
    uintptr_t targetAddr = 0;
    float targetRadius = 65.f;
    float targetRange = 0.f;
    bool targetIsDummy = false;

    if (mode == 1 || mode == 2) {
        if (g_orbSelectedHero >= 0) {
            const auto& sel = g_heroes[g_orbSelectedHero];
            float rng = attackRangeTo(sel.boundingRadius);
            if (sel.visible && sel.health > 0.f && dist3d(g_localPos, sel.pos) <= rng) {
                g_orbTargetIdx = g_orbSelectedHero; g_orbTargetIsMinion = false; targetWorldPos = sel.pos; targetAddr = sel.addr; targetRadius = sel.boundingRadius; targetRange = rng;
            }
        }

        // Sticky target
        if (g_orbTargetIdx < 0 && g_orbStickyAddr && now < g_orbStickyUntil) {
            bool kept = false;
            for (int i = 0; i < (int)g_heroes.size(); i++) {
                const auto& h = g_heroes[i];
                if (h.addr != g_orbStickyAddr) continue;
                float rng = attackRangeTo(h.boundingRadius);
                if (h.health > 0.f && h.visible && dist3d(g_localPos, h.pos) <= rng * 1.05f) {
                    g_orbTargetIdx = i; g_orbTargetIsMinion = false; targetWorldPos = h.pos; targetAddr = h.addr; targetRadius = h.boundingRadius; targetRange = rng; kept = true;
                }
                break;
            }
            if (!kept) { g_orbStickyAddr = 0; g_orbStickyUntil = 0; }
        } else if (g_orbStickyAddr && now >= g_orbStickyUntil) { g_orbStickyAddr = 0; g_orbStickyUntil = 0; }

        if (g_orbTargetIdx < 0) {
            float bestScore = -999999.f;
            for (int i = 0; i < (int)g_heroes.size(); i++) {
                const auto& h = g_heroes[i];
                if (!h.addr || h.team == g_localTeam || h.health <= 0.f || !h.visible) continue;
                float d = dist3d(g_localPos, h.pos);
                float rng = attackRangeTo(h.boundingRadius);
                if (d > rng) continue;

                float score = 0.f;
                switch (g_orbPriorityMode) {
                case 0: score = -d; break;
                case 1: score = -h.health; break;
                case 2: score = h.baseAD + h.bonusAD; break;
                case 3: score = -d; break; // AP not available in hero cache
                default: score = -d; break;
                }
                score += (float)(5 - getPrio(h.name)) * 60.f;
                if (score > bestScore) {
                    bestScore = score;
                    g_orbTargetIdx = i; g_orbTargetIsMinion = false; targetWorldPos = h.pos; targetAddr = h.addr; targetRadius = h.boundingRadius; targetRange = rng;
                }
            }
        }

        // target dummies (minions with maxHP > 2000)
        if (g_orbTargetIdx < 0) {
            float bestDist = 99999.f;
            for (int i = 0; i < (int)g_minions.size(); i++) {
                const auto& m = g_minions[i];
                if (!m.addr || m.team == g_localTeam || m.health <= 0.f || !m.visible) continue;
                if (m.maxHealth < 2000.f) continue;
                float d = dist3d(g_localPos, m.pos);
                float rng = attackRangeTo(m.boundingRadius);
                if (d > rng) continue;
                if (d < bestDist) {
                    bestDist = d;
                    g_orbTargetIdx = i; g_orbTargetIsMinion = true; targetIsDummy = true; targetWorldPos = m.pos; targetAddr = m.addr; targetRadius = m.boundingRadius; targetRange = rng;
                }
            }
        }
    }
    else if (mode == 3 || mode == 4) {
        float lastHitThresh = effectiveAD * (std::clamp(g_orbLastHitBufferPct, 50, 100) / 100.f);
        float bestScore = -999999.f;
        int fallbackIdx = -1; float fallbackD = 999999.f;
        for (int i = 0; i < (int)g_minions.size(); i++) {
            const auto& m = g_minions[i];
            if (!m.addr || m.team == g_localTeam || m.health <= 0.f || !m.visible) continue;
            float d = dist3d(g_localPos, m.pos);
            float rng = attackRangeTo(m.boundingRadius);
            if (d > rng) continue;
            if (mode == 3 && d < fallbackD) { fallbackD = d; fallbackIdx = i; }
            // melee (range < 300) = instant hit, ranged = projectile travel
            float travelMs = (g_localAttackRange < 300.f) ? 0.f : (d / 1800.f) * 1000.f;
            float hitMs = windupMsFallback + travelMs + std::max(0.f, g_orbPingMs * 0.25f);
            float pred = OrbPredictMinionHp(m.addr, m.health, hitMs, now);
            if (pred > 0.f && pred <= lastHitThresh) {
                float score = 100000.f - pred + (m.maxHealth > 1000.f ? 50.f : 0.f);
                if (score > bestScore) {
                    bestScore = score; g_orbTargetIdx = i; g_orbTargetIsMinion = true; targetWorldPos = m.pos; targetAddr = m.addr; targetRadius = m.boundingRadius; targetRange = rng;
                }
            }
        }
        if (g_orbTargetIdx < 0 && mode == 3 && !g_orbLastHitOnly && fallbackIdx >= 0) {
            const auto& m = g_minions[fallbackIdx];
            float rng = attackRangeTo(m.boundingRadius);
            g_orbTargetIdx = fallbackIdx; g_orbTargetIsMinion = true; targetWorldPos = m.pos; targetAddr = m.addr; targetRadius = m.boundingRadius; targetRange = rng;
        }
    }

    bool hasTarget = (g_orbTargetIdx >= 0);
    if (hasTarget) OrbNoteActionEvent(targetAddr, now);
    bool reactReady = !hasTarget || OrbCanReact(now);
    Vec2 targetScreenPos{};
    bool targetOnScreen = false;
    if (g_orbTargetIdx >= 0) {
        // keep target selected for draw; react gate only blocks action
        uintptr_t drawAddr = 0; float drawRadius = 65.f; Vec3 drawPos{};
        if (g_orbTargetIsMinion && g_orbTargetIdx < (int)g_minions.size()) { drawAddr = g_minions[g_orbTargetIdx].addr; drawRadius = g_minions[g_orbTargetIdx].boundingRadius; drawPos = g_minions[g_orbTargetIdx].pos; }
        else if (!g_orbTargetIsMinion && g_orbTargetIdx < (int)g_heroes.size()) { drawAddr = g_heroes[g_orbTargetIdx].addr; drawRadius = g_heroes[g_orbTargetIdx].boundingRadius; drawPos = g_heroes[g_orbTargetIdx].pos; }
        RollClickOffset(drawAddr ? drawAddr : targetAddr, drawRadius);
        targetOnScreen = WorldToScreen(ApplyClickOffset(drawPos.x || drawPos.y || drawPos.z ? drawPos : targetWorldPos), targetScreenPos);
    } else {
        g_orbClickTarget = 0;
    }
    bool actionHasTarget = hasTarget && reactReady;
    if (g_orbTargetIdx >= 0 && g_orbTargetIsMinion && g_orbTargetIdx < (int)g_minions.size())
        targetIsDummy = g_minions[g_orbTargetIdx].maxHealth >= 2000.f;

    // Compute predicted aim position (current pos + velocity * prediction time)
    // prediction lambda: given delay in seconds, returns screen pos with movement prediction
    Vec3 targetVelocity{};
    if (!g_orbTargetIsMinion && g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_heroes.size())
        targetVelocity = g_heroes[g_orbTargetIdx].velocity;

    auto predictedScreenPos = [&](float delaySec) -> std::pair<Vec2, bool> {
        Vec3 basePos{};
        if (g_orbTargetIsMinion && g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_minions.size())
            basePos = g_minions[g_orbTargetIdx].pos;
        else if (!g_orbTargetIsMinion && g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_heroes.size())
            basePos = g_heroes[g_orbTargetIdx].pos;
        if (!(basePos.x || basePos.y || basePos.z))
            return {targetScreenPos, targetOnScreen};
        // apply movement prediction
        Vec3 predicted = basePos;
        if (delaySec > 0.f) {
            predicted.x += targetVelocity.x * delaySec;
            predicted.z += targetVelocity.z * delaySec;
        }
        Vec2 sp{};
        bool ok = WorldToScreen(predicted, sp);
        return {sp, ok};
    };

    // default aim pos (no prediction) for attack targeting
    Vec2 targetTrueScreenPos{};
    bool targetTrueOnScreen = false;
    {
        auto [sp, ok] = predictedScreenPos(0.f);
        targetTrueScreenPos = sp;
        targetTrueOnScreen = ok;
    }

    Vec2 cursorRestore{}; bool cursorMoved = false;
    auto aimAction = [&](const Vec2& aim, bool doAim, bool forceAim = false) {
        if (!doAim || (!forceAim && !aimEnabled())) return;
        float dx = aim.x - g_cursorX, dy = aim.y - g_cursorY;
        if ((dx*dx + dy*dy) < 9.f) return;
        if (!cursorMoved) cursorRestore = { g_cursorX, g_cursorY };
        MoveCursorPath(aim.x, aim.y, 0);
        cursorMoved = true;
    };

    // spells (combo/harass) on heroes or dummies — only when NOT in AA windup to avoid canceling autos
    bool canSpellTarget = actionHasTarget && targetOnScreen && canMove && (mode == 1 || mode == 2) && (!g_orbTargetIsMinion || targetIsDummy);
    if (canSpellTarget) {
        bool didSpell = false;
        if (g_comboEnabled && g_localChampName[0] != 0) {
            ChampionCombo::Query q = {};
            q.champion = g_localChampName;
            if (g_orbTargetIsMinion && g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_minions.size()) {
                const auto& tgtm = g_minions[g_orbTargetIdx];
                float dx = g_localPos.x - tgtm.pos.x, dz = g_localPos.z - tgtm.pos.z; q.distance = sqrtf(dx*dx + dz*dz);
                q.targetHpPct = (tgtm.maxHealth > 0.f) ? (tgtm.health / tgtm.maxHealth * 100.f) : 100.f;
                q.targetHp = tgtm.health; q.targetArmor = 0.f;
            } else if (g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_heroes.size()) {
                const auto& tgt = g_heroes[g_orbTargetIdx];
                float dx = g_localPos.x - tgt.pos.x, dz = g_localPos.z - tgt.pos.z; q.distance = sqrtf(dx*dx + dz*dz);
                q.targetHpPct = (tgt.maxHealth > 0.f) ? (tgt.health / tgt.maxHealth * 100.f) : 100.f;
                q.targetHp = tgt.health; q.targetArmor = tgt.armor;
            }
            q.localHpPct  = (g_localMaxHealth > 0.f) ? (g_localHealth / g_localMaxHealth * 100.f) : 100.f;
            q.totalAttackDamage = effectiveAD;
            q.bonusAttackDamage = g_localBonusAD;
            q.localAttackRange = g_localAttackRange;
            q.aaGapMs = (int)attackDelayMs;
            q.hasAttackTarget = actionHasTarget;
            q.hasSpellWindow = canAttack || canMove;
            q.targetIsMinion = g_orbTargetIsMinion && !targetIsDummy;
            for (int s = 0; s < 4; s++) {
                q.spells[s].slot = s;
                q.spells[s].key = g_keyOptions[g_orbSpellKeyIdx[s]].hid;
                q.spells[s].ready = g_orbReadCooldowns ? g_spells[s].ready : true;
                q.spells[s].level = g_spells[s].level;
                q.spells[s].name = g_spells[s].name;
                q.spells[s].range = g_spells[s].range;
                q.retryReady[s] = (now >= g_comboRetryCooldown[s]);
            }
            ChampionCombo::Action act = ChampionCombo::Choose(q, g_comboSettings);
            if (act.valid && act.slot >= 0 && act.slot < 4 && act.castHoldMs == 0 && !act.useAuxTarget) {
                if (act.castType == ChampionCombo::CastType::Position) {
                    // predict where enemy will be when spell lands
                    float predTime = act.delaySeconds + 0.05f; // spell delay + network/input lag
                    auto [predSp, predOk] = predictedScreenPos(predTime);
                    aimAction(predOk ? predSp : (targetTrueOnScreen ? targetTrueScreenPos : targetScreenPos), true, true);
                    Sleep(25); // KMBox USB round-trip needs ~20ms for game to register cursor
                }
                PressKey((uint8_t)act.key);
                if (act.castType == ChampionCombo::CastType::Position)
                    Sleep(20); // hold cursor on target until game processes the cast
                g_orbLastSpell[act.slot] = now;
                if (act.retryMs > 0) g_comboRetryCooldown[act.slot] = now + act.retryMs;
                didSpell = true;
            }
        }
        if (!didSpell) {
            const bool* useSpell[] = {&g_orbUseQ, &g_orbUseW, &g_orbUseE, &g_orbUseR};
            for (int s = 0; s < 4; s++) {
                if (!*useSpell[s]) continue;
                if (g_orbReadCooldowns && g_spells[s].level > 0 && !g_spells[s].ready) continue;
                if ((now - g_orbLastSpell[s]) < (ULONGLONG)(g_orbSpellGateMs + (g_orbHumanize ? randInt(0, 10) : 0))) continue;
                {
                    // generic fallback: predict with ~0.3s default delay
                    auto [predSp, predOk] = predictedScreenPos(0.3f);
                    aimAction(predOk ? predSp : (targetTrueOnScreen ? targetTrueScreenPos : targetScreenPos), true, true);
                }
                Sleep(25); // KMBox USB round-trip needs ~20ms for game to register cursor
                PressKey(g_keyOptions[g_orbSpellKeyIdx[s]].hid);
                Sleep(20); // hold cursor on target until game processes the cast
                g_orbLastSpell[s] = now;
                break;
            }
        }
    }

    // attack
    bool didAttack = false;
    if (canAttack && actionHasTarget) {
        float dNow = dist3d(g_localPos, targetWorldPos);
        if (dNow <= targetRange * 1.08f) {
            if (targetOnScreen) aimAction(targetScreenPos, true);
            IssueAttackKey(g_keyOptions[g_orbAtkKeyIdx].hid);
            g_orbLastAttack = now;
            didAttack = true;
            attackJitter = g_orbHumanize ? xorFloat(2.f, 8.f) : 0.f;
            moveJitter   = g_orbHumanize ? xorFloat(1.f, 5.f) : 0.f;
            sessionDrift = g_orbHumanize ? std::clamp(sessionDrift + xorFloat(-1.f, 1.f), -4.f, 4.f) : 0.f;
            g_orbClickOffY = 0.f;
            if (!g_orbTargetIsMinion && g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_heroes.size()) {
                g_orbStickyAddr = g_heroes[g_orbTargetIdx].addr;
                g_orbStickyUntil = now + 3000;
            }
        } else {
            g_orbLastAttack = now - (ULONGLONG)std::max(80.f, attackDelayMs - 110.f);
        }
    }

    if (cursorMoved) OrbRestoreCursor(cursorRestore, true);

    // move — right-click wherever user's mouse is (no cursor movement)
    // Pre-attack pause: stop issuing move commands when the next attack is almost ready
    // so the game cleanly transitions from moving to attacking without input overlap
    float timeUntilAttack = (attackDelayMs + pingComp + humanJitter) - (float)(now - g_orbLastAttack);
    bool attackSoon = hasTarget && (timeUntilAttack > 0.f && timeUntilAttack < 80.f);
    if (!didAttack && !attackSoon) {
        if (mode == 4 && !hasTarget) {
            // last-hit mode, no killable minion — do nothing, don't spam ground
        } else {
            orbDoMove(std::max(20, (int)g_orbMoveDelay));
        }
    }
}

static void OrbwalkerDraw()
{
    if (!g_orbEnabled || g_gameTime < 1.0f) return;
    auto* dl = ImGui::GetBackgroundDrawList();

    if (g_orbDrawRange && g_localAttackRange > 50.f) {
        ImU32 rangeCol = IM_COL32(0, 180, 140, 255);
        float drawRadius = (g_localBoundingRadius > 10.f && g_localBoundingRadius < 200.f) ? g_localBoundingRadius : 65.f;
        DrawWorldCircle(dl, g_localPos, g_localAttackRange + drawRadius, rangeCol, 1.5f, 0);
    }

    if (g_orbDrawTarget && g_orbTargetIdx >= 0) {
        Vec3 targetPos{};
        bool valid = false;
        if (g_orbTargetIsMinion && g_orbTargetIdx < (int)g_minions.size()) {
            targetPos = g_minions[g_orbTargetIdx].pos;
            valid = true;
        } else if (!g_orbTargetIsMinion && g_orbTargetIdx < (int)g_heroes.size()) {
            targetPos = g_heroes[g_orbTargetIdx].pos;
            valid = true;
        }
        if (valid) {
            Vec2 sp;
            if (WorldToScreen(targetPos, sp)) {
                dl->AddCircle(ImVec2(sp.x, sp.y), 20.f, IM_COL32(255, 50, 50, 255), 0, 2.5f);
                dl->AddCircle(ImVec2(sp.x, sp.y), 22.f, IM_COL32(40, 40, 40, 255), 0, 1.f);
            }
        }
    }

    if (g_orbTargetMode == 1 && g_orbSelectedHero >= 0 && g_orbSelectedHero < (int)g_heroes.size()) {
        const auto& sel = g_heroes[g_orbSelectedHero];
        if (sel.addr && sel.health > 0.f) {
            // pulsing world-space ring under target
            float pulse = 0.6f + 0.4f * sinf(g_gameTime * 4.f);
            uint8_t pulseR = (uint8_t)(255 * pulse), pulseG = (uint8_t)(200 * pulse);
            DrawWorldCircle(dl, sel.pos, 80.f, IM_COL32(pulseR, pulseG, 0, 255), 2.5f, 0);
            DrawWorldCircle(dl, sel.pos, 85.f, IM_COL32((uint8_t)(255*pulse), (uint8_t)(100*pulse), 0, 255), 1.5f, 0);

            Vec2 sp;
            if (WorldToScreen(sel.pos, sp)) {
                ImU32 gold = IM_COL32(255, 200, 0, 255);
                ImU32 glow = IM_COL32((uint8_t)(255*pulse), (uint8_t)(160*pulse), 0, 255);
                ImU32 dark = IM_COL32(0, 0, 0, 255);
                float r = 36.f;
                // outer glow
                dl->AddCircle(ImVec2(sp.x, sp.y), r + 4, glow, 0, 4.f);
                // main ring
                dl->AddCircle(ImVec2(sp.x, sp.y), r, dark, 0, 4.f);
                dl->AddCircle(ImVec2(sp.x, sp.y), r, gold, 0, 2.5f);
                // crosshair lines
                dl->AddLine(ImVec2(sp.x - r - 8, sp.y), ImVec2(sp.x - r + 12, sp.y), gold, 2.5f);
                dl->AddLine(ImVec2(sp.x + r - 12, sp.y), ImVec2(sp.x + r + 8, sp.y), gold, 2.5f);
                dl->AddLine(ImVec2(sp.x, sp.y - r - 8), ImVec2(sp.x, sp.y - r + 12), gold, 2.5f);
                dl->AddLine(ImVec2(sp.x, sp.y + r - 12), ImVec2(sp.x, sp.y + r + 8), gold, 2.5f);
                // target name + "SELECTED" label
                char selBuf[96];
                snprintf(selBuf, sizeof(selBuf), ">> %s <<", sel.name);
                auto sz = ImGui::CalcTextSize(selBuf);
                float labelY = sp.y - r - 22;
                dl->AddRectFilled(ImVec2(sp.x - sz.x/2 - 4, labelY - 2), ImVec2(sp.x + sz.x/2 + 4, labelY + sz.y + 2), IM_COL32(20,20,20,255), 4.f);
                dl->AddRect(ImVec2(sp.x - sz.x/2 - 4, labelY - 2), ImVec2(sp.x + sz.x/2 + 4, labelY + sz.y + 2), gold, 4.f, 0, 1.f);
                dl->AddText(ImVec2(sp.x - sz.x/2, labelY), gold, selBuf);
            }
        }
    }

    if (g_orbDrawLastHit) {
        ULONGLONG now = GetTickCount64();
        float lastHitThresh = g_orbEffectiveAD * (std::clamp(g_orbLastHitBufferPct, 50, 100) / 100.f);
        for (const auto& m : g_minions) {
            if (!m.addr || m.team == g_localTeam || m.health <= 0.f || !m.visible) continue;
            float dx = g_localPos.x - m.pos.x, dz = g_localPos.z - m.pos.z;
            float d = sqrtf(dx*dx + dz*dz);
            float travelDraw = (g_localAttackRange < 300.f) ? 0.f : (d / 1800.f) * 1000.f;
            float pred = OrbPredictMinionHp(m.addr, m.health, travelDraw + g_orbPingMs * 0.25f, now);
            if ((pred > 0.f ? pred : m.health) > lastHitThresh) continue;
            Vec2 sp;
            if (!WorldToScreen(m.pos, sp)) continue;
            if (sp.x < 0 || sp.x > g_screenW || sp.y < 0 || sp.y > g_screenH) continue;
            dl->AddCircleFilled(ImVec2(sp.x, sp.y - 10.f), 5.f, IM_COL32(255, 50, 50, 255));
            dl->AddCircle(ImVec2(sp.x, sp.y - 10.f), 5.f, IM_COL32(255, 255, 255, 255), 0, 1.f);
        }
    }

    if (g_orbMode > 0) {
        static const char* names[] = {"", "COMBO", "HARASS", "LANE CLEAR", "LAST HIT", "FLEE"};
        static const ImU32 colors[] = {0, IM_COL32(255,50,50,255), IM_COL32(255,180,0,255),
                          IM_COL32(0,200,255,255), IM_COL32(255,255,0,255), IM_COL32(0,255,100,255)};
        const char* txt = names[g_orbMode];
        auto sz = ImGui::CalcTextSize(txt);
        float cx = g_screenW / 2.f;
        float cy = (float)g_screenH - 60.f;
        dl->AddRectFilled(ImVec2(cx - sz.x/2 - 8, cy - 2), ImVec2(cx + sz.x/2 + 8, cy + sz.y + 2),
                          IM_COL32(20,20,20,255), 4.f);
        dl->AddText(ImVec2(cx - sz.x/2.f, cy), colors[g_orbMode], txt);
    }
}

static void DrawKeyDisplay()
{
    if (!g_keyDisplayOpen) return;

    ImGui::SetNextWindowSize(ImVec2(160, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Keys", &g_keyDisplayOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    static const char* modeNames[] = {"Combo","Harass","Lane Clear","Last Hit","Flee"};
    bool kbReady = Keyboard::IsReady();
    for (int m = 0; m < 5; m++) {
        uint32_t vk = HidToVk(g_keyOptions[g_orbModeKeyIdx[m]].hid);
        bool down = kbReady && Keyboard::IsKeyDown(vk);
        ImVec4 col = down ? ImVec4(0.f, 1.f, 0.f, 1.f) : ImVec4(0.9f, 0.9f, 0.9f, 1.f);
        char buf[64]; snprintf(buf, sizeof(buf), "[%s] %s", g_keyOptions[g_orbModeKeyIdx[m]].label, modeNames[m]);
        ImGui::TextColored(col, "%s", buf);
    }

    if (!kbReady)
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 0.7f), "(no DMA keyboard)");

    ImGui::End();
}

// iOS-style toggle switch (public API only)
static bool Toggle(const char* label, bool* v) {
    float h = ImGui::GetFrameHeight() * 0.85f;
    float w = h * 1.75f;
    float radius = h * 0.5f;
    float knobR = radius * 0.78f;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImU32 bgCol = *v ? IM_COL32(88, 142, 255, 255) : IM_COL32(60, 60, 72, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bgCol, radius);
    float knobX = *v ? (pos.x + w - radius) : (pos.x + radius);
    dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), knobR, IM_COL32(240, 240, 245, 255));

    ImGui::InvisibleButton(label, ImVec2(w, h));
    bool pressed = ImGui::IsItemClicked();
    if (pressed) *v = !*v;

    ImGui::SameLine(0, 8);
    ImGui::TextUnformatted(label);
    return pressed;
}

// Section header with accent bar
static void SectionHeader(const char* label) {
    ImGui::Spacing();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + 3, pos.y + ImGui::GetTextLineHeight()), IM_COL32(88, 142, 255, 255), 1.5f);
    ImGui::Indent(10);
    ImGui::TextColored(ImVec4(0.88f, 0.88f, 0.92f, 1.0f), "%s", label);
    ImGui::Unindent(10);
    ImGui::Spacing();
}

// Status pill: colored rounded rect with text
static void StatusPill(const char* text, bool ok) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pad = 6.f;
    ImU32 bg = ok ? IM_COL32(30, 85, 45, 255) : IM_COL32(85, 30, 30, 255);
    ImU32 tx = ok ? IM_COL32(100, 220, 120, 255) : IM_COL32(240, 100, 100, 255);
    dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + sz.x + pad*2, pos.y + sz.y + 4), bg, 4.f);
    dl->AddText(ImVec2(pos.x + pad, pos.y + 2), tx, text);
    ImGui::Dummy(ImVec2(sz.x + pad*2, sz.y + 4));
}

// Backward compat — old code still calls Chk in some places
static bool Chk(const char* label, bool* v) { return Toggle(label, v); }

static void DrawMenu()
{
    if (!g_menuOpen) return;

    ImGui::SetNextWindowSize(ImVec2(540, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(xorstr_("VeoxDMA"), &g_menuOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 sz  = ImGui::GetWindowSize();
        bool clamp = false;
        if (pos.x + sz.x < 50)  { pos.x = 0; clamp = true; }
        if (pos.y + sz.y < 50)  { pos.y = 0; clamp = true; }
        if (pos.x > g_screenW - 50) { pos.x = (float)g_screenW - sz.x; clamp = true; }
        if (pos.y > g_screenH - 50) { pos.y = (float)g_screenH - sz.y; clamp = true; }
        if (clamp) ImGui::SetWindowPos(pos);
    }

    // ── Title bar ──
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        float ww = ImGui::GetWindowWidth();
        dl->AddRectFilled(wp, ImVec2(wp.x + ww, wp.y + 36), IM_COL32(25, 40, 72, 255), 8.f, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(wp.x + 14, wp.y + 9), IM_COL32(88, 142, 255, 255), "VEOX");
        dl->AddText(ImVec2(wp.x + 52, wp.y + 9), IM_COL32(180, 180, 195, 255), "DMA");
        // Minimize button
        {
            float btnW = 24.f, btnH = 20.f;
            float btnX = wp.x + ww - btnW - 8.f;
            float btnY = wp.y + 8.f;
            ImVec2 btnMin(btnX, btnY), btnMax(btnX + btnW, btnY + btnH);
            ImVec2 mouse = ImGui::GetIO().MousePos;
            bool hovered = mouse.x >= btnMin.x && mouse.x <= btnMax.x && mouse.y >= btnMin.y && mouse.y <= btnMax.y;
            dl->AddRectFilled(btnMin, btnMax, hovered ? IM_COL32(255, 80, 80, 200) : IM_COL32(60, 80, 120, 200), 4.f);
            dl->AddLine(ImVec2(btnX + 6, btnY + btnH * 0.5f), ImVec2(btnX + btnW - 6, btnY + btnH * 0.5f), IM_COL32(255, 255, 255, 230), 2.f);
            if (hovered && ImGui::IsMouseClicked(0)) g_menuOpen = false;
        }
        if (g_localChampName[0]) {
            char cb[80]; snprintf(cb, sizeof(cb), "%s  Lv%d", g_localChampName, g_localLevel);
            ImVec2 ts = ImGui::CalcTextSize(cb);
            dl->AddText(ImVec2(wp.x + ww - ts.x - 42, wp.y + 9), IM_COL32(140, 140, 160, 255), cb);
        }
        ImGui::Dummy(ImVec2(0, 28));
    }

    // ── Build key combo string once ──
    static char keyComboStr[2048] = {};
    static bool keyComboBuilt = false;
    if (!keyComboBuilt) {
        char* p = keyComboStr;
        for (int i = 0; i < g_keyOptionCount; i++) {
            int len = (int)strlen(g_keyOptions[i].label);
            memcpy(p, g_keyOptions[i].label, len); p += len; *p++ = '\0';
        }
        *p = '\0';
        keyComboBuilt = true;
    }

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        // ════════════════════════════════════════════
        //  COMBAT
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Combat"))
        {
            // Device status row
            {
                bool mOk = InputDevice::IsConnected();
                bool kOk = Keyboard::IsReady();
                StatusPill(mOk ? "Device OK" : "Device OFF", mOk);
                ImGui::SameLine(0, 8);
                StatusPill(kOk ? "Keyboard OK" : "No Keyboard", kOk);
            }

            ImGui::Spacing();
            SectionHeader("Orbwalker");
            Toggle("Enabled", &g_orbEnabled);
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Targeting");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##target_mode", &g_orbTargetMode, "Mouse\0Target\0");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Attack Key");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##atk_key", &g_orbAtkKeyIdx, keyComboStr);
            if (g_orbTargetMode == 1) {
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Priority");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                ImGui::Combo("##prio_mode", &g_orbPriorityMode, "Closest Enemy\0Lowest HP\0Most AD\0Most AP\0");
            }

            ImGui::Spacing();
            SectionHeader("Combat Modes");
            {
                static const char* modeLabels[] = {"Combo","Harass","Lane Clear","Last Hit","Flee"};
                static const ImU32 modeColors[] = {
                    IM_COL32(255,80,80,255), IM_COL32(255,180,50,255),
                    IM_COL32(50,200,255,255), IM_COL32(255,230,50,255), IM_COL32(80,255,130,255)
                };
                for (int m = 0; m < 5; m++) {
                    char togId[32]; snprintf(togId, sizeof(togId), "##mode_en_%d", m);
                    // toggle + colored label
                    Toggle(togId, &g_orbModeEnabled[m]);
                    ImGui::SameLine(0, 6);
                    ImU32 labelCol = g_orbModeEnabled[m] ? modeColors[m] : IM_COL32(90, 90, 100, 255);
                    ImGui::TextColored(ImColor(labelCol).Value, "%s", modeLabels[m]);
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f + 60.f);
                    ImGui::SetNextItemWidth(sliderW * 0.75f);
                    char keyId[32]; snprintf(keyId, sizeof(keyId), "##mode_key_%d", m);
                    ImGui::Combo(keyId, &g_orbModeKeyIdx[m], keyComboStr);
                }
            }

            if (g_orbMode > 0) {
                static const char* mn[] = {"","COMBO","HARASS","CLEAR","LAST HIT","FLEE"};
                static const ImU32 mc[] = {0, IM_COL32(255,80,80,255), IM_COL32(255,180,50,255),
                    IM_COL32(50,200,255,255), IM_COL32(255,230,50,255), IM_COL32(80,255,130,255)};
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 ts = ImGui::CalcTextSize(mn[g_orbMode]);
                dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + 16, pos.y + ts.y + 6), mc[g_orbMode] & 0x40FFFFFF, 4.f);
                dl->AddText(ImVec2(pos.x + 8, pos.y + 3), mc[g_orbMode], "%s", mn[g_orbMode]);
                ImGui::Dummy(ImVec2(ts.x + 16, ts.y + 6));
            }

            ImGui::Spacing();
            SectionHeader("Spells");
            Toggle("Q", &g_orbUseQ); ImGui::SameLine(0, 16);
            Toggle("W", &g_orbUseW); ImGui::SameLine(0, 16);
            Toggle("E", &g_orbUseE); ImGui::SameLine(0, 16);
            Toggle("R", &g_orbUseR);
            Toggle("Read Cooldowns", &g_orbReadCooldowns);

            if (g_orbReadCooldowns) {
                ImGui::Spacing();
                const char* sn[] = {"Q","W","E","R"};
                for (int s = 0; s < 4; s++) {
                    if (s > 0) ImGui::SameLine(0, 6);
                    if (g_spells[s].level > 0) {
                        StatusPill(sn[s], g_spells[s].ready);
                    } else {
                        ImGui::TextDisabled("[%s]", sn[s]);
                    }
                }
            }

            ImGui::Spacing();
            SectionHeader("Champion Combo");
            Toggle("Enabled##combo", &g_comboEnabled);
            if (g_localChampName[0]) {
                bool sup = ChampionCombo::Supports(g_localChampName);
                ImGui::SameLine(0, 12);
                StatusPill(sup ? "Profile" : "Generic", sup);
            }
            Toggle("Generic Fallback", &g_comboSettings.genericEnabled);
            Toggle("Generic Uses R", &g_comboSettings.genericUltimate);

            ImGui::Spacing();
            SectionHeader("Last Hit");
            Toggle("Only Killables (no push AA)", &g_orbLastHitOnly);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Hit Buffer");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##lhbuf", &g_orbLastHitBufferPct, 1, 5)) g_orbLastHitBufferPct = std::clamp(g_orbLastHitBufferPct, 70, 100);

            ImGui::Spacing();
            SectionHeader("Timing");
            Toggle("Auto Stats (DMA)", &g_orbAutoStats);
            if (g_orbAutoStats) {
                float totalAD = g_localBaseAD + g_localBonusAD;
                float baseAS = (g_localASScale > 0.5f && g_localASScale < 0.85f) ? g_localASScale : g_orbBaseAS;
                float totalAS = std::clamp(baseAS * g_localASMod, 0.2f, 2.5f);
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1),
                    "AD %.0f  |  AS %.3f  |  Range %.0f", totalAD, totalAS, g_localAttackRange);
                ImGui::TextColored(ImVec4(0.45f,0.45f,0.52f,1),
                    "DMA: scale=%.3f  mod=%.3f  base=%.3f", g_localASScale, g_localASMod, baseAS);
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Base AS (fallback)");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                if (ImGui::InputFloat("##base_as", &g_orbBaseAS, 0.01f, 0.05f, "%.3f")) g_orbBaseAS = std::clamp(g_orbBaseAS, 0.5f, 0.9f);
            } else {
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Atk Speed");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                if (ImGui::InputFloat("##man_as", &g_orbAttackSpeed, 0.05f, 0.1f, "%.2f")) g_orbAttackSpeed = std::clamp(g_orbAttackSpeed, 0.3f, 3.0f);
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Atk Damage");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                if (ImGui::InputFloat("##man_ad", &g_orbPlayerAD, 5.f, 25.f, "%.0f")) g_orbPlayerAD = std::clamp(g_orbPlayerAD, 40.f, 500.f);
            }
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Windup %%");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##windup", &g_orbWindupPct, 1, 5)) g_orbWindupPct = std::clamp(g_orbWindupPct, 1, 100);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Move Delay");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputFloat("##movedelay", &g_orbMoveDelay, 5.f, 20.f, "%.0f ms")) g_orbMoveDelay = std::clamp(g_orbMoveDelay, 20.f, 200.f);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Spell Gate");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##spellgate", &g_orbSpellGateMs, 10, 50)) g_orbSpellGateMs = std::clamp(g_orbSpellGateMs, 0, 500);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Click Spread");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##click_spread", &g_orbClickSpread, 1, 10)) g_orbClickSpread = std::clamp(g_orbClickSpread, 0, 100);
            Toggle("Ping Compensation##pingcomp", &g_orbPingComp);
            if (g_orbPingComp) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f,0.7f,0.5f,1), "(%.0f ms)", g_orbPingMs);
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Timing Bias");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                if (ImGui::InputFloat("##timing_bias", &g_orbTimingBias, 5.f, 20.f, "%.0f ms")) g_orbTimingBias = std::clamp(g_orbTimingBias, -100.f, 100.f);
            }

            ImGui::Spacing();
            SectionHeader("Cursor / Behavior");
            Toggle("Humanize Timing", &g_orbHumanize);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Cursor Policy");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##curpol", &g_orbCursorPolicy, "Free (no aim move)\0Aim + Restore (smooth)\0");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Path Min");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##pathmin", &g_orbPathMinMs, 5, 20)) g_orbPathMinMs = std::clamp(g_orbPathMinMs, 10, 200);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Path Max");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##pathmax", &g_orbPathMaxMs, 5, 20)) g_orbPathMaxMs = std::clamp(g_orbPathMaxMs, 20, 300);
            if (g_orbPathMaxMs < g_orbPathMinMs) g_orbPathMaxMs = g_orbPathMinMs;
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "React Min");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##reactmin", &g_orbReactMinMs, 5, 20)) g_orbReactMinMs = std::clamp(g_orbReactMinMs, 0, 250);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "React Jitter");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            if (ImGui::InputInt("##reactjit", &g_orbReactJitterMs, 5, 20)) g_orbReactJitterMs = std::clamp(g_orbReactJitterMs, 0, 250);
            if (g_orbTargetMode == 1)
                Toggle("Click-Select In Mode", &g_orbClickSelectInMode);
            if (g_localChampName[0]) {
                std::string champKey = g_localChampName;
                auto wit = g_orbWindupPctChamp.find(champKey);
                bool hasOverride = (wit != g_orbWindupPctChamp.end());
                char ovLabel[96];
                snprintf(ovLabel, sizeof(ovLabel), "Per-champ Windup##ov_%s", g_localChampName);
                bool toggled = Toggle(ovLabel, &hasOverride);
                if (toggled) {
                    if (hasOverride) g_orbWindupPctChamp[champKey] = std::clamp(g_orbWindupPct, 1, 100);
                    else g_orbWindupPctChamp.erase(champKey);
                }
                if (hasOverride) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "(%s)", g_localChampName);
                    ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "  Override %%");
                    ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                    int ov = std::clamp(g_orbWindupPctChamp[champKey], 1, 100);
                    char ovId[96]; snprintf(ovId, sizeof(ovId), "##windupov_%s", g_localChampName);
                    if (ImGui::InputInt(ovId, &ov, 1, 5)) {
                        ov = std::clamp(ov, 1, 100);
                        g_orbWindupPctChamp[champKey] = ov;
                    }
                } else {
                    ImGui::TextDisabled("Uses global Windup %% (%d%%). Saved as orb_windup_champ_%s", g_orbWindupPct, g_localChampName);
                }
            }

            // Priority (target mode only)
            if (g_orbTargetMode == 1) {
                ImGui::Spacing();
                SectionHeader("Target Priority");
                if (g_orbSelectedAddr) {
                    for (const auto& h : g_heroes) {
                        if (h.addr == g_orbSelectedAddr) {
                            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Locked: %s", h.name);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear##lock")) { g_orbSelectedAddr = 0; g_orbSelectedHero = -1; }
                            break;
                        }
                    }
                }

                for (int i = 0; i < (int)g_heroes.size(); i++) {
                    const auto& h = g_heroes[i];
                    if (!h.addr || h.team == g_localTeam) continue;
                    ImGui::PushID(i);

                    auto* icon = GetChampIcon(h.name);
                    if (icon) {
                        ImVec2 cur = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddImage((ImTextureID)icon, cur, ImVec2(cur.x+18, cur.y+18));
                        ImGui::Dummy(ImVec2(18, 18)); ImGui::SameLine();
                    }

                    bool dead = h.isDead || h.health <= 0.f;
                    bool sel = (h.addr == g_orbSelectedAddr);
                    ImVec4 nc = dead ? ImVec4(0.4f,0.4f,0.4f,1) : sel ? ImVec4(1,0.8f,0,1) : ImVec4(0.88f,0.88f,0.92f,1);
                    ImGui::TextColored(nc, "%-10s", h.name);

                    if (!dead) {
                        ImGui::SameLine();
                        if (sel) { if (ImGui::SmallButton("X##d")) { g_orbSelectedAddr = 0; g_orbSelectedHero = -1; } }
                        else     { if (ImGui::SmallButton("Lock")) { g_orbSelectedAddr = h.addr; g_orbSelectedHero = i; } }
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  VISUALS
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Visuals"))
        {
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            SectionHeader("Champion ESP");
            Toggle("Enable Drawings", &g_espEnabled);
            Toggle("Enemy Only", &g_espEnemyOnly);
            Toggle("Names", &g_espNames);
            Toggle("Health Bars", &g_espHealth);
            Toggle("Shields", &g_espShields);
            Toggle("Bounding Box", &g_espBox);
            Toggle("Visibility", &g_espVisStatus);
            Toggle("Last Known Position", &g_espLastPosition);

            ImGui::Spacing();
            SectionHeader("Orbwalker Visuals");
            Toggle("Attack Range Circle", &g_orbDrawRange);
            Toggle("Target Highlight", &g_orbDrawTarget);
            Toggle("Last-Hit Markers", &g_orbDrawLastHit);

            ImGui::Spacing();
            SectionHeader("Range Circles");
            Toggle("Enemy Attack Range", &g_espAttackRange);
            Toggle("Turret Range", &g_espTurretRange);
            if (g_espTurretRange) {
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##turr", &g_turretRange, 700.f, 1000.f, "%.0f units");
            }
            ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##drawmode", &g_drawMode, "Outlined\0Filled\0");
            Toggle("Circle Glow", &g_drawGlow);

            ImGui::Spacing();
            SectionHeader("Skillshot Alerts");
            Toggle("Enable Skillshot Alerts", &g_skillshotAlerts);
            Toggle("Missile Object Tracking", &g_missileTracking);
            if (g_missileTracking) {
                if (g_missileOffsetsProbed) {
                    ImGui::TextColored(ImVec4(0.f,1.f,0.f,1.f), "  Offsets: OK (SI=0x%llX N=0x%llX C=0x%llX S=0x%llX E=0x%llX)",
                        (uint64_t)Offsets::MissileClient::SpellInfo,
                        (uint64_t)Offsets::MissileSpellInfo::NamePointer,
                        (uint64_t)Offsets::MissileClient::CurrentPos,
                        (uint64_t)Offsets::MissileClient::StartPos,
                        (uint64_t)Offsets::MissileClient::EndPos);
                } else {
                    ImGui::TextColored(ImVec4(1.f,1.f,0.f,1.f), "  Probing offsets... (%d/100)", g_missileOffsetProbeAttempts);
                }
            }
            Toggle("Above-Head Cooldowns (QWER)", &g_castCDTracker);

            ImGui::Spacing();
            SectionHeader("Minions");
            Toggle("Minion Health Bars", &g_espMinions);

            ImGui::Spacing();
            SectionHeader("Jungler Tracker");
            Toggle("JG Last Seen", &g_junglerTracker);
            Toggle("JG Danger Alert", &g_junglerDangerAlert);
            if (g_junglerDangerAlert) {
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##jgrange", &g_junglerDangerRange, 1000.f, 6000.f, "Range: %.0f");
            }
            if (g_enemyJunglerIdx >= 0 && g_enemyJunglerIdx < (int)g_heroes.size()) {
                ImGui::TextColored(ImVec4(0.6f,0.9f,0.4f,1), "JG: %s", g_heroes[g_enemyJunglerIdx].name);
            } else {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "JG: scanning...");
            }

            ImGui::Spacing();
            SectionHeader("Minimap");
            Toggle("Enable Minimap", &g_minimapEnabled);
            if (g_minimapEnabled) {
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmsz", &g_minimapSize, 100.f, g_screenH * 0.4f, "Size: %.0f px");
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmx", &g_minimapOffX, 0.f, (float)g_screenW - g_minimapSize, "X: %.0f");
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmy", &g_minimapOffY, 0.f, (float)g_screenH - g_minimapSize, "Y: %.0f");
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmop", &g_minimapOpacity, 0.3f, 1.f, "Opacity: %.2f");
                if (ImGui::Button("Reset Position")) MinimapResetToGame();
            }

            ImGui::Spacing();
            SectionHeader("Colors");
            ImGui::ColorEdit4("Enemy##col", g_enemyColor, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine(0, 16);
            ImGui::ColorEdit4("Ally##col", g_allyColor, ImGuiColorEditFlags_NoInputs);

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  TRACKER
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Tracker"))
        {
            SectionHeader("Auto Smite");
            Toggle("Enabled##smite", &g_smiteEnabled);
            if (g_smiteSlot >= 0) {
                ImGui::SameLine(0, 12);
                char sb[64]; snprintf(sb, sizeof(sb), "%s (%c)", g_smiteName, g_smiteSlot == 4 ? 'D' : 'F');
                StatusPill(sb, g_smiteReady);
                if (!g_smiteReady && g_smiteReadyAt > g_gameTime) {
                    ImGui::SameLine(0, 6);
                    ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "%.0fs", g_smiteReadyAt - g_gameTime);
                }
            }

            ImGui::Spacing();
            SectionHeader("Summoner Spells");
            Toggle("Track Summoners", &g_trackerSumms);
            if (g_trackerSumms) {
                for (int i = 0; i < (int)g_heroes.size() && i < 12; i++) {
                    const auto& h = g_heroes[i];
                    if (!h.addr || h.team == g_localTeam) continue;
                    auto& summs = g_heroSumms[i];

                    ImGui::PushID(200 + i);
                    ImVec4 statusCol = h.isDead ? ImVec4(0.4f,0.4f,0.4f,1) :
                        h.visible ? ImVec4(0.3f,0.85f,0.45f,1) : ImVec4(1.f,0.5f,0.2f,1);
                    ImGui::TextColored(statusCol, "%-12s", h.name);
                    ImGui::SameLine();

                    auto DrawSumm = [](const SummonerSpell& sp, float gt) {
                        if (!sp.name[0]) { ImGui::TextDisabled("?"); return; }
                        const char* sn = sp.name;
                        if (strncmp(sn, "Summoner", 8) == 0) sn += 8;
                        if (sp.ready) ImGui::TextColored(ImVec4(0.3f,0.85f,0.45f,1), "%s", sn);
                        else {
                            float cd = sp.readyAt - gt; if (cd < 0) cd = 0;
                            ImGui::TextColored(ImVec4(0.95f,0.35f,0.35f,1), "%s %.0fs", sn, cd);
                        }
                    };
                    DrawSumm(summs.d, g_gameTime);
                    ImGui::SameLine(0, 12);
                    DrawSumm(summs.f, g_gameTime);
                    ImGui::PopID();
                }
            }

            ImGui::Spacing();
            SectionHeader("Enemy Ability Cooldowns");
            Toggle("Track Casts (Q/W/E/R)", &g_castCDTracker);
            if (g_castCDTracker) {
                const char* slotLabels[] = { "Q", "W", "E", "R", "D", "F" };
                for (int i = 0; i < (int)g_heroes.size() && i < 12; i++) {
                    const auto& h = g_heroes[i];
                    if (!h.addr || h.team == g_localTeam) continue;
                    auto& cds = g_enemyCDs[i];

                    ImGui::PushID(500 + i);
                    ImVec4 statusCol = h.isDead ? ImVec4(0.4f,0.4f,0.4f,1) :
                        h.visible ? ImVec4(0.3f,0.85f,0.45f,1) : ImVec4(1.f,0.5f,0.2f,1);
                    ImGui::TextColored(statusCol, "%-12s", h.name);
                    ImGui::SameLine();

                    for (int s = 0; s < 4; s++) {
                        auto& cd = cds.slots[s];
                        if (!cd.valid) {
                            ImGui::TextDisabled("%s:?", slotLabels[s]);
                        } else if (cd.readyAt <= g_gameTime) {
                            ImGui::TextColored(ImVec4(0.3f,0.85f,0.45f,1), "%s", slotLabels[s]);
                        } else {
                            float rem = cd.readyAt - g_gameTime;
                            ImGui::TextColored(ImVec4(0.95f,0.35f,0.35f,1), "%s:%.0f", slotLabels[s], rem);
                        }
                        if (s < 3) ImGui::SameLine(0, 8);
                    }
                    ImGui::PopID();
                }
            }

            ImGui::Spacing();
            SectionHeader("Jungle Timers");
            Toggle("Track Camps", &g_trackerJungle);
            if (g_trackerJungle) {
                for (auto& camp : g_jungleCamps) {
                    ImGui::PushID(&camp);
                    if (camp.alive) {
                        ImGui::TextColored(ImVec4(0.3f,0.85f,0.45f,1), "%-20s", camp.display);
                        ImGui::SameLine(); StatusPill("Alive", true);
                    } else {
                        ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "%-20s", camp.display);
                        ImGui::SameLine();
                        if (camp.buffRespawnAt > 0.f || camp.killedAt > 0.f) {
                            float ra = camp.buffRespawnAt > 0.f ? camp.buffRespawnAt : camp.killedAt + camp.respawnTime;
                            float tl = ra - g_gameTime;
                            if (tl > 0.f) {
                                char tb[16]; snprintf(tb, sizeof(tb), "%d:%02d", (int)(tl/60.f), (int)tl % 60);
                                ImU32 col = camp.buffRespawnAt > 0.f ? IM_COL32(100,220,120,255) : IM_COL32(200,200,210,255);
                                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", tb);
                            } else {
                                StatusPill("Spawning", true);
                            }
                        } else {
                            ImGui::TextDisabled("--:--");
                        }
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  DEVICE
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Device"))
        {
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            SectionHeader("Input Device");
            {
                const char* deviceNames[] = { "Makcu (Serial)", "KMBox B+ (Serial)", "KMBox B+ (Network)" };
                ImGui::SetNextItemWidth(sliderW);
                if (ImGui::Combo("##devicetype", &g_inputDeviceType, deviceNames, 3)) {
                    InputDevice::g_deviceType = (InputDevice::DeviceType)g_inputDeviceType;
                }
            }

            ImGui::Spacing();
            StatusPill(InputDevice::StateString(), InputDevice::IsConnected());
            {
                char err[240]; InputDevice::GetError(err, sizeof(err));
                if (err[0]) ImGui::TextColored(ImVec4(0.95f,0.35f,0.35f,1), "%s", err);
            }

            ImGui::Spacing();
            if (g_inputDeviceType == 0 || g_inputDeviceType == 1) {
                // ── Serial panel (Makcu or KMBox B+ COM) ──
                ImGui::SetNextItemWidth(sliderW);
                ImGui::InputText("##port", g_makcuPort, sizeof(g_makcuPort));
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "COM Port");
                ImGui::SameLine();
                if (ImGui::Button("Detect")) {
                    auto ports = InputDevice::ListPorts();
                    for (auto& p : ports) {
                        if (p.find("CH343") != std::string::npos || p.find("CH340") != std::string::npos) {
                            auto c = p.find("COM"); auto e = p.find(")", c);
                            if (c != std::string::npos && e != std::string::npos)
                                snprintf(g_makcuPort, sizeof(g_makcuPort), "%s", p.substr(c, e-c).c_str());
                            break;
                        }
                    }
                }

                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderInt("##baud", &g_makcuBaud, 115200, 4000000, "%d baud");

                ImGui::Spacing();
                {
                    float bw = ImGui::GetContentRegionAvail().x * 0.5f;
                    auto state = Makcu::GetState();
                    if (state == Makcu::State::Connected || state == Makcu::State::Connecting) {
                        if (ImGui::Button("Disconnect##serial", ImVec2(bw, 32))) InputDevice::Disconnect();
                    } else {
                        if (ImGui::Button("Connect##serial", ImVec2(bw, 32))) {
                            if (g_inputDeviceType == 0)
                                InputDevice::Connect(g_makcuPort[0] ? g_makcuPort : nullptr, g_makcuBaud);
                            else
                                InputDevice::ConnectKMBoxCOM(g_makcuPort[0] ? g_makcuPort : nullptr, g_makcuBaud);
                        }
                    }
                }
            } else {
                // ── KMBox B+ network panel ──
                ImGui::SetNextItemWidth(sliderW);
                ImGui::InputText("##kmip", g_kmboxIP, sizeof(g_kmboxIP));
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "IP Address");

                ImGui::SetNextItemWidth(sliderW);
                ImGui::InputInt("##kmport", &g_kmboxPort, 0, 0);
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Port");

                ImGui::SetNextItemWidth(sliderW);
                ImGui::InputText("##kmuuid", g_kmboxUUID, sizeof(g_kmboxUUID));
                ImGui::SameLine(); ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "UUID");

                ImGui::Spacing();
                {
                    float bw = ImGui::GetContentRegionAvail().x * 0.5f;
                    auto state = KMBoxNet::GetState();
                    if (state == KMBoxNet::State::Connected || state == KMBoxNet::State::Connecting) {
                        if (ImGui::Button("Disconnect##kmbox", ImVec2(bw, 32))) InputDevice::Disconnect();
                    } else {
                        if (ImGui::Button("Connect##kmbox", ImVec2(bw, 32)))
                            InputDevice::ConnectKMBoxNet(g_kmboxIP, g_kmboxPort, g_kmboxUUID);
                    }
                }
            }

            if (InputDevice::IsConnected()) {
                ImGui::Spacing();
                SectionHeader("Quick Test");
                if (ImGui::Button("Move(5,5)", ImVec2(90, 28))) InputDevice::Move(5,5);
                ImGui::SameLine();
                if (ImGui::Button("L-Click", ImVec2(90, 28))) InputDevice::LeftClick();
                ImGui::SameLine();
                if (ImGui::Button("R-Click", ImVec2(90, 28))) InputDevice::RightClick();
            }

            ImGui::Spacing();
            SectionHeader("Fuser Overlay");
            ImGui::Text("%d monitor(s) detected", (int)g_overlayMonitors.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) DetectMonitors();

            for (int i = 0; i < (int)g_overlayMonitors.size(); ++i) {
                const RECT& r = g_overlayMonitors[i];
                char label[128];
                snprintf(label, sizeof(label), "%ldx%ld##mon%d", r.right - r.left, r.bottom - r.top, i);
                bool sel = (i == g_monitorIndex);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.557f, 1.0f, 0.35f));
                if (ImGui::Button(label, ImVec2(100, 28))) g_monitorIndex = i;
                if (sel) ImGui::PopStyleColor();
                if (i + 1 < (int)g_overlayMonitors.size()) ImGui::SameLine();
            }

            ImGui::Spacing();
            ImGui::ColorEdit3("BG Color", g_bgColor, ImGuiColorEditFlags_NoInputs);
            Toggle("Show FPS", &g_fpsShow);
            Toggle("Show Key Display", &g_keyDisplayOpen);

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  SCRIPTS
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Scripts"))
        {
            if (ImGui::Button("Reload All", ImVec2(100, 28))) LuaScanScripts();

            if (g_scripts.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "No scripts found.");
            }

            ImGui::Spacing();
            for (int i = 0; i < (int)g_scripts.size(); i++) {
                ImGui::PushID(i);
                auto& sc = g_scripts[i];

                Toggle(sc.name.c_str(), &sc.enabled);
                ImGui::SameLine(0, 8);
                if (sc.hasError) {
                    StatusPill("ERR", false);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sc.errorMsg);
                } else if (sc.L) {
                    StatusPill("OK", true);
                }
                ImGui::SameLine(0, 8);
                if (ImGui::SmallButton("Reload")) LuaLoadScript(sc);

                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  CONFIG
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Config"))
        {
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            SectionHeader("Save / Load");
            ImGui::SetNextItemWidth(sliderW);
            ImGui::InputText("##cfgname", g_configName, sizeof(g_configName));

            ImGui::Spacing();
            if (ImGui::Button("Save", ImVec2(100, 30))) SaveConfig(g_configName);
            ImGui::SameLine();
            if (ImGui::Button("Load", ImVec2(100, 30))) LoadConfig(g_configName);
            ImGui::SameLine();
            if (ImGui::Button("Save Default", ImVec2(120, 30))) SaveConfig("default");

            if (g_configStatus[0]) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.3f,0.85f,0.45f,1), "%s", g_configStatus);
            }

            ImGui::Spacing();
            SectionHeader("Saved Configs");
            auto configs = ListConfigs();
            for (auto& name : configs) {
                bool active = (strcmp(g_configName, name.c_str()) == 0);
                if (active) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.345f, 0.557f, 1.0f, 0.25f));
                if (ImGui::Selectable(name.c_str(), active)) {
                    strncpy(g_configName, name.c_str(), sizeof(g_configName) - 1);
                    LoadConfig(g_configName);
                }
                if (active) ImGui::PopStyleColor();
            }
            if (configs.empty()) ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "No configs yet.");

            ImGui::Spacing();
            SectionHeader("Info");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Game Time: %.1f", g_gameTime);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Heroes: %d  Minions: %d  Turrets: %d",
                (int)g_heroes.size(), (int)g_minions.size(), (int)g_turrets.size());
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Base: 0x%llX  PID: %d", (uint64_t)g_base, TargetProcess.current_process.PID);
            ImGui::TextColored(g_globalsScanned ? ImVec4(0.30f,0.85f,0.45f,1) : ImVec4(0.95f,0.35f,0.35f,1),
                "SigScan: %s  GT=0x%llX  LP=0x%llX  HM=0x%llX",
                g_globalsScanned ? "OK" : "PENDING",
                (uint64_t)Offsets::Globals::GameTime,
                (uint64_t)Offsets::Globals::LocalPlayer,
                (uint64_t)Offsets::Globals::HeroManager);

#ifndef NDEBUG
            ImGui::Spacing();
            SectionHeader("Debug Tools");
            if (ImGui::Button("Scan SpellBook") && g_localAddr) {
                uintptr_t sb = g_localAddr + Offsets::GameObject::SpellBook;
                VX_LOG("\n[SpellScan] SB=0x%llX  gameTime=%.1f\n", (uint64_t)sb, g_gameTime);
                VX_LOG("--- pointer scan ---\n");
                for (int off = 0x0; off <= 0xA00; off += 0x8) {
                    uintptr_t ptr = TargetProcess.Read<uintptr_t>(sb + off);
                    if (ptr > 0x100000 && ptr < 0x7FFFFFFFFFFF) {
                        int lv = TargetProcess.Read<int>(ptr + 0x28);
                        if (lv >= 0 && lv <= 6) {
                            float f30 = TargetProcess.Read<float>(ptr + 0x30);
                            float f74 = TargetProcess.Read<float>(ptr + 0x74);
                            VX_LOG("  +0x%03X -> 0x%llX lv=%d [+0x30]=%.2f [+0x74]=%.2f\n",
                                off, (uint64_t)ptr, lv, f30, f74);
                        }
                    }
                }
                VX_LOG("[SpellScan] Done.\n\n");
            }
            ImGui::SameLine();
            if (ImGui::Button("Find Slots") && g_localAddr) {
                uintptr_t sb = g_localAddr + Offsets::GameObject::SpellBook;
                VX_LOG("\n[SlotFinder] SB=0x%llX\n", (uint64_t)sb);
                for (int off = 0x0; off <= 0x1000; off += 0x8) {
                    uintptr_t p0 = TargetProcess.Read<uintptr_t>(sb + off);
                    if (p0 < 0x10000 || p0 > 0x7FFFFFFFFFFF) continue;
                    int lv0 = TargetProcess.Read<int>(p0 + 0x28);
                    if (lv0 < 0 || lv0 > 6) continue;
                    int good = 1;
                    for (int s = 1; s < 4; s++) {
                        uintptr_t ps = TargetProcess.Read<uintptr_t>(sb + off + s * 8);
                        if (ps < 0x10000 || ps > 0x7FFFFFFFFFFF) break;
                        int lvs = TargetProcess.Read<int>(ps + 0x28);
                        if (lvs < 0 || lvs > 6) break;
                        good++;
                    }
                    if (good < 3) continue;
                    VX_LOG("  ** HIT SB+0x%03X **\n", off);
                    for (int s = 0; s < 6; s++) {
                        uintptr_t ps = TargetProcess.Read<uintptr_t>(sb + off + s * 8);
                        if (!ps) continue;
                        int lv = TargetProcess.Read<int>(ps + 0x28);
                        float cd = TargetProcess.Read<float>(ps + 0x74);
                        uintptr_t info = ReadSpellSlotData(ps);
                        char nm[80]{};
                        if (info) ReadSpellNameSSO(info, Offsets::SpellSlot::SpellInfoNamePtr, nm, 80);
                        VX_LOG("    [%d] lv=%d cd=%.1f %s\n", s, lv, cd, nm);
                    }
                }
                VX_LOG("[SlotFinder] Done.\n\n");
            }
#endif

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f,0.4f,0.47f,1), "INSERT to toggle menu");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            g_screenW = LOWORD(lParam);
            g_screenH = HIWORD(lParam);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        DetectMonitors();
        ApplyOverlayMonitor(hWnd);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_INSERT) g_menuOpen = !g_menuOpen;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static bool GameMemoryReadable()
{
    return g_base && TargetProcess.Read<uint16_t>(g_base) == 0x5A4D;
}

static void InitEvadeSpells() {
    g_evadeSpells.reserve(1024);
    g_evadeSpells["AatroxR"] = {EVADE_CIRCLE_START,550.0f,25000.0f,33333.3f,0.0f,250,750};
    g_evadeSpells["AatroxW"] = {EVADE_LINE,80.0f,825.0f,1021.0f,0.0f,250,808};
    g_evadeSpells["AhriE"] = {EVADE_LINE,60.0f,975.0f,839.1f,0.0f,250,1162};
    g_evadeSpells["AhriEMissile"] = {EVADE_LINE,60.0f,1000.0f,1005.0f,0.0f,0,995};
    g_evadeSpells["AhriQ"] = {EVADE_LINE,100.0f,970.0f,787.3f,0.0f,250,1232};
    g_evadeSpells["AhriQMissile"] = {EVADE_LINE,100.0f,1000.0f,1333.3f,0.0f,0,750};
    g_evadeSpells["AhriQReturnMissile"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["AkaliE"] = {EVADE_LINE,70.0f,825.0f,794.8f,0.0f,250,1038};
    g_evadeSpells["AkaliEb"] = {EVADE_LINE,70.0f,625.0f,717.6f,0.0f,125,871};
    g_evadeSpells["AkaliEMis"] = {EVADE_LINE,60.0f,1175.0f,1213.8f,0.0f,0,968};
    g_evadeSpells["AkaliQ"] = {EVADE_LINE,70.0f,550.0f,680.7f,0.0f,250,808};
    g_evadeSpells["AkaliQMis"] = {EVADE_LINE,25.0f,1175.0f,2350.0f,0.0f,0,500};
    g_evadeSpells["AkaliQMis0"] = {EVADE_LINE,60.0f,1200.0f,1655.2f,0.0f,0,725};
    g_evadeSpells["AkaliQMis1"] = {EVADE_LINE,60.0f,1200.0f,1655.2f,0.0f,0,725};
    g_evadeSpells["AkaliQMis2"] = {EVADE_LINE,60.0f,1200.0f,1655.2f,0.0f,0,725};
    g_evadeSpells["AkaliQMis3"] = {EVADE_LINE,60.0f,1200.0f,1655.2f,0.0f,0,725};
    g_evadeSpells["AkaliQMis4"] = {EVADE_LINE,60.0f,1200.0f,1655.2f,0.0f,0,725};
    g_evadeSpells["AkaliQMis5"] = {EVADE_LINE,60.0f,1200.0f,1655.2f,0.0f,0,725};
    g_evadeSpells["AkaliRb"] = {EVADE_CIRCLE,400.0f,575.0f,638.9f,0.0f,0,900};
    g_evadeSpells["AkaliW"] = {EVADE_LINE,70.0f,350.0f,545.2f,0.0f,250,642};
    g_evadeSpells["AkaliWSmokeMissile"] = {EVADE_LINE,205.0f,1200.0f,774.2f,0.0f,0,1550};
    g_evadeSpells["AkaliWSmokeMissileRiver"] = {EVADE_LINE,205.0f,1200.0f,774.2f,0.0f,0,1550};
    g_evadeSpells["AkaliWSmokeParent"] = {EVADE_LINE,60.0f,1175.0f,1252.7f,0.0f,0,938};
    g_evadeSpells["AkaliWSmokeParentRiver"] = {EVADE_LINE,60.0f,1175.0f,1252.7f,0.0f,0,938};
    g_evadeSpells["AkshanEMissile"] = {EVADE_LINE,40.0f,750.0f,1153.8f,0.0f,0,650};
    g_evadeSpells["AkshanQMissile"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["AkshanQMissileReturn"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["AkshanRMissile"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,125,3500};
    g_evadeSpells["AlistarE"] = {EVADE_CIRCLE_START,575.0f,575.0f,766.7f,0.0f,0,750};
    g_evadeSpells["Pulverize"] = {EVADE_CIRCLE_START,365.0f,365.0f,486.7f,0.0f,0,750};
    g_evadeSpells["AmbessaQ"] = {EVADE_LINE,25.0f,650.0f,1300.0f,0.0f,0,500};
    g_evadeSpells["AmbessaQ1"] = {EVADE_LINE,25.0f,375.0f,750.0f,0.0f,250,500};
    g_evadeSpells["AmbessaQ2"] = {EVADE_LINE,25.0f,375.0f,750.0f,0.0f,250,500};
    g_evadeSpells["AuraofDespair"] = {EVADE_CIRCLE_START,300.0f,300.0f,400.0f,0.0f,0,750};
    g_evadeSpells["BandageToss"] = {EVADE_LINE,80.0f,1100.0f,1222.2f,0.0f,0,900};
    g_evadeSpells["CurseoftheSadMummy"] = {EVADE_CIRCLE_START,550.0f,550.0f,733.3f,0.0f,0,750};
    g_evadeSpells["SadMummyBandageToss"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Tantrum"] = {EVADE_CIRCLE_START,350.0f,350.0f,466.7f,0.0f,0,750};
    g_evadeSpells["FlashFrost"] = {EVADE_LINE,110.0f,1075.0f,665.6f,0.0f,0,1615};
    g_evadeSpells["FlashFrostSpell"] = {EVADE_LINE,110.0f,1100.0f,729.4f,0.0f,0,1508};
    g_evadeSpells["GlacialStorm"] = {EVADE_LINE,150.0f,750.0f,214.3f,0.0f,0,3500};
    g_evadeSpells["GlacialStormSpell"] = {EVADE_CIRCLE,300.0f,700.0f,777.8f,0.0f,0,900};
    g_evadeSpells["AnnieR"] = {EVADE_CIRCLE,250.0f,600.0f,666.7f,0.0f,0,900};
    g_evadeSpells["AnnieW"] = {EVADE_CONE,0.0f,600.0f,750.0f,12.380000114440918f,250,800};
    g_evadeSpells["Incinerate"] = {EVADE_CONE,0.0f,600.0f,750.0f,12.380000114440918f,250,800};
    g_evadeSpells["InfernalGuardian"] = {EVADE_CIRCLE,250.0f,600.0f,666.7f,0.0f,0,900};
    g_evadeSpells["ApheliosCalibrumQ"] = {EVADE_LINE,60.0f,1450.0f,1278.7f,0.0f,350,1134};
    g_evadeSpells["ApheliosCrescendumQ"] = {EVADE_LINE,110.0f,475.0f,712.1f,0.0f,250,667};
    g_evadeSpells["ApheliosCrescendumTurretMis"] = {EVADE_LINE,90.0f,3000.0f,857.1f,0.0f,0,3500};
    g_evadeSpells["ApheliosGravitumQ"] = {EVADE_LINE,65.0f,5000.0f,1428.6f,0.0f,250,3500};
    g_evadeSpells["ApheliosInfernumQ"] = {EVADE_LINE,65.0f,850.0f,1050.7f,0.0f,400,809};
    g_evadeSpells["ApheliosInfernumQMis"] = {EVADE_LINE,40.0f,3000.0f,1276.6f,0.0f,100,2350};
    g_evadeSpells["ApheliosR"] = {EVADE_LINE,110.0f,1300.0f,787.9f,0.0f,500,1650};
    g_evadeSpells["ApheliosRMis"] = {EVADE_LINE,125.0f,1300.0f,2292.8f,0.0f,250,567};
    g_evadeSpells["ApheliosSeverumQ"] = {EVADE_LINE,65.0f,620.0f,905.1f,0.0f,0,685};
    g_evadeSpells["EnchantedCrystalArrow"] = {EVADE_LINE,130.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Volley"] = {EVADE_CONE,0.0f,1200.0f,1500.0f,10.0f,0,800};
    g_evadeSpells["VolleyRank2"] = {EVADE_CONE,0.0f,1200.0f,1500.0f,10.0f,0,800};
    g_evadeSpells["VolleyRank3"] = {EVADE_CONE,0.0f,1200.0f,1500.0f,10.0f,0,800};
    g_evadeSpells["VolleyRank4"] = {EVADE_CONE,0.0f,1200.0f,1500.0f,10.0f,0,800};
    g_evadeSpells["VolleyRank5"] = {EVADE_CONE,0.0f,1200.0f,1500.0f,10.0f,0,800};
    g_evadeSpells["AuroraQ"] = {EVADE_LINE,90.0f,900.0f,986.8f,0.0f,250,912};
    g_evadeSpells["AuroraQReturnMissile"] = {EVADE_LINE,90.0f,5000.0f,1428.6f,0.0f,350,3500};
    g_evadeSpells["AuroraRMissile"] = {EVADE_LINE,100.0f,700.0f,518.5f,0.0f,0,1350};
    g_evadeSpells["AzirSoldierMissile"] = {EVADE_LINE,70.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["AzirSoldierRMissile"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["BardQMissile"] = {EVADE_LINE,60.0f,950.0f,966.4f,0.0f,0,983};
    g_evadeSpells["BardQMissile2"] = {EVADE_LINE,60.0f,525.0f,750.0f,0.0f,0,700};
    g_evadeSpells["BelvethW"] = {EVADE_LINE,100.0f,715.0f,1430.0f,0.0f,500,500};
    g_evadeSpells["RocketGrab"] = {EVADE_LINE,70.0f,1079.0f,1137.0f,0.0f,0,949};
    g_evadeSpells["RocketGrabMissile"] = {EVADE_LINE,70.0f,1080.0f,1136.8f,0.0f,0,950};
    g_evadeSpells["StaticField"] = {EVADE_CIRCLE_START,600.0f,600.0f,800.0f,0.0f,0,750};
    g_evadeSpells["BrandAblaze"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["BrandAblazeDetonateMarker"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["BrandBlaze"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["BrandBlazeMissile"] = {EVADE_LINE,60.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["BrandFissure"] = {EVADE_CIRCLE,240.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["BrandQ"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["BrandQMissile"] = {EVADE_LINE,60.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["BrandW"] = {EVADE_CIRCLE,240.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["BraumBlockStanceTimerMis1"] = {EVADE_LINE,85.0f,825.0f,1082.7f,0.0f,0,762};
    g_evadeSpells["BraumPulseLine"] = {EVADE_CIRCLE,250.0f,750.0f,833.3f,0.0f,0,900};
    g_evadeSpells["BraumPulseLineWrapper"] = {EVADE_LINE,80.0f,1250.0f,898.0f,0.0f,500,1392};
    g_evadeSpells["BraumQ"] = {EVADE_LINE,70.0f,1000.0f,794.3f,0.0f,250,1259};
    g_evadeSpells["BraumQMissile"] = {EVADE_LINE,60.0f,1050.0f,1084.7f,0.0f,250,968};
    g_evadeSpells["BraumRMissile"] = {EVADE_LINE,115.0f,1200.0f,994.2f,0.0f,250,1207};
    g_evadeSpells["BraumRWrapper"] = {EVADE_LINE,80.0f,1250.0f,898.0f,0.0f,500,1392};
    g_evadeSpells["BriarEMis"] = {EVADE_LINE,190.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["BriarEMisStrong"] = {EVADE_LINE,190.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["BriarR"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,1000,3500};
    g_evadeSpells["CaitlynE"] = {EVADE_LINE,70.0f,750.0f,915.8f,0.0f,0,819};
    g_evadeSpells["CaitlynEMissile"] = {EVADE_LINE,70.0f,800.0f,941.2f,0.0f,0,850};
    g_evadeSpells["CaitlynQ"] = {EVADE_LINE,60.0f,1300.0f,1381.5f,0.0f,0,941};
    g_evadeSpells["CaitlynQ2"] = {EVADE_LINE,90.0f,1300.0f,1381.5f,0.0f,0,941};
    g_evadeSpells["CaitlynRMissile"] = {EVADE_LINE,40.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["CamilleELeftMissile"] = {EVADE_LINE,30.0f,1100.0f,968.3f,0.0f,0,1136};
    g_evadeSpells["CamilleEMissile"] = {EVADE_LINE,30.0f,1100.0f,968.3f,0.0f,0,1136};
    g_evadeSpells["CamilleERightMissile"] = {EVADE_LINE,30.0f,1100.0f,968.3f,0.0f,0,1136};
    g_evadeSpells["CamilleW"] = {EVADE_LINE,100.0f,610.0f,872.7f,0.0f,0,699};
    g_evadeSpells["CassiopeiaQ"] = {EVADE_CIRCLE,160.0f,850.0f,944.4f,0.0f,0,900};
    g_evadeSpells["CassiopeiaR"] = {EVADE_CONE,0.0f,825.0f,1031.2f,20.0f,0,800};
    g_evadeSpells["CassiopeiaRStun"] = {EVADE_CONE,0.0f,750.0f,937.5f,20.0f,0,800};
    g_evadeSpells["CassiopeiaWMissile"] = {EVADE_LINE,25.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["CassiopeiaWSlow"] = {EVADE_CONE,0.0f,750.0f,937.5f,20.0f,0,800};
    g_evadeSpells["Chogath_Skin07_Blackhole"] = {EVADE_LINE,25.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["FeralScream"] = {EVADE_CONE,0.0f,585.0f,731.2f,14.0f,0,800};
    g_evadeSpells["Rupture"] = {EVADE_CIRCLE,230.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["VorpalSpikesMissle"] = {EVADE_LINE,170.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["VorpalSpikesMissle2"] = {EVADE_LINE,185.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["VorpalSpikesMissle3"] = {EVADE_LINE,190.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["VorpalSpikesMissle4"] = {EVADE_LINE,205.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["VorpalSpikesMissle5"] = {EVADE_LINE,220.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["VorpalSpikesMissle6"] = {EVADE_LINE,235.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["VorpalSpikesMissle7"] = {EVADE_LINE,250.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["CarpetBomb"] = {EVADE_LINE,160.0f,600.0f,497.1f,0.0f,0,1207};
    g_evadeSpells["DangerZone"] = {EVADE_CIRCLE,250.0f,900.0f,1000.0f,0.0f,0,900};
    g_evadeSpells["GGSpray"] = {EVADE_CONE,0.0f,685.0f,856.2f,15.380000114440918f,0,800};
    g_evadeSpells["GGun"] = {EVADE_CONE,0.0f,725.0f,906.2f,14.0f,0,800};
    g_evadeSpells["MissileBarrage"] = {EVADE_LINE,40.0f,1225.0f,669.8f,0.0f,0,1829};
    g_evadeSpells["MissileBarrageMissile"] = {EVADE_LINE,40.0f,1300.0f,1300.0f,0.0f,0,1000};
    g_evadeSpells["MissileBarrageMissile2"] = {EVADE_LINE,40.0f,1500.0f,1363.6f,0.0f,0,1100};
    g_evadeSpells["PhosphorusBomb"] = {EVADE_CIRCLE,250.0f,825.0f,916.7f,0.0f,0,900};
    g_evadeSpells["PhosphorusBombMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["PhosphorusBombMissileMin"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["DariusAxeGrabCone"] = {EVADE_CONE,0.0f,550.0f,687.5f,12.5f,250,800};
    g_evadeSpells["DariusHemoInternal"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["DariusHemoVisual"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["DianaQ"] = {EVADE_LINE,100.0f,900.0f,863.7f,0.0f,0,1042};
    g_evadeSpells["DianaQInnerMissile"] = {EVADE_LINE,70.0f,900.0f,1092.2f,0.0f,0,824};
    g_evadeSpells["DianaQOuterMissile"] = {EVADE_LINE,70.0f,900.0f,1155.3f,0.0f,0,779};
    g_evadeSpells["DianaR"] = {EVADE_CIRCLE_START,475.0f,475.0f,633.3f,0.0f,0,750};
    g_evadeSpells["DravenDoubleShot"] = {EVADE_LINE,130.0f,1050.0f,1043.7f,0.0f,250,1006};
    g_evadeSpells["DravenDoubleShotMissile"] = {EVADE_LINE,130.0f,1100.0f,968.3f,0.0f,251,1136};
    g_evadeSpells["DravenR"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,500,3500};
    g_evadeSpells["DravenRCast"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,500,3500};
    g_evadeSpells["DravenSpinning"] = {EVADE_LINE,50.0f,300.0f,85.7f,0.0f,0,3500};
    g_evadeSpells["DravenSpinningReturn"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["DravenSpinningReturnCatch"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["DravenSpinningReturnLeftAxe"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["DrMundoQ"] = {EVADE_LINE,60.0f,1050.0f,1200.0f,0.0f,0,875};
    g_evadeSpells["EkkoQ"] = {EVADE_LINE,60.0f,1075.0f,862.8f,0.0f,0,1246};
    g_evadeSpells["EkkoQMis"] = {EVADE_LINE,60.0f,950.0f,1025.9f,0.0f,251,926};
    g_evadeSpells["EkkoQReturn"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["EkkoQReturnDead"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["EkkoW"] = {EVADE_CIRCLE,375.0f,1600.0f,1777.8f,0.0f,0,900};
    g_evadeSpells["EkkoWMis"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["EliseHumanE"] = {EVADE_LINE,55.0f,1100.0f,1059.7f,0.0f,250,1038};
    g_evadeSpells["EliseHumanEMissile"] = {EVADE_LINE,55.0f,1100.0f,1059.7f,0.0f,250,1038};
    g_evadeSpells["EliseHumanW"] = {EVADE_LINE,100.0f,950.0f,1900.0f,0.0f,0,500};
    g_evadeSpells["EvelynnE"] = {EVADE_LINE,200.0f,210.0f,360.2f,0.0f,250,583};
    g_evadeSpells["EvelynnE2"] = {EVADE_LINE,200.0f,210.0f,360.2f,0.0f,150,583};
    g_evadeSpells["EvelynnQ"] = {EVADE_LINE,60.0f,800.0f,1171.3f,0.0f,250,683};
    g_evadeSpells["EvelynnQLineMissile"] = {EVADE_LINE,90.0f,680.0f,1178.5f,0.0f,0,577};
    g_evadeSpells["EvelynnR"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,350,3500};
    g_evadeSpells["EvelynnW"] = {EVADE_LINE,100.0f,1200.0f,1411.8f,0.0f,150,850};
    g_evadeSpells["EvelynnWApplyMark"] = {EVADE_LINE,100.0f,1200.0f,1411.8f,0.0f,150,850};
    g_evadeSpells["EzrealEMissile"] = {EVADE_LINE,80.0f,1400.0f,1333.3f,0.0f,0,1050};
    g_evadeSpells["EzrealQ"] = {EVADE_LINE,60.0f,1200.0f,1263.2f,0.0f,0,950};
    g_evadeSpells["EzrealR"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["EzrealW"] = {EVADE_LINE,80.0f,1200.0f,1136.4f,0.0f,0,1056};
    g_evadeSpells["FiddleSticksE"] = {EVADE_LINE,70.0f,850.0f,1034.1f,0.0f,400,822};
    g_evadeSpells["FiddleSticksQMissileNoFear"] = {EVADE_LINE,150.0f,1050.0f,1162.8f,0.0f,0,903};
    g_evadeSpells["FiddleSticksWCosmeticMissileBig"] = {EVADE_LINE,150.0f,1050.0f,1162.8f,0.0f,0,903};
    g_evadeSpells["FiddleSticksWCosmeticMissileSmall"] = {EVADE_LINE,150.0f,1050.0f,1162.8f,0.0f,0,903};
    g_evadeSpells["FioraE"] = {EVADE_LINE,40.0f,425.0f,603.7f,0.0f,0,704};
    g_evadeSpells["FioraWMissile"] = {EVADE_LINE,70.0f,5000.0f,2615.1f,0.0f,0,1912};
    g_evadeSpells["FioraWMissile2"] = {EVADE_LINE,70.0f,5000.0f,2615.1f,0.0f,0,1912};
    g_evadeSpells["FizzE"] = {EVADE_CIRCLE,330.0f,400.0f,444.4f,0.0f,0,900};
    g_evadeSpells["FizzETwo"] = {EVADE_CIRCLE,270.0f,400.0f,444.4f,0.0f,0,900};
    g_evadeSpells["FizzR"] = {EVADE_LINE,150.0f,1300.0f,963.0f,0.0f,250,1350};
    g_evadeSpells["FizzRHop"] = {EVADE_LINE,80.0f,1175.0f,884.1f,0.0f,251,1329};
    g_evadeSpells["FizzRMissile"] = {EVADE_LINE,80.0f,1175.0f,937.0f,0.0f,251,1254};
    g_evadeSpells["GalioQ"] = {EVADE_LINE,100.0f,825.0f,837.6f,0.0f,0,985};
    g_evadeSpells["GalioQMissile"] = {EVADE_LINE,60.0f,1200.0f,994.2f,0.0f,0,1207};
    g_evadeSpells["GalioQMissileR"] = {EVADE_LINE,60.0f,1200.0f,994.2f,0.0f,0,1207};
    g_evadeSpells["GalioW2"] = {EVADE_CIRCLE_START,400.0f,400.0f,533.3f,0.0f,0,750};
    g_evadeSpells["GangplankE"] = {EVADE_CIRCLE,325.0f,1000.0f,1111.1f,0.0f,250,900};
    g_evadeSpells["GangplankEBarrelFuseMissile"] = {EVADE_LINE,60.0f,1200.0f,1263.2f,0.0f,251,950};
    g_evadeSpells["GarenE"] = {EVADE_LINE,160.0f,660.0f,510.4f,0.0f,0,1293};
    g_evadeSpells["GnarBigE"] = {EVADE_CIRCLE,350.0f,675.0f,750.0f,0.0f,250,900};
    g_evadeSpells["GnarBigQ"] = {EVADE_LINE,80.0f,1100.0f,868.2f,0.0f,0,1267};
    g_evadeSpells["GnarBigQMissile"] = {EVADE_LINE,90.0f,1150.0f,1280.6f,0.0f,500,898};
    g_evadeSpells["GnarE"] = {EVADE_CIRCLE,150.0f,475.0f,527.8f,0.0f,0,900};
    g_evadeSpells["GnarQ"] = {EVADE_LINE,55.0f,1100.0f,868.2f,0.0f,0,1267};
    g_evadeSpells["GnarQMissile"] = {EVADE_LINE,55.0f,1125.0f,1406.2f,0.0f,250,800};
    g_evadeSpells["GnarQMissileReturn"] = {EVADE_LINE,75.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["GnarW"] = {EVADE_CIRCLE_START,300.0f,300.0f,400.0f,0.0f,0,750};
    g_evadeSpells["GragasQ"] = {EVADE_LINE,110.0f,850.0f,708.3f,0.0f,250,1200};
    g_evadeSpells["GragasQMissile"] = {EVADE_LINE,110.0f,5000.0f,1428.6f,0.0f,250,3500};
    g_evadeSpells["GragasQToggle"] = {EVADE_LINE,110.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["GragasR"] = {EVADE_LINE,120.0f,1050.0f,300.0f,0.0f,250,3500};
    g_evadeSpells["GragasRBoom"] = {EVADE_LINE,120.0f,1300.0f,1212.7f,0.0f,0,1072};
    g_evadeSpells["GravesChargeShot"] = {EVADE_LINE,100.0f,1000.0f,939.8f,0.0f,0,1064};
    g_evadeSpells["GravesChargeShotFxMissile"] = {EVADE_LINE,25.0f,650.0f,963.0f,0.0f,0,675};
    g_evadeSpells["GravesChargeShotFxMissile2"] = {EVADE_LINE,25.0f,700.0f,1000.0f,0.0f,0,700};
    g_evadeSpells["GravesChargeShotShot"] = {EVADE_LINE,100.0f,1100.0f,1258.6f,0.0f,250,874};
    g_evadeSpells["GravesChargeShotXplode"] = {EVADE_CONE,0.0f,735.0f,918.8f,15.0f,0,800};
    g_evadeSpells["GravesClusterShotRicochet"] = {EVADE_LINE,60.0f,1200.0f,1263.2f,0.0f,0,950};
    g_evadeSpells["GravesClusterShotSoundMissile"] = {EVADE_LINE,25.0f,700.0f,1000.0f,0.0f,0,700};
    g_evadeSpells["GravesQLineMis"] = {EVADE_LINE,40.0f,1200.0f,1600.0f,0.0f,0,750};
    g_evadeSpells["GravesQLineSpell"] = {EVADE_CONE,0.0f,900.0f,1125.0f,7.659999847412109f,0,800};
    g_evadeSpells["GravesQReturn"] = {EVADE_LINE,100.0f,1200.0f,1090.9f,0.0f,0,1100};
    g_evadeSpells["GravesQZoneMis"] = {EVADE_LINE,40.0f,1200.0f,1600.0f,0.0f,0,750};
    g_evadeSpells["GravesQZoneSpell"] = {EVADE_CONE,0.0f,900.0f,1125.0f,7.659999847412109f,0,800};
    g_evadeSpells["GravesSmokeGrenade"] = {EVADE_CIRCLE,225.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["GravesSmokeGrenadeBoom"] = {EVADE_LINE,120.0f,1500.0f,1111.1f,0.0f,0,1350};
    g_evadeSpells["GwenQ"] = {EVADE_LINE,60.0f,500.0f,833.3f,0.0f,500,600};
    g_evadeSpells["GwenRMis"] = {EVADE_LINE,120.0f,1350.0f,1227.3f,0.0f,0,1100};
    g_evadeSpells["GwenRRecast"] = {EVADE_LINE,60.0f,1300.0f,1068.2f,0.0f,500,1217};
    g_evadeSpells["HecarimCharge"] = {EVADE_LINE,120.0f,1350.0f,915.3f,0.0f,500,1475};
    g_evadeSpells["HecarimChargeCharge"] = {EVADE_LINE,50.0f,600.0f,171.4f,0.0f,0,3500};
    g_evadeSpells["HecarimRapidSlash"] = {EVADE_CIRCLE_START,375.0f,375.0f,500.0f,0.0f,0,750};
    g_evadeSpells["HecarimUlt"] = {EVADE_LINE,200.0f,5000.0f,1428.6f,0.0f,10,3500};
    g_evadeSpells["HecarimUltCharge"] = {EVADE_LINE,50.0f,600.0f,171.4f,0.0f,0,3500};
    g_evadeSpells["HecarimUltMissile"] = {EVADE_LINE,40.0f,1650.0f,891.9f,0.0f,0,1850};
    g_evadeSpells["HecarimUltMissileGrab"] = {EVADE_LINE,80.0f,1450.0f,930.7f,0.0f,0,1558};
    g_evadeSpells["HecarimUltMissileGrabEmpty"] = {EVADE_LINE,180.0f,2000.0f,991.6f,0.0f,0,2017};
    g_evadeSpells["HecarimUltMissileSkn4Audio"] = {EVADE_LINE,280.0f,1650.0f,891.9f,0.0f,0,1850};
    g_evadeSpells["HecarimUltMissileSkn4C"] = {EVADE_LINE,40.0f,1650.0f,891.9f,0.0f,0,1850};
    g_evadeSpells["HecarimUltMissileSkn4L1"] = {EVADE_LINE,40.0f,1650.0f,891.9f,0.0f,0,1850};
    g_evadeSpells["HecarimUltMissileSkn4L2"] = {EVADE_LINE,40.0f,1650.0f,891.9f,0.0f,0,1850};
    g_evadeSpells["HecarimUltMissileSkn4R1"] = {EVADE_LINE,40.0f,1650.0f,891.9f,0.0f,0,1850};
    g_evadeSpells["HecarimUltMissileSkn4R2"] = {EVADE_LINE,40.0f,1650.0f,891.9f,0.0f,0,1850};
    g_evadeSpells["HecarimW"] = {EVADE_CIRCLE_START,575.0f,575.0f,766.7f,0.0f,0,750};
    g_evadeSpells["HeimerdingerE"] = {EVADE_LINE,120.0f,925.0f,1284.7f,0.0f,250,720};
    g_evadeSpells["HeimerdingerE_Ult"] = {EVADE_LINE,120.0f,2500.0f,1851.9f,0.0f,0,1350};
    g_evadeSpells["HeimerdingerESpell"] = {EVADE_LINE,120.0f,1400.0f,922.9f,0.0f,0,1517};
    g_evadeSpells["HeimerdingerESpell_ult"] = {EVADE_LINE,120.0f,1000.0f,845.3f,0.0f,0,1183};
    g_evadeSpells["HeimerdingerESpell_ult2"] = {EVADE_LINE,120.0f,1000.0f,939.8f,0.0f,0,1064};
    g_evadeSpells["HeimerdingerESpell_ult3"] = {EVADE_LINE,120.0f,1000.0f,939.8f,0.0f,0,1064};
    g_evadeSpells["HeimerdingerEUlt"] = {EVADE_LINE,120.0f,2500.0f,1851.9f,0.0f,0,1350};
    g_evadeSpells["HeimerdingerW"] = {EVADE_LINE,200.0f,1325.0f,728.4f,0.0f,250,1819};
    g_evadeSpells["HeimerdingerWUlt"] = {EVADE_LINE,200.0f,1325.0f,728.4f,0.0f,250,1819};
    g_evadeSpells["HweiEE"] = {EVADE_CIRCLE,400.0f,800.0f,888.9f,0.0f,350,900};
    g_evadeSpells["HweiEQ"] = {EVADE_LINE,70.0f,1100.0f,919.7f,0.0f,250,1196};
    g_evadeSpells["HweiQE"] = {EVADE_LINE,90.0f,1200.0f,648.6f,0.0f,350,1850};
    g_evadeSpells["HweiQETell"] = {EVADE_LINE,90.0f,1100.0f,1015.7f,0.0f,350,1083};
    g_evadeSpells["HweiQQ"] = {EVADE_LINE,70.0f,800.0f,1066.7f,0.0f,250,750};
    g_evadeSpells["HweiR"] = {EVADE_LINE,90.0f,1300.0f,1016.4f,0.0f,250,1279};
    g_evadeSpells["HweiWQ"] = {EVADE_LINE,145.0f,1200.0f,1136.4f,0.0f,250,1056};
    g_evadeSpells["IllaoiEMis"] = {EVADE_LINE,50.0f,950.0f,1117.6f,0.0f,0,850};
    g_evadeSpells["IreliaEMissile"] = {EVADE_LINE,90.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["IreliaESecondary"] = {EVADE_LINE,70.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["IreliaR"] = {EVADE_LINE,160.0f,1000.0f,1176.5f,0.0f,0,850};
    g_evadeSpells["IvernQ"] = {EVADE_LINE,80.0f,1150.0f,931.2f,0.0f,250,1235};
    g_evadeSpells["IvernR"] = {EVADE_CIRCLE,600.0f,600.0f,666.7f,0.0f,500,900};
    g_evadeSpells["IvernRMissile"] = {EVADE_LINE,100.0f,800.0f,868.6f,0.0f,250,921};
    g_evadeSpells["HowlingGaleSpell"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ReapTheWhirlwind"] = {EVADE_CIRCLE_START,600.0f,725.0f,966.7f,0.0f,1,750};
    g_evadeSpells["SowTheWind"] = {EVADE_LINE,60.0f,550.0f,792.5f,0.0f,245,694};
    g_evadeSpells["JarvanIVDemacianStandard"] = {EVADE_CIRCLE,175.0f,860.0f,955.6f,0.0f,0,900};
    g_evadeSpells["JarvanIVDragonStrike"] = {EVADE_LINE,70.0f,770.0f,220.0f,0.0f,0,3500};
    g_evadeSpells["JarvanIVGoldenAegis"] = {EVADE_CIRCLE_START,400.0f,625.0f,833.3f,0.0f,0,750};
    g_evadeSpells["JaxR"] = {EVADE_CIRCLE_START,250.0f,260.0f,346.7f,0.0f,250,750};
    g_evadeSpells["JayceShockBlast"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,214,1225};
    g_evadeSpells["JayceShockBlastMis"] = {EVADE_LINE,70.0f,1300.0f,1042.5f,0.0f,250,1247};
    g_evadeSpells["JayceShockBlastWallMis"] = {EVADE_LINE,70.0f,2000.0f,1665.3f,0.0f,0,1201};
    g_evadeSpells["JayceStaticField"] = {EVADE_LINE,200.0f,285.0f,527.8f,0.0f,0,540};
    g_evadeSpells["JayceThunderingBlow"] = {EVADE_LINE,80.0f,240.0f,68.6f,0.0f,250,3500};
    g_evadeSpells["JhinE"] = {EVADE_LINE,120.0f,750.0f,681.8f,0.0f,250,1100};
    g_evadeSpells["JhinETrap"] = {EVADE_LINE,120.0f,2000.0f,1250.0f,0.0f,0,1600};
    g_evadeSpells["JhinQ"] = {EVADE_LINE,80.0f,550.0f,838.4f,0.0f,0,656};
    g_evadeSpells["JhinR"] = {EVADE_CONE,0.0f,25000.0f,31250.0f,0.0f,1000,800};
    g_evadeSpells["JhinRShot"] = {EVADE_LINE,80.0f,3500.0f,1071.3f,0.0f,0,3267};
    g_evadeSpells["JhinRShotMis"] = {EVADE_LINE,80.0f,3500.0f,5000.0f,0.0f,0,700};
    g_evadeSpells["JhinRShotMis4"] = {EVADE_LINE,80.0f,3500.0f,5000.0f,0.0f,0,700};
    g_evadeSpells["JhinW"] = {EVADE_LINE,40.0f,3000.0f,4615.4f,0.0f,750,650};
    g_evadeSpells["JinxEHit"] = {EVADE_LINE,120.0f,1200.0f,832.8f,0.0f,0,1441};
    g_evadeSpells["JinxEMine"] = {EVADE_CIRCLE,100.0f,800.0f,888.9f,0.0f,0,900};
    g_evadeSpells["JinxR"] = {EVADE_LINE,140.0f,5000.0f,1428.6f,0.0f,600,3500};
    g_evadeSpells["JinxW"] = {EVADE_LINE,60.0f,1450.0f,930.7f,0.0f,250,1558};
    g_evadeSpells["JinxWMissile"] = {EVADE_LINE,60.0f,1500.0f,1863.4f,0.0f,600,805};
    g_evadeSpells["KaisaQ"] = {EVADE_LINE,120.0f,600.0f,1016.9f,0.0f,250,590};
    g_evadeSpells["KaisaQLeftMissile1"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQLeftMissile2"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQLeftMissile3"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQLeftMissile4"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQLeftMissile5"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQLeftMissile6"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQRightMissile1"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQRightMissile2"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQRightMissile3"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQRightMissile4"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQRightMissile5"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaQRightMissile6"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KaisaW"] = {EVADE_LINE,100.0f,3000.0f,1453.5f,0.0f,400,2064};
    g_evadeSpells["KalistaAltarOut"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["KalistaMysticShot"] = {EVADE_LINE,40.0f,1150.0f,879.2f,0.0f,0,1308};
    g_evadeSpells["KalistaMysticShotMis"] = {EVADE_LINE,25.0f,1200.0f,1445.8f,0.0f,350,830};
    g_evadeSpells["KalistaMysticShotMissile"] = {EVADE_LINE,25.0f,1200.0f,1600.0f,0.0f,251,750};
    g_evadeSpells["KalistaMysticShotMisTrue"] = {EVADE_LINE,40.0f,1200.0f,1411.8f,0.0f,350,850};
    g_evadeSpells["KalistaPAltarBuff"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["KalistaPInvocation"] = {EVADE_LINE,60.0f,1200.0f,1263.2f,0.0f,3000,950};
    g_evadeSpells["KalistaRAlly"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KalistaRAllyStun"] = {EVADE_LINE,160.0f,1200.0f,581.4f,0.0f,0,2064};
    g_evadeSpells["KalistaRAllyStunBuffer"] = {EVADE_LINE,160.0f,1200.0f,581.4f,0.0f,0,2064};
    g_evadeSpells["KalistaRAllyStunMarker"] = {EVADE_LINE,160.0f,1200.0f,581.4f,0.0f,0,2064};
    g_evadeSpells["KalistaRMis"] = {EVADE_LINE,80.0f,1450.0f,1101.0f,0.0f,0,1317};
    g_evadeSpells["KalistaWSpawnTempMis"] = {EVADE_LINE,50.0f,1200.0f,1243.5f,0.0f,251,965};
    g_evadeSpells["KarmaQ"] = {EVADE_CONE,0.0f,950.0f,1187.5f,12.15999984741211f,0,800};
    g_evadeSpells["KarmaQMissile"] = {EVADE_LINE,60.0f,950.0f,1045.1f,0.0f,0,909};
    g_evadeSpells["KarmaQMissileMantra"] = {EVADE_LINE,80.0f,950.0f,1045.1f,0.0f,0,909};
    g_evadeSpells["KarmaSpiritBind"] = {EVADE_LINE,60.0f,675.0f,1027.4f,0.0f,250,657};
    g_evadeSpells["KarthusDefile"] = {EVADE_LINE,150.0f,550.0f,611.1f,0.0f,250,900};
    g_evadeSpells["KarthusDefile2"] = {EVADE_LINE,150.0f,550.0f,611.1f,0.0f,250,900};
    g_evadeSpells["KarthusLayWaste"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusLayWaste2"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusLayWasteA1"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusLayWasteA2"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusLayWasteA3"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusLayWasteDeadA1"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusLayWasteDeadA2"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusLayWasteDeadA3"] = {EVADE_CIRCLE,160.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["KarthusWallofPainExtra"] = {EVADE_LINE,130.0f,1000.0f,1025.6f,0.0f,0,975};
    g_evadeSpells["KarthusWallOfPainExtra2"] = {EVADE_LINE,130.0f,1000.0f,1025.6f,0.0f,0,975};
    g_evadeSpells["ForcePulse"] = {EVADE_CONE,0.0f,600.0f,750.0f,19.5f,250,800};
    g_evadeSpells["RiftWalk"] = {EVADE_CIRCLE,270.0f,500.0f,555.6f,0.0f,250,900};
    g_evadeSpells["KatarinaQ"] = {EVADE_LINE,60.0f,625.0f,843.5f,0.0f,250,741};
    g_evadeSpells["KatarinaR"] = {EVADE_CIRCLE_START,550.0f,550.0f,733.3f,0.0f,0,750};
    g_evadeSpells["KatarinaRTrigger"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KatarinaRTriggerSound"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KatarinaW"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KayleEnrageConeMis"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KayleEnrageConeMisUpgrade"] = {EVADE_LINE,100.0f,850.0f,1299.7f,0.0f,0,654};
    g_evadeSpells["KayleQMis"] = {EVADE_LINE,75.0f,900.0f,1125.0f,0.0f,0,800};
    g_evadeSpells["KayleW"] = {EVADE_CONE,0.0f,1200.0f,1500.0f,10.0f,250,800};
    g_evadeSpells["KaynSlayRMis"] = {EVADE_LINE,150.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KennenShurikenHurl1"] = {EVADE_LINE,50.0f,1000.0f,1066.1f,0.0f,0,938};
    g_evadeSpells["KennenShurikenHurlMissile1"] = {EVADE_LINE,50.0f,1050.0f,1084.7f,0.0f,0,968};
    g_evadeSpells["KennenShurikenStorm"] = {EVADE_CIRCLE_START,210.0f,550.0f,733.3f,0.0f,0,750};
    g_evadeSpells["KhazixE"] = {EVADE_CIRCLE,300.0f,700.0f,777.8f,0.0f,250,900};
    g_evadeSpells["KhazixEInvisMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["KhazixELong"] = {EVADE_CIRCLE,300.0f,900.0f,1000.0f,0.0f,0,900};
    g_evadeSpells["KhazixW"] = {EVADE_LINE,60.0f,1000.0f,642.3f,0.0f,250,1557};
    g_evadeSpells["KhazixWLong"] = {EVADE_CONE,0.0f,1000.0f,1250.0f,14.0f,250,800};
    g_evadeSpells["KhazixWMissile"] = {EVADE_LINE,70.0f,1025.0f,1075.6f,0.0f,250,953};
    g_evadeSpells["KindredERefresher"] = {EVADE_CIRCLE_START,550.0f,550.0f,733.3f,0.0f,0,750};
    g_evadeSpells["KindredESlow"] = {EVADE_CIRCLE_START,550.0f,550.0f,733.3f,0.0f,0,750};
    g_evadeSpells["KindredEWolfMissileWorkaroundBuff"] = {EVADE_CIRCLE_START,550.0f,550.0f,733.3f,0.0f,0,750};
    g_evadeSpells["KindredFakeCastTimeSpell"] = {EVADE_CIRCLE_START,400.0f,400.0f,533.3f,0.0f,250,750};
    g_evadeSpells["KindredQMissile"] = {EVADE_LINE,80.0f,1175.0f,1083.9f,0.0f,0,1084};
    g_evadeSpells["KindredQMissileMinor"] = {EVADE_LINE,80.0f,1175.0f,1083.9f,0.0f,0,1084};
    g_evadeSpells["KindredRNoDeathBuff"] = {EVADE_CIRCLE,280.0f,900.0f,1000.0f,0.0f,0,900};
    g_evadeSpells["KledQMissile"] = {EVADE_LINE,45.0f,2000.0f,1250.0f,0.0f,251,1600};
    g_evadeSpells["KledRiderQ"] = {EVADE_LINE,80.0f,700.0f,750.3f,0.0f,250,933};
    g_evadeSpells["KledRiderQMissile"] = {EVADE_LINE,40.0f,700.0f,1200.7f,0.0f,251,583};
    g_evadeSpells["KogMawLivingArtillery"] = {EVADE_CIRCLE,240.0f,1300.0f,1444.4f,0.0f,0,900};
    g_evadeSpells["KogMawQ"] = {EVADE_LINE,70.0f,1200.0f,1114.2f,0.0f,0,1077};
    g_evadeSpells["KogMawQMis"] = {EVADE_LINE,70.0f,1200.0f,1114.2f,0.0f,251,1077};
    g_evadeSpells["KogMawVoidOoze"] = {EVADE_LINE,120.0f,1200.0f,888.9f,0.0f,0,1350};
    g_evadeSpells["KogMawVoidOozeMissile"] = {EVADE_LINE,120.0f,1360.0f,1029.5f,0.0f,0,1321};
    g_evadeSpells["KSanteQ3Missile"] = {EVADE_LINE,70.0f,825.0f,1021.0f,0.0f,0,808};
    g_evadeSpells["KSanteRMissile1"] = {EVADE_LINE,70.0f,2000.0f,1690.6f,0.0f,0,1183};
    g_evadeSpells["KSanteRMissile2"] = {EVADE_LINE,70.0f,2000.0f,1368.9f,0.0f,0,1461};
    g_evadeSpells["KSanteW"] = {EVADE_LINE,55.0f,250.0f,483.6f,0.0f,0,517};
    g_evadeSpells["KSanteW_AllOut"] = {EVADE_LINE,55.0f,250.0f,483.6f,0.0f,0,517};
    g_evadeSpells["LeblancEMissile"] = {EVADE_LINE,55.0f,950.0f,1063.8f,0.0f,0,893};
    g_evadeSpells["LeblancRE"] = {EVADE_LINE,55.0f,925.0f,1052.3f,0.0f,0,879};
    g_evadeSpells["LeblancREMissile"] = {EVADE_LINE,55.0f,950.0f,1063.8f,0.0f,0,893};
    g_evadeSpells["LeblancRW"] = {EVADE_CIRCLE,220.0f,600.0f,666.7f,0.0f,0,900};
    g_evadeSpells["LeblancRWMove"] = {EVADE_CIRCLE,220.0f,600.0f,666.7f,0.0f,0,900};
    g_evadeSpells["LeblancW"] = {EVADE_CIRCLE,220.0f,600.0f,666.7f,0.0f,0,900};
    g_evadeSpells["LeblancWMove"] = {EVADE_CIRCLE,220.0f,600.0f,666.7f,0.0f,0,900};
    g_evadeSpells["LeeSinEOne"] = {EVADE_CIRCLE_START,450.0f,450.0f,600.0f,0.0f,0,750};
    g_evadeSpells["LeeSinETwo"] = {EVADE_CIRCLE_START,575.0f,575.0f,766.7f,0.0f,0,750};
    g_evadeSpells["LeeSinQOne"] = {EVADE_LINE,60.0f,1200.0f,1179.9f,0.0f,0,1017};
    g_evadeSpells["LeeSinQTwo"] = {EVADE_CIRCLE_START,650.0f,1250.0f,1666.7f,0.0f,0,750};
    g_evadeSpells["LeonaSolarBarrier"] = {EVADE_CIRCLE_START,500.0f,500.0f,666.7f,0.0f,0,750};
    g_evadeSpells["LeonaSolarFlare"] = {EVADE_CIRCLE,120.0f,1200.0f,1333.3f,0.0f,0,900};
    g_evadeSpells["LeonaZenithBlade"] = {EVADE_LINE,80.0f,875.0f,810.9f,0.0f,250,1079};
    g_evadeSpells["LeonaZenithBladeMissile"] = {EVADE_LINE,70.0f,900.0f,1125.0f,0.0f,0,800};
    g_evadeSpells["LilliaE"] = {EVADE_LINE,80.0f,700.0f,933.3f,0.0f,350,750};
    g_evadeSpells["LilliaERollingMissile"] = {EVADE_LINE,85.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["LilliaPranceStack"] = {EVADE_LINE,160.0f,650.0f,508.2f,0.0f,0,1279};
    g_evadeSpells["LilliaRExpungeMissile"] = {EVADE_LINE,50.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["LilliaRExpungeMissileTwo"] = {EVADE_LINE,50.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["LissandraE"] = {EVADE_LINE,110.0f,1050.0f,662.5f,0.0f,0,1585};
    g_evadeSpells["LissandraEBuffer"] = {EVADE_LINE,110.0f,1100.0f,669.1f,0.0f,0,1644};
    g_evadeSpells["LissandraEDamage"] = {EVADE_LINE,110.0f,1100.0f,669.1f,0.0f,0,1644};
    g_evadeSpells["LissandraEMissile"] = {EVADE_LINE,125.0f,1025.0f,969.7f,0.0f,0,1057};
    g_evadeSpells["LissandraQ"] = {EVADE_LINE,75.0f,725.0f,760.0f,0.0f,0,954};
    g_evadeSpells["LissandraQMissile"] = {EVADE_LINE,75.0f,700.0f,1047.9f,0.0f,251,668};
    g_evadeSpells["LissandraQShards"] = {EVADE_LINE,90.0f,1650.0f,1500.0f,0.0f,0,1100};
    g_evadeSpells["LissandraRRemoval"] = {EVADE_LINE,120.0f,1050.0f,1043.7f,0.0f,0,1006};
    g_evadeSpells["LissandraRSlow"] = {EVADE_LINE,80.0f,650.0f,963.0f,0.0f,0,675};
    g_evadeSpells["LissandraW"] = {EVADE_CONE,0.0f,600.0f,750.0f,10.0f,0,800};
    g_evadeSpells["LissandraWFrozen"] = {EVADE_LINE,250.0f,375.0f,731.0f,0.0f,0,513};
    g_evadeSpells["LockeE"] = {EVADE_CONE,0.0f,450.0f,562.5f,15.0f,175,800};
    g_evadeSpells["LockeEDashOut"] = {EVADE_CONE,0.0f,475.0f,593.8f,15.0f,0,800};
    g_evadeSpells["LockeQMissile"] = {EVADE_LINE,65.0f,950.0f,1025.9f,0.0f,250,926};
    g_evadeSpells["LockeR"] = {EVADE_LINE,100.0f,1000.0f,285.7f,0.0f,250,3500};
    g_evadeSpells["LockeRNailMissile"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,125,3500};
    g_evadeSpells["LucianRMissile"] = {EVADE_LINE,110.0f,1200.0f,1540.4f,0.0f,0,779};
    g_evadeSpells["LucianRMissileOffhand"] = {EVADE_LINE,110.0f,1200.0f,1540.4f,0.0f,0,779};
    g_evadeSpells["LucianWMissile"] = {EVADE_LINE,55.0f,900.0f,986.8f,0.0f,0,912};
    g_evadeSpells["LuluQ"] = {EVADE_LINE,80.0f,925.0f,914.9f,0.0f,0,1011};
    g_evadeSpells["LuluQMissile"] = {EVADE_LINE,60.0f,950.0f,945.3f,0.0f,0,1005};
    g_evadeSpells["LuluQMissileTwo"] = {EVADE_LINE,60.0f,950.0f,945.3f,0.0f,0,1005};
    g_evadeSpells["LuxLightBinding"] = {EVADE_LINE,80.0f,1175.0f,884.1f,0.0f,0,1329};
    g_evadeSpells["LuxLightBindingMis"] = {EVADE_LINE,70.0f,1300.0f,907.2f,0.0f,0,1433};
    g_evadeSpells["LuxLightStrikeKugel"] = {EVADE_CIRCLE,295.0f,1100.0f,1222.2f,0.0f,0,900};
    g_evadeSpells["LuxPrismaticWave"] = {EVADE_LINE,150.0f,1150.0f,879.2f,0.0f,0,1308};
    g_evadeSpells["LuxPrismaticWaveMissile"] = {EVADE_LINE,110.0f,1175.0f,1329.2f,0.0f,0,884};
    g_evadeSpells["LuxPrismaticWaveReturn"] = {EVADE_LINE,110.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["LuxPrismaticWaveReturnDead"] = {EVADE_LINE,110.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["LuxR"] = {EVADE_LINE,190.0f,3340.0f,2283.0f,0.0f,0,1463};
    g_evadeSpells["LuxRMis"] = {EVADE_LINE,250.0f,3500.0f,2307.2f,0.0f,0,1517};
    g_evadeSpells["Landslide"] = {EVADE_CIRCLE_START,400.0f,400.0f,533.3f,0.0f,242,750};
    g_evadeSpells["UFSlash"] = {EVADE_LINE,160.0f,1000.0f,562.1f,0.0f,0,1779};
    g_evadeSpells["UnstoppableForceStun"] = {EVADE_LINE,160.0f,1200.0f,581.4f,0.0f,0,2064};
    g_evadeSpells["MalzaharQMissile"] = {EVADE_LINE,85.0f,750.0f,915.8f,0.0f,600,819};
    g_evadeSpells["MaokaiE"] = {EVADE_LINE,120.0f,1100.0f,1015.7f,0.0f,0,1083};
    g_evadeSpells["MaokaiEMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["MaokaiEMissileBrush"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["MaokaiEMissileBrushTiny"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["MaokaiEMissileBrushTiny2"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["MaokaiQ"] = {EVADE_LINE,70.0f,600.0f,827.6f,0.0f,0,725};
    g_evadeSpells["MaokaiQMissile"] = {EVADE_LINE,110.0f,650.0f,859.8f,0.0f,0,756};
    g_evadeSpells["MaokaiRMis"] = {EVADE_LINE,120.0f,3000.0f,857.1f,0.0f,0,3500};
    g_evadeSpells["MaokaiRMisExtra"] = {EVADE_LINE,150.0f,3000.0f,857.1f,0.0f,0,3500};
    g_evadeSpells["MaokaiRSoundMis"] = {EVADE_LINE,120.0f,2200.0f,628.6f,0.0f,0,3500};
    g_evadeSpells["MaokaiW"] = {EVADE_LINE,120.0f,525.0f,750.0f,0.0f,0,700};
    g_evadeSpells["MelE"] = {EVADE_LINE,150.0f,1000.0f,794.3f,0.0f,250,1259};
    g_evadeSpells["MelQ"] = {EVADE_CIRCLE,140.0f,950.0f,1055.6f,0.0f,350,900};
    g_evadeSpells["MilioQ"] = {EVADE_CONE,0.0f,1200.0f,1500.0f,17.5f,250,800};
    g_evadeSpells["MissFortuneBulletEMPTY"] = {EVADE_CONE,0.0f,1500.0f,1875.0f,11.5f,0,800};
    g_evadeSpells["MissFortuneBullets"] = {EVADE_LINE,25.0f,1450.0f,1348.8f,0.0f,250,1075};
    g_evadeSpells["MissFortuneBulletTime"] = {EVADE_CONE,0.0f,25000.0f,31250.0f,8.5f,1,800};
    g_evadeSpells["MissFortuneRicochetShot"] = {EVADE_CONE,0.0f,550.0f,687.5f,30.0f,250,800};
    g_evadeSpells["MonkeyKingSpinToWin"] = {EVADE_LINE,160.0f,660.0f,510.4f,0.0f,0,1293};
    g_evadeSpells["MordekaiserEMissile"] = {EVADE_LINE,100.0f,700.0f,1200.7f,0.0f,0,583};
    g_evadeSpells["MordekaiserSoulMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["MorganaQ"] = {EVADE_LINE,70.0f,1300.0f,907.2f,0.0f,250,1433};
    g_evadeSpells["MorganaR"] = {EVADE_CIRCLE_START,625.0f,625.0f,833.3f,0.0f,0,750};
    g_evadeSpells["MorganaW"] = {EVADE_CIRCLE,280.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["NaafiriPackmateLeap"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["NaafiriQ"] = {EVADE_LINE,150.0f,900.0f,1023.9f,0.0f,250,879};
    g_evadeSpells["NaafiriQRecast"] = {EVADE_LINE,150.0f,900.0f,1023.9f,0.0f,250,879};
    g_evadeSpells["NamiQ"] = {EVADE_CIRCLE,200.0f,850.0f,944.4f,0.0f,250,900};
    g_evadeSpells["NamiQDebuff"] = {EVADE_CIRCLE,155.0f,850.0f,944.4f,0.0f,250,900};
    g_evadeSpells["NamiQMissile"] = {EVADE_LINE,120.0f,1625.0f,1625.0f,0.0f,0,1000};
    g_evadeSpells["NamiR"] = {EVADE_LINE,325.0f,400.0f,585.7f,0.0f,500,683};
    g_evadeSpells["NamiRMissile"] = {EVADE_LINE,250.0f,2750.0f,785.7f,0.0f,0,3500};
    g_evadeSpells["NasusE"] = {EVADE_CIRCLE,380.0f,650.0f,722.2f,0.0f,0,900};
    g_evadeSpells["NautilusAnchorDrag"] = {EVADE_LINE,80.0f,1150.0f,879.2f,0.0f,0,1308};
    g_evadeSpells["NautilusAnchorDragMissile"] = {EVADE_LINE,90.0f,925.0f,1139.2f,0.0f,250,812};
    g_evadeSpells["NautilusGrandLineStart"] = {EVADE_LINE,90.0f,825.0f,878.6f,0.0f,400,939};
    g_evadeSpells["NautilusPiercingGaze"] = {EVADE_CONE,0.0f,1400.0f,1750.0f,10.0f,0,800};
    g_evadeSpells["NautilusSplashZone"] = {EVADE_CIRCLE_START,600.0f,600.0f,800.0f,0.0f,250,750};
    g_evadeSpells["NautilusSplashZoneSplash"] = {EVADE_LINE,85.0f,350.0f,310.3f,0.0f,250,1128};
    g_evadeSpells["NautilusWDoT"] = {EVADE_CONE,0.0f,1400.0f,1750.0f,10.0f,0,800};
    g_evadeSpells["NeekoE"] = {EVADE_LINE,70.0f,1000.0f,893.7f,0.0f,0,1119};
    g_evadeSpells["Bushwhack"] = {EVADE_CIRCLE,80.0f,900.0f,1000.0f,0.0f,0,900};
    g_evadeSpells["JavelinToss"] = {EVADE_LINE,40.0f,1500.0f,997.3f,0.0f,250,1504};
    g_evadeSpells["Pounce"] = {EVADE_CIRCLE,235.0f,400.0f,444.4f,0.0f,0,900};
    g_evadeSpells["Swipe"] = {EVADE_CONE,0.0f,350.0f,437.5f,45.0f,250,800};
    g_evadeSpells["NilahEQMis"] = {EVADE_LINE,125.0f,1200.0f,1411.8f,0.0f,0,850};
    g_evadeSpells["NocturneDuskbringer"] = {EVADE_LINE,60.0f,1200.0f,1090.9f,0.0f,0,1100};
    g_evadeSpells["NunuE"] = {EVADE_LINE,80.0f,5000.0f,1637.7f,0.0f,0,3053};
    g_evadeSpells["NunuESnowballBurstFire"] = {EVADE_LINE,80.0f,700.0f,961.5f,0.0f,0,728};
    g_evadeSpells["NunuESnowballMissile"] = {EVADE_LINE,40.0f,700.0f,1000.0f,0.0f,0,700};
    g_evadeSpells["NunuR"] = {EVADE_CIRCLE_START,500.0f,650.0f,866.7f,0.0f,10,750};
    g_evadeSpells["NunuR_Recast"] = {EVADE_LINE,80.0f,650.0f,927.2f,0.0f,0,701};
    g_evadeSpells["NunuW"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["NunuW_Recast"] = {EVADE_LINE,80.0f,650.0f,927.2f,0.0f,0,701};
    g_evadeSpells["NunuWSnowballMissile"] = {EVADE_LINE,137.5f,1175.0f,335.7f,0.0f,251,3500};
    g_evadeSpells["OlafAxeThrow"] = {EVADE_LINE,90.0f,1000.0f,1025.6f,0.0f,0,975};
    g_evadeSpells["OlafAxeThrowCast"] = {EVADE_LINE,90.0f,1000.0f,1025.6f,0.0f,250,975};
    g_evadeSpells["OlafAxeThrowDamage"] = {EVADE_LINE,90.0f,1000.0f,1025.6f,0.0f,0,975};
    g_evadeSpells["OrianaDetonateCommand"] = {EVADE_LINE,80.0f,410.0f,592.5f,0.0f,500,692};
    g_evadeSpells["OrianaDissonanceCommand"] = {EVADE_LINE,80.0f,225.0f,418.2f,0.0f,0,538};
    g_evadeSpells["OrianaIzuna"] = {EVADE_LINE,80.0f,2000.0f,1124.2f,0.0f,0,1779};
    g_evadeSpells["OrianaIzunaCommand"] = {EVADE_LINE,80.0f,825.0f,794.8f,0.0f,0,1038};
    g_evadeSpells["OrianaRedact"] = {EVADE_LINE,80.0f,2500.0f,1469.7f,0.0f,0,1701};
    g_evadeSpells["OrianaRedactCommand"] = {EVADE_LINE,80.0f,1120.0f,873.0f,0.0f,0,1283};
    g_evadeSpells["OrianaReturn"] = {EVADE_LINE,200.0f,1500.0f,1474.9f,0.0f,0,1017};
    g_evadeSpells["OrnnQ"] = {EVADE_LINE,65.0f,750.0f,977.8f,0.0f,300,767};
    g_evadeSpells["OrnnRWave"] = {EVADE_LINE,250.0f,3500.0f,1000.0f,0.0f,250,3500};
    g_evadeSpells["OrnnRWave2"] = {EVADE_LINE,200.0f,3500.0f,1416.4f,0.0f,250,2471};
    g_evadeSpells["PantheonQ"] = {EVADE_LINE,80.0f,575.0f,693.6f,0.0f,0,829};
    g_evadeSpells["PantheonQMissile"] = {EVADE_LINE,60.0f,1000.0f,1388.9f,0.0f,0,720};
    g_evadeSpells["PantheonRMissile"] = {EVADE_LINE,250.0f,2000.0f,2666.7f,0.0f,0,750};
    g_evadeSpells["PantheonRMissile2"] = {EVADE_LINE,450.0f,1350.0f,385.7f,0.0f,0,3500};
    g_evadeSpells["PantheonRMissile3"] = {EVADE_LINE,55.0f,3350.0f,5687.6f,0.0f,0,589};
    g_evadeSpells["PoppyRMissile"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["PykeEMissile"] = {EVADE_LINE,110.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["PykeQRange"] = {EVADE_LINE,70.0f,1100.0f,1222.2f,0.0f,0,900};
    g_evadeSpells["QiyanaQ_ExplosionMissile"] = {EVADE_LINE,100.0f,425.0f,756.2f,0.0f,250,562};
    g_evadeSpells["QiyanaQ_Grass"] = {EVADE_LINE,100.0f,500.0f,755.3f,0.0f,250,662};
    g_evadeSpells["QiyanaQ_Rock"] = {EVADE_LINE,100.0f,500.0f,755.3f,0.0f,250,662};
    g_evadeSpells["QiyanaQ_Water"] = {EVADE_LINE,100.0f,500.0f,755.3f,0.0f,250,662};
    g_evadeSpells["QiyanaRMis"] = {EVADE_LINE,140.0f,1550.0f,1377.8f,0.0f,250,1125};
    g_evadeSpells["QiyanaRWallFollowMis"] = {EVADE_LINE,130.0f,1550.0f,1729.9f,0.0f,250,896};
    g_evadeSpells["QiyanaRWallFollowMisCounterClockwise"] = {EVADE_LINE,130.0f,1550.0f,1729.9f,0.0f,250,896};
    g_evadeSpells["QiyanaRWallFollowMisShadow"] = {EVADE_LINE,120.0f,1550.0f,1967.0f,0.0f,250,788};
    g_evadeSpells["QiyanaRWallFollowMisShadowCounterClockwise"] = {EVADE_LINE,120.0f,1550.0f,1967.0f,0.0f,250,788};
    g_evadeSpells["QiyanaRWallHitMis"] = {EVADE_LINE,120.0f,1050.0f,1200.0f,0.0f,250,875};
    g_evadeSpells["QiyanaWMis_Grass"] = {EVADE_LINE,40.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["QiyanaWMis_Rock"] = {EVADE_LINE,40.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["QiyanaWMis_Water"] = {EVADE_LINE,40.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["QuinnQ"] = {EVADE_LINE,60.0f,1050.0f,1022.4f,0.0f,250,1027};
    g_evadeSpells["QuinnR"] = {EVADE_LINE,200.0f,650.0f,1007.8f,0.0f,250,645};
    g_evadeSpells["QuinnRFinale"] = {EVADE_CIRCLE_START,700.0f,700.0f,933.3f,0.0f,250,750};
    g_evadeSpells["QuinnRReturnToQuinn"] = {EVADE_LINE,200.0f,650.0f,1007.8f,0.0f,250,645};
    g_evadeSpells["RakanE"] = {EVADE_LINE,50.0f,650.0f,185.7f,0.0f,0,3500};
    g_evadeSpells["RakanECast"] = {EVADE_LINE,50.0f,700.0f,200.0f,0.0f,0,3500};
    g_evadeSpells["RakanECastXaya"] = {EVADE_LINE,50.0f,1000.0f,285.7f,0.0f,0,3500};
    g_evadeSpells["RakanERecast"] = {EVADE_LINE,50.0f,650.0f,185.7f,0.0f,0,3500};
    g_evadeSpells["RakanQ"] = {EVADE_LINE,65.0f,850.0f,1050.7f,0.0f,250,809};
    g_evadeSpells["RakanQMis"] = {EVADE_LINE,65.0f,900.0f,1076.6f,0.0f,0,836};
    g_evadeSpells["RekSaiE"] = {EVADE_LINE,60.0f,225.0f,450.0f,0.0f,250,500};
    g_evadeSpells["RekSaiEBurrowed"] = {EVADE_LINE,60.0f,750.0f,915.8f,0.0f,0,819};
    g_evadeSpells["RekSaiQ"] = {EVADE_CONE,0.0f,350.0f,437.5f,17.5f,0,800};
    g_evadeSpells["RekSaiQBurrowed"] = {EVADE_LINE,60.0f,1625.0f,2149.5f,0.0f,125,756};
    g_evadeSpells["RekSaiQBurrowedMis"] = {EVADE_LINE,65.0f,1500.0f,1340.5f,0.0f,0,1119};
    g_evadeSpells["RekSaiR2"] = {EVADE_CIRCLE,150.0f,700.0f,777.8f,0.0f,0,900};
    g_evadeSpells["RekSaiW"] = {EVADE_CIRCLE_START,100.0f,1500.0f,2000.0f,0.0f,0,750};
    g_evadeSpells["RekSaiWBurrowed"] = {EVADE_CIRCLE_START,100.0f,1500.0f,2000.0f,0.0f,0,750};
    g_evadeSpells["RenataE"] = {EVADE_LINE,110.0f,800.0f,886.9f,0.0f,250,902};
    g_evadeSpells["RenataQ"] = {EVADE_LINE,70.0f,900.0f,926.9f,0.0f,250,971};
    g_evadeSpells["RenataR"] = {EVADE_LINE,325.0f,2000.0f,991.6f,0.0f,750,2017};
    g_evadeSpells["RenataRMissile"] = {EVADE_LINE,250.0f,2000.0f,583.6f,0.0f,0,3427};
    g_evadeSpells["RenataRMissileSides"] = {EVADE_LINE,250.0f,2000.0f,583.6f,0.0f,0,3427};
    g_evadeSpells["RenektonCleave"] = {EVADE_CIRCLE_START,325.0f,325.0f,433.3f,0.0f,0,750};
    g_evadeSpells["RenektonDice"] = {EVADE_LINE,50.0f,450.0f,128.6f,0.0f,0,3500};
    g_evadeSpells["RenektonSliceAndDice"] = {EVADE_LINE,50.0f,450.0f,128.6f,0.0f,0,3500};
    g_evadeSpells["RengarE"] = {EVADE_LINE,70.0f,1000.0f,983.3f,0.0f,250,1017};
    g_evadeSpells["RengarEEmp"] = {EVADE_LINE,70.0f,1000.0f,983.3f,0.0f,250,1017};
    g_evadeSpells["RengarEEmpMis"] = {EVADE_LINE,70.0f,1000.0f,983.3f,0.0f,250,1017};
    g_evadeSpells["RengarEMis"] = {EVADE_LINE,70.0f,1000.0f,983.3f,0.0f,250,1017};
    g_evadeSpells["RengarQ"] = {EVADE_CONE,0.0f,450.0f,562.5f,45.0f,0,800};
    g_evadeSpells["RengarQEmp"] = {EVADE_CONE,0.0f,450.0f,562.5f,45.0f,0,800};
    g_evadeSpells["RengarQEmpowered"] = {EVADE_CONE,0.0f,450.0f,562.5f,45.0f,200,800};
    g_evadeSpells["RengarW"] = {EVADE_CIRCLE_START,400.0f,450.0f,600.0f,0.0f,0,750};
    g_evadeSpells["RengarWEmp"] = {EVADE_CIRCLE_START,400.0f,450.0f,600.0f,0.0f,0,750};
    g_evadeSpells["RivenIzunaBlade"] = {EVADE_CONE,0.0f,25000.0f,31250.0f,11.0f,250,800};
    g_evadeSpells["RivenMartyr"] = {EVADE_CIRCLE_START,250.0f,650.0f,866.7f,0.0f,267,750};
    g_evadeSpells["RivenWindslashMissileCenter"] = {EVADE_LINE,100.0f,1100.0f,1059.7f,0.0f,400,1038};
    g_evadeSpells["RivenWindslashMissileLeft"] = {EVADE_LINE,100.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["RivenWindslashMissileRight"] = {EVADE_LINE,100.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["Ruby_Amumu_AuraofDespair"] = {EVADE_CIRCLE_START,300.0f,300.0f,400.0f,0.0f,0,750};
    g_evadeSpells["Ruby_Amumu_BandageToss"] = {EVADE_LINE,80.0f,1100.0f,1222.2f,0.0f,0,900};
    g_evadeSpells["Ruby_Amumu_CurseoftheSadMummy"] = {EVADE_CIRCLE_START,550.0f,1100.0f,1466.7f,0.0f,0,750};
    g_evadeSpells["Ruby_Amumu_SadMummyBandageToss"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_Amumu_Tantrum"] = {EVADE_CIRCLE_START,350.0f,350.0f,466.7f,0.0f,0,750};
    g_evadeSpells["Ruby_Blitzcrank_RocketGrab"] = {EVADE_LINE,70.0f,1079.0f,1137.0f,0.0f,0,949};
    g_evadeSpells["Ruby_Blitzcrank_RocketGrab_Knockback_Missile"] = {EVADE_LINE,130.0f,1100.0f,1222.2f,0.0f,0,900};
    g_evadeSpells["Ruby_Blitzcrank_RocketGrabMissile"] = {EVADE_LINE,70.0f,1200.0f,1179.9f,0.0f,0,1017};
    g_evadeSpells["Ruby_Blitzcrank_StaticField"] = {EVADE_CIRCLE_START,600.0f,600.0f,800.0f,0.0f,0,750};
    g_evadeSpells["Ruby_BrandAblaze"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["Ruby_BrandAblazeDetonateMarker"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["Ruby_BrandBlaze"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["Ruby_BrandBlazeMissile"] = {EVADE_LINE,60.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["Ruby_BrandFissure"] = {EVADE_CIRCLE,240.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["Ruby_BrandQ"] = {EVADE_LINE,80.0f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["Ruby_BrandQMissile"] = {EVADE_LINE,60.0f,1300.0f,1142.4f,0.0f,0,1138};
    g_evadeSpells["Ruby_BrandW"] = {EVADE_CIRCLE,240.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["Ruby_CaitlynE"] = {EVADE_LINE,70.0f,750.0f,915.8f,0.0f,0,819};
    g_evadeSpells["Ruby_CaitlynEMissile"] = {EVADE_LINE,70.0f,800.0f,941.2f,0.0f,0,850};
    g_evadeSpells["Ruby_CaitlynQ"] = {EVADE_LINE,60.0f,1600.0f,1573.3f,0.0f,0,1017};
    g_evadeSpells["Ruby_CaitlynQ2"] = {EVADE_LINE,150.0f,1600.0f,1485.6f,0.0f,0,1077};
    g_evadeSpells["Ruby_CaitlynRMissile"] = {EVADE_LINE,40.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_Chogath_FeralScream"] = {EVADE_CONE,0.0f,585.0f,731.2f,14.0f,0,800};
    g_evadeSpells["Ruby_Chogath_Rupture"] = {EVADE_CIRCLE,230.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["Ruby_Chogath_Skin07_Blackhole"] = {EVADE_LINE,25.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_Chogath_VorpalSpikesMissle"] = {EVADE_LINE,170.0f,650.0f,821.7f,0.0f,0,791};
    g_evadeSpells["Ruby_Chogath_VorpalSpikesMissle2"] = {EVADE_LINE,185.0f,675.0f,835.4f,0.0f,0,808};
    g_evadeSpells["Ruby_Chogath_VorpalSpikesMissle3"] = {EVADE_LINE,190.0f,700.0f,848.5f,0.0f,0,825};
    g_evadeSpells["Ruby_Chogath_VorpalSpikesMissle4"] = {EVADE_LINE,205.0f,725.0f,861.0f,0.0f,0,842};
    g_evadeSpells["Ruby_Chogath_VorpalSpikesMissle5"] = {EVADE_LINE,220.0f,750.0f,874.1f,0.0f,0,858};
    g_evadeSpells["Ruby_Chogath_VorpalSpikesMissle6"] = {EVADE_LINE,235.0f,775.0f,885.7f,0.0f,0,875};
    g_evadeSpells["Ruby_Chogath_VorpalSpikesMissle7"] = {EVADE_LINE,250.0f,800.0f,896.9f,0.0f,0,892};
    g_evadeSpells["Ruby_Corki_CarpetBomb"] = {EVADE_LINE,160.0f,600.0f,497.1f,0.0f,0,1207};
    g_evadeSpells["Ruby_Corki_DangerZone"] = {EVADE_CIRCLE,250.0f,900.0f,1000.0f,0.0f,0,900};
    g_evadeSpells["Ruby_Corki_GGSpray"] = {EVADE_CONE,0.0f,685.0f,856.2f,15.380000114440918f,0,800};
    g_evadeSpells["Ruby_Corki_GGun"] = {EVADE_CONE,0.0f,725.0f,906.2f,14.0f,0,800};
    g_evadeSpells["Ruby_Corki_MissileBarrage"] = {EVADE_LINE,40.0f,1225.0f,669.8f,0.0f,0,1829};
    g_evadeSpells["Ruby_Corki_MissileBarrageMissile"] = {EVADE_LINE,50.0f,1300.0f,1300.0f,0.0f,0,1000};
    g_evadeSpells["Ruby_Corki_MissileBarrageMissile2"] = {EVADE_LINE,75.0f,1550.0f,1377.8f,0.0f,0,1125};
    g_evadeSpells["Ruby_Corki_PhosphorusBomb"] = {EVADE_CIRCLE,250.0f,825.0f,916.7f,0.0f,0,900};
    g_evadeSpells["Ruby_Corki_PhosphorusBombMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_Corki_PhosphorusBombMissileMin"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_DianaQ"] = {EVADE_LINE,100.0f,1300.0f,963.0f,0.0f,0,1350};
    g_evadeSpells["Ruby_DianaQInnerMissile"] = {EVADE_LINE,70.0f,1300.0f,1341.6f,0.0f,0,969};
    g_evadeSpells["Ruby_DianaQOuterMissile"] = {EVADE_LINE,70.0f,1300.0f,1341.6f,0.0f,0,969};
    g_evadeSpells["Ruby_DianaR"] = {EVADE_CIRCLE_START,475.0f,475.0f,633.3f,0.0f,0,750};
    g_evadeSpells["Ruby_JinxEHit"] = {EVADE_LINE,120.0f,1200.0f,832.8f,0.0f,0,1441};
    g_evadeSpells["Ruby_JinxEHit_HexMonkey"] = {EVADE_LINE,120.0f,1200.0f,832.8f,0.0f,0,1441};
    g_evadeSpells["Ruby_JinxEMine"] = {EVADE_CIRCLE,100.0f,800.0f,888.9f,0.0f,0,900};
    g_evadeSpells["Ruby_JinxEMonkeyMine"] = {EVADE_CIRCLE,100.0f,800.0f,888.9f,0.0f,0,900};
    g_evadeSpells["Ruby_JinxR"] = {EVADE_LINE,140.0f,5000.0f,1428.6f,0.0f,600,3500};
    g_evadeSpells["Ruby_JinxW"] = {EVADE_LINE,60.0f,1450.0f,930.7f,0.0f,250,1558};
    g_evadeSpells["Ruby_JinxWMissile"] = {EVADE_LINE,60.0f,1500.0f,1863.4f,0.0f,600,805};
    g_evadeSpells["Ruby_LeeSinEOne"] = {EVADE_CIRCLE_START,450.0f,515.0f,686.7f,0.0f,0,750};
    g_evadeSpells["Ruby_LeeSinETwo"] = {EVADE_CIRCLE_START,575.0f,575.0f,766.7f,0.0f,0,750};
    g_evadeSpells["Ruby_LeeSinQOne"] = {EVADE_LINE,60.0f,1200.0f,1263.2f,0.0f,0,950};
    g_evadeSpells["Ruby_LeeSinQTwo"] = {EVADE_CIRCLE_START,650.0f,1200.0f,1600.0f,0.0f,0,750};
    g_evadeSpells["Ruby_LuxLightBinding"] = {EVADE_LINE,80.0f,1175.0f,884.1f,0.0f,0,1329};
    g_evadeSpells["Ruby_LuxLightBindingMis"] = {EVADE_LINE,70.0f,1300.0f,907.2f,0.0f,0,1433};
    g_evadeSpells["Ruby_LuxLightStrikeKugel"] = {EVADE_CIRCLE,295.0f,1100.0f,1222.2f,0.0f,0,900};
    g_evadeSpells["Ruby_LuxPrismaticWave"] = {EVADE_LINE,150.0f,1150.0f,879.2f,0.0f,0,1308};
    g_evadeSpells["Ruby_LuxPrismaticWaveMissile"] = {EVADE_LINE,110.0f,1175.0f,1329.2f,0.0f,0,884};
    g_evadeSpells["Ruby_LuxPrismaticWaveReturn"] = {EVADE_LINE,110.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_LuxPrismaticWaveReturnDead"] = {EVADE_LINE,110.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_LuxR"] = {EVADE_LINE,190.0f,3340.0f,2283.0f,0.0f,0,1463};
    g_evadeSpells["Ruby_LuxRMis"] = {EVADE_LINE,250.0f,3500.0f,2307.2f,0.0f,0,1517};
    g_evadeSpells["Ruby_MalzaharQMissile"] = {EVADE_LINE,85.0f,750.0f,915.8f,0.0f,600,819};
    g_evadeSpells["Ruby_MaokaiE"] = {EVADE_LINE,120.0f,1100.0f,1015.7f,0.0f,0,1083};
    g_evadeSpells["Ruby_MaokaiEMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_MaokaiEMissileBrush"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_MaokaiEMissileBrushTiny"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_MaokaiEMissileBrushTiny2"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_MaokaiQ"] = {EVADE_LINE,70.0f,600.0f,827.6f,0.0f,0,725};
    g_evadeSpells["Ruby_MaokaiQMissile"] = {EVADE_LINE,110.0f,1000.0f,1025.6f,0.0f,0,975};
    g_evadeSpells["Ruby_MaokaiRMis"] = {EVADE_LINE,120.0f,3000.0f,857.1f,0.0f,0,3500};
    g_evadeSpells["Ruby_MaokaiRMisExtra"] = {EVADE_LINE,150.0f,3000.0f,857.1f,0.0f,0,3500};
    g_evadeSpells["Ruby_MaokaiRSoundMis"] = {EVADE_LINE,120.0f,2200.0f,628.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_MaokaiW"] = {EVADE_LINE,120.0f,525.0f,750.0f,0.0f,0,700};
    g_evadeSpells["Ruby_NamiQ"] = {EVADE_CIRCLE,200.0f,850.0f,944.4f,0.0f,250,900};
    g_evadeSpells["Ruby_NamiQDebuff"] = {EVADE_CIRCLE,155.0f,850.0f,944.4f,0.0f,250,900};
    g_evadeSpells["Ruby_NamiQMissile"] = {EVADE_LINE,120.0f,1625.0f,1625.0f,0.0f,0,1000};
    g_evadeSpells["Ruby_NamiQMissileSmall"] = {EVADE_LINE,120.0f,1625.0f,1625.0f,0.0f,0,1000};
    g_evadeSpells["Ruby_NamiR"] = {EVADE_LINE,325.0f,400.0f,585.7f,0.0f,500,683};
    g_evadeSpells["Ruby_NamiRMissile"] = {EVADE_LINE,250.0f,2750.0f,785.7f,0.0f,0,3500};
    g_evadeSpells["Ruby_NamiRMissileBig"] = {EVADE_LINE,500.0f,2750.0f,785.7f,0.0f,0,3500};
    g_evadeSpells["Ruby_NautilusAnchorDrag"] = {EVADE_LINE,80.0f,1150.0f,879.2f,0.0f,0,1308};
    g_evadeSpells["Ruby_NautilusAnchorDragMissile"] = {EVADE_LINE,200.0f,925.0f,1139.2f,0.0f,250,812};
    g_evadeSpells["Ruby_NautilusGrandLineStart"] = {EVADE_LINE,90.0f,825.0f,878.6f,0.0f,400,939};
    g_evadeSpells["Ruby_NautilusPiercingGaze"] = {EVADE_CONE,0.0f,1400.0f,1750.0f,10.0f,0,800};
    g_evadeSpells["Ruby_NautilusSplashZone"] = {EVADE_CIRCLE_START,600.0f,1000.0f,1333.3f,0.0f,250,750};
    g_evadeSpells["Ruby_NautilusSplashZoneSplash"] = {EVADE_LINE,85.0f,1000.0f,425.5f,0.0f,250,2350};
    g_evadeSpells["Ruby_NautilusWDoT"] = {EVADE_CONE,0.0f,1400.0f,1750.0f,10.0f,0,800};
    g_evadeSpells["Ruby_NautilusWSizeIncrease"] = {EVADE_CONE,0.0f,1400.0f,1750.0f,10.0f,0,800};
    g_evadeSpells["Ruby_SorakaE"] = {EVADE_CIRCLE,250.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["Ruby_SorakaQ"] = {EVADE_CIRCLE,230.0f,800.0f,888.9f,0.0f,0,900};
    g_evadeSpells["Ruby_SorakaQAdditionalMissile"] = {EVADE_LINE,25.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_SorakaQMissile"] = {EVADE_LINE,25.0f,1500.0f,875.1f,0.0f,0,1714};
    g_evadeSpells["Ruby_SorakaRCastTime"] = {EVADE_CIRCLE,400.0f,1000.0f,1111.1f,0.0f,250,900};
    g_evadeSpells["Ruby_TalonR"] = {EVADE_CIRCLE_START,650.0f,650.0f,866.7f,0.0f,0,750};
    g_evadeSpells["Ruby_TalonRakeMissileTwoDead"] = {EVADE_LINE,75.0f,5000.0f,1637.7f,0.0f,0,3053};
    g_evadeSpells["Ruby_TalonRMisOne"] = {EVADE_LINE,140.0f,800.0f,1171.3f,0.0f,0,683};
    g_evadeSpells["Ruby_TalonRMisTwo"] = {EVADE_LINE,140.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_TalonShadowAssaultMisOneHalf"] = {EVADE_LINE,125.0f,1200.0f,1376.1f,0.0f,0,872};
    g_evadeSpells["Ruby_TalonW"] = {EVADE_CONE,0.0f,700.0f,875.0f,13.0f,0,800};
    g_evadeSpells["Ruby_TalonWMissileOne"] = {EVADE_LINE,75.0f,1200.0f,1445.8f,0.0f,0,830};
    g_evadeSpells["Ruby_TalonWMissileTwo"] = {EVADE_LINE,75.0f,5000.0f,2478.9f,0.0f,0,2017};
    g_evadeSpells["Ruby_TristanaW"] = {EVADE_CIRCLE,270.0f,900.0f,1000.0f,0.0f,0,900};
    g_evadeSpells["Ruby_UrgotE"] = {EVADE_LINE,100.0f,475.0f,712.1f,0.0f,450,667};
    g_evadeSpells["Ruby_UrgotEDash"] = {EVADE_LINE,100.0f,450.0f,692.3f,0.0f,0,650};
    g_evadeSpells["Ruby_UrgotR"] = {EVADE_LINE,80.0f,2500.0f,2210.4f,0.0f,500,1131};
    g_evadeSpells["Ruby_UrgotRRecastMissile"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Ruby_UrgotW"] = {EVADE_LINE,40.0f,450.0f,713.2f,0.0f,0,631};
    g_evadeSpells["Ruby_WukongSpinToWin"] = {EVADE_LINE,160.0f,660.0f,510.4f,0.0f,0,1293};
    g_evadeSpells["Ruby_WukongSpinToWin_MinionBuff"] = {EVADE_LINE,160.0f,660.0f,510.4f,0.0f,0,1293};
    g_evadeSpells["Ruby_XerathArcaneBarrage2"] = {EVADE_CIRCLE,150.0f,1500.0f,1666.7f,0.0f,250,900};
    g_evadeSpells["Ruby_XerathLocusOfPower2Toggle"] = {EVADE_LINE,150.0f,1600.0f,457.1f,0.0f,0,3500};
    g_evadeSpells["Ruby_XerathMageSpear"] = {EVADE_LINE,70.0f,1050.0f,1043.7f,0.0f,250,1006};
    g_evadeSpells["Ruby_XerathMageSpearMissile"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,500,3500};
    g_evadeSpells["RumbleCarpetBomb"] = {EVADE_LINE,90.0f,1700.0f,962.1f,0.0f,0,1767};
    g_evadeSpells["RumbleCarpetBombMissile"] = {EVADE_LINE,200.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["RumbleFlameThrower"] = {EVADE_CONE,0.0f,625.0f,781.2f,16.0f,0,800};
    g_evadeSpells["RumbleFlameThrowerSpray"] = {EVADE_CONE,0.0f,600.0f,750.0f,16.0f,0,800};
    g_evadeSpells["RumbleFlameThrowerSpraySuper"] = {EVADE_CONE,0.0f,600.0f,750.0f,16.0f,0,800};
    g_evadeSpells["RumbleGrenade"] = {EVADE_LINE,90.0f,850.0f,803.4f,0.0f,250,1058};
    g_evadeSpells["RumbleGrenadeMissile"] = {EVADE_LINE,60.0f,950.0f,1151.5f,0.0f,0,825};
    g_evadeSpells["RumbleGrenadeMissileDangerZone"] = {EVADE_LINE,60.0f,950.0f,1151.5f,0.0f,0,825};
    g_evadeSpells["RyzeQ"] = {EVADE_LINE,55.0f,1000.0f,1066.1f,0.0f,250,938};
    g_evadeSpells["RyzeQMissile"] = {EVADE_LINE,55.0f,5000.0f,1428.6f,0.0f,250,3500};
    g_evadeSpells["RyzeQWrapper"] = {EVADE_LINE,55.0f,1000.0f,1066.1f,0.0f,250,938};
    g_evadeSpells["RyzeR2"] = {EVADE_LINE,55.0f,2500.0f,2564.1f,0.0f,750,975};
    g_evadeSpells["SamiraPMissile"] = {EVADE_LINE,110.0f,850.0f,1299.7f,0.0f,0,654};
    g_evadeSpells["SamiraPMissileOffhand"] = {EVADE_LINE,110.0f,850.0f,1299.7f,0.0f,0,654};
    g_evadeSpells["SamiraQGun"] = {EVADE_LINE,60.0f,950.0f,1328.7f,0.0f,0,715};
    g_evadeSpells["SamiraRMissile"] = {EVADE_LINE,50.0f,950.0f,1378.8f,0.0f,0,689};
    g_evadeSpells["SejuaniRMissile"] = {EVADE_LINE,120.0f,1300.0f,1118.8f,0.0f,0,1162};
    g_evadeSpells["SejuaniW"] = {EVADE_CONE,0.0f,600.0f,750.0f,15.0f,0,800};
    g_evadeSpells["SennaR"] = {EVADE_LINE,180.0f,5000.0f,3125.0f,0.0f,0,1600};
    g_evadeSpells["SennaRAlly"] = {EVADE_LINE,1200.0f,5000.0f,3125.0f,0.0f,500,1600};
    g_evadeSpells["SennaRWarningMis"] = {EVADE_LINE,1200.0f,5000.0f,3125.0f,0.0f,500,1600};
    g_evadeSpells["SennaW"] = {EVADE_LINE,70.0f,1300.0f,907.2f,0.0f,250,1433};
    g_evadeSpells["SeraphineEMissile"] = {EVADE_LINE,80.0f,1300.0f,907.2f,0.0f,0,1433};
    g_evadeSpells["SeraphineQSecondaryMissile"] = {EVADE_LINE,50.0f,350.0f,375.1f,0.0f,0,933};
    g_evadeSpells["SeraphineR"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,500,3500};
    g_evadeSpells["ShacoWPlacementMissile"] = {EVADE_LINE,60.0f,1200.0f,1600.0f,0.0f,0,750};
    g_evadeSpells["ShenQMissile"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ShyvanaE"] = {EVADE_LINE,75.0f,800.0f,906.0f,0.0f,250,883};
    g_evadeSpells["ShyvanaEDragon"] = {EVADE_LINE,90.0f,1000.0f,1025.6f,0.0f,250,975};
    g_evadeSpells["MegaAdhesive"] = {EVADE_CIRCLE,265.0f,1000.0f,1111.1f,0.0f,0,900};
    g_evadeSpells["SionEMissile"] = {EVADE_LINE,80.0f,800.0f,1007.6f,0.0f,0,794};
    g_evadeSpells["SionQDamage"] = {EVADE_CONE,0.0f,1550.0f,1937.5f,4.0f,0,800};
    g_evadeSpells["SionQIndicatorMissile"] = {EVADE_LINE,80.0f,1050.0f,300.0f,0.0f,0,3500};
    g_evadeSpells["SionQIndicatorMissile2"] = {EVADE_LINE,80.0f,1050.0f,300.0f,0.0f,0,3500};
    g_evadeSpells["SionQMissile"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["SionQShapeCut"] = {EVADE_CONE,0.0f,720.0f,900.0f,4.0f,0,800};
    g_evadeSpells["SionR"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["SionRRun"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["SivirQ"] = {EVADE_LINE,90.0f,1200.0f,968.5f,0.0f,250,1239};
    g_evadeSpells["SivirQMissile"] = {EVADE_LINE,90.0f,1250.0f,1031.4f,0.0f,250,1212};
    g_evadeSpells["SivirQMissileReturn"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["SivirQMissileReturnDead"] = {EVADE_LINE,100.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["SkarnerQRockThrow"] = {EVADE_LINE,90.0f,1050.0f,1043.7f,0.0f,250,1006};
    g_evadeSpells["SkarnerW"] = {EVADE_LINE,200.0f,650.0f,873.7f,0.0f,0,744};
    g_evadeSpells["SmolderE"] = {EVADE_LINE,120.0f,700.0f,750.3f,0.0f,0,933};
    g_evadeSpells["SmolderQ"] = {EVADE_CONE,0.0f,550.0f,687.5f,30.0f,250,800};
    g_evadeSpells["SmolderQ2BlowbackMissileA"] = {EVADE_LINE,25.0f,2500.0f,799.2f,0.0f,0,3128};
    g_evadeSpells["SmolderQ2BlowbackMissileB"] = {EVADE_LINE,25.0f,2500.0f,799.2f,0.0f,0,3128};
    g_evadeSpells["SmolderQ2BlowbackMissileC"] = {EVADE_LINE,25.0f,2500.0f,799.2f,0.0f,0,3128};
    g_evadeSpells["SmolderRMomMissile"] = {EVADE_LINE,300.0f,4250.0f,1491.2f,0.0f,0,2850};
    g_evadeSpells["SmolderRMomMissileSweetspot"] = {EVADE_LINE,125.0f,4250.0f,1491.2f,0.0f,0,2850};
    g_evadeSpells["SonaR"] = {EVADE_LINE,140.0f,1000.0f,1303.8f,0.0f,0,767};
    g_evadeSpells["SorakaE"] = {EVADE_CIRCLE,260.0f,875.0f,972.2f,0.0f,250,900};
    g_evadeSpells["SorakaQ"] = {EVADE_CIRCLE,230.0f,800.0f,888.9f,0.0f,0,900};
    g_evadeSpells["SorakaQMissile"] = {EVADE_LINE,25.0f,1500.0f,875.1f,0.0f,0,1714};
    g_evadeSpells["SorakaRCastTime"] = {EVADE_CIRCLE,400.0f,1000.0f,1111.1f,0.0f,250,900};
    g_evadeSpells["Strawberry_AuroraQReturnMissile"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,350,3500};
    g_evadeSpells["Strawberry_AuroraRMissile"] = {EVADE_LINE,100.0f,700.0f,518.5f,0.0f,0,1350};
    g_evadeSpells["Strawberry_Briar_AOEDash_Missile"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,1000,3500};
    g_evadeSpells["Strawberry_BriarQ"] = {EVADE_CONE,0.0f,1500.0f,1875.0f,45.0f,500,800};
    g_evadeSpells["Strawberry_BriarR"] = {EVADE_LINE,160.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Strawberry_JinxR"] = {EVADE_LINE,140.0f,2500.0f,1562.5f,0.0f,500,1600};
    g_evadeSpells["Strawberry_LeonaSolarBarrier"] = {EVADE_CIRCLE_START,500.0f,500.0f,666.7f,0.0f,0,750};
    g_evadeSpells["Strawberry_LeonaSolarFlare"] = {EVADE_CIRCLE,120.0f,2000.0f,2222.2f,0.0f,250,900};
    g_evadeSpells["Strawberry_RivenMartyr"] = {EVADE_CIRCLE_START,250.0f,650.0f,866.7f,0.0f,267,750};
    g_evadeSpells["Strawberry_RivenWindslashMissileCenter"] = {EVADE_LINE,250.0f,1300.0f,1212.7f,0.0f,400,1072};
    g_evadeSpells["Strawberry_SeraphineEMissile"] = {EVADE_LINE,100.0f,1000.0f,562.1f,0.0f,0,1779};
    g_evadeSpells["Strawberry_SeraphineR"] = {EVADE_LINE,50.0f,5000.0f,1428.6f,0.0f,1000,3500};
    g_evadeSpells["Strawberry_XayahAutoMis"] = {EVADE_LINE,90.0f,850.0f,1512.5f,0.0f,0,562};
    g_evadeSpells["Strawberry_XayahAutoMisEvolve"] = {EVADE_LINE,90.0f,850.0f,926.9f,0.0f,0,917};
    g_evadeSpells["Strawberry_XayahAutoMisEvolveBounce"] = {EVADE_LINE,60.0f,1150.0f,879.2f,0.0f,0,1308};
    g_evadeSpells["Strawberry_XayahEMissile"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["Strawberry_XayahRMissile"] = {EVADE_CONE,0.0f,700.0f,875.0f,0.0f,0,800};
    g_evadeSpells["Strawberry_YasuoAutoEvoMis"] = {EVADE_LINE,280.0f,800.0f,786.6f,0.0f,0,1017};
    g_evadeSpells["Strawberry_YasuoAutoMis"] = {EVADE_LINE,90.0f,1100.0f,868.2f,0.0f,0,1267};
    g_evadeSpells["Strawberry_YasuoQ"] = {EVADE_LINE,25.0f,520.0f,1040.0f,0.0f,350,500};
    g_evadeSpells["Strawberry_YasuoQ1"] = {EVADE_LINE,25.0f,520.0f,1040.0f,0.0f,350,500};
    g_evadeSpells["Strawberry_YasuoQ1Wrapper"] = {EVADE_LINE,55.0f,475.0f,712.1f,0.0f,0,667};
    g_evadeSpells["Strawberry_YasuoQ2Wrapper"] = {EVADE_LINE,55.0f,475.0f,712.1f,0.0f,0,667};
    g_evadeSpells["Strawberry_YasuoQ3Wrapper"] = {EVADE_LINE,90.0f,1000.0f,983.3f,0.0f,0,1017};
    g_evadeSpells["Strawberry_YasuoQE1"] = {EVADE_LINE,25.0f,260.0f,520.0f,0.0f,0,500};
    g_evadeSpells["Strawberry_YasuoQE2"] = {EVADE_LINE,25.0f,260.0f,520.0f,0.0f,0,500};
    g_evadeSpells["Strawberry_YasuoQE3"] = {EVADE_LINE,25.0f,260.0f,520.0f,0.0f,0,500};
    g_evadeSpells["Strawberry_YasuoQW"] = {EVADE_LINE,55.0f,3250.0f,1291.2f,0.0f,0,2517};
    g_evadeSpells["Strawberry_YasuoWMovingWallEnemy"] = {EVADE_CONE,0.0f,650.0f,812.5f,11.15999984741211f,13,800};
    g_evadeSpells["SwainBirdMissileSelfOnly"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["SwainE"] = {EVADE_LINE,85.0f,850.0f,675.1f,0.0f,0,1259};
    g_evadeSpells["SwainEReturnMissile"] = {EVADE_LINE,90.0f,1600.0f,530.3f,0.0f,50,3017};
    g_evadeSpells["SwainQ"] = {EVADE_CONE,0.0f,750.0f,937.5f,10.0f,0,800};
    g_evadeSpells["SwainRMegaNovaMissile"] = {EVADE_LINE,60.0f,675.0f,1173.9f,0.0f,0,575};
    g_evadeSpells["SylasE2"] = {EVADE_LINE,60.0f,850.0f,964.8f,0.0f,0,881};
    g_evadeSpells["SylasQ"] = {EVADE_LINE,70.0f,850.0f,1034.1f,0.0f,400,822};
    g_evadeSpells["SylasR"] = {EVADE_LINE,100.0f,950.0f,1214.8f,0.0f,250,782};
    g_evadeSpells["SylasRCosmeticMissile"] = {EVADE_LINE,100.0f,1500.0f,1693.0f,0.0f,250,886};
    g_evadeSpells["SyndraE"] = {EVADE_CONE,0.0f,675.0f,843.8f,15.0f,0,800};
    g_evadeSpells["SyndraE5"] = {EVADE_CONE,0.0f,675.0f,843.8f,0.0f,0,800};
    g_evadeSpells["SyndraE5Sound"] = {EVADE_LINE,60.0f,650.0f,830.1f,0.0f,0,783};
    g_evadeSpells["SyndraEDebuff"] = {EVADE_LINE,60.0f,650.0f,963.0f,0.0f,0,675};
    g_evadeSpells["SyndraEMissile"] = {EVADE_LINE,60.0f,700.0f,1111.1f,0.0f,0,630};
    g_evadeSpells["SyndraEMissile2"] = {EVADE_LINE,60.0f,700.0f,888.3f,0.0f,0,788};
    g_evadeSpells["SyndraEMissile3"] = {EVADE_LINE,60.0f,700.0f,709.9f,0.0f,0,986};
    g_evadeSpells["SyndraESound"] = {EVADE_LINE,300.0f,650.0f,830.1f,0.0f,0,783};
    g_evadeSpells["SyndraESphereMissile"] = {EVADE_LINE,100.0f,3000.0f,1621.6f,0.0f,0,1850};
    g_evadeSpells["SyndraQSpell"] = {EVADE_LINE,25.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["SyndraRBounce"] = {EVADE_LINE,120.0f,1100.0f,314.3f,0.0f,0,3500};
    g_evadeSpells["SyndraRBounce5"] = {EVADE_LINE,120.0f,1100.0f,314.3f,0.0f,0,3500};
    g_evadeSpells["SyndraRSpread1"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["SyndraRSpread15"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["SyndraRSpread2"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["SyndraRSpread25"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["SyndraRSpread3"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["SyndraRSpread35"] = {EVADE_LINE,120.0f,1100.0f,637.7f,0.0f,0,1725};
    g_evadeSpells["SyndraRTrigger"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["SyndraWCast"] = {EVADE_CIRCLE,195.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["SyndraWCDTimer"] = {EVADE_LINE,120.0f,1100.0f,314.3f,0.0f,0,3500};
    g_evadeSpells["SyndraWDebuff"] = {EVADE_CIRCLE,210.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["TahmKenchQ"] = {EVADE_LINE,70.0f,900.0f,1341.3f,0.0f,250,671};
    g_evadeSpells["TaliyahQ"] = {EVADE_LINE,80.0f,1000.0f,1592.4f,0.0f,250,628};
    g_evadeSpells["TaliyahQMis"] = {EVADE_LINE,100.0f,4000.0f,2737.9f,0.0f,250,1461};
    g_evadeSpells["TaliyahQMisBig"] = {EVADE_LINE,100.0f,4000.0f,2737.9f,0.0f,250,1461};
    g_evadeSpells["TaliyahR"] = {EVADE_LINE,120.0f,2500.0f,1562.5f,0.0f,10,1600};
    g_evadeSpells["TaliyahRBlowUpSoundMis"] = {EVADE_LINE,120.0f,3000.0f,1418.4f,0.0f,10,2115};
    g_evadeSpells["TaliyahRMis"] = {EVADE_LINE,120.0f,3000.0f,1418.4f,0.0f,10,2115};
    g_evadeSpells["TaliyahRNoClick"] = {EVADE_LINE,120.0f,2000.0f,1481.5f,0.0f,10,1350};
    g_evadeSpells["TaliyahRPreMis"] = {EVADE_LINE,115.0f,1200.0f,994.2f,0.0f,250,1207};
    g_evadeSpells["WASD_R_Reposition"] = {EVADE_LINE,25.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["TalonR"] = {EVADE_CIRCLE_START,650.0f,650.0f,866.7f,0.0f,0,750};
    g_evadeSpells["TalonRakeMissileTwoDead"] = {EVADE_LINE,75.0f,5000.0f,1637.7f,0.0f,0,3053};
    g_evadeSpells["TalonRMisOne"] = {EVADE_LINE,140.0f,800.0f,1171.3f,0.0f,0,683};
    g_evadeSpells["TalonRMisTwo"] = {EVADE_LINE,140.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["TalonShadowAssaultMisOneHalf"] = {EVADE_LINE,125.0f,1200.0f,1376.1f,0.0f,0,872};
    g_evadeSpells["TalonW"] = {EVADE_CONE,0.0f,700.0f,875.0f,13.0f,0,800};
    g_evadeSpells["TalonWMissileOne"] = {EVADE_LINE,75.0f,1200.0f,1445.8f,0.0f,0,830};
    g_evadeSpells["TalonWMissileTwo"] = {EVADE_LINE,75.0f,5000.0f,2478.9f,0.0f,0,2017};
    g_evadeSpells["TaricE"] = {EVADE_LINE,100.0f,610.0f,872.7f,0.0f,0,699};
    g_evadeSpells["TaricQ"] = {EVADE_CIRCLE_START,325.0f,325.0f,433.3f,0.0f,0,750};
    g_evadeSpells["TaricR"] = {EVADE_CIRCLE_START,400.0f,400.0f,533.3f,0.0f,0,750};
    g_evadeSpells["TaricW"] = {EVADE_LINE,60.0f,800.0f,974.4f,0.0f,0,821};
    g_evadeSpells["TeemoR"] = {EVADE_LINE,120.0f,600.0f,631.6f,0.0f,0,950};
    g_evadeSpells["TeemoRTrap"] = {EVADE_LINE,120.0f,2000.0f,851.1f,0.0f,0,2350};
    g_evadeSpells["ThreshEMissile1"] = {EVADE_LINE,110.0f,1075.0f,1210.6f,0.0f,0,888};
    g_evadeSpells["ThreshQ"] = {EVADE_LINE,60.0f,1075.0f,862.8f,0.0f,500,1246};
    g_evadeSpells["ThreshQInternal"] = {EVADE_LINE,60.0f,1075.0f,862.8f,0.0f,500,1246};
    g_evadeSpells["ThreshQLeap"] = {EVADE_LINE,60.0f,975.0f,839.1f,0.0f,250,1162};
    g_evadeSpells["ThreshQMissile"] = {EVADE_LINE,70.0f,1100.0f,1184.1f,0.0f,0,929};
    g_evadeSpells["ThreshQPullMissile"] = {EVADE_LINE,85.0f,2000.0f,851.1f,0.0f,0,2350};
    g_evadeSpells["ThreshRPenta"] = {EVADE_CIRCLE_START,400.0f,850.0f,1133.3f,0.0f,450,750};
    g_evadeSpells["ThreshW"] = {EVADE_CIRCLE,350.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["ThreshWLanternOut"] = {EVADE_LINE,120.0f,1500.0f,674.2f,0.0f,0,2225};
    g_evadeSpells["ThreshWLanternReturn"] = {EVADE_LINE,120.0f,1100.0f,1015.7f,0.0f,0,1083};
    g_evadeSpells["TristanaW"] = {EVADE_CIRCLE,270.0f,900.0f,1000.0f,0.0f,0,900};
    g_evadeSpells["TrundleCircle"] = {EVADE_CIRCLE,125.0f,1000.0f,1111.1f,0.0f,0,900};
    g_evadeSpells["trundledesecrate"] = {EVADE_CIRCLE,750.0f,750.0f,833.3f,0.0f,0,900};
    g_evadeSpells["slash"] = {EVADE_LINE,92.5f,660.0f,510.4f,0.0f,0,1293};
    g_evadeSpells["TryndamereE"] = {EVADE_LINE,160.0f,650.0f,508.2f,0.0f,0,1279};
    g_evadeSpells["SealFateMissile"] = {EVADE_LINE,40.0f,1450.0f,805.6f,0.0f,0,1800};
    g_evadeSpells["TwitchVenomCask"] = {EVADE_CIRCLE,275.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["TwitchVenomCaskDebuff"] = {EVADE_CIRCLE,250.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["TwitchVenomCaskMissile"] = {EVADE_LINE,120.0f,1500.0f,1055.6f,0.0f,0,1421};
    g_evadeSpells["UrgotE"] = {EVADE_LINE,100.0f,475.0f,712.1f,0.0f,450,667};
    g_evadeSpells["UrgotEDash"] = {EVADE_LINE,100.0f,450.0f,692.3f,0.0f,0,650};
    g_evadeSpells["UrgotR"] = {EVADE_LINE,80.0f,2500.0f,2210.4f,0.0f,500,1131};
    g_evadeSpells["UrgotRRecastMissile"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["UrgotW"] = {EVADE_LINE,40.0f,450.0f,713.2f,0.0f,0,631};
    g_evadeSpells["VarusEMissile"] = {EVADE_LINE,120.0f,1500.0f,1111.1f,0.0f,0,1350};
    g_evadeSpells["VarusQ"] = {EVADE_LINE,75.0f,925.0f,956.6f,0.0f,0,967};
    g_evadeSpells["VarusQLaunch"] = {EVADE_LINE,120.0f,1200.0f,888.9f,0.0f,0,1350};
    g_evadeSpells["VarusQMissile"] = {EVADE_LINE,70.0f,2000.0f,1425.5f,0.0f,0,1403};
    g_evadeSpells["VarusR"] = {EVADE_LINE,450.0f,1300.0f,907.2f,0.0f,242,1433};
    g_evadeSpells["VarusRMissile"] = {EVADE_LINE,120.0f,1250.0f,1261.4f,0.0f,251,991};
    g_evadeSpells["VarusRMissileEnd"] = {EVADE_LINE,80.0f,1250.0f,1282.1f,0.0f,251,975};
    g_evadeSpells["VarusRSecondary"] = {EVADE_CIRCLE_START,625.0f,625.0f,833.3f,0.0f,0,750};
    g_evadeSpells["VarusW"] = {EVADE_LINE,50.0f,750.0f,769.2f,0.0f,0,975};
    g_evadeSpells["VarusWDebuff"] = {EVADE_LINE,50.0f,750.0f,769.2f,0.0f,0,975};
    g_evadeSpells["VarusWDetonate"] = {EVADE_LINE,50.0f,750.0f,769.2f,0.0f,0,975};
    g_evadeSpells["VayneCondemn"] = {EVADE_LINE,80.0f,550.0f,680.7f,0.0f,0,808};
    g_evadeSpells["VayneCondemnMissile"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["VayneSilveredBolts"] = {EVADE_LINE,50.0f,750.0f,769.2f,0.0f,0,975};
    g_evadeSpells["VayneSilveredDebuff"] = {EVADE_LINE,50.0f,750.0f,769.2f,0.0f,0,975};
    g_evadeSpells["VayneTumble"] = {EVADE_LINE,50.0f,300.0f,85.7f,0.0f,0,3500};
    g_evadeSpells["VeigarBalefulStrike"] = {EVADE_LINE,70.0f,1000.0f,845.3f,0.0f,0,1183};
    g_evadeSpells["VeigarBalefulStrikeMis"] = {EVADE_LINE,70.0f,1050.0f,1269.6f,0.0f,251,827};
    g_evadeSpells["VeigarDarkMatter"] = {EVADE_CIRCLE,225.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["VeigarDarkMatterCastLockout"] = {EVADE_CIRCLE,225.0f,950.0f,1055.6f,0.0f,0,900};
    g_evadeSpells["VeigarEventHorizon"] = {EVADE_CIRCLE,390.0f,700.0f,777.8f,0.0f,0,900};
    g_evadeSpells["VelkozE"] = {EVADE_CIRCLE,225.0f,800.0f,888.9f,0.0f,250,900};
    g_evadeSpells["VelkozEMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["VelkozQ"] = {EVADE_LINE,60.0f,1050.0f,857.1f,0.0f,251,1225};
    g_evadeSpells["VelkozQMissile"] = {EVADE_LINE,50.0f,1100.0f,919.7f,0.0f,251,1196};
    g_evadeSpells["VelkozQMissileSplit"] = {EVADE_LINE,45.0f,1100.0f,1258.6f,0.0f,0,874};
    g_evadeSpells["VelkozW"] = {EVADE_LINE,87.5f,1050.0f,857.1f,0.0f,0,1225};
    g_evadeSpells["VelkozWMissile"] = {EVADE_LINE,87.5f,1200.0f,1136.4f,0.0f,251,1056};
    g_evadeSpells["VexE"] = {EVADE_LINE,80.0f,800.0f,829.0f,0.0f,250,965};
    g_evadeSpells["VexQ"] = {EVADE_LINE,180.0f,1200.0f,581.4f,0.0f,0,2064};
    g_evadeSpells["VexR"] = {EVADE_LINE,130.0f,2000.0f,1339.6f,0.0f,0,1493};
    g_evadeSpells["ViEArea"] = {EVADE_CONE,0.0f,535.0f,668.8f,17.5f,0,800};
    g_evadeSpells["ViEFx"] = {EVADE_LINE,25.0f,800.0f,1066.7f,0.0f,0,750};
    g_evadeSpells["ViQ"] = {EVADE_LINE,55.0f,250.0f,483.6f,0.0f,0,517};
    g_evadeSpells["ViQLaunch"] = {EVADE_LINE,120.0f,1200.0f,888.9f,0.0f,0,1350};
    g_evadeSpells["ViQMissile"] = {EVADE_LINE,90.0f,2000.0f,1188.4f,0.0f,484,1683};
    g_evadeSpells["ViRMissile"] = {EVADE_LINE,85.0f,2000.0f,1481.5f,0.0f,0,1350};
    g_evadeSpells["ViW"] = {EVADE_LINE,50.0f,750.0f,769.2f,0.0f,0,975};
    g_evadeSpells["ViegoE"] = {EVADE_LINE,70.0f,750.0f,769.2f,0.0f,0,975};
    g_evadeSpells["ViegoEMissile"] = {EVADE_LINE,120.0f,775.0f,929.3f,0.0f,0,834};
    g_evadeSpells["ViegoEWallFollowMis"] = {EVADE_LINE,130.0f,1550.0f,944.0f,0.0f,250,1642};
    g_evadeSpells["ViegoEWallFollowMisCounterClockwise"] = {EVADE_LINE,130.0f,1550.0f,944.0f,0.0f,250,1642};
    g_evadeSpells["ViegoEWallFollowMisCounterClockwiseHead"] = {EVADE_LINE,130.0f,1550.0f,944.0f,0.0f,250,1642};
    g_evadeSpells["ViegoEWallFollowMisHead"] = {EVADE_LINE,130.0f,1550.0f,944.0f,0.0f,250,1642};
    g_evadeSpells["ViegoQ"] = {EVADE_LINE,70.0f,600.0f,171.4f,0.0f,250,3500};
    g_evadeSpells["ViegoWMis"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ViktorE"] = {EVADE_LINE,90.0f,550.0f,629.3f,0.0f,0,874};
    g_evadeSpells["ViktorEAugMissile"] = {EVADE_LINE,90.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ViktorEMissile"] = {EVADE_LINE,90.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ViktorEMissile2"] = {EVADE_LINE,90.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ViktorR"] = {EVADE_CIRCLE,300.0f,700.0f,777.8f,0.0f,250,900};
    g_evadeSpells["ViktorW"] = {EVADE_CIRCLE,300.0f,800.0f,888.9f,0.0f,250,900};
    g_evadeSpells["VladimirE"] = {EVADE_CIRCLE_START,550.0f,550.0f,733.3f,0.0f,0,750};
    g_evadeSpells["VladimirEMissile"] = {EVADE_LINE,60.0f,1200.0f,1846.2f,0.0f,0,650};
    g_evadeSpells["VladimirHemoplague"] = {EVADE_LINE,150.0f,625.0f,717.6f,0.0f,0,871};
    g_evadeSpells["VladimirHemoplagueMissile"] = {EVADE_LINE,120.0f,1100.0f,1144.6f,0.0f,0,961};
    g_evadeSpells["VladimirSanguinePool"] = {EVADE_LINE,120.0f,1050.0f,1043.7f,0.0f,0,1006};
    g_evadeSpells["VladimirTidesofBlood"] = {EVADE_LINE,120.0f,350.0f,524.0f,0.0f,0,668};
    g_evadeSpells["WarwickE"] = {EVADE_LINE,90.0f,375.0f,642.1f,0.0f,0,584};
    g_evadeSpells["WarwickPSpellInfo"] = {EVADE_LINE,55.0f,350.0f,600.3f,0.0f,0,583};
    g_evadeSpells["WarwickQ"] = {EVADE_LINE,55.0f,365.0f,615.5f,0.0f,0,593};
    g_evadeSpells["XayahEMissile"] = {EVADE_LINE,80.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["XayahQ"] = {EVADE_LINE,50.0f,1100.0f,572.6f,0.0f,250,1921};
    g_evadeSpells["XayahQMissile1"] = {EVADE_LINE,50.0f,1100.0f,354.8f,0.0f,250,3100};
    g_evadeSpells["XayahQMissile2"] = {EVADE_LINE,50.0f,1100.0f,354.8f,0.0f,250,3100};
    g_evadeSpells["XayahRMissile"] = {EVADE_CONE,0.0f,1100.0f,1375.0f,0.0f,0,800};
    g_evadeSpells["XerathArcaneBarrage2"] = {EVADE_CIRCLE,100.0f,1000.0f,1111.1f,0.0f,250,900};
    g_evadeSpells["XerathLocusOfPower2Toggle"] = {EVADE_LINE,150.0f,1600.0f,457.1f,0.0f,0,3500};
    g_evadeSpells["XerathMageSpear"] = {EVADE_LINE,70.0f,1050.0f,1043.7f,0.0f,250,1006};
    g_evadeSpells["XerathMageSpearMissile"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,500,3500};
    g_evadeSpells["XinZhaoR"] = {EVADE_CIRCLE_START,210.0f,500.0f,666.7f,0.0f,0,750};
    g_evadeSpells["XinZhaoWMissile"] = {EVADE_LINE,40.0f,1175.0f,2317.6f,0.0f,0,507};
    g_evadeSpells["YasuoQ"] = {EVADE_LINE,25.0f,520.0f,1040.0f,0.0f,350,500};
    g_evadeSpells["YasuoQ1"] = {EVADE_LINE,25.0f,520.0f,1040.0f,0.0f,350,500};
    g_evadeSpells["YasuoQ1Wrapper"] = {EVADE_LINE,55.0f,475.0f,712.1f,0.0f,0,667};
    g_evadeSpells["YasuoQ2Wrapper"] = {EVADE_LINE,55.0f,475.0f,712.1f,0.0f,0,667};
    g_evadeSpells["YasuoQ3Mis"] = {EVADE_LINE,90.0f,1100.0f,868.2f,0.0f,0,1267};
    g_evadeSpells["YasuoQ3W"] = {EVADE_LINE,90.0f,3250.0f,1291.2f,0.0f,0,2517};
    g_evadeSpells["YasuoQ3WAutoSmartCast"] = {EVADE_LINE,90.0f,3250.0f,1291.2f,0.0f,0,2517};
    g_evadeSpells["YasuoQ3Wrapper"] = {EVADE_LINE,90.0f,1000.0f,983.3f,0.0f,0,1017};
    g_evadeSpells["YasuoQE1"] = {EVADE_LINE,25.0f,260.0f,520.0f,0.0f,0,500};
    g_evadeSpells["YasuoQE2"] = {EVADE_LINE,25.0f,260.0f,520.0f,0.0f,0,500};
    g_evadeSpells["YasuoQE3"] = {EVADE_LINE,25.0f,260.0f,520.0f,0.0f,0,500};
    g_evadeSpells["YasuoQW"] = {EVADE_LINE,55.0f,3250.0f,1291.2f,0.0f,0,2517};
    g_evadeSpells["YasuoWindWallTimerMis1"] = {EVADE_LINE,85.0f,825.0f,1082.7f,0.0f,0,762};
    g_evadeSpells["YasuoWMovingWallEnemy"] = {EVADE_CONE,0.0f,650.0f,812.5f,11.15999984741211f,13,800};
    g_evadeSpells["YasuoWMovingWallMis"] = {EVADE_LINE,25.0f,1200.0f,681.0f,0.0f,0,1762};
    g_evadeSpells["YasuoWMovingWallMisL"] = {EVADE_LINE,25.0f,1800.0f,729.3f,0.0f,0,2468};
    g_evadeSpells["YasuoWMovingWallMisOld"] = {EVADE_LINE,25.0f,1200.0f,473.9f,0.0f,0,2532};
    g_evadeSpells["YasuoWMovingWallMisR"] = {EVADE_LINE,25.0f,1200.0f,681.0f,0.0f,0,1762};
    g_evadeSpells["YasuoWMovingWallMisVis"] = {EVADE_LINE,25.0f,1200.0f,681.0f,0.0f,0,1762};
    g_evadeSpells["YoneQ"] = {EVADE_LINE,25.0f,450.0f,900.0f,0.0f,350,500};
    g_evadeSpells["YoneQ3Missile"] = {EVADE_LINE,80.0f,950.0f,966.4f,0.0f,0,983};
    g_evadeSpells["YoneR"] = {EVADE_LINE,225.0f,1000.0f,983.3f,0.0f,750,1017};
    g_evadeSpells["YoneW"] = {EVADE_CONE,0.0f,700.0f,875.0f,27.5f,500,800};
    g_evadeSpells["YorickEMissile"] = {EVADE_LINE,80.0f,1200.0f,1179.9f,0.0f,0,1017};
    g_evadeSpells["YorickW"] = {EVADE_CIRCLE,225.0f,600.0f,666.7f,0.0f,0,900};
    g_evadeSpells["YunaraW"] = {EVADE_LINE,60.0f,1150.0f,1299.4f,0.0f,0,885};
    g_evadeSpells["YuumiPHealMis"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["YuumiQ"] = {EVADE_LINE,65.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["YuumiQCast"] = {EVADE_LINE,60.0f,1250.0f,1128.2f,0.0f,0,1108};
    g_evadeSpells["YuumiQSkillShot"] = {EVADE_LINE,35.0f,1150.0f,328.6f,0.0f,0,3500};
    g_evadeSpells["YuumiRMissile"] = {EVADE_LINE,225.0f,1100.0f,1534.2f,0.0f,250,717};
    g_evadeSpells["ZaahenW"] = {EVADE_LINE,70.0f,850.0f,1321.9f,0.0f,500,643};
    g_evadeSpells["ZacE"] = {EVADE_CIRCLE,250.0f,300.0f,333.3f,0.0f,0,900};
    g_evadeSpells["ZacETimingMissile"] = {EVADE_LINE,70.0f,3000.0f,895.5f,0.0f,0,3350};
    g_evadeSpells["ZacQMissile"] = {EVADE_LINE,80.0f,951.0f,1378.3f,0.0f,0,690};
    g_evadeSpells["ZacR"] = {EVADE_CIRCLE,300.0f,300.0f,333.3f,0.0f,300,900};
    g_evadeSpells["ZedE"] = {EVADE_CIRCLE_START,290.0f,290.0f,386.7f,0.0f,0,750};
    g_evadeSpells["ZedMarker"] = {EVADE_CIRCLE_START,325.0f,325.0f,433.3f,0.0f,0,750};
    g_evadeSpells["ZedQ"] = {EVADE_LINE,45.0f,900.0f,667.7f,0.0f,0,1348};
    g_evadeSpells["ZedQMissile"] = {EVADE_LINE,50.0f,925.0f,1034.7f,0.0f,0,894};
    g_evadeSpells["ZedRMissile"] = {EVADE_LINE,60.0f,1175.0f,1150.8f,0.0f,251,1021};
    g_evadeSpells["ZedW"] = {EVADE_LINE,40.0f,700.0f,888.3f,0.0f,0,788};
    g_evadeSpells["ZedWMissile"] = {EVADE_LINE,60.0f,750.0f,1153.8f,0.0f,251,650};
    g_evadeSpells["ZeriQMis"] = {EVADE_LINE,60.0f,750.0f,1175.5f,0.0f,0,638};
    g_evadeSpells["ZeriQMisEmpowered"] = {EVADE_LINE,60.0f,750.0f,1313.5f,0.0f,0,571};
    g_evadeSpells["ZeriQMisEmpoweredParent"] = {EVADE_LINE,25.0f,750.0f,1034.5f,0.0f,0,725};
    g_evadeSpells["ZeriQMisEmpoweredPierce"] = {EVADE_LINE,60.0f,750.0f,1313.5f,0.0f,0,571};
    g_evadeSpells["ZeriQMisEmpoweredPierceParent"] = {EVADE_LINE,25.0f,750.0f,1034.5f,0.0f,0,725};
    g_evadeSpells["ZeriQMisParent"] = {EVADE_LINE,25.0f,825.0f,1082.7f,0.0f,0,762};
    g_evadeSpells["ZeriQMisPierce"] = {EVADE_LINE,60.0f,750.0f,1175.5f,0.0f,0,638};
    g_evadeSpells["ZeriQMisPierceParent"] = {EVADE_LINE,25.0f,750.0f,1034.5f,0.0f,0,725};
    g_evadeSpells["ZeriW"] = {EVADE_LINE,40.0f,1200.0f,1680.7f,0.0f,550,714};
    g_evadeSpells["ZiggsE"] = {EVADE_CIRCLE,235.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["ZiggsE2"] = {EVADE_LINE,120.0f,1675.0f,1170.5f,0.0f,0,1431};
    g_evadeSpells["ZiggsE3"] = {EVADE_LINE,120.0f,1100.0f,1015.7f,0.0f,0,1083};
    g_evadeSpells["ZiggsEMine"] = {EVADE_LINE,90.0f,500.0f,651.9f,0.0f,0,767};
    g_evadeSpells["ZiggsQ"] = {EVADE_CIRCLE,125.0f,850.0f,944.4f,0.0f,250,900};
    g_evadeSpells["ZiggsQSpell"] = {EVADE_LINE,120.0f,1625.0f,1244.3f,0.0f,0,1306};
    g_evadeSpells["ZiggsQSpell2"] = {EVADE_LINE,120.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["ZiggsQSpell3"] = {EVADE_LINE,120.0f,1100.0f,1059.7f,0.0f,0,1038};
    g_evadeSpells["ZiggsRBoom"] = {EVADE_LINE,120.0f,2775.0f,1261.4f,0.0f,0,2200};
    g_evadeSpells["ZiggsRBoomExtraLong"] = {EVADE_LINE,120.0f,5000.0f,1714.1f,0.0f,0,2917};
    g_evadeSpells["ZiggsRBoomLong"] = {EVADE_LINE,120.0f,5000.0f,1714.1f,0.0f,0,2917};
    g_evadeSpells["ZiggsRBoomMedium"] = {EVADE_LINE,120.0f,5000.0f,1714.1f,0.0f,0,2917};
    g_evadeSpells["ZiggsRTimer"] = {EVADE_CIRCLE,650.0f,950.0f,1055.6f,0.0f,250,900};
    g_evadeSpells["ZileanQ"] = {EVADE_CIRCLE,250.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["ZileanQAttachMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ZileanQAttachMissileMinion"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ZileanQAttachMissileShort"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ZileanQAttachMissileTall"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ZileanQBuffer"] = {EVADE_CIRCLE,250.0f,900.0f,1000.0f,0.0f,250,900};
    g_evadeSpells["ZileanQGround"] = {EVADE_CIRCLE,250.0f,825.0f,916.7f,0.0f,0,900};
    g_evadeSpells["ZileanQMissile"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ZoeE"] = {EVADE_LINE,40.0f,800.0f,1023.0f,0.0f,300,782};
    g_evadeSpells["ZoeEb"] = {EVADE_LINE,70.0f,800.0f,1023.0f,0.0f,250,782};
    g_evadeSpells["ZoeEb2"] = {EVADE_LINE,80.0f,2875.0f,1408.6f,0.0f,250,2041};
    g_evadeSpells["ZoeEb3"] = {EVADE_LINE,80.0f,2875.0f,1408.6f,0.0f,250,2041};
    g_evadeSpells["ZoeEb4"] = {EVADE_LINE,80.0f,2875.0f,1408.6f,0.0f,250,2041};
    g_evadeSpells["ZoeEb5"] = {EVADE_LINE,80.0f,2875.0f,1408.6f,0.0f,250,2041};
    g_evadeSpells["ZoeEc"] = {EVADE_LINE,60.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ZoeEDistanceSolver"] = {EVADE_LINE,40.0f,800.0f,1600.0f,0.0f,0,500};
    g_evadeSpells["ZoeEDistanceSolver2"] = {EVADE_LINE,40.0f,800.0f,1600.0f,0.0f,0,500};
    g_evadeSpells["ZoeEDistanceSolver3"] = {EVADE_LINE,40.0f,800.0f,1600.0f,0.0f,0,500};
    g_evadeSpells["ZoeEDistanceSolver4"] = {EVADE_LINE,40.0f,800.0f,1600.0f,0.0f,0,500};
    g_evadeSpells["ZoeEMis"] = {EVADE_LINE,50.0f,800.0f,1023.0f,0.0f,300,782};
    g_evadeSpells["ZoeEMisAudio"] = {EVADE_LINE,25.0f,800.0f,1023.0f,0.0f,0,782};
    g_evadeSpells["ZoeQ"] = {EVADE_LINE,70.0f,800.0f,786.6f,0.0f,0,1017};
    g_evadeSpells["ZoeQInterstitial"] = {EVADE_LINE,50.0f,875.0f,729.2f,0.0f,0,1200};
    g_evadeSpells["ZoeQMis2"] = {EVADE_LINE,70.0f,1175.0f,1432.9f,0.0f,0,820};
    g_evadeSpells["ZoeQMis2SleepCombo"] = {EVADE_LINE,70.0f,1175.0f,1526.0f,0.0f,0,770};
    g_evadeSpells["ZoeQMis2Warning"] = {EVADE_LINE,60.0f,1175.0f,1432.9f,0.0f,0,820};
    g_evadeSpells["ZoeQMis2Warning2"] = {EVADE_LINE,60.0f,1175.0f,1432.9f,0.0f,0,820};
    g_evadeSpells["ZoeQMis2WarningSleepCombo"] = {EVADE_LINE,60.0f,1175.0f,1526.0f,0.0f,0,770};
    g_evadeSpells["ZoeQMissile"] = {EVADE_LINE,50.0f,875.0f,810.9f,0.0f,250,1079};
    g_evadeSpells["ZoeQRecast"] = {EVADE_LINE,70.0f,900.0f,818.2f,0.0f,0,1100};
    g_evadeSpells["ZoeW"] = {EVADE_LINE,120.0f,3000.0f,1621.6f,0.0f,10,1850};
    g_evadeSpells["ZoeWVisualMis4"] = {EVADE_LINE,120.0f,5000.0f,1428.6f,0.0f,0,3500};
    g_evadeSpells["ZyraE"] = {EVADE_LINE,70.0f,1150.0f,851.9f,0.0f,0,1350};
    g_evadeSpells["ZyraQ"] = {EVADE_LINE,85.0f,800.0f,868.6f,0.0f,0,921};
    g_evadeSpells["ZyraRSplash"] = {EVADE_LINE,85.0f,1170.0f,544.2f,0.0f,242,2150};
    g_evadeSpellsLoaded = true;
    printf("[*] Evade spell database loaded: %zu entries\n", g_evadeSpells.size());

    // Build champion → line spells index (for fallback when slot matching fails)
    // Key = champion name (matches g_heroes[i].name), Value = list of their LINE spells (no Missile entries)
    struct ChampSpellInit { const char* champ; const char* spell; };
    static const ChampSpellInit champMap[] = {
        {"Aatrox","AatroxW"},
        {"Ahri","AhriE"},{"Ahri","AhriQ"},
        {"Akali","AkaliE"},{"Akali","AkaliQ"},{"Akali","AkaliW"},
        {"Akshan","AkshanQMissile"},
        {"Amumu","AmumuQ"},
        {"Anivia","AniviaQ"},{"Anivia","FlashFrostSpell"},
        {"Aphelios","ApheliosCalibrumQ"},
        {"Ashe","AsheQ"},{"Ashe","AsheR"},{"Ashe","Volley"},
        {"AurelionSol","AurelionSolQ"},{"AurelionSol","AurelionSolR"},
        {"Aurora","AuroraQ"},
        {"Bard","BardQ"},{"Bard","BardR"},
        {"Belveth","BelvethQ"},{"Belveth","BelvethW"},
        {"Blitzcrank","BlitzcrankQ"},
        {"Brand","BrandQ"},{"Brand","BrandW"},
        {"Braum","BraumQ"},{"Braum","BraumR"},
        {"Briar","BriarE"},{"Briar","BriarR"},{"Briar","BriarW"},
        {"Caitlyn","CaitlynEntrapment"},{"Caitlyn","CaitlynPiltoverPeacemaker"},{"Caitlyn","CaitlynQ"},
        {"Camille","CamilleE"},
        {"Cassiopeia","CassiopeiaQ"},{"Cassiopeia","CassiopeiaR"},
        {"Corki","CorkiMissile"},{"Corki","CorkiQ"},{"Corki","CorkiR"},
        {"Darius","DariusAxeGrabCone"},
        {"Diana","DianaQ"},
        {"DrMundo","DrMundoQ"},
        {"Draven","DravenDoubleShot"},{"Draven","DravenR"},
        {"Ekko","EkkoQ"},{"Ekko","EkkoR"},{"Ekko","EkkoW"},
        {"Elise","EliseHumanE"},
        {"Evelynn","EvelynnQ"},{"Evelynn","EvelynnR"},
        {"Ezreal","EzrealMysticShot"},{"Ezreal","EzrealQ"},{"Ezreal","EzrealR"},{"Ezreal","EzrealTrueshotBarrage"},{"Ezreal","EzrealW"},
        {"Fiddlesticks","FiddleSticksQ"},
        {"Fiora","FioraW"},
        {"Fizz","FizzR"},
        {"Galio","GalioE"},{"Galio","GalioQ"},
        {"Gnar","GnarBigW"},{"Gnar","GnarQ"},{"Gnar","GnarQReturn"},{"Gnar","GnarR"},
        {"Gragas","GragasQ"},{"Gragas","GragasR"},
        {"Graves","GravesChargeShot"},{"Graves","GravesClusterShot"},{"Graves","GravesQ"},{"Graves","GravesR"},{"Graves","GravesSmokeGrenade"},
        {"Gwen","GwenE"},{"Gwen","GwenQ"},{"Gwen","GwenR"},
        {"Hecarim","HecarimR"},{"Hecarim","HecarimRMissile"},
        {"Heimerdinger","HeimerdingerE"},{"Heimerdinger","HeimerdingerW"},
        {"Hwei","HweiEE"},{"Hwei","HweiEQ"},{"Hwei","HweiEW"},{"Hwei","HweiQQ"},{"Hwei","HweiQW"},{"Hwei","HweiWQ"},{"Hwei","HweiWW"},
        {"Illaoi","IllaoiE"},{"Illaoi","IllaoiQ"},{"Illaoi","IllaoiR"},
        {"Irelia","IreliaE"},{"Irelia","IreliaR"},
        {"Ivern","IvernQ"},
        {"Janna","JannaQ"},
        {"JarvanIV","JarvanIVDragonStrike"},{"JarvanIV","JarvanIVE"},
        {"Jayce","JayceQ"},{"Jayce","JayceShockBlast"},
        {"Jhin","JhinE"},{"Jhin","JhinQ"},{"Jhin","JhinR"},{"Jhin","JhinW"},
        {"Jinx","JinxR"},{"Jinx","JinxW"},{"Jinx","JinxWMissile"},
        {"KSante","KSanteQ"},{"KSante","KSanteQ3"},
        {"Kaisa","KaisaW"},
        {"Kalista","KalistaQ"},
        {"Karma","KarmaQ"},
        {"Karthus","KarthusQ"},
        {"Kassadin","KassadinE"},
        {"Kayn","KaynQ"},{"Kayn","KaynW"},
        {"Kennen","KennenQ"},
        {"Khazix","KhazixW"},
        {"Kindred","KindredQ"},
        {"Kled","KledQ"},{"Kled","KledR"},
        {"KogMaw","KogMawE"},{"KogMaw","KogMawQ"},{"KogMaw","KogMawR"},{"KogMaw","KogMawVoidOoze"},
        {"Leblanc","LeblancE"},{"Leblanc","LeblancRE"},{"Leblanc","LeblancW"},
        {"LeeSin","LeeSinQ"},
        {"Leona","LeonaR"},{"Leona","LeonaSolarFlare"},{"Leona","LeonaZenithBlade"},
        {"Lillia","LilliaQ"},{"Lillia","LilliaR"},{"Lillia","LilliaW"},
        {"Lissandra","LissandraQ"},
        {"Lucian","LucianQ"},{"Lucian","LucianR"},{"Lucian","LucianW"},
        {"Lulu","LuluQ"},
        {"Lux","LuxLightBinding"},{"Lux","LuxLightStrikeKugel"},{"Lux","LuxQ"},{"Lux","LuxR"},
        {"Malphite","MalphiteR"},
        {"Malzahar","MalzaharQ"},
        {"Maokai","MaokaiQ"},{"Maokai","MaokaiR"},
        {"Mel","MelQ"},{"Mel","MelR"},{"Mel","MelW"},
        {"MissFortune","MissFortuneE"},{"MissFortune","MissFortuneR"},
        {"Mordekaiser","MordekaiserE"},{"Mordekaiser","MordekaiserQ"},
        {"Morgana","MorganaQ"},
        {"Naafiri","NaafiriQ"},{"Naafiri","NaafiriW"},
        {"Nami","NamiQ"},{"Nami","NamiR"},
        {"Nasus","NasusE"},
        {"Nautilus","NautilusQ"},
        {"Neeko","NeekoE"},{"Neeko","NeekoQ"},
        {"Nidalee","NidaleeQ"},
        {"Nilah","NilahQ"},{"Nilah","NilahR"},
        {"Nocturne","NocturneQ"},
        {"Nunu","NunuR"},
        {"Olaf","OlafAxeThrow"},{"Olaf","OlafQ"},
        {"Orianna","OriannaQ"},{"Orianna","OriannaR"},
        {"Ornn","OrnnQ"},{"Ornn","OrnnR"},{"Ornn","OrnnRCharge"},
        {"Pantheon","PantheonQ"},{"Pantheon","PantheonR"},
        {"Poppy","PoppyQ"},{"Poppy","PoppyR"},
        {"Pyke","PykeE"},{"Pyke","PykeQ"},{"Pyke","PykeR"},
        {"Qiyana","QiyanaQ"},{"Qiyana","QiyanaR"},
        {"Quinn","QuinnQ"},
        {"Rakan","RakanQ"},{"Rakan","RakanW"},
        {"RekSai","RekSaiQ"},
        {"Rell","RellQ"},{"Rell","RellR"},{"Rell","RellW"},
        {"Renata","RenataE"},{"Renata","RenataQ"},{"Renata","RenataR"},
        {"Renekton","RenektonE"},
        {"Rengar","RengarE"},
        {"Riven","RivenQ"},{"Riven","RivenR"},
        {"Rumble","RumbleE"},{"Rumble","RumbleR"},
        {"Ryze","RyzeQ"},
        {"Samira","SamiraQ"},{"Samira","SamiraR"},
        {"Sejuani","SejuaniQ"},{"Sejuani","SejuaniR"},
        {"Senna","SennaQ"},{"Senna","SennaR"},{"Senna","SennaW"},
        {"Seraphine","SeraphineE"},{"Seraphine","SeraphineQ"},{"Seraphine","SeraphineR"},
        {"Sett","SettQ"},{"Sett","SettW"},
        {"Shaco","ShacoE"},
        {"Shen","ShenE"},
        {"Shyvana","ShyvanaE"},
        {"Singed","SingedW"},
        {"Sion","SionE"},{"Sion","SionQ"},{"Sion","SionR"},
        {"Sivir","SivirQ"},
        {"Skarner","SkarnerE"},{"Skarner","SkarnerQ"},{"Skarner","SkarnerR"},
        {"Smolder","SmolderQ"},{"Smolder","SmolderR"},{"Smolder","SmolderW"},
        {"Sona","SonaR"},
        {"Soraka","SorakaQ"},
        {"Swain","SwainE"},{"Swain","SwainW"},
        {"Sylas","SylasE"},{"Sylas","SylasQ"},
        {"Syndra","SyndraQ"},{"Syndra","SyndraW"},
        {"TahmKench","TahmKenchQ"},
        {"Taliyah","TaliyahQ"},{"Taliyah","TaliyahR"},{"Taliyah","TaliyahW"},
        {"Talon","TalonR"},{"Talon","TalonW"},
        {"Taric","TaricE"},
        {"Thresh","ThreshE"},{"Thresh","ThreshQ"},{"Thresh","ThreshR"},
        {"Tristana","TristanaW"},
        {"Trundle","TrundleE"},
        {"TwistedFate","TwistedFateQ"},
        {"Twitch","TwitchW"},
        {"Urgot","UrgotQ"},{"Urgot","UrgotR"},
        {"Varus","VarusE"},{"Varus","VarusQ"},{"Varus","VarusR"},
        {"Vayne","VayneE"},
        {"Veigar","VeigarE"},{"Veigar","VeigarW"},
        {"Velkoz","VelkozQ"},{"Velkoz","VelkozW"},
        {"Vex","VexQ"},{"Vex","VexR"},
        {"Vi","ViQ"},
        {"Viego","ViegoQ"},{"Viego","ViegoW"},
        {"Viktor","ViktorE"},{"Viktor","ViktorQ"},{"Viktor","ViktorR"},{"Viktor","ViktorW"},
        {"Vladimir","VladimirE"},
        {"Warwick","WarwickR"},
        {"Xayah","XayahQ"},
        {"Xerath","XerathE"},{"Xerath","XerathQ"},{"Xerath","XerathR"},{"Xerath","XerathW"},
        {"XinZhao","XinZhaoW"},
        {"Yasuo","YasuoQ"},{"Yasuo","YasuoQ3"},
        {"Yone","YoneQ"},{"Yone","YoneQ3"},{"Yone","YoneR"},
        {"Yorick","YorickE"},
        {"Yuumi","YuumiQ"},
        {"Zac","ZacQ"},
        {"Zed","ZedQ"},{"Zed","ZedW"},
        {"Zeri","ZeriQ"},{"Zeri","ZeriR"},{"Zeri","ZeriW"},
        {"Ziggs","ZiggsE"},{"Ziggs","ZiggsQ"},{"Ziggs","ZiggsR"},{"Ziggs","ZiggsW"},
        {"Zilean","ZileanQ"},
        {"Zoe","ZoeE"},{"Zoe","ZoeQ"},
        {"Zyra","ZyraE"},{"Zyra","ZyraQ"},{"Zyra","ZyraR"},
    };
    for (auto& cm : champMap) {
        auto sit = g_evadeSpells.find(cm.spell);
        if (sit != g_evadeSpells.end() && sit->second.shape == EVADE_LINE && sit->second.speed > 10.f && sit->second.range > 50.f) {
            g_champLineSpells[cm.champ].push_back({cm.spell, &sit->second});
        }
    }
    printf("[*] Champion line-spell index: %zu champions\n", g_champLineSpells.size());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    SetUnhandledExceptionFilter(CrashHandler);
    InitPaths();
    strncpy(g_scriptDir, xorstr_("C:\\VeoxDMA\\Scripts"), MAX_PATH - 1);
    EnsureDirs();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&g_gdipToken, &gdipInput, nullptr);

    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONIN$",  "r", stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    printf(xorstr_("VeoxDMA - League of Legends\n"));
    printf(xorstr_("===========================\n"));

    Auth::g_authenticated = true;
    printf(xorstr_("[*] KeyAuth disabled\n"));

    FetchDDVersion();
    InitSinCos();
    InitEvadeSpells();
    LoadConfig("default");
    LuaInit();
    LuaScanScripts();

    if (!TargetProcess.Init(xorstr_("League of Legends.exe")))
    {
        printf(xorstr_("[!] Failed to find League of Legends. Is the game running?\n"));
        printf(xorstr_("Press Enter to exit...\n"));
        getchar();
        return -1;
    }
    TargetProcess.FixCr3();

    g_base = TargetProcess.current_process.base_address;
    VX_LOG("[+] Base address: 0x%llX\n", (uint64_t)g_base);
    VX_LOG("MZ check: 0x%llX (want ...5a4d)\n", (uint64_t)TargetProcess.Read<uint64_t>(g_base));

    SigScanGlobals();
    ProbeNameOffset();
    ProbeTeamOffset();
    ProbeManagerLayout();
    ProbeHeroList();
    ProbeReadyAtOffset();
    FixInvalidManagers();

    if (Keyboard::InitKeyboard())
        VX_LOG("[+] Keyboard initialized (game PC key state via DMA)\n");
    else
        VX_LOG("[!] Keyboard init failed - keybinds will not work\n");

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEX wc{};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName  = L"OverlayWnd";
    RegisterClassEx(&wc);

    DetectMonitors();
    HWND hWnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName, L"Overlay",
        WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL);
    if (!hWnd) return -1;

    if (!CreateDeviceD3D(hWnd))
    {
        CleanupDeviceD3D();
        return -1;
    }
    ApplyOverlayMonitor(hWnd);
    ShowWindow(hWnd, nCmdShow);

    {
        const RECT& r = g_overlayMonitors[g_monitorIndex];
        g_screenW = r.right - r.left;
        g_screenH = r.bottom - r.top;
        g_cursorX = g_screenW / 2.f;
        g_cursorY = g_screenH / 2.f;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    // ── Modern dark theme ──
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 8.f;
        s.ChildRounding     = 6.f;
        s.FrameRounding     = 6.f;
        s.PopupRounding     = 6.f;
        s.GrabRounding      = 4.f;
        s.TabRounding       = 6.f;
        s.ScrollbarRounding = 6.f;
        s.WindowPadding     = ImVec2(14, 14);
        s.FramePadding      = ImVec2(10, 6);
        s.ItemSpacing       = ImVec2(10, 8);
        s.ItemInnerSpacing  = ImVec2(8, 4);
        s.ScrollbarSize     = 12.f;
        s.GrabMinSize       = 10.f;
        s.WindowBorderSize  = 0.f;
        s.ChildBorderSize   = 0.f;
        s.TabBorderSize     = 0.f;
        s.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

        ImVec4* c = s.Colors;
        ImVec4 bg      (0.082f, 0.082f, 0.098f, 1.0f);
        ImVec4 surface (0.110f, 0.110f, 0.133f, 1.0f);
        ImVec4 raised  (0.145f, 0.145f, 0.173f, 1.0f);
        ImVec4 border  (0.200f, 0.200f, 0.235f, 0.5f);
        ImVec4 accent  (0.345f, 0.557f, 1.000f, 1.0f); // blue accent
        ImVec4 accentDim(0.250f, 0.400f, 0.750f, 1.0f);
        ImVec4 accentBg(0.200f, 0.300f, 0.600f, 0.25f);
        ImVec4 textMain(0.92f, 0.92f, 0.95f, 1.0f);
        ImVec4 textDim (0.55f, 0.55f, 0.62f, 1.0f);
        ImVec4 green   (0.30f, 0.85f, 0.45f, 1.0f);
        ImVec4 red     (0.95f, 0.35f, 0.35f, 1.0f);

        c[ImGuiCol_Text]                  = textMain;
        c[ImGuiCol_TextDisabled]          = textDim;
        c[ImGuiCol_WindowBg]              = bg;
        c[ImGuiCol_ChildBg]               = ImVec4(0,0,0,0);
        c[ImGuiCol_PopupBg]               = surface;
        c[ImGuiCol_Border]                = border;
        c[ImGuiCol_FrameBg]               = surface;
        c[ImGuiCol_FrameBgHovered]        = raised;
        c[ImGuiCol_FrameBgActive]         = raised;
        c[ImGuiCol_TitleBg]               = bg;
        c[ImGuiCol_TitleBgActive]         = bg;
        c[ImGuiCol_TitleBgCollapsed]      = bg;
        c[ImGuiCol_MenuBarBg]             = surface;
        c[ImGuiCol_ScrollbarBg]           = bg;
        c[ImGuiCol_ScrollbarGrab]         = raised;
        c[ImGuiCol_ScrollbarGrabHovered]  = accentDim;
        c[ImGuiCol_ScrollbarGrabActive]   = accent;
        c[ImGuiCol_CheckMark]             = accent;
        c[ImGuiCol_SliderGrab]            = accentDim;
        c[ImGuiCol_SliderGrabActive]      = accent;
        c[ImGuiCol_Button]                = raised;
        c[ImGuiCol_ButtonHovered]         = accentBg;
        c[ImGuiCol_ButtonActive]          = accentDim;
        c[ImGuiCol_Header]                = accentBg;
        c[ImGuiCol_HeaderHovered]         = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        c[ImGuiCol_HeaderActive]          = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        c[ImGuiCol_Separator]             = border;
        c[ImGuiCol_SeparatorHovered]      = accent;
        c[ImGuiCol_SeparatorActive]       = accent;
        c[ImGuiCol_ResizeGrip]            = ImVec4(0,0,0,0);
        c[ImGuiCol_ResizeGripHovered]     = accentDim;
        c[ImGuiCol_ResizeGripActive]      = accent;
        c[ImGuiCol_Tab]                   = surface;
        c[ImGuiCol_TabHovered]            = ImVec4(accent.x, accent.y, accent.z, 0.40f);
        c[ImGuiCol_TabActive]             = raised;
        c[ImGuiCol_TabUnfocused]          = surface;
        c[ImGuiCol_TabUnfocusedActive]    = raised;
    }
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    VX_LOG("[+] Overlay ready. Press INSERT to toggle menu.\n");

    DWORD lastCacheTick = 0;

    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        ApplyOverlayMonitor(hWnd);

        DWORD now = GetTickCount();
        if (now - lastCacheTick > (DWORD)(20 + randInt(0, 12)))
        {
            if (!GameMemoryReadable())
            {
                static DWORD lastReattach = 0;
                static DWORD retryInterval = 2000;
                static bool printed = false;
                if (!printed) {
                    VX_LOG("[!] Game memory unreadable, waiting for game...\n");
                    printed = true;
                    g_heroes.clear();
                    g_minions.clear();
                    g_turrets.clear();
                    g_gameTime = 0.f;
                    g_enemyJunglerIdx = -1;
                    g_junglerAlertTime = 0.f;
                }
                if (now - lastReattach > retryInterval) {
                    lastReattach = now;
                    if (TargetProcess.Reattach() && GameMemoryReadable())
                    {
                        g_base = TargetProcess.current_process.base_address;
                        g_viewPortScanned = false;
                        g_globalsScanned  = false;
                        SigScanGlobals();
                        ProbeNameOffset();
                        ProbeTeamOffset();
                        ProbeManagerLayout();
                        FixInvalidManagers();
                        printed = false;
                        retryInterval = 2000;
                        VX_LOG("[+] Game found! (base 0x%llX)\n", (uint64_t)g_base);
                    }
                    else if (retryInterval < 10000) {
                        retryInterval += 2000;
                    }
                }
            }
            else
            {
                CacheGameState();
                OrbwalkerTick();
                LuaOnTick();
            }
            lastCacheTick = now;

            // Runtime integrity check (~every 10s given typical tick rate)
            static DWORD lastIntegrityCheck = 0;
            if (now - lastIntegrityCheck > 10000) {
                lastIntegrityCheck = now;
#if defined(NDEBUG) && !defined(VX_NO_AUTH)
                if (!Auth::RuntimeIntegrityCheck()) {
                    VX_LOG("[!] Runtime integrity check failed. Exiting.\n");
                    PostQuitMessage(0);
                }
#endif
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_fpsShow)
        {
            char fpsBuf[32];
            snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.0f", io.Framerate);
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(10.f, 10.f), IM_COL32(0, 255, 0, 255), fpsBuf);
        }

        DrawESP();
        DrawMinimap();
        OrbwalkerDraw();
        LuaOnDraw();
        DrawKeyDisplay();
        DrawMenu();

        ImGui::Render();

        const float clear[4] = { g_bgColor[0], g_bgColor[1], g_bgColor[2], 1.f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    LuaShutdown();
    for (auto& [k, v] : g_iconCache) { if (v) v->Release(); }
    g_iconCache.clear();
    InputDevice::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    if (g_gdipToken) Gdiplus::GdiplusShutdown(g_gdipToken);
    CoUninitialize();
    DestroyWindow(hWnd);
    return (int)msg.wParam;
}
