#pragma once
// ProMenu 0.4 - touch-first overlay: floating circle button + cheat panel.
//
// ImGui is used for DRAWING ONLY.  Tap, drag and scroll are handled here from the raw touch
// events (1 = up, 2 = down, 3 = move), so nothing depends on ImGui's mouse click state machine.
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <vector>
#include "imgui.h"

namespace UI {

// ======================================================================= data
// The 100 cheats of the game, in the game's own index order (index into CCheat::m_aCheatFunctions
// and CCheat::m_aCheatsActive).  Order and names were read from the original ARM menu.
struct CheatDef { const char* name; const char* label; unsigned char group; };
static const CheatDef kCheat[] = {
#include "cheats100.inc"
};
static const int kNumCheats = (int)(sizeof(kCheat) / sizeof(kCheat[0]));
static const char* kCheatTh[] = {
#include "cheats100_th.inc"
};
static_assert(sizeof(kCheatTh) / sizeof(kCheatTh[0]) == sizeof(kCheat) / sizeof(kCheat[0]), "Thai cheat labels must match the cheat list");
static const char* kGroupName[] = {
#include "cheat_groups.inc"
};
static const int kNumGroups = (int)(sizeof(kGroupName) / sizeof(kGroupName[0]));
static const char* kGroupNameTh[] = {
#include "cheat_groups_th.inc"
};
static_assert(sizeof(kGroupNameTh) / sizeof(kGroupNameTh[0]) == sizeof(kGroupName) / sizeof(kGroupName[0]), "Thai group names must match");
static const int kDangerGroup = 10;

struct Veh { const char* section; const char* label; int model; };
static const Veh kVeh[] = {
#include "vehicles.inc"
};
static const int kNumVeh = (int)(sizeof(kVeh) / sizeof(kVeh[0]));

enum { kTabPlayer = 0, kTabVehicle, kTabGame, kTabCheats, kTabMenu, kTabCount };
static const char* kTabs[] = {"Player", "Vehicle", "Game", "Cheats", "Menu"};

// ============================================================== game access
typedef void (*CheatFn)();
static CheatFn* g_cheatFns = nullptr;               // CCheat::m_aCheatFunctions  (nullptr entry = plain flag cheat)
static unsigned char* g_cheatOn = nullptr;          // CCheat::m_aCheatsActive
static bool g_cheatKnownToggle[128];                // learned: this cheat changed its flag when run
static void (*g_vehicleCheat)(int) = nullptr;       // CCheat::VehicleCheat(model)
static void (*g_setClock)(unsigned char, unsigned char, unsigned char) = nullptr;   // CClock::SetGameClock
static void (*g_cheatWanted)(void*, int) = nullptr; // CPlayerPed::CheatWantedLevel(level)
static void* (*g_findPlayerPed)(int) = nullptr;     // FindPlayerPed(0)
static void* (*g_getPlayerInfo)(void*) = nullptr;   // CPlayerPed::GetPlayerInfoForThisPlayerPed()
static unsigned char* g_pCurrentDay = nullptr;      // CClock::CurrentDay
static bool* g_pGodFlag = nullptr;                  // CPlayerPed::bDebugPlayerInvincible
static int g_godIdx = -1;                           // index of INVINCIBILITY in the cheat list

// Field offsets inside the 64-bit game structures (read from the original menu's code)
static const int kOffHealth = 0x6AC, kOffMaxHealth = 0x6B0, kOffArmour = 0x6B4;   // CPed
static const int kOffMoney = 0xF0;                                                // CPlayerInfo

template <typename T> static void Res(uintptr_t (*lookup)(const char*), T& out, const char* name, int& found) {
    uintptr_t a = lookup(name);
    out = reinterpret_cast<T>(a);
    if (a) found++;
}

// returns how many of the 10 essential game symbols were found
static int ResolveGame(uintptr_t (*lookup)(const char*)) {
    int found = 0;
    Res(lookup, g_cheatFns, "_ZN6CCheat17m_aCheatFunctionsE", found);
    Res(lookup, g_cheatOn, "_ZN6CCheat15m_aCheatsActiveE", found);
    Res(lookup, g_vehicleCheat, "_ZN6CCheat12VehicleCheatEi", found);
    Res(lookup, g_setClock, "_ZN6CClock12SetGameClockEhhh", found);
    Res(lookup, g_cheatWanted, "_ZN10CPlayerPed16CheatWantedLevelEi", found);
    Res(lookup, g_findPlayerPed, "_Z13FindPlayerPedi", found);
    Res(lookup, g_getPlayerInfo, "_ZN10CPlayerPed29GetPlayerInfoForThisPlayerPedEv", found);
    Res(lookup, g_pCurrentDay, "_ZN6CClock10CurrentDayE", found);
    Res(lookup, g_pGodFlag, "_ZN10CPlayerPed22bDebugPlayerInvincibleE", found);
    for (int i = 0; i < kNumCheats; i++) if (!strcmp(kCheat[i].name, "INVINCIBILITY")) g_godIdx = i;
    return found;
}

static bool CheatTablesOk() { return g_cheatFns != nullptr || g_cheatOn != nullptr; }

// current state of cheat `idx` (what the game thinks is active)
static bool StateOf(int idx) {
    if (idx == g_godIdx && g_pGodFlag) return *g_pGodFlag;
    return g_cheatOn ? g_cheatOn[idx] != 0 : false;
}
// does this cheat have an on/off state we can show?
static bool IsToggle(int idx) {
    if (idx == g_godIdx && g_pGodFlag) return true;
    if (!g_cheatOn) return false;
    if (g_cheatFns && g_cheatFns[idx] == nullptr) return true;     // plain flag cheat
    return g_cheatKnownToggle[idx];
}

// same logic the original menu uses: call the game's cheat function, or flip the flag when there is none
static void ExecCheat(int idx) {
    const bool before = StateOf(idx);
    CheatFn fn = g_cheatFns ? g_cheatFns[idx] : nullptr;
    if (fn) fn();
    else if (g_cheatOn) g_cheatOn[idx] = g_cheatOn[idx] ? 0 : 1;
    if (StateOf(idx) != before) g_cheatKnownToggle[idx] = true;
}

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static char* PlayerPed() { return g_findPlayerPed ? (char*)g_findPlayerPed(0) : nullptr; }
static char* PlayerInfo(char* ped) { return (ped && g_getPlayerInfo) ? (char*)g_getPlayerInfo(ped) : nullptr; }

// ============================================================ action queue
// The UI never touches the game directly; it queues actions that run inside the game's update tick.
enum { aCheat = 0, aVehicle, aClock, aWanted, aHealth, aArmour, aMoney };
struct Act { int kind; int i; float f; };
static const int kQueueSize = 32;
static Act g_queue[kQueueSize];
static volatile int g_qHead = 0, g_qTail = 0;

static void Enqueue(int kind, int i, float f = 0.f) {
    int next = (g_qTail + 1) % kQueueSize;
    if (next == g_qHead) return;
    g_queue[g_qTail].kind = kind; g_queue[g_qTail].i = i; g_queue[g_qTail].f = f;
    g_qTail = next;
}

static void RunQueued(bool gameIsRunning) {
    while (g_qHead != g_qTail) {
        const Act a = g_queue[g_qHead];
        g_qHead = (g_qHead + 1) % kQueueSize;
        if (!gameIsRunning) continue;
        switch (a.kind) {
            case aCheat:   if (a.i >= 0 && a.i < kNumCheats) ExecCheat(a.i); break;
            case aVehicle: if (g_vehicleCheat) g_vehicleCheat(a.i); break;
            case aClock:   if (g_setClock) g_setClock((unsigned char)a.i, 0, g_pCurrentDay ? *g_pCurrentDay : 0); break;
            case aWanted: {
                char* ped = PlayerPed();
                if (ped && g_cheatWanted) g_cheatWanted(ped, a.i);
                break;
            }
            case aHealth: {
                char* ped = PlayerPed();
                if (ped) { float mx = *(float*)(ped + kOffMaxHealth); if (mx < 100.f) mx = 100.f; *(float*)(ped + kOffHealth) = Clamp(a.f, 1.f, mx); }
                break;
            }
            case aArmour: {
                char* ped = PlayerPed();
                if (ped) *(float*)(ped + kOffArmour) = Clamp(a.f, 0.f, 100.f);
                break;
            }
            case aMoney: {
                char* info = PlayerInfo(PlayerPed());
                if (info) *(int*)(info + kOffMoney) = (int)Clamp(a.f, 0.f, 99999999.f);
                break;
            }
        }
    }
}

// =============================================================== UI state
static float g_sc = 1.f;                          // UI scale (screen height / 1080)
static float g_slop = 48.f;                       // finger jitter allowed before a tap becomes a drag (pixels)
static float g_scrW = 0.f, g_scrH = 0.f, g_fabD = 100.f;
static volatile float g_fabX = 0.f, g_fabY = 0.f; // top-left of the floating button (written by touch thread while dragging)
static bool g_posInit = false;
static float g_cfgFx = -1.f, g_cfgFy = -1.f;      // saved position as a fraction of the screen
static bool g_open = false;
static float g_openAnim = 0.f, g_idle = 0.f, g_fade = 1.f;
static float g_btnAlpha = 0.92f, g_btnScale = 1.f;
static bool g_fadeIdle = true;
static bool g_lockPos = false;                    // when on, the floating button cannot be dragged
static int g_lang = 0;                            // 0 = English, 1 = Thai
static bool g_thaiOk = false;                     // a Thai font was loaded (set by main.cpp)
static int g_tab = kTabCheats, g_lastTab = -1;
static char g_toast[80] = "";
static float g_toastT = 0.f;
static bool g_expanded[64];                       // section expand/collapse state (cheat groups 0..11, vehicle sections 20..)
static int g_armedIdx = -1;                       // danger cheat waiting for its second tap
static float g_armedT = 0.f;

// shown in Menu, filled in by main.cpp
static int g_dbgGameState = -1, g_dbgFound = 0;
static bool g_canRun = false;                     // game is in "playing" state
static volatile int g_dbgTouches = 0, g_dbgTaps = 0;
static volatile int g_dbgRaw[4] = {0, 0, 0, 0};   // raw event counts by type (1 = up, 2 = down, 3 = move)

// ------------------------------------------------------------ touch input
// Touch events arrive on the game's input callback, drawing happens on the render
// hook.  Shared data is either a plain volatile value or protected by g_lock.
struct TapRect { float x0, y0, x1, y1; int id; };
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static std::vector<TapRect> g_rects;          // published by the render side, read by the touch side
static std::vector<TapRect> g_rectsBuild;     // being built this frame

static volatile bool g_visible = false, g_panelValid = false;
static volatile float g_pX0 = 0, g_pY0 = 0, g_pX1 = 0, g_pY1 = 0;   // panel rectangle
static volatile float g_cX0 = 0, g_cY0 = 0, g_cX1 = 0, g_cY1 = 0;   // scrollable content rectangle

static int g_finger = -1, g_target = 0;       // target: 1 = floating button, 2 = panel
static float g_downX = 0, g_downY = 0, g_lastX = 0, g_lastY = 0, g_startFabX = 0, g_startFabY = 0;
static bool g_moved = false, g_scrollArea = false;
static int g_downId = 0;
static float g_scrollDy = 0.f, g_velY = 0.f;
static volatile float g_fling = 0.f;
static double g_lastMoveT = 0.0, g_lastEventT = 0.0;
static volatile bool g_fabTap = false, g_saveReq = false;
static int g_tapQ[8];
static volatile int g_tqH = 0, g_tqT = 0;

static double Now() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
static bool InRect(float x, float y, float x0, float y0, float x1, float y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }

static int FindId(float x, float y) {
    int id = 0;
    pthread_mutex_lock(&g_lock);
    for (size_t i = 0; i < g_rects.size(); i++) {
        const TapRect& r = g_rects[i];
        if (InRect(x, y, r.x0, r.y0, r.x1, r.y1)) { id = r.id; break; }
    }
    pthread_mutex_unlock(&g_lock);
    return id;
}

static void PushTap(int id) {
    pthread_mutex_lock(&g_lock);
    int next = (g_tqT + 1) % 8;
    if (next != g_tqH) { g_tapQ[g_tqT] = id; g_tqT = next; }
    pthread_mutex_unlock(&g_lock);
}

static bool HitFab(float x, float y) {
    const float r = g_fabD * 0.5f + 18.f * g_sc;
    const float dx = x - (g_fabX + g_fabD * 0.5f), dy = y - (g_fabY + g_fabD * 0.5f);
    return dx * dx + dy * dy <= r * r;
}

// Touch event types of the game's AND_TouchEvent(type, finger, x, y).
// Read out of the original menu's touch hook:  1 = finger UP,  2 = finger DOWN,  3 = MOVE.
enum { kTouchUp = 1, kTouchDown = 2, kTouchMove = 3 };

static void ResetFinger() { g_finger = -1; g_target = 0; g_downId = 0; g_moved = false; }

// returns true when the touch belongs to the menu and must NOT reach the game
static bool OnTouch(int type, int idx, int x, int y) {
    if (type >= 1 && type <= 3) g_dbgRaw[type] = g_dbgRaw[type] + 1;
    if (!g_visible) return false;
    const float fx = (float)x, fy = (float)y;

    if (type == kTouchDown) {
        if (g_finger == idx) ResetFinger();            // we missed its UP: start over
        if (g_finger >= 0) return false;               // one menu finger at a time
        int target = 0, id = 0;
        if (HitFab(fx, fy)) target = 1;
        else if (g_panelValid && InRect(fx, fy, g_pX0, g_pY0, g_pX1, g_pY1)) { target = 2; id = FindId(fx, fy); }
        if (!target) return false;                     // not ours: the game gets it
        g_finger = idx; g_target = target;
        g_downX = g_lastX = fx; g_downY = g_lastY = fy;
        g_startFabX = g_fabX; g_startFabY = g_fabY;
        g_moved = false; g_downId = id;
        g_scrollArea = (target == 2) && InRect(fx, fy, g_cX0, g_cY0, g_cX1, g_cY1);
        g_velY = 0.f; g_fling = 0.f; g_lastMoveT = g_lastEventT = Now();
        g_dbgTouches = g_dbgTouches + 1;
        return true;
    }
    if (idx != g_finger) return false;                 // some other finger: the game's business
    g_lastEventT = Now();

    if (type == kTouchMove) {
        const float mx = fx - g_downX, my = fy - g_downY;
        if (!g_moved && mx * mx + my * my > g_slop * g_slop) g_moved = true;
        if (g_moved) {
            if (g_target == 1) {
                if (!g_lockPos) {
                    g_fabX = Clamp(g_startFabX + mx, 0.f, g_scrW - g_fabD);
                    g_fabY = Clamp(g_startFabY + my, 0.f, g_scrH - g_fabD);
                }
            } else if (g_scrollArea) {
                const double t = Now();
                float dtm = (float)(t - g_lastMoveT);
                g_lastMoveT = t;
                if (dtm < 0.004f) dtm = 0.004f;
                const float dy = fy - g_lastY;
                g_velY = g_velY * 0.6f + Clamp(dy / dtm, -6000.f, 6000.f) * 0.4f;
                pthread_mutex_lock(&g_lock);
                g_scrollDy += dy;
                pthread_mutex_unlock(&g_lock);
            }
        }
        g_lastX = fx; g_lastY = fy;
        return true;
    }

    if (type == kTouchUp) {
        if (!g_moved) {
            if (g_target == 1) g_fabTap = true;
            else {
                const int id = FindId(fx, fy);
                if (id && id == g_downId) PushTap(id);
            }
        } else if (g_target == 1) {
            if (!g_lockPos) g_saveReq = true;          // remember where the button was dropped
        } else if (g_scrollArea && (Now() - g_lastMoveT) < 0.08) {
            g_fling = g_velY;                          // let the list glide on
        }
        ResetFinger();
        return true;
    }
    return false;
}

// called by main.cpp every frame before Draw()
static void SetVisible(bool visible) {
    if (g_visible && !visible && g_finger >= 0) ResetFinger();
    g_visible = visible;
    if (!visible) g_panelValid = false;
}

// ------------------------------------------------------------------ config
static const char* kCfgDirs[2] = {
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/ProMenu",
    "/sdcard/Android/data/com.rockstargames.gtasa/files/ProMenu",
};

static void LoadConfig() {
    char path[320];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s/config.txt", kCfgDirs[i]);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        float fx, fy, a, s; int fade, lock = 0, lang = 0;
        if (fscanf(f, "%f %f %f %f %d %d %d", &fx, &fy, &a, &s, &fade, &lock, &lang) >= 5) {
            g_cfgFx = Clamp(fx, 0.f, 1.f); g_cfgFy = Clamp(fy, 0.f, 1.f);
            g_btnAlpha = Clamp(a, 0.15f, 1.f); g_btnScale = Clamp(s, 0.7f, 1.5f);
            g_fadeIdle = fade != 0;
            g_lockPos = lock != 0;
            g_lang = (lang == 1 && g_thaiOk) ? 1 : 0;
        }
        fclose(f);
        return;
    }
}

static void SaveConfig(float W, float H) {
    char path[320];
    for (int i = 0; i < 2; i++) {
        mkdir(kCfgDirs[i], 0777);
        snprintf(path, sizeof(path), "%s/config.txt", kCfgDirs[i]);
        FILE* f = fopen(path, "w");
        if (!f) continue;
        fprintf(f, "%.5f %.5f %.3f %.3f %d %d %d\n", g_fabX / W, g_fabY / H, g_btnAlpha, g_btnScale, g_fadeIdle ? 1 : 0, g_lockPos ? 1 : 0, g_lang);
        fclose(f);
        return;
    }
}

// ======================================================================= helpers
// ================================================================ language
struct TrPair { const char* en; const char* th; };
static const TrPair kTr[] = {
    {"Player", "ผู้เล่น"}, {"Vehicle", "ยานพาหนะ"}, {"Game", "เกม"}, {"Cheats", "สูตรโกง"}, {"Menu", "เมนู"},
    {"CHEAT MENU", "เมนูสูตรโกง"},
    {"ON", "เปิด"}, {"OFF", "ปิด"}, {"TAP AGAIN", "แตะอีกครั้ง"},
    {"Start playing first", "เข้าเกมก่อน"}, {"Cheat tables not found", "ไม่พบตารางสูตร"},
    {"Tap again to confirm", "แตะอีกครั้งเพื่อยืนยัน"},
    {"Wanted level", "ระดับตำรวจ"}, {"Wanted level set", "ตั้งระดับตำรวจแล้ว"},
    {"Health", "เลือด"}, {"Armour", "เกราะ"}, {"Money", "เงิน"},
    {"Min", "ต่ำสุด"}, {"Default", "ค่าเริ่มต้น"}, {"Max", "สูงสุด"},
    {"Start playing to edit these values", "เข้าเกมก่อนจึงแก้ค่าเหล่านี้ได้"},
    {"Cars", "รถยนต์"}, {"Bikes", "มอเตอร์ไซค์"}, {"Trucks, off-road & special", "รถบรรทุก ออฟโรด และพิเศษ"},
    {"Air", "อากาศยาน"}, {"Sea", "เรือ"},
    {"Set game time", "ตั้งเวลาในเกม"},
    {"Weather and time cheats are in the Cheats tab.", "สูตรอากาศและเวลาอยู่ในแท็บสูตรโกง"},
    {"Cheat tables not found in this game version.", "ไม่พบตารางสูตรในเกมเวอร์ชันนี้"},
    {"Floating button", "ปุ่มลอย"},
    {"Opacity: %d%%", "ความโปร่งใส: %d%%"}, {"Size: %d%%", "ขนาด: %d%%"},
    {"Fade when idle", "จางเมื่อไม่แตะ"}, {"Lock position", "ล็อกตำแหน่ง"}, {"Reset position", "รีเซ็ตตำแหน่ง"},
    {"Language", "ภาษา"}, {"About", "เกี่ยวกับ"},
    {"Game state: %d    Game symbols: %d/9", "สถานะเกม: %d    สัญลักษณ์เกม: %d/9"},
    {"Cheat tables: %s", "ตารางสูตร: %s"}, {"found", "พบ"}, {"missing", "ไม่พบ"},
    {"Touches: %d    Taps: %d", "สัมผัส: %d    แตะ: %d"},
    {"Raw events  up: %d  down: %d  move: %d", "เหตุการณ์ดิบ  ยก: %d  กด: %d  เลื่อน: %d"},
};
// text in the current language; unknown strings fall back to English
static const char* Tr(const char* en) {
    if (g_lang != 1) return en;
    for (size_t i = 0; i < sizeof(kTr) / sizeof(kTr[0]); i++) if (!strcmp(kTr[i].en, en)) return kTr[i].th;
    return en;
}
static const char* CheatLabel(int i) { return g_lang == 1 ? kCheatTh[i] : kCheat[i].label; }
static const char* GroupLabel(int g) { return g_lang == 1 ? kGroupNameTh[g] : kGroupName[g]; }

static void Toast(const char* text) {
    snprintf(g_toast, sizeof(g_toast), "%s", text);
    g_toastT = text[0] ? 1.8f : 0.f;
}

// tap on cheat `idx`: danger cheats need a second tap, everything else runs at once
static void OnCheatTap(int idx) {
    if (!g_canRun) { Toast(Tr("Start playing first")); return; }
    if (!CheatTablesOk()) { Toast(Tr("Cheat tables not found")); return; }
    const CheatDef& c = kCheat[idx];
    if (c.group == kDangerGroup && !(g_armedIdx == idx && g_armedT > 0.f)) {
        g_armedIdx = idx; g_armedT = 2.5f;
        Toast(Tr("Tap again to confirm"));
        return;
    }
    g_armedIdx = -1; g_armedT = 0.f;
    const bool wasOn = StateOf(idx);
    Enqueue(aCheat, idx);
    char msg[80];
    if (g_lang == 1) { if (wasOn) snprintf(msg, sizeof(msg), "ปิดสูตร %s แล้ว", c.name); else snprintf(msg, sizeof(msg), "เปิดสูตร %s แล้ว", c.name); }
    else             { snprintf(msg, sizeof(msg), "Cheat %s %s", c.name, wasOn ? "Deactivated" : "Activated"); }   // same wording as the original
    Toast(msg);
}

static int g_fired[8];
static int g_nFired = 0;
static bool Fired(int id) { for (int i = 0; i < g_nFired; i++) if (g_fired[i] == id) return true; return false; }

static void Reg(ImVec2 p, ImVec2 q, int id) {
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    TapRect r = { fmaxf(p.x, wp.x), fmaxf(p.y, wp.y), fminf(q.x, wp.x + ws.x), fminf(q.y, wp.y + ws.y), id };
    if (r.x1 > r.x0 && r.y1 > r.y0) g_rectsBuild.push_back(r);
}
static bool Pressed(int id) { return g_finger >= 0 && !g_moved && g_downId == id; }

// A button drawn by hand.  It is "tapped" when the raw touch logic reports its id.
static bool Btn(const char* label, ImVec2 size, int id, ImU32 fill, bool enabled = true,
                const char* tag = nullptr, bool tagOn = false) {
    const float sc = g_sc;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const ImVec2 q(p.x + size.x, p.y + size.y);
    if (enabled && id) Reg(p, q, id);

    ImU32 col = (enabled && Pressed(id)) ? IM_COL32(52, 120, 246, 255) : fill;
    if (!enabled) col = IM_COL32(30, 34, 46, 255);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, col, 18.f * sc);

    const ImU32 tc = enabled ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 125, 140, 255);
    const float fs = ImGui::GetFontSize();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float maxW = size.x - 36.f * sc - (tag ? 96.f * sc : 0.f);
    float k = 1.f;
    if (ts.x > maxW && ts.x > 0.f) k = maxW / ts.x;
    const float tw = ts.x * k, th = ts.y * k;
    const float tx = tag ? p.x + 22.f * sc : p.x + (size.x - tw) * 0.5f;
    dl->AddText(ImGui::GetFont(), fs * k, ImVec2(tx, p.y + (size.y - th) * 0.5f), tc, label);

    if (tag) {
        const ImVec2 gs = ImGui::CalcTextSize(tag);
        const float pw = gs.x + 26.f * sc, ph = 42.f * sc;
        const ImVec2 a(q.x - pw - 16.f * sc, p.y + (size.y - ph) * 0.5f);
        dl->AddRectFilled(a, ImVec2(a.x + pw, a.y + ph), tagOn ? IM_COL32(36, 170, 90, 255) : IM_COL32(70, 75, 92, 255), ph * 0.5f);
        dl->AddText(ImVec2(a.x + 13.f * sc, a.y + (ph - gs.y) * 0.5f), IM_COL32(255, 255, 255, 255), tag);
    }
    return enabled && Fired(id);
}

// collapsible section bar; returns true when tapped
static bool Header(const char* label, int count, int id, bool expanded) {
    const float sc = g_sc;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 4.f * sc;
    const float h = 68.f * sc;
    ImGui::Dummy(ImVec2(w, h));
    const ImVec2 q(p.x + w, p.y + h);
    Reg(p, q, id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, Pressed(id) ? IM_COL32(35, 60, 110, 255) : IM_COL32(26, 34, 54, 255), 16.f * sc);
    const float cx = p.x + 36.f * sc, cy = p.y + h * 0.5f, r = 11.f * sc;
    const ImU32 ac = IM_COL32(111, 160, 255, 255);
    if (expanded) dl->AddTriangleFilled(ImVec2(cx - r, cy - r * 0.6f), ImVec2(cx + r, cy - r * 0.6f), ImVec2(cx, cy + r * 0.8f), ac);
    else          dl->AddTriangleFilled(ImVec2(cx - r * 0.6f, cy - r), ImVec2(cx - r * 0.6f, cy + r), ImVec2(cx + r * 0.8f, cy), ac);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + 70.f * sc, p.y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), label);
    if (count > 0) {
        char n[16]; snprintf(n, sizeof(n), "%d", count);
        const ImVec2 ns = ImGui::CalcTextSize(n);
        dl->AddText(ImVec2(q.x - ns.x - 24.f * sc, p.y + (h - ns.y) * 0.5f), IM_COL32(140, 150, 175, 255), n);
    }
    return Fired(id);
}

static float RowWidth() { return ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 4.f * g_sc; }

// value editor:  [--] [-] [ value ] [+] [++]  and a row of preset buttons.
// returns: -2 / -1 / +1 / +2 for the step buttons, 10 + k for preset k, 0 for nothing
static int Stepper(const char* title, int idBase, const char* value, const char* small, const char* big,
                   const char* const* presets, int nPresets, bool enabled) {
    const float sc = g_sc, sp = ImGui::GetStyle().ItemSpacing.x;
    const ImU32 kBtn = IM_COL32(40, 48, 68, 255);
    ImGui::TextColored(ImVec4(0.45f, 0.62f, 1.f, 1.f), "%s", title);
    const float avail = RowWidth();
    const float bw = 120.f * sc, bh = 92.f * sc, vw = avail - 4.f * bw - 4.f * sp;
    char b1[24], b2[24], b3[24], b4[24];
    snprintf(b1, sizeof(b1), "-%s", big);   snprintf(b2, sizeof(b2), "-%s", small);
    snprintf(b3, sizeof(b3), "+%s", small); snprintf(b4, sizeof(b4), "+%s", big);
    int r = 0;
    if (Btn(b1, ImVec2(bw, bh), idBase + 0, kBtn, enabled)) r = -2;
    ImGui::SameLine();
    if (Btn(b2, ImVec2(bw, bh), idBase + 1, kBtn, enabled)) r = -1;
    ImGui::SameLine();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(vw, bh));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + vw, p.y + bh), IM_COL32(16, 20, 30, 255), 18.f * sc);
        const ImVec2 ts = ImGui::CalcTextSize(value);
        dl->AddText(ImVec2(p.x + (vw - ts.x) * 0.5f, p.y + (bh - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), value);
    }
    ImGui::SameLine();
    if (Btn(b3, ImVec2(bw, bh), idBase + 2, kBtn, enabled)) r = 1;
    ImGui::SameLine();
    if (Btn(b4, ImVec2(bw, bh), idBase + 3, kBtn, enabled)) r = 2;
    if (nPresets > 0) {
        const float pw = (avail - sp * (float)(nPresets - 1)) / (float)nPresets;
        for (int k = 0; k < nPresets; k++) {
            if (k) ImGui::SameLine();
            if (Btn(presets[k], ImVec2(pw, 80.f * sc), idBase + 10 + k, kBtn, enabled)) r = 10 + k;
        }
    }
    ImGui::Spacing();
    return r;
}

static void Init(float scale) {
    g_sc = scale;
    g_slop = fmaxf(40.f, 48.f * scale);
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 28.f * scale;
    st.ChildRounding = 20.f * scale;
    st.ScrollbarSize = 8.f * scale;
    st.ScrollbarRounding = 4.f * scale;
    st.ItemSpacing = ImVec2(14.f * scale, 14.f * scale);
    st.WindowBorderSize = 0.f;
    st.ChildBorderSize = 0.f;
    st.Colors[ImGuiCol_ChildBg]       = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_ScrollbarBg]   = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.22f);
    g_expanded[0] = true;        // first cheat group
    g_expanded[20] = true;       // first vehicle section
}

// ========================================================================= pages
static const ImU32 kBtnCol = IM_COL32(40, 48, 68, 255);
static const ImU32 kDangerCol = IM_COL32(86, 44, 52, 255);
static const ImU32 kAccentCol = IM_COL32(52, 120, 246, 255);

static void PagePlayer() {
    const float sc = g_sc, sp = ImGui::GetStyle().ItemSpacing.x;
    char* ped = g_canRun ? PlayerPed() : nullptr;
    char* info = PlayerInfo(ped);
    const bool ok = ped != nullptr;

    // wanted level 0..6
    ImGui::TextColored(ImVec4(0.45f, 0.62f, 1.f, 1.f), "%s", Tr("Wanted level"));
    {
        const float w = (RowWidth() - sp * 6.f) / 7.f;
        for (int lv = 0; lv <= 6; lv++) {
            if (lv) ImGui::SameLine();
            char t[4]; snprintf(t, sizeof(t), "%d", lv);
            if (Btn(t, ImVec2(w, 92.f * sc), 380 + lv, kBtnCol, ok && g_cheatWanted != nullptr)) { Enqueue(aWanted, lv); Toast(Tr("Wanted level set")); }
        }
    }
    ImGui::Spacing();

    float hp = 0.f, mx = 100.f, ar = 0.f; int money = 0;
    if (ped) { hp = *(float*)(ped + kOffHealth); mx = *(float*)(ped + kOffMaxHealth); ar = *(float*)(ped + kOffArmour); }
    if (info) money = *(int*)(info + kOffMoney);
    if (mx < 100.f) mx = 100.f;
    char v[48];

    const char* const kHp[] = {Tr("Min"), Tr("Default"), Tr("Max")};
    snprintf(v, sizeof(v), ok ? "%d / %d" : "-", (int)(hp + 0.5f), (int)(mx + 0.5f));
    int r = Stepper(Tr("Health"), 400, v, "1", "10", kHp, 3, ok);
    if (r == -2) Enqueue(aHealth, 0, hp - 10.f); else if (r == -1) Enqueue(aHealth, 0, hp - 1.f);
    else if (r == 1) Enqueue(aHealth, 0, hp + 1.f); else if (r == 2) Enqueue(aHealth, 0, hp + 10.f);
    else if (r == 10) Enqueue(aHealth, 0, 1.f); else if (r == 11) Enqueue(aHealth, 0, 100.f); else if (r == 12) Enqueue(aHealth, 0, mx);

    snprintf(v, sizeof(v), ok ? "%d / 100" : "-", (int)(ar + 0.5f));
    r = Stepper(Tr("Armour"), 430, v, "1", "10", kHp, 3, ok);
    if (r == -2) Enqueue(aArmour, 0, ar - 10.f); else if (r == -1) Enqueue(aArmour, 0, ar - 1.f);
    else if (r == 1) Enqueue(aArmour, 0, ar + 1.f); else if (r == 2) Enqueue(aArmour, 0, ar + 10.f);
    else if (r == 10) Enqueue(aArmour, 0, 0.f); else if (r == 11) Enqueue(aArmour, 0, 0.f); else if (r == 12) Enqueue(aArmour, 0, 100.f);

    const char* const kMoney[] = {"$0", "$10k", "$1M", Tr("Max")};
    snprintf(v, sizeof(v), info ? "$%d" : "-", money);
    r = Stepper(Tr("Money"), 460, v, "1k", "100k", kMoney, 4, info != nullptr);
    const float m = (float)money;
    if (r == -2) Enqueue(aMoney, 0, m - 100000.f); else if (r == -1) Enqueue(aMoney, 0, m - 1000.f);
    else if (r == 1) Enqueue(aMoney, 0, m + 1000.f); else if (r == 2) Enqueue(aMoney, 0, m + 100000.f);
    else if (r == 10) Enqueue(aMoney, 0, 0.f); else if (r == 11) Enqueue(aMoney, 0, 10000.f);
    else if (r == 12) Enqueue(aMoney, 0, 1000000.f); else if (r == 13) Enqueue(aMoney, 0, 99999999.f);

    if (!ok) ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1), "%s", Tr("Start playing to edit these values"));
    ImGui::Dummy(ImVec2(1.f, 90.f * sc));
}

static void PageVehicle() {
    const float sc = g_sc;
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float colW = (RowWidth() - sp * 2.f) / 3.f;
    const char* lastSec = nullptr;
    int secIdx = -1, n = 0;
    bool open = true;
    for (int i = 0; i < kNumVeh; i++) {
        const Veh& v = kVeh[i];
        if (!lastSec || strcmp(v.section, lastSec) != 0) {
            lastSec = v.section; secIdx++; n = 0;
            int count = 0; for (int j = i; j < kNumVeh && !strcmp(kVeh[j].section, v.section); j++) count++;
            open = g_expanded[20 + secIdx];
            if (Header(Tr(v.section), count, 320 + secIdx, open)) g_expanded[20 + secIdx] = !g_expanded[20 + secIdx];
        }
        if (!open) continue;
        if (n % 3) ImGui::SameLine();
        if (Btn(v.label, ImVec2(colW, 96.f * sc), 2000 + i, kBtnCol, g_vehicleCheat != nullptr)) {
            if (!g_canRun) Toast(Tr("Start playing first"));
            else { Enqueue(aVehicle, v.model); Toast(v.label); }
        }
        n++;
    }
    ImGui::Dummy(ImVec2(1.f, 90.f * sc));
}

static void PageGame() {
    const float sc = g_sc, sp = ImGui::GetStyle().ItemSpacing.x;
    ImGui::TextColored(ImVec4(0.45f, 0.62f, 1.f, 1.f), "%s", Tr("Set game time"));
    const float w = (RowWidth() - sp * 3.f) / 4.f;
    static const char* const kT[] = {"00:00", "06:00", "12:00", "18:00"};
    for (int k = 0; k < 4; k++) {
        if (k) ImGui::SameLine();
        if (Btn(kT[k], ImVec2(w, 96.f * sc), 340 + k, kBtnCol, g_setClock != nullptr)) {
            if (!g_canRun) Toast(Tr("Start playing first")); else { Enqueue(aClock, k * 6); Toast(kT[k]); }
        }
    }
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1), "%s", Tr("Weather and time cheats are in the Cheats tab."));
}

static void PageCheats() {
    const float sc = g_sc, sp = ImGui::GetStyle().ItemSpacing.x;
    if (!CheatTablesOk()) {
        ImGui::TextColored(ImVec4(1.f, 0.55f, 0.45f, 1), "%s", Tr("Cheat tables not found in this game version."));
        return;
    }
    const float colW = (RowWidth() - sp) * 0.5f;
    for (int g = 0; g < kNumGroups; g++) {
        int count = 0;
        for (int i = 0; i < kNumCheats; i++) if (kCheat[i].group == g) count++;
        if (Header(GroupLabel(g), count, 300 + g, g_expanded[g])) g_expanded[g] = !g_expanded[g];
        if (!g_expanded[g]) continue;
        int n = 0;
        for (int i = 0; i < kNumCheats; i++) {
            if (kCheat[i].group != g) continue;
            if (n % 2) ImGui::SameLine();
            const bool armed = (g_armedIdx == i && g_armedT > 0.f);
            const char* tag = nullptr; bool on = false;
            if (!armed && IsToggle(i)) { on = StateOf(i); tag = on ? Tr("ON") : Tr("OFF"); }
            const ImU32 fill = armed ? IM_COL32(190, 70, 40, 255) : (g == kDangerGroup ? kDangerCol : kBtnCol);
            if (Btn(armed ? Tr("TAP AGAIN") : CheatLabel(i), ImVec2(colW, 96.f * sc), 1000 + i, fill, true, tag, on)) OnCheatTap(i);
            n++;
        }
    }
    ImGui::Dummy(ImVec2(1.f, 90.f * sc));
}

static void PageMenu() {
    const float sc = g_sc, sp = ImGui::GetStyle().ItemSpacing.x;
    const float sq = 88.f * sc, wide = RowWidth(), btnH = 96.f * sc;
    const ImVec4 hdr(0.45f, 0.62f, 1.f, 1.f), dim(0.55f, 0.60f, 0.70f, 1);

    ImGui::TextColored(hdr, "%s", g_thaiOk ? "Language / ภาษา" : "Language");
    {
        const float lw = (wide - sp) * 0.5f;
        if (Btn("English", ImVec2(lw, btnH), 40, g_lang == 0 ? kAccentCol : kBtnCol)) { g_lang = 0; g_saveReq = true; }
        ImGui::SameLine();
        if (Btn(g_thaiOk ? "ไทย" : "Thai", ImVec2(lw, btnH), 41, g_lang == 1 ? kAccentCol : kBtnCol, g_thaiOk)) { g_lang = 1; g_saveReq = true; }
        if (!g_thaiOk) ImGui::TextColored(dim, "Thai font not found. Put a Thai .ttf at files/ProMenu/font_th.ttf");
    }
    ImGui::Spacing();

    ImGui::TextColored(hdr, "%s", Tr("Floating button"));
    ImGui::Text(Tr("Opacity: %d%%"), (int)(g_btnAlpha * 100.f + 0.5f));
    if (Btn("-", ImVec2(sq, sq), 30, kBtnCol)) { g_btnAlpha = Clamp(g_btnAlpha - 0.10f, 0.15f, 1.f); g_saveReq = true; }
    ImGui::SameLine();
    if (Btn("+", ImVec2(sq, sq), 31, kBtnCol)) { g_btnAlpha = Clamp(g_btnAlpha + 0.10f, 0.15f, 1.f); g_saveReq = true; }
    ImGui::Text(Tr("Size: %d%%"), (int)(g_btnScale * 100.f + 0.5f));
    if (Btn("-", ImVec2(sq, sq), 32, kBtnCol)) { g_btnScale = Clamp(g_btnScale - 0.10f, 0.70f, 1.50f); g_saveReq = true; }
    ImGui::SameLine();
    if (Btn("+", ImVec2(sq, sq), 33, kBtnCol)) { g_btnScale = Clamp(g_btnScale + 0.10f, 0.70f, 1.50f); g_saveReq = true; }
    if (Btn(Tr("Fade when idle"), ImVec2(wide, btnH), 35, kBtnCol, true, g_fadeIdle ? Tr("ON") : Tr("OFF"), g_fadeIdle)) { g_fadeIdle = !g_fadeIdle; g_saveReq = true; }
    if (Btn(Tr("Lock position"), ImVec2(wide, btnH), 36, kBtnCol, true, g_lockPos ? Tr("ON") : Tr("OFF"), g_lockPos)) { g_lockPos = !g_lockPos; g_saveReq = true; }
    if (Btn(Tr("Reset position"), ImVec2(wide, btnH), 34, kBtnCol)) { g_posInit = false; g_cfgFx = g_cfgFy = -1.f; g_saveReq = true; }
    ImGui::Spacing();

    ImGui::TextColored(hdr, "%s", Tr("About"));
    ImGui::TextColored(dim, "ProMenu 0.5");
    ImGui::TextColored(dim, Tr("Game state: %d    Game symbols: %d/9"), g_dbgGameState, g_dbgFound);
    ImGui::TextColored(dim, Tr("Cheat tables: %s"), CheatTablesOk() ? Tr("found") : Tr("missing"));
    ImGui::TextColored(dim, Tr("Touches: %d    Taps: %d"), (int)g_dbgTouches, (int)g_dbgTaps);
    ImGui::TextColored(dim, Tr("Raw events  up: %d  down: %d  move: %d"), (int)g_dbgRaw[1], (int)g_dbgRaw[2], (int)g_dbgRaw[3]);
    ImGui::Dummy(ImVec2(1.f, 60.f * sc));
}

// ======================================================================= draw
static void Draw(float W, float H, float dt) {
    const float sc = g_sc;
    g_scrW = W; g_scrH = H;
    const float D = 110.f * sc * g_btnScale;
    g_fabD = D;

    if (!g_posInit) {
        g_fabX = (g_cfgFx >= 0.f) ? g_cfgFx * W : W - D - 24.f * sc;
        g_fabY = (g_cfgFy >= 0.f) ? g_cfgFy * H : H * 0.30f;
        g_posInit = true;
    }
    g_fabX = Clamp(g_fabX, 0.f, W - D);
    g_fabY = Clamp(g_fabY, 0.f, H - D);

    // collect what the touch side reported since the last frame
    float scrollDy = 0.f;
    pthread_mutex_lock(&g_lock);
    g_nFired = 0;
    while (g_tqH != g_tqT && g_nFired < 8) { g_fired[g_nFired++] = g_tapQ[g_tqH]; g_tqH = (g_tqH + 1) % 8; }
    scrollDy = g_scrollDy; g_scrollDy = 0.f;
    pthread_mutex_unlock(&g_lock);
    g_rectsBuild.clear();
    g_dbgTaps = g_dbgTaps + g_nFired;
    if (g_armedT > 0.f) { g_armedT -= dt; if (g_armedT <= 0.f) g_armedIdx = -1; }

    if (g_finger >= 0 && Now() - g_lastEventT > 5.0) ResetFinger();      // lost UP event: never stay stuck
    if (g_fabTap) { g_fabTap = false; g_open = !g_open; Toast(""); }
    if (g_saveReq) { g_saveReq = false; SaveConfig(W, H); }

    // ---- floating button (always on top, stays exactly where you put it) -----
    if (g_finger >= 0 || g_open) g_idle = 0.f; else g_idle += dt;
    const float fadeTarget = (g_fadeIdle && g_idle > 3.f) ? 0.45f : 1.f;
    g_fade += (fadeTarget - g_fade) * fminf(1.f, dt * 5.f);
    const float alpha = g_btnAlpha * g_fade;
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const bool down = (g_finger >= 0 && g_target == 1);
        const ImVec2 c(g_fabX + D * 0.5f, g_fabY + D * 0.5f);
        const float r = down ? D * 0.47f : D * 0.5f;
        #define A(base) ((int)((base) * alpha))
        fg->AddCircleFilled(ImVec2(c.x, c.y + 5.f * sc), r + sc, IM_COL32(0, 0, 0, A(90)), 48);
        ImU32 body = g_open ? IM_COL32(235, 87, 87, A(255)) : IM_COL32(52, 120, 246, A(255));
        if (down) body = g_open ? IM_COL32(200, 60, 60, A(255)) : IM_COL32(35, 95, 215, A(255));
        fg->AddCircleFilled(c, r, body, 48);
        fg->AddCircle(c, r - 2.f * sc, IM_COL32(255, 255, 255, A(70)), 48, 2.f * sc);
        const ImU32 ic = IM_COL32(255, 255, 255, A(255));
        const float s = r * 0.38f, th = 5.f * sc;
        if (g_open) {
            fg->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), ic, th);
            fg->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), ic, th);
        } else {
            for (int i = -1; i <= 1; i++)
                fg->AddLine(ImVec2(c.x - s, c.y + i * s * 0.8f), ImVec2(c.x + s, c.y + i * s * 0.8f), ic, th);
        }
        #undef A
    }

    // ---- panel --------------------------------------------------------------
    g_openAnim += ((g_open ? 1.f : 0.f) - g_openAnim) * fminf(1.f, dt * 14.f);
    if (g_toastT > 0.f) g_toastT -= dt;
    if (!g_open && g_openAnim < 0.02f) {
        g_panelValid = false;
        pthread_mutex_lock(&g_lock); g_rects.clear(); pthread_mutex_unlock(&g_lock);
        return;
    }

    const float gap = 18.f * sc;
    const bool fabRight = (g_fabX + D * 0.5f) > W * 0.5f;
    const float room = fabRight ? (g_fabX - gap) : (W - (g_fabX + D) - gap);
    float pw, px;
    if (room - gap >= 560.f * sc) {                                   // sit beside the button
        pw = fminf(1280.f * sc, room - gap);
        px = fabRight ? (g_fabX - gap - pw) : (g_fabX + D + gap);
    } else {                                                           // button is in the way: centre it
        pw = fminf(W * 0.9f, 1280.f * sc);
        px = (W - pw) * 0.5f;
    }
    const float ph = fminf(H * 0.92f, 960.f * sc);
    const float py = (H - ph) * 0.5f;
    px += (fabRight ? 1.f : -1.f) * (1.f - g_openAnim) * 60.f * sc;    // slide in
    g_pX0 = px; g_pY0 = py; g_pX1 = px + pw; g_pY1 = py + ph;
    g_panelValid = true;

    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(px, py));
    ImGui::SetNextWindowSize(ImVec2(pw, ph));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_openAnim);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f * sc, 12.f * sc));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.11f, 0.95f));
    ImGui::Begin("##panel", nullptr, panelFlags);

    const float hh = 84.f * sc;                      // header height
    const float btnH = 96.f * sc;                    // sidebar button height
    const ImU32 kAccent = IM_COL32(52, 120, 246, 255);

    ImGui::SetCursorPos(ImVec2(18.f * sc, (hh - ImGui::GetFontSize()) * 0.5f));
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", Tr("CHEAT MENU"));
    {
        const float cs = 90.f * sc;
        ImGui::SetCursorPos(ImVec2(pw - cs - 26.f * sc, (hh - cs) * 0.5f));
        if (Btn("X", ImVec2(cs, cs), 20, IM_COL32(200, 60, 60, 255))) g_open = false;
    }

    ImGui::SetCursorPos(ImVec2(16.f * sc, hh));
    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float sideW = fminf(280.f * sc, pw * 0.27f);

    // sidebar
    ImGui::BeginChild("##side", ImVec2(sideW, bodyH), 0, ImGuiWindowFlags_NoScrollbar);
    for (int t = 0; t < kTabCount; t++)
        if (Btn(Tr(kTabs[t]), ImVec2(ImGui::GetContentRegionAvail().x, btnH), 1 + t, g_tab == t ? kAccent : kBtnCol)) g_tab = t;
    ImGui::EndChild();

    ImGui::SameLine();

    // content (scrolls by dragging, with a little glide)
    ImGui::BeginChild("##content", ImVec2(0, bodyH), 0, 0);
    {
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        g_cX0 = wp.x; g_cY0 = wp.y; g_cX1 = wp.x + ws.x; g_cY1 = wp.y + ws.y;
        if (g_tab != g_lastTab) { ImGui::SetScrollY(0.f); g_lastTab = g_tab; g_fling = 0.f; }
        else if (scrollDy != 0.f) { ImGui::SetScrollY(ImGui::GetScrollY() - scrollDy); g_fling = 0.f; }
        else if (g_finger >= 0 && g_scrollArea) g_fling = 0.f;
        else if (fabsf(g_fling) > 40.f) { ImGui::SetScrollY(ImGui::GetScrollY() - g_fling * dt); g_fling = g_fling * expf(-dt * 3.5f); }
        else g_fling = 0.f;
    }
    switch (g_tab) {
        case kTabPlayer:  PagePlayer();  break;
        case kTabVehicle: PageVehicle(); break;
        case kTabGame:    PageGame();    break;
        case kTabCheats:  PageCheats();  break;
        default:          PageMenu();    break;
    }
    ImGui::EndChild();

    // toast
    if (g_toastT > 0.f && g_toast[0]) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ts = ImGui::CalcTextSize(g_toast);
        const float a = Clamp(g_toastT / 0.4f, 0.f, 1.f);
        const float bx = wp.x + (pw - ts.x) * 0.5f, by = wp.y + ph - 76.f * sc;
        dl->AddRectFilled(ImVec2(bx - 26.f * sc, by - 12.f * sc), ImVec2(bx + ts.x + 26.f * sc, by + ts.y + 12.f * sc),
                          IM_COL32(20, 130, 80, (int)(235 * a)), 20.f * sc);
        dl->AddText(ImVec2(bx, by), IM_COL32(255, 255, 255, (int)(255 * a)), g_toast);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // publish this frame's tappable rectangles to the touch side
    pthread_mutex_lock(&g_lock);
    g_rects.swap(g_rectsBuild);
    pthread_mutex_unlock(&g_lock);
}

} // namespace UI
