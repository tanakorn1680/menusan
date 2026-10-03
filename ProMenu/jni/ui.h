#pragma once
// Touch-first overlay: floating circle button + cheat panel.
//
// ImGui is used for DRAWING ONLY.  Tap, drag and scroll are handled right here from
// the raw touch events, so nothing depends on ImGui's mouse click state machine
// (which needs several frames per tap and is easily broken by finger jitter).
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

// ---------------------------------------------------------------- cheats
// Calls into the game's own CCheat functions (same code the typed cheats run).
// Every symbol name below was checked against the original ARM CheatMenu binary.
enum { kCall0 = 0, kCallInt = 1, kClock = 2, kGod = 3 };
struct Cheat { int tab; const char* section; const char* label; const char* sym; int arg; int kind; void* fn; };

static Cheat g_cheats[] = {
#include "cheat_table.inc"
};
static const int kCheatCount = (int)(sizeof(g_cheats) / sizeof(g_cheats[0]));
static const char* kTabs[] = {"Player", "Vehicles", "Weapons", "World", "Fun", "Settings"};
static const int kTabCount = 6;
static const int kSettingsTab = 5;

static unsigned char* g_pCurrentDay = nullptr;   // CClock::CurrentDay
static bool* g_pGodFlag = nullptr;               // CPlayerPed::bDebugPlayerInvincible

static int ResolveCheats(uintptr_t (*lookup)(const char*)) {
    int found = 0;
    for (int i = 0; i < kCheatCount; i++) {
        uintptr_t a = lookup(g_cheats[i].sym);
        g_cheats[i].fn = (void*)a;
        if (a) found++;
    }
    g_pCurrentDay = (unsigned char*)lookup("_ZN6CClock10CurrentDayE");
    g_pGodFlag = (bool*)lookup("_ZN10CPlayerPed22bDebugPlayerInvincibleE");
    return found;
}

// cheats are queued from the UI and executed inside the game's update tick
static const int kQueueSize = 16;
static int g_queue[kQueueSize];
static volatile int g_qHead = 0, g_qTail = 0;

static void Enqueue(int cheatIndex) {
    int next = (g_qTail + 1) % kQueueSize;
    if (next == g_qHead) return;
    g_queue[g_qTail] = cheatIndex;
    g_qTail = next;
}

static void RunQueued(bool gameIsRunning) {
    while (g_qHead != g_qTail) {
        int i = g_queue[g_qHead];
        g_qHead = (g_qHead + 1) % kQueueSize;
        if (!gameIsRunning) continue;
        const Cheat& c = g_cheats[i];
        if (!c.fn) continue;
        switch (c.kind) {
            case kCall0:
            case kGod:     ((void (*)())c.fn)(); break;
            case kCallInt: ((void (*)(int))c.fn)(c.arg); break;
            case kClock: {
                unsigned char day = g_pCurrentDay ? *g_pCurrentDay : 0;
                ((void (*)(unsigned char, unsigned char, unsigned char))c.fn)((unsigned char)c.arg, 0, day);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- state
static float g_sc = 1.f;                          // UI scale (screen height / 1080)
static float g_slop = 36.f;                       // finger jitter allowed before a tap becomes a drag
static float g_scrW = 0.f, g_scrH = 0.f, g_fabD = 100.f;
static volatile float g_fabX = 0.f, g_fabY = 0.f; // top-left of the floating button (written by touch thread while dragging)
static bool g_posInit = false;
static float g_cfgFx = -1.f, g_cfgFy = -1.f;      // saved position as a fraction of the screen
static bool g_open = false;
static float g_openAnim = 0.f, g_idle = 0.f, g_fade = 1.f;
static float g_btnAlpha = 0.92f, g_btnScale = 1.f;
static bool g_fadeIdle = true;
static int g_tab = 0;
static char g_toast[64] = "";
static float g_toastT = 0.f;

// shown in Settings, filled in by main.cpp
static int g_dbgGameState = -1, g_dbgCheatsFound = 0;
static bool g_canRun = false;                     // game is in "playing" state
static volatile int g_dbgTouches = 0, g_dbgTaps = 0;

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
static double g_lastMoveT = 0.0;
static volatile bool g_fabTap = false, g_saveReq = false;
static int g_tapQ[8];
static volatile int g_tqH = 0, g_tqT = 0;

static double Now() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
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

// returns true when the touch belongs to the menu and must NOT reach the game
// type: 1 = down, 2 = move, 3 = up  (same codes the original menu uses)
static bool OnTouch(int type, int idx, int x, int y) {
    if (!g_visible) return false;
    const float fx = (float)x, fy = (float)y;

    if (type == 1) {
        if (g_finger >= 0) return false;
        int target = 0, id = 0;
        if (HitFab(fx, fy)) target = 1;
        else if (g_panelValid && InRect(fx, fy, g_pX0, g_pY0, g_pX1, g_pY1)) { target = 2; id = FindId(fx, fy); }
        if (!target) return false;
        g_finger = idx; g_target = target;
        g_downX = g_lastX = fx; g_downY = g_lastY = fy;
        g_startFabX = g_fabX; g_startFabY = g_fabY;
        g_moved = false; g_downId = id;
        g_scrollArea = (target == 2) && InRect(fx, fy, g_cX0, g_cY0, g_cX1, g_cY1);
        g_velY = 0.f; g_fling = 0.f; g_lastMoveT = Now();
        g_dbgTouches = g_dbgTouches + 1;
        return true;
    }
    if (idx != g_finger) return false;

    if (type == 2) {
        const float mx = fx - g_downX, my = fy - g_downY;
        if (!g_moved && mx * mx + my * my > g_slop * g_slop) g_moved = true;
        if (g_moved) {
            if (g_target == 1) {
                g_fabX = Clamp(g_startFabX + mx, 0.f, g_scrW - g_fabD);
                g_fabY = Clamp(g_startFabY + my, 0.f, g_scrH - g_fabD);
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

    if (type == 3) {
        if (!g_moved) {
            if (g_target == 1) g_fabTap = true;
            else {
                const int id = FindId(fx, fy);
                if (id && id == g_downId) PushTap(id);
            }
        } else if (g_target == 1) {
            g_saveReq = true;                          // remember where the button was dropped
        } else if (g_scrollArea && (Now() - g_lastMoveT) < 0.08) {
            g_fling = g_velY;                          // let the list glide on
        }
        g_finger = -1; g_target = 0; g_downId = 0;
        return true;
    }
    return false;
}

// called by main.cpp every frame before Draw()
static void SetVisible(bool visible) {
    if (g_visible && !visible && g_finger >= 0) { g_finger = -1; g_target = 0; g_downId = 0; }
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
        float fx, fy, a, s; int fade;
        if (fscanf(f, "%f %f %f %f %d", &fx, &fy, &a, &s, &fade) == 5) {
            g_cfgFx = Clamp(fx, 0.f, 1.f); g_cfgFy = Clamp(fy, 0.f, 1.f);
            g_btnAlpha = Clamp(a, 0.15f, 1.f); g_btnScale = Clamp(s, 0.7f, 1.5f);
            g_fadeIdle = fade != 0;
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
        fprintf(f, "%.5f %.5f %.3f %.3f %d\n", g_fabX / W, g_fabY / H, g_btnAlpha, g_btnScale, g_fadeIdle ? 1 : 0);
        fclose(f);
        return;
    }
}

static void Init(float scale) {
    g_sc = scale;
    g_slop = 36.f * scale;
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
}

// ---------------------------------------------------------------- widgets
static int g_fired[8];
static int g_nFired = 0;
static bool Fired(int id) { for (int i = 0; i < g_nFired; i++) if (g_fired[i] == id) return true; return false; }

static void Toast(const char* text) {
    snprintf(g_toast, sizeof(g_toast), "%s", text);
    g_toastT = text[0] ? 1.6f : 0.f;
}

// A button drawn by hand.  It is "tapped" when the raw touch logic above reports its id.
static bool Btn(const char* label, ImVec2 size, int id, ImU32 fill, bool enabled = true,
                const char* tag = nullptr, bool tagOn = false) {
    const float sc = g_sc;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const ImVec2 q(p.x + size.x, p.y + size.y);

    if (enabled && id) {
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        TapRect r = { fmaxf(p.x, wp.x), fmaxf(p.y, wp.y), fminf(q.x, wp.x + ws.x), fminf(q.y, wp.y + ws.y), id };
        if (r.x1 > r.x0 && r.y1 > r.y0) g_rectsBuild.push_back(r);
    }

    const bool pressed = enabled && g_finger >= 0 && !g_moved && g_downId == id;
    ImU32 col = pressed ? IM_COL32(52, 120, 246, 255) : fill;
    if (!enabled) col = IM_COL32(30, 34, 46, 255);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, col, 18.f * sc);

    const ImU32 tc = enabled ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 125, 140, 255);
    const float fs = ImGui::GetFontSize();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float maxW = size.x - 36.f * sc - (tag ? 100.f * sc : 0.f);
    float k = 1.f;
    if (ts.x > maxW && ts.x > 0.f) k = maxW / ts.x;
    const float tw = ts.x * k, th = ts.y * k;
    const float tx = tag ? p.x + 22.f * sc : p.x + (size.x - tw) * 0.5f;
    dl->AddText(ImGui::GetFont(), fs * k, ImVec2(tx, p.y + (size.y - th) * 0.5f), tc, label);

    if (tag) {
        const ImVec2 gs = ImGui::CalcTextSize(tag);
        const float pw = gs.x + 28.f * sc, ph = 42.f * sc;
        const ImVec2 a(q.x - pw - 18.f * sc, p.y + (size.y - ph) * 0.5f);
        dl->AddRectFilled(a, ImVec2(a.x + pw, a.y + ph), tagOn ? IM_COL32(36, 170, 90, 255) : IM_COL32(70, 75, 92, 255), ph * 0.5f);
        dl->AddText(ImVec2(a.x + 14.f * sc, a.y + (ph - gs.y) * 0.5f), IM_COL32(255, 255, 255, 255), tag);
    }
    return enabled && Fired(id);
}

static void Section(const char* text) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.62f, 1.f, 1.f), "%s", text);
}

static void OnCheatPressed(int i) {
    if (!g_canRun) { Toast("Start playing first"); return; }
    Enqueue(i);
    Toast(g_cheats[i].label);
}

// ------------------------------------------------------------------- draw
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
    const float btnH = 104.f * sc;                   // standard button height
    const ImU32 kBtn = IM_COL32(40, 48, 68, 255);
    const ImU32 kAccent = IM_COL32(52, 120, 246, 255);

    ImGui::SetCursorPos(ImVec2(18.f * sc, (hh - ImGui::GetFontSize()) * 0.5f));
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "CHEAT MENU");
    {
        const float cs = 76.f * sc;
        ImGui::SetCursorPos(ImVec2(pw - cs - 26.f * sc, (hh - cs) * 0.5f));
        if (Btn("X", ImVec2(cs, cs), 20, IM_COL32(200, 60, 60, 255))) g_open = false;
    }

    ImGui::SetCursorPos(ImVec2(16.f * sc, hh));
    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float sideW = fminf(280.f * sc, pw * 0.27f);

    // sidebar
    ImGui::BeginChild("##side", ImVec2(sideW, bodyH), 0, ImGuiWindowFlags_NoScrollbar);
    for (int t = 0; t < kTabCount; t++)
        if (Btn(kTabs[t], ImVec2(ImGui::GetContentRegionAvail().x, btnH), 1 + t, g_tab == t ? kAccent : kBtn)) g_tab = t;
    ImGui::EndChild();

    ImGui::SameLine();

    // content (scrolls by dragging, with a little glide)
    ImGui::BeginChild("##content", ImVec2(0, bodyH), 0, 0);
    {
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        g_cX0 = wp.x; g_cY0 = wp.y; g_cX1 = wp.x + ws.x; g_cY1 = wp.y + ws.y;
        if (scrollDy != 0.f) { ImGui::SetScrollY(ImGui::GetScrollY() - scrollDy); g_fling = 0.f; }
        else if (g_finger >= 0 && g_scrollArea) g_fling = 0.f;
        else if (fabsf(g_fling) > 40.f) { ImGui::SetScrollY(ImGui::GetScrollY() - g_fling * dt); g_fling = g_fling * expf(-dt * 3.5f); }
        else g_fling = 0.f;
    }

    if (g_tab != kSettingsTab) {
        const int cols = (g_tab == 1) ? 3 : 2;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float availW = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 4.f * sc;
        const float colW = (availW - spacing * (float)(cols - 1)) / (float)cols;
        const char* lastSec = nullptr;
        int n = 0;
        for (int i = 0; i < kCheatCount; i++) {
            Cheat& c = g_cheats[i];
            if (c.tab != g_tab) continue;
            if (c.section && (!lastSec || strcmp(c.section, lastSec) != 0)) { Section(c.section); lastSec = c.section; n = 0; }
            if (n % cols) ImGui::SameLine();
            const char* tag = nullptr; bool tagOn = false;
            if (c.kind == kGod && g_pGodFlag) { tagOn = *g_pGodFlag; tag = tagOn ? "ON" : "OFF"; }
            if (Btn(c.label, ImVec2(colW, btnH), 100 + i, kBtn, c.fn != nullptr, tag, tagOn)) OnCheatPressed(i);
            n++;
        }
        ImGui::Dummy(ImVec2(1.f, 90.f * sc));       // room for the toast
    } else {
        const float sq = 88.f * sc;
        const float wide = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 4.f * sc;
        ImGui::Text("Button opacity: %d%%", (int)(g_btnAlpha * 100.f + 0.5f));
        if (Btn("-", ImVec2(sq, sq), 30, kBtn)) { g_btnAlpha = Clamp(g_btnAlpha - 0.10f, 0.15f, 1.f); g_saveReq = true; }
        ImGui::SameLine();
        if (Btn("+", ImVec2(sq, sq), 31, kBtn)) { g_btnAlpha = Clamp(g_btnAlpha + 0.10f, 0.15f, 1.f); g_saveReq = true; }
        ImGui::Spacing();
        ImGui::Text("Button size: %d%%", (int)(g_btnScale * 100.f + 0.5f));
        if (Btn("-", ImVec2(sq, sq), 32, kBtn)) { g_btnScale = Clamp(g_btnScale - 0.10f, 0.70f, 1.50f); g_saveReq = true; }
        ImGui::SameLine();
        if (Btn("+", ImVec2(sq, sq), 33, kBtn)) { g_btnScale = Clamp(g_btnScale + 0.10f, 0.70f, 1.50f); g_saveReq = true; }
        ImGui::Spacing();
        if (Btn("Fade button when idle", ImVec2(wide, btnH), 35, kBtn, true, g_fadeIdle ? "ON" : "OFF", g_fadeIdle)) { g_fadeIdle = !g_fadeIdle; g_saveReq = true; }
        if (Btn("Reset button position", ImVec2(wide, btnH), 34, kBtn)) { g_posInit = false; g_cfgFx = g_cfgFy = -1.f; g_saveReq = true; }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1), "ProMenu 0.2");
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1), "Game state: %d    Cheats found: %d/%d", g_dbgGameState, g_dbgCheatsFound, kCheatCount);
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1), "Touches: %d    Taps: %d", (int)g_dbgTouches, (int)g_dbgTaps);
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
