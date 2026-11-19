/* Hallmark · studied-DNA (source: https://yoyu.design/ via Awwwards MCP)
 * paper: #F7F8FA (architectural drafting grid #E4E7EE) · surface: #FFFFFF · ink: #111111
 * accent: #FF4130 (vermilion <=5% footprint) · status: #00B894 (mint) / #0EA5E9 (cyan)
 * display: VT323 / IBM Plex Mono · body: Yu Gothic UI / Segoe UI · corners: sharp 0px architectural
 * controls: direct segmented pickers (zero blind cycling) + 420ms debounced apply + 150ms live sync
 * pre-emit critique: P5 H5 E5 S5 R5 V5
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ghost_core.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif

// ============================================================================
// Locked Design Tokens (Light Drafting Paper & Dark Obsidian Blueprint)
// ============================================================================
struct Palette {
    UINT32 paper_bg;
    UINT32 grid_line;
    UINT32 grid_major;
    UINT32 surface;
    UINT32 surface_muted;
    UINT32 ink;
    UINT32 ink_on_active;
    UINT32 ink_muted;
    UINT32 ink_subtle;
    UINT32 border;
    UINT32 border_strong;
    UINT32 shadow_ink;
    UINT32 ghost_outline;
    UINT32 ghost_fill;
    UINT32 ghost_eyes;
    UINT32 ghost_shadow;
    UINT32 accent_vermilion;
    UINT32 pixel_mint;
    UINT32 pixel_cyan;
};

constexpr Palette PALETTE_LIGHT = {
    0xF7F8FA, // paper_bg
    0xE4E7EE, // grid_line
    0xCBD1DF, // grid_major
    0xFFFFFF, // surface
    0xEFF1F5, // surface_muted
    0x111111, // ink
    0xFFFFFF, // ink_on_active
    0x5E636E, // ink_muted
    0x858890, // ink_subtle
    0xD4D8E2, // border
    0x111111, // border_strong
    0x111111, // shadow_ink
    0x111111, // ghost_outline
    0xFFFFFF, // ghost_fill
    0x111111, // ghost_eyes
    0xCBD1DF, // ghost_shadow
    0xFF4130, // accent_vermilion
    0x00B894, // pixel_mint
    0x0EA5E9  // pixel_cyan
};

constexpr Palette PALETTE_DARK = {
    0x0E0F12, // paper_bg (Obsidian drafting surface)
    0x181B22, // grid_line (16px blueprint grid)
    0x2B303C, // grid_major (64px crosshairs)
    0x16181F, // surface (elevated tile via lightness)
    0x111318, // surface_muted (recessed inactive tile)
    0xF2F4F8, // ink (crisp off-white primary ink & active bg)
    0x0E0F12, // ink_on_active (obsidian text on active bright fill)
    0x9DA4B4, // ink_muted (WCAG AA >7.5:1)
    0x6E7585, // ink_subtle
    0x272B36, // border
    0xF2F4F8, // border_strong
    0x3B4252, // shadow_ink (crisp architectural offset step)
    0xF2F4F8, // ghost_outline (luminous bright pixel ghost)
    0xF2F4F8, // ghost_fill
    0x0E0F12, // ghost_eyes
    0x060709, // ghost_shadow
    0xFF4B3A, // accent_vermilion
    0x00D2A0, // pixel_mint
    0x38BDF8  // pixel_cyan
};

static inline const Palette& active_palette(bool dark) {
    return dark ? PALETTE_DARK : PALETTE_LIGHT;
}

namespace Tok {
    constexpr UINT32 ACCENT_VERMILION = PALETTE_LIGHT.accent_vermilion;
    constexpr UINT32 PIXEL_MINT       = PALETTE_LIGHT.pixel_mint;
    constexpr UINT32 PIXEL_CYAN       = PALETTE_LIGHT.pixel_cyan;
    constexpr UINT32 INK              = PALETTE_LIGHT.ink;
}

constexpr float LOGICAL_W = 460.0f;
constexpr float LOGICAL_H = 704.0f;

constexpr UINT IDI_GHOST_ICON      = 101;
constexpr UINT WM_TRAYICON         = WM_APP + 1;
constexpr UINT WM_APPLY_WASAPI     = WM_APP + 2;
constexpr UINT WM_STATE_CHANGED    = WM_APP + 3;
constexpr UINT ID_TRAY_SHOW        = 1001;
constexpr UINT ID_TRAY_RESTART     = 1002;
constexpr UINT ID_TRAY_MOTION      = 1003;
constexpr UINT ID_TRAY_THEME       = 1004;
constexpr UINT ID_TRAY_EXIT        = 1005;
constexpr UINT_PTR TIMER_ANIM      = 1;
constexpr UINT_PTR TIMER_WASAPI    = 2;
constexpr UINT_PTR TIMER_VID_SKIN  = 3;
constexpr UINT_PTR TIMER_APPLY_CFG = 4;

// Direct-Click Hit IDs (zero blind cycling)
enum HitId : int {
    HIT_NONE     = 0,
    HIT_CLOSE    = 1,
    HIT_MIN      = 2,
    HIT_MOTION   = 3,
    HIT_THEME    = 4,
    HIT_HERO     = 5,
    // Direct Presets (3)
    HIT_PRESET_0 = 6,
    HIT_PRESET_1 = 7,
    HIT_PRESET_2 = 8,
    // Direct Resolution (4)
    HIT_RES_0    = 9,
    HIT_RES_1    = 10,
    HIT_RES_2    = 11,
    HIT_RES_3    = 12,
    // Direct FPS (3)
    HIT_FPS_0    = 13,
    HIT_FPS_1    = 14,
    HIT_FPS_2    = 15,
    // Direct Codec (2)
    HIT_CODEC_0  = 16,
    HIT_CODEC_1  = 17,
    // Direct Toggle Switches (4)
    HIT_VIEW     = 18,
    HIT_VSYNC    = 19,
    HIT_PIN      = 20,
    HIT_AUDIO    = 21,
    // Volume & Footer
    HIT_VOL      = 22,
    HIT_RESTART  = 23,
    HIT_COUNT    = 24
};

// ============================================================================
// Direct2D / DirectWrite / Win32 Global State
// ============================================================================
static HWND g_hwnd = nullptr;
static HICON g_appIcon = nullptr;
static HICON g_appIconSmall = nullptr;
static HWND g_styled_vid_hwnd = nullptr;
static std::wstring g_styled_vid_title;
static bool g_had_video_window = false;

static ID2D1Factory* g_d2dFactory = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static ID2D1SolidColorBrush* g_br = nullptr; // Cached brush: zero per-frame COM allocs!
static IDWriteFactory* g_dwFactory = nullptr;

static IDWriteTextFormat* g_fmtBrand     = nullptr;
static IDWriteTextFormat* g_fmtTag       = nullptr;
static IDWriteTextFormat* g_fmtTitle     = nullptr;
static IDWriteTextFormat* g_fmtSub       = nullptr;
static IDWriteTextFormat* g_fmtPin       = nullptr;
static IDWriteTextFormat* g_fmtSeg       = nullptr; // Direct segmented picker text
static IDWriteTextFormat* g_fmtMicro     = nullptr; // Section micro-label
static IDWriteTextFormat* g_fmtSwitchVal = nullptr; // Toggle switch value
static IDWriteTextFormat* g_fmtFooter    = nullptr;
static IDWriteTextFormat* g_fmtRight     = nullptr;

static std::mutex g_mu;
static GhostConfig g_cfg;
static GhostState g_state;
static GhostPhase g_prev_phase = GhostPhase::Starting;
static bool g_pending_apply = false;

static HANDLE g_job = nullptr;
static HANDLE g_proc = nullptr;
static DWORD g_uxplay_pid = 0;
static std::atomic<bool> g_quitting{false};
static std::atomic<int> g_generation{0};

// Animation & Interaction State (real QPC delta-time)
static LARGE_INTEGER g_qpc_freq = {};
static LARGE_INTEGER g_qpc_prev = {};
static float g_time = 0.0f;
static float g_intro_t = 0.0f;
static bool g_anim_running = false;
static bool g_window_minimized = false;

static int g_hover_id = HIT_NONE;
static int g_pressed_id = HIT_NONE;
static bool g_dragging_vol = false;
static float g_mouse_x = LOGICAL_W * 0.5f;
static float g_mouse_y = 148.0f;
static bool g_mouse_inside = false;

static std::array<float, HIT_COUNT> g_hover_anim = {};
static std::array<float, HIT_COUNT> g_press_anim = {};
static float g_vol_visual = 0.85f;

static NOTIFYICONDATAW g_nid = {};

// ============================================================================
// Crisp Click/Event Pixel Sparkle Bursts (zero ambient floating clutter)
// ============================================================================
struct PixelSparkle {
    float x, y;
    float vx, vy;
    float life;
    float max_life;
    UINT32 color;
    bool active;
};

static std::array<PixelSparkle, 24> g_sparkles = {};
static unsigned int g_rng = 0x594F5955u;

static float rand01() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return static_cast<float>((g_rng >> 8) & 0xFFFFu) / 65535.0f;
}

static UINT32 pick_pixel_accent(int idx) {
    switch (idx % 3) {
        case 0: return Tok::PIXEL_CYAN;
        case 1: return Tok::PIXEL_MINT;
        default: return Tok::ACCENT_VERMILION;
    }
}

static void spawn_sparkles(float cx, float cy, int count, UINT32 preferred = 0) {
    int spawned = 0;
    for (auto& s : g_sparkles) {
        if (!s.active && spawned < count) {
            float angle = (6.283185f * spawned / static_cast<float>(count)) + (rand01() - 0.5f) * 0.5f;
            float speed = 34.0f + rand01() * 44.0f;
            s.x = cx;
            s.y = cy;
            s.vx = std::cos(angle) * speed;
            s.vy = std::sin(angle) * speed - 8.0f;
            s.life = 0.0f;
            s.max_life = 0.40f + rand01() * 0.25f;
            s.color = preferred ? preferred : pick_pixel_accent(spawned);
            s.active = true;
            ++spawned;
        }
    }
}

static void init_particles() {
    for (auto& s : g_sparkles) s.active = false;
}

// ============================================================================
// UI Rectangles (Strict 8px / 4px Architectural Grid on 460 x 704 DIPs)
// ============================================================================
struct UiRects {
    // Top architectural header (y: 0..48)
    D2D1_RECT_F themeBtn   = D2D1::RectF(274.0f, 12.0f, 298.0f, 36.0f);
    D2D1_RECT_F motionBtn  = D2D1::RectF(304.0f, 12.0f, 386.0f, 36.0f);
    D2D1_RECT_F minBtn     = D2D1::RectF(394.0f, 12.0f, 418.0f, 36.0f);
    D2D1_RECT_F closeBtn   = D2D1::RectF(424.0f, 12.0f, 448.0f, 36.0f);

    // Interactive Pixel Ghost Hero Stage (y: 48..296)
    D2D1_RECT_F heroHit    = D2D1::RectF(146.0f, 84.0f, 314.0f, 208.0f);

    // Group 1: Direct Quick Presets (3 buttons, y: 326..364)
    D2D1_RECT_F presetBtn[3] = {
        D2D1::RectF(24.0f,  326.0f, 156.0f, 364.0f),
        D2D1::RectF(164.0f, 326.0f, 296.0f, 364.0f),
        D2D1::RectF(304.0f, 326.0f, 436.0f, 364.0f)
    };

    // Group 2: Direct Resolution 4-way Segmented Bar (y: 392..428)
    D2D1_RECT_F resBar     = D2D1::RectF(24.0f, 392.0f, 436.0f, 428.0f);
    D2D1_RECT_F resSeg[4]  = {
        D2D1::RectF(24.0f,  392.0f, 127.0f, 428.0f),
        D2D1::RectF(127.0f, 392.0f, 230.0f, 428.0f),
        D2D1::RectF(230.0f, 392.0f, 333.0f, 428.0f),
        D2D1::RectF(333.0f, 392.0f, 436.0f, 428.0f)
    };

    // Group 3: Direct FPS (3-way) + Codec (2-way) Segmented Bars (y: 456..492)
    D2D1_RECT_F fpsBar     = D2D1::RectF(24.0f,  456.0f, 249.0f, 492.0f);
    D2D1_RECT_F fpsSeg[3]  = {
        D2D1::RectF(24.0f,  456.0f, 99.0f,  492.0f),
        D2D1::RectF(99.0f,  456.0f, 174.0f, 492.0f),
        D2D1::RectF(174.0f, 456.0f, 249.0f, 492.0f)
    };
    D2D1_RECT_F codecBar   = D2D1::RectF(262.0f, 456.0f, 436.0f, 492.0f);
    D2D1_RECT_F codecSeg[2]= {
        D2D1::RectF(262.0f, 456.0f, 349.0f, 492.0f),
        D2D1::RectF(349.0f, 456.0f, 436.0f, 492.0f)
    };

    // Group 4: Direct Toggle Switches (y: 522..566)
    D2D1_RECT_F viewTile   = D2D1::RectF(24.0f,  522.0f, 121.0f, 566.0f);
    D2D1_RECT_F vsyncTile  = D2D1::RectF(129.0f, 522.0f, 226.0f, 566.0f);
    D2D1_RECT_F pinTile    = D2D1::RectF(234.0f, 522.0f, 331.0f, 566.0f);
    D2D1_RECT_F audioTile  = D2D1::RectF(339.0f, 522.0f, 436.0f, 566.0f);

    // Group 5: 20-Segment Pixel WASAPI Volume Fader (y: 580..628)
    D2D1_RECT_F volBox     = D2D1::RectF(24.0f, 580.0f, 436.0f, 628.0f);
    D2D1_RECT_F volTrack   = D2D1::RectF(94.0f, 592.0f, 370.0f, 616.0f);

    // Footer Restart CTA (y: 652..704)
    D2D1_RECT_F restartBtn = D2D1::RectF(336.0f, 664.0f, 436.0f, 692.0f);
};

static UiRects g_rects;

static bool pt_in_rect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static int hit_test_ui(float x, float y) {
    if (pt_in_rect(x, y, g_rects.closeBtn))     return HIT_CLOSE;
    if (pt_in_rect(x, y, g_rects.minBtn))       return HIT_MIN;
    if (pt_in_rect(x, y, g_rects.motionBtn))    return HIT_MOTION;
    if (pt_in_rect(x, y, g_rects.themeBtn))     return HIT_THEME;
    if (pt_in_rect(x, y, g_rects.heroHit))      return HIT_HERO;
    for (int i = 0; i < 3; ++i)
        if (pt_in_rect(x, y, g_rects.presetBtn[i])) return HIT_PRESET_0 + i;
    for (int i = 0; i < 4; ++i)
        if (pt_in_rect(x, y, g_rects.resSeg[i]))    return HIT_RES_0 + i;
    for (int i = 0; i < 3; ++i)
        if (pt_in_rect(x, y, g_rects.fpsSeg[i]))    return HIT_FPS_0 + i;
    for (int i = 0; i < 2; ++i)
        if (pt_in_rect(x, y, g_rects.codecSeg[i]))  return HIT_CODEC_0 + i;
    if (pt_in_rect(x, y, g_rects.viewTile))     return HIT_VIEW;
    if (pt_in_rect(x, y, g_rects.vsyncTile))    return HIT_VSYNC;
    if (pt_in_rect(x, y, g_rects.pinTile))      return HIT_PIN;
    if (pt_in_rect(x, y, g_rects.audioTile))    return HIT_AUDIO;
    if (pt_in_rect(x, y, g_rects.volBox))       return HIT_VOL;
    if (pt_in_rect(x, y, g_rects.restartBtn))   return HIT_RESTART;
    return HIT_NONE;
}

// ============================================================================
// Utilities & Per-Monitor DPI Support
// ============================================================================
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

static std::wstring get_exe_dir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? p.substr(0, pos) : L".";
}

static bool file_exists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static bool dir_exists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static float get_window_dpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi > 0) return static_cast<float>(dpi);
    }
    return 96.0f;
}

static void enable_per_monitor_dpi() {
    using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<SetDpiCtxFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (fn) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

static float px_to_dip(HWND hwnd, int px) {
    return static_cast<float>(px) * 96.0f / get_window_dpi(hwnd);
}

// ============================================================================
// Event-Driven Real-Time WASAPI Per-Process Volume Control
// ============================================================================
static void apply_wasapi_volume() {
    DWORD target_pid = 0;
    float vol = 0.85f;
    bool muted = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        target_pid = g_uxplay_pid;
        vol = std::clamp(g_cfg.volume_percent / 100.0f, 0.0f, 1.0f);
        muted = !g_cfg.audio_enabled || (g_cfg.volume_percent <= 0);
    }
    if (target_pid == 0) return;

    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
    if (FAILED(hr) || !pEnum) return;

    IMMDevice* pDevice = nullptr;
    if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice)) && pDevice) {
        IAudioSessionManager2* pMgr = nullptr;
        if (SUCCEEDED(pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pMgr)) && pMgr) {
            IAudioSessionEnumerator* pSessEnum = nullptr;
            if (SUCCEEDED(pMgr->GetSessionEnumerator(&pSessEnum)) && pSessEnum) {
                int count = 0;
                pSessEnum->GetCount(&count);
                for (int i = 0; i < count; i++) {
                    IAudioSessionControl* pCtrl = nullptr;
                    if (SUCCEEDED(pSessEnum->GetSession(i, &pCtrl)) && pCtrl) {
                        IAudioSessionControl2* pCtrl2 = nullptr;
                        if (SUCCEEDED(pCtrl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pCtrl2)) && pCtrl2) {
                            DWORD pid = 0;
                            pCtrl2->GetProcessId(&pid);
                            if (pid == target_pid) {
                                ISimpleAudioVolume* pVol = nullptr;
                                if (SUCCEEDED(pCtrl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVol)) && pVol) {
                                    pVol->SetMasterVolume(vol, nullptr);
                                    pVol->SetMute(muted ? TRUE : FALSE, nullptr);
                                    pVol->Release();
                                }
                            }
                            pCtrl2->Release();
                        }
                        pCtrl->Release();
                    }
                }
                pSessEnum->Release();
            }
            pMgr->Release();
        }
        pDevice->Release();
    }
    pEnum->Release();
}

// ============================================================================
// UxPlay Engine Process Management
// ============================================================================
static std::wstring locate_uxplay_exe(std::wstring& out_bin_dir, std::wstring& out_gst_dir) {
    std::wstring exe_dir = get_exe_dir();
    std::vector<std::wstring> candidates = {
        exe_dir + L"\\engine\\uxplay.exe",
        exe_dir + L"\\uxplay.exe",
        exe_dir + L"\\..\\uxplay-src\\build\\uxplay.exe",
        exe_dir + L"\\uxplay-src\\build\\uxplay.exe",
        L"C:\\Users\\ADMIN\\.gemini\\antigravity\\scratch\\ghost-mirror\\dist\\GhostMirror\\engine\\uxplay.exe",
        L"C:\\Users\\ADMIN\\.gemini\\antigravity\\scratch\\ghost-mirror\\uxplay-src\\build\\uxplay.exe",
        L"C:\\msys64\\ucrt64\\bin\\uxplay.exe"
    };

    if (dir_exists(exe_dir + L"\\engine\\lib\\gstreamer-1.0")) {
        out_bin_dir = exe_dir + L"\\engine";
        out_gst_dir = exe_dir + L"\\engine\\lib\\gstreamer-1.0";
    } else if (dir_exists(L"C:\\msys64\\ucrt64\\lib\\gstreamer-1.0")) {
        out_bin_dir = L"C:\\msys64\\ucrt64\\bin";
        out_gst_dir = L"C:\\msys64\\ucrt64\\lib\\gstreamer-1.0";
    }

    for (const auto& c : candidates) {
        if (file_exists(c)) {
            if (out_bin_dir.empty()) {
                auto slash = c.find_last_of(L"\\/");
                out_bin_dir = (slash != std::wstring::npos) ? c.substr(0, slash) : exe_dir;
            }
            return c;
        }
    }
    return L"";
}

static void stop_uxplay_engine() {
    g_generation.fetch_add(1);
    HANDLE p = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        p = g_proc;
        g_proc = nullptr;
        g_uxplay_pid = 0;
    }
    g_styled_vid_hwnd = nullptr;
    g_styled_vid_title.clear();
    g_had_video_window = false;
    if (p) {
        TerminateProcess(p, 0);
        WaitForSingleObject(p, 1500);
        CloseHandle(p);
    }
}

static void start_uxplay_engine() {
    stop_uxplay_engine();
    if (g_quitting.load()) return;

    int my_gen = g_generation.load();
    GhostConfig cfg_copy;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cfg_copy = g_cfg;
        cfg_copy.audio_enabled = true; // Keep WASAPI sink alive for 0ms real-time mute/fader
        g_state = GhostState{};
        g_state.phase = GhostPhase::Starting;
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_STATE_CHANGED, 0, 0);

    std::thread([my_gen, cfg_copy]() {
        std::wstring bin_dir, gst_dir;
        std::wstring exe_path = locate_uxplay_exe(bin_dir, gst_dir);
        if (exe_path.empty()) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_generation.load() == my_gen) {
                g_state.phase = GhostPhase::Error;
                g_state.error_msg = "uxplay.exe not found in ./engine or build folder";
            }
            if (g_hwnd) PostMessageW(g_hwnd, WM_STATE_CHANGED, 0, 0);
            return;
        }

        if (!bin_dir.empty()) {
            wchar_t old_path[32767] = {};
            GetEnvironmentVariableW(L"PATH", old_path, 32767);
            std::wstring new_path = bin_dir + L";C:\\msys64\\ucrt64\\bin;" + old_path;
            SetEnvironmentVariableW(L"PATH", new_path.c_str());
        }
        if (!gst_dir.empty()) {
            SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH_1_0", gst_dir.c_str());
            SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", gst_dir.c_str());
            std::wstring scanner = bin_dir + L"\\gst-plugin-scanner.exe";
            if (file_exists(scanner)) {
                SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER_1_0", scanner.c_str());
            }
        }

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE rPipe = nullptr, wPipe = nullptr;
        if (!CreatePipe(&rPipe, &wPipe, &sa, 0)) {
            return;
        }
        SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);

        std::wstring cmd = L"\"" + exe_path + L"\" " + utf8_to_wide(build_uxplay_args(cfg_copy));
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(L'\0');

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOWNORMAL;
        si.hStdOutput = wPipe;
        si.hStdError = wPipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessW(
            nullptr,
            cmd_buf.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            bin_dir.empty() ? nullptr : bin_dir.c_str(),
            &si,
            &pi
        );
        CloseHandle(wPipe);

        if (!ok) {
            CloseHandle(rPipe);
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_generation.load() == my_gen) {
                g_state.phase = GhostPhase::Error;
                g_state.error_msg = "Failed to launch uxplay.exe (error " + std::to_string(GetLastError()) + ")";
            }
            if (g_hwnd) PostMessageW(g_hwnd, WM_STATE_CHANGED, 0, 0);
            return;
        }

        if (g_job) {
            AssignProcessToJobObject(g_job, pi.hProcess);
        }
        CloseHandle(pi.hThread);

        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_generation.load() != my_gen || g_quitting.load()) {
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess);
                CloseHandle(rPipe);
                return;
            }
            g_proc = pi.hProcess;
            g_uxplay_pid = pi.dwProcessId;
            g_state.phase = GhostPhase::Waiting;
        }
        if (g_hwnd) PostMessageW(g_hwnd, WM_STATE_CHANGED, 0, 0);

        std::string pending;
        char buf[512];
        DWORD nRead = 0;
        while (ReadFile(rPipe, buf, sizeof(buf) - 1, &nRead, nullptr) && nRead > 0) {
            buf[nRead] = '\0';
            pending.append(buf, nRead);
            size_t nl = 0;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                bool became_connected = false;
                bool state_updated = false;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    if (g_generation.load() == my_gen) {
                        GhostPhase beforePhase = g_state.phase;
                        std::string beforeName = g_state.client_name;
                        std::string beforePin = g_state.pin;
                        std::string beforeIp = g_state.lan_ip;
                        parse_uxplay_line(g_state, line);
                        if (beforePhase != GhostPhase::Connected && g_state.phase == GhostPhase::Connected) {
                            became_connected = true;
                        }
                        if (beforePhase != g_state.phase || beforeName != g_state.client_name ||
                            beforePin != g_state.pin || beforeIp != g_state.lan_ip) {
                            state_updated = true;
                        }
                    }
                }
                if (became_connected && g_hwnd) {
                    PostMessageW(g_hwnd, WM_APPLY_WASAPI, 0, 0);
                }
                if (state_updated && g_hwnd) {
                    PostMessageW(g_hwnd, WM_STATE_CHANGED, 0, 0);
                }
            }
        }
        CloseHandle(rPipe);

        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_generation.load() == my_gen && !g_quitting.load()) {
                if (g_state.phase != GhostPhase::Error) {
                    g_state.phase = GhostPhase::Error;
                    g_state.error_msg = "Engine stopped unexpectedly. Click mascot or RESTART.";
                }
            }
        }
        if (g_hwnd) PostMessageW(g_hwnd, WM_STATE_CHANGED, 0, 0);
    }).detach();
}

// ============================================================================
// DirectWrite Font Discovery & Cached Direct2D Resources
// ============================================================================
static std::wstring pick_installed_font(const std::vector<const wchar_t*>& candidates) {
    if (!g_dwFactory) return candidates.back();
    IDWriteFontCollection* coll = nullptr;
    if (SUCCEEDED(g_dwFactory->GetSystemFontCollection(&coll)) && coll) {
        for (const wchar_t* name : candidates) {
            UINT32 idx = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(name, &idx, &exists)) && exists) {
                coll->Release();
                return name;
            }
        }
        coll->Release();
    }
    return candidates.back();
}

static void discard_d2d_targets() {
    if (g_br) { g_br->Release(); g_br = nullptr; }
    if (g_rt) { g_rt->Release(); g_rt = nullptr; }
}

static void release_all_d2d() {
    discard_d2d_targets();
    for (auto** fmt : {
        &g_fmtBrand, &g_fmtTag, &g_fmtTitle, &g_fmtSub, &g_fmtPin,
        &g_fmtSeg, &g_fmtMicro, &g_fmtSwitchVal, &g_fmtFooter, &g_fmtRight
    }) {
        if (*fmt) { (*fmt)->Release(); *fmt = nullptr; }
    }
    if (g_dwFactory)  { g_dwFactory->Release();  g_dwFactory = nullptr; }
    if (g_d2dFactory) { g_d2dFactory->Release(); g_d2dFactory = nullptr; }
}

static HRESULT init_d2d(HWND hwnd) {
    if (!g_d2dFactory) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);
    }
    if (!g_dwFactory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&g_dwFactory));

        std::wstring fontMono  = pick_installed_font({L"IBM Plex Mono", L"JetBrains Mono", L"Cascadia Mono", L"Consolas"});
        std::wstring fontPixel = pick_installed_font({L"VT323", L"IBM Plex Mono", L"Consolas"});
        std::wstring fontSans  = pick_installed_font({L"Yu Gothic UI", L"Segoe UI Variable Display", L"Segoe UI"});

        auto make_fmt = [&](const std::wstring& fam, DWRITE_FONT_WEIGHT wt, float sz,
                            DWRITE_TEXT_ALIGNMENT halign, IDWriteTextFormat** out) {
            g_dwFactory->CreateTextFormat(
                fam.c_str(), nullptr, wt, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                sz, L"en-us", out);
            if (*out) {
                (*out)->SetTextAlignment(halign);
                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        };

        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_BOLD,      12.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtBrand);
        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_SEMI_BOLD,  9.5f, DWRITE_TEXT_ALIGNMENT_CENTER,   &g_fmtTag);
        make_fmt(fontSans,  DWRITE_FONT_WEIGHT_SEMI_BOLD, 21.0f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtTitle);
        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_REGULAR,   10.8f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtSub);
        make_fmt(fontPixel, DWRITE_FONT_WEIGHT_REGULAR,   52.0f, DWRITE_TEXT_ALIGNMENT_CENTER,   &g_fmtPin);
        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_BOLD,      11.5f, DWRITE_TEXT_ALIGNMENT_CENTER,   &g_fmtSeg);
        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_SEMI_BOLD,  9.2f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtMicro);
        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_BOLD,      11.2f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtSwitchVal);
        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_MEDIUM,    10.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtFooter);
        make_fmt(fontMono,  DWRITE_FONT_WEIGHT_SEMI_BOLD, 10.0f, DWRITE_TEXT_ALIGNMENT_TRAILING, &g_fmtRight);
    }

    if (!g_rt) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        UINT32 w = std::max<UINT32>(1, (UINT32)(rc.right - rc.left));
        UINT32 h = std::max<UINT32>(1, (UINT32)(rc.bottom - rc.top));
        float dpi = get_window_dpi(hwnd);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = dpi;
        props.dpiY = dpi;
        HRESULT hr = g_d2dFactory->CreateHwndRenderTarget(
            props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(w, h)), &g_rt);
        if (SUCCEEDED(hr) && g_rt) {
            g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
            g_rt->CreateSolidColorBrush(D2D1::ColorF(Tok::INK, 1.0f), &g_br);
        }
    }
    return (g_rt && g_br) ? S_OK : E_FAIL;
}

// ============================================================================
// Crisp Pixel-Art Drawing Primitives (Yoyu Design DNA)
// ============================================================================
static inline void fill_px(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                           float x, float y, float w, float h, UINT32 hex, float alpha = 1.0f) {
    br->SetColor(D2D1::ColorF(hex, alpha));
    rt->FillRectangle(D2D1::RectF(std::floor(x), std::floor(y), std::floor(x + w), std::floor(y + h)), br);
}

static inline void draw_rect_1px(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                 const D2D1_RECT_F& r, UINT32 hex, float alpha = 1.0f, float stroke = 1.0f) {
    br->SetColor(D2D1::ColorF(hex, alpha));
    D2D1_RECT_F snapped = D2D1::RectF(
        std::floor(r.left) + 0.5f, std::floor(r.top) + 0.5f,
        std::floor(r.right) - 0.5f, std::floor(r.bottom) - 0.5f);
    rt->DrawRectangle(snapped, br, stroke);
}

static inline D2D1_RECT_F offset_rect(const D2D1_RECT_F& r, float dx, float dy) {
    return D2D1::RectF(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy);
}

static void draw_pixel_sparkle(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                               float cx, float cy, float unit, UINT32 hex, float alpha) {
    float x = std::floor(cx);
    float y = std::floor(cy);
    fill_px(rt, br, x, y - unit * 2.0f, unit, unit * 5.0f, hex, alpha);
    fill_px(rt, br, x - unit * 2.0f, y, unit * 5.0f, unit, hex, alpha);
}

// Crisp 7x7 Pixel Moon / Pixel Sun glyph for the compact Dark/Light header button
static void draw_theme_icon(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                            const D2D1_RECT_F& r, bool darkMode, UINT32 col) {
    static const char* moon[7] = {
        "..XXX..",
        ".XX....",
        "XX.....",
        "XX.....",
        "XX....X",
        ".XX..XX",
        "..XXXX."
    };
    static const char* sun[7] = {
        "...X...",
        ".X...X.",
        "..XXX..",
        "X.XXX.X",
        "..XXX..",
        ".X...X.",
        "...X..."
    };
    const char** glyph = darkMode ? sun : moon;
    float u = 2.0f;
    float x0 = std::floor((r.left + r.right) * 0.5f - 3.5f * u);
    float y0 = std::floor((r.top + r.bottom) * 0.5f - 3.5f * u);
    for (int row = 0; row < 7; ++row) {
        for (int c = 0; c < 7; ++c) {
            if (glyph[row][c] == 'X') {
                fill_px(rt, br, x0 + c * u, y0 + row * u, u, u, col, 1.0f);
            }
        }
    }
}

static void draw_mini_pixel_ghost(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                  const Palette& pal, float x0, float y0, float u, UINT32 statusColor) {
    static const char* rows[8] = {
        "..XXXX..",
        ".XXXXXX.",
        "XXXXXXXX",
        "X.XXXX.X",
        "XXXXXXXX",
        "XXXXXXXX",
        "XXXXXXXX",
        "X.X..X.X"
    };
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (rows[r][c] == 'X') {
                fill_px(rt, br, x0 + c * u, y0 + r * u, u, u, pal.ink, 1.0f);
            }
        }
    }
    fill_px(rt, br, x0 + 6 * u, y0 + 6 * u, u * 1.5f, u * 1.5f, statusColor, 1.0f);
}

static void draw_hero_pixel_ghost(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                  const Palette& pal, float cx, float cy, float u, GhostPhase phase,
                                  int frame, bool blink, int eye_dx, int eye_dy, float alpha) {
    static const char* body_f0[14] = {
        ".....######.....",
        "...##WWWWWW##...",
        "..#WWWWWWWWWW#..",
        ".#WWWWWWWWWWWW#.",
        ".#WWWWWWWWWWWW#.",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWW#WWWWWW#WWW#",
        "#WW#.#WWWW#.#WW#",
        ".##...####...##."
    };
    static const char* body_f1[14] = {
        ".....######.....",
        "...##WWWWWW##...",
        "..#WWWWWWWWWW#..",
        ".#WWWWWWWWWWWW#.",
        ".#WWWWWWWWWWWW#.",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WW#WWWWWWWW#WW#",
        ".#W#.#WWWW#.#W#.",
        "..##..####..##.."
    };

    const char** sprite = (frame % 2 == 0) ? body_f0 : body_f1;
    float ox = std::floor(cx - 8.0f * u);
    float oy = std::floor(cy - 8.0f * u);

    for (int r = 0; r < 14; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (sprite[r][c] != '.') {
                fill_px(rt, br, ox + c * u + 3.0f, oy + r * u + 3.0f, u, u, pal.ghost_shadow, 0.65f * alpha);
            }
        }
    }

    for (int r = 0; r < 14; ++r) {
        for (int c = 0; c < 16; ++c) {
            char ch = sprite[r][c];
            if (ch == '#') {
                fill_px(rt, br, ox + c * u, oy + r * u, u, u, pal.ghost_outline, alpha);
            } else if (ch == 'W') {
                fill_px(rt, br, ox + c * u, oy + r * u, u, u, pal.ghost_fill, alpha);
            }
        }
    }

    if (phase == GhostPhase::Error) {
        UINT32 ec = pal.accent_vermilion;
        fill_px(rt, br, ox + 4 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 5 * u, oy + 6 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 4 * u, oy + 7 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 7 * u, u, u, ec, alpha);

        fill_px(rt, br, ox + 9 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 11 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 10 * u, oy + 6 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 7 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 11 * u, oy + 7 * u, u, u, ec, alpha);

        fill_px(rt, br, ox + 6 * u, oy + 9 * u, 4 * u, u, pal.ghost_eyes, alpha);
    } else if (phase == GhostPhase::Connected) {
        fill_px(rt, br, ox + 5 * u, oy + 5 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 4 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);

        fill_px(rt, br, ox + 10 * u, oy + 5 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 11 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);

        fill_px(rt, br, ox + 3 * u, oy + 8 * u, u, u, pal.pixel_mint, alpha);
        fill_px(rt, br, ox + 12 * u, oy + 8 * u, u, u, pal.pixel_mint, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 8 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 7 * u, oy + 9 * u, 2 * u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 8 * u, u, u, pal.ghost_eyes, alpha);
    } else if (blink) {
        fill_px(rt, br, ox + 4 * u, oy + 6 * u, 3 * u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 6 * u, 3 * u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 7 * u, oy + 9 * u, 2 * u, u, pal.ghost_eyes, alpha);
    } else {
        float lx = ox + (5 + eye_dx) * u;
        float rx = ox + (10 + eye_dx) * u;
        float ey = oy + (6 + eye_dy) * u;
        fill_px(rt, br, lx, ey, 2 * u, 2 * u, pal.ghost_eyes, alpha);
        fill_px(rt, br, rx, ey, 2 * u, 2 * u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 9 * u, 4 * u, u, pal.ghost_eyes, alpha);
    }

    UINT32 prismCol = (phase == GhostPhase::Connected) ? pal.pixel_mint : pal.accent_vermilion;
    float py = oy + 15.0f * u;
    fill_px(rt, br, ox + 7 * u, py, 2 * u, u, prismCol, alpha);
    fill_px(rt, br, ox + 6 * u, py + u, 4 * u, u, prismCol, alpha);
    fill_px(rt, br, ox + 5 * u, py + 2 * u, 6 * u, u, prismCol, alpha);
}

// ============================================================================
// Direct Segmented Picker & Tactile Architectural Controls
// ============================================================================
static void draw_preset_btn(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, const Palette& pal,
                            int hitId, const D2D1_RECT_F& baseRect, float yOffset, float alpha,
                            const wchar_t* label, bool selected) {
    float hv = g_hover_anim[hitId];
    float pr = g_press_anim[hitId];
    float lift = -2.0f * hv + 2.0f * pr;

    D2D1_RECT_F r = offset_rect(baseRect, lift, yOffset + lift);

    if (hv > 0.02f) {
        D2D1_RECT_F sh = offset_rect(baseRect, 3.0f * hv, yOffset + 3.0f * hv);
        br->SetColor(D2D1::ColorF(selected ? pal.accent_vermilion : pal.shadow_ink, alpha * hv));
        rt->FillRectangle(sh, br);
    }

    br->SetColor(D2D1::ColorF(selected ? pal.ink : pal.surface, alpha));
    rt->FillRectangle(r, br);
    draw_rect_1px(rt, br, r, (selected || hv > 0.25f) ? pal.border_strong : pal.border, alpha);

    if (selected) {
        fill_px(rt, br, r.left + 8.0f, (r.top + r.bottom) * 0.5f - 2.5f, 5.0f, 5.0f, pal.accent_vermilion, alpha);
    }

    br->SetColor(D2D1::ColorF(selected ? pal.ink_on_active : (hv > 0.25f ? pal.ink : pal.ink_muted), alpha));
    D2D1_RECT_F tr = selected ? D2D1::RectF(r.left + 12.0f, r.top, r.right - 2.0f, r.bottom) : r;
    rt->DrawTextW(label, (UINT32)wcslen(label), g_fmtTag, tr, br);
}

static void draw_segmented_bar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, const Palette& pal,
                               const D2D1_RECT_F& outerRect, const D2D1_RECT_F* segRects,
                               int firstHitId, int count, int selectedIdx,
                               const wchar_t* const* labels, float yOffset, float alpha) {
    D2D1_RECT_F frame = offset_rect(outerRect, 0.0f, yOffset);
    br->SetColor(D2D1::ColorF(pal.surface, alpha));
    rt->FillRectangle(frame, br);

    for (int i = 0; i < count; ++i) {
        int hid = firstHitId + i;
        float hv = g_hover_anim[hid];
        bool sel = (i == selectedIdx);
        D2D1_RECT_F sr = offset_rect(segRects[i], 0.0f, yOffset);

        if (sel) {
            br->SetColor(D2D1::ColorF(pal.ink, alpha));
            rt->FillRectangle(sr, br);
            fill_px(rt, br, sr.left, sr.bottom - 2.0f, sr.right - sr.left, 2.0f, pal.accent_vermilion, alpha);
        } else if (hv > 0.02f) {
            br->SetColor(D2D1::ColorF(pal.surface_muted, alpha * hv));
            rt->FillRectangle(sr, br);
        }

        if (i > 0) {
            br->SetColor(D2D1::ColorF(pal.border, alpha));
            rt->DrawLine(D2D1::Point2F(std::floor(sr.left) + 0.5f, sr.top + 1.0f),
                         D2D1::Point2F(std::floor(sr.left) + 0.5f, sr.bottom - 1.0f), br, 1.0f);
        }

        br->SetColor(D2D1::ColorF(sel ? pal.ink_on_active : (hv > 0.25f ? pal.ink : pal.ink_muted), alpha));
        rt->DrawTextW(labels[i], (UINT32)wcslen(labels[i]), g_fmtSeg, sr, br);
    }

    draw_rect_1px(rt, br, frame, pal.border_strong, alpha);
}

static void draw_switch_tile(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, const Palette& pal,
                             int hitId, const D2D1_RECT_F& baseRect, float yOffset, float alpha,
                             const wchar_t* microLabel, const wchar_t* valueText,
                             bool active, UINT32 ledColor) {
    float hv = g_hover_anim[hitId];
    float pr = g_press_anim[hitId];
    float lift = -2.0f * hv + 2.0f * pr;

    D2D1_RECT_F r = offset_rect(baseRect, lift, yOffset + lift);

    if (hv > 0.02f) {
        D2D1_RECT_F sh = offset_rect(baseRect, 3.0f * hv, yOffset + 3.0f * hv);
        br->SetColor(D2D1::ColorF(pal.shadow_ink, alpha * hv));
        rt->FillRectangle(sh, br);
    }

    br->SetColor(D2D1::ColorF(active ? pal.surface : pal.surface_muted, alpha));
    rt->FillRectangle(r, br);
    draw_rect_1px(rt, br, r, (hv > 0.25f || active) ? pal.border_strong : pal.border, alpha);

    br->SetColor(D2D1::ColorF(pal.ink_subtle, alpha));
    rt->DrawTextW(microLabel, (UINT32)wcslen(microLabel), g_fmtMicro,
                  D2D1::RectF(r.left + 8.0f, r.top + 5.0f, r.right - 18.0f, r.top + 19.0f), br);

    fill_px(rt, br, r.right - 13.0f, r.top + 9.0f, 5.0f, 5.0f, active ? ledColor : pal.border, alpha);

    br->SetColor(D2D1::ColorF(active ? pal.ink : pal.ink_muted, alpha));
    rt->DrawTextW(valueText, (UINT32)wcslen(valueText), g_fmtSwitchVal,
                  D2D1::RectF(r.left + 8.0f, r.top + 20.0f, r.right - 6.0f, r.bottom - 4.0f), br);
}

// ============================================================================
// Main Direct2D Scene Renderer (GPU HwndRenderTarget + PrintClient DC)
// ============================================================================
static void draw_scene(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                       const GhostConfig& cfg, const GhostState& st) {
    const Palette& pal = active_palette(cfg.dark_mode);

    auto stagger_prog = [&](float delaySec) -> float {
        if (!cfg.motion_enabled) return 1.0f;
        float t = std::clamp((g_intro_t - delaySec) / 0.34f, 0.0f, 1.0f);
        return ease_out_expo(t);
    };

    float pHero   = stagger_prog(0.00f);
    float pPreset = stagger_prog(0.05f);
    float pRes    = stagger_prog(0.09f);
    float pFps    = stagger_prog(0.13f);
    float pOpt    = stagger_prog(0.17f);
    float pVol    = stagger_prog(0.21f);

    UINT32 statusAccent = pal.pixel_cyan;
    const wchar_t* statusTag = L"READY";
    if (g_pending_apply)                         { statusAccent = pal.accent_vermilion; statusTag = L"SYNCING"; }
    else if (st.phase == GhostPhase::Starting)   { statusAccent = pal.ink_subtle;       statusTag = L"BOOTING"; }
    else if (st.phase == GhostPhase::Waiting)    { statusAccent = pal.pixel_cyan;       statusTag = L"READY"; }
    else if (st.phase == GhostPhase::Connected)  { statusAccent = pal.pixel_mint;       statusTag = L"LIVE"; }
    else if (st.phase == GhostPhase::PairingPin) { statusAccent = pal.accent_vermilion; statusTag = L"PIN"; }
    else if (st.phase == GhostPhase::Error)      { statusAccent = pal.accent_vermilion; statusTag = L"ERROR"; }

    rt->BeginDraw();
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    rt->Clear(D2D1::ColorF(pal.paper_bg, 1.0f));

    // 1. Architectural Drafting Graph-Paper Grid (Hero Canvas y: 48..296)
    const float gridPitch = 16.0f;
    br->SetColor(D2D1::ColorF(pal.grid_line, 0.90f));
    for (float gx = gridPitch; gx < LOGICAL_W; gx += gridPitch) {
        rt->DrawLine(D2D1::Point2F(gx + 0.5f, 48.0f), D2D1::Point2F(gx + 0.5f, 296.0f), br, 1.0f);
    }
    for (float gy = 48.0f + gridPitch; gy < 296.0f; gy += gridPitch) {
        rt->DrawLine(D2D1::Point2F(0.0f, gy + 0.5f), D2D1::Point2F(LOGICAL_W, gy + 0.5f), br, 1.0f);
    }
    br->SetColor(D2D1::ColorF(pal.grid_major, 0.95f));
    for (float gx = 64.0f; gx < LOGICAL_W; gx += 64.0f) {
        for (float gy = 96.0f; gy < 280.0f; gy += 64.0f) {
            rt->DrawLine(D2D1::Point2F(gx - 3.0f, gy + 0.5f), D2D1::Point2F(gx + 4.0f, gy + 0.5f), br, 1.0f);
            rt->DrawLine(D2D1::Point2F(gx + 0.5f, gy - 3.0f), D2D1::Point2F(gx + 0.5f, gy + 4.0f), br, 1.0f);
        }
    }

    // 2. Top Architectural Bar (y: 0..48)
    br->SetColor(D2D1::ColorF(pal.surface, 1.0f));
    rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, LOGICAL_W, 48.0f), br);
    br->SetColor(D2D1::ColorF(pal.border, 1.0f));
    rt->DrawLine(D2D1::Point2F(0.0f, 48.5f), D2D1::Point2F(LOGICAL_W, 48.5f), br, 1.0f);

    draw_mini_pixel_ghost(rt, br, pal, 16.0f, 16.0f, 2.0f, statusAccent);

    br->SetColor(D2D1::ColorF(pal.ink, 1.0f));
    const wchar_t* brandTxt = L"GHOST MIRROR";
    rt->DrawTextW(brandTxt, (UINT32)wcslen(brandTxt), g_fmtBrand,
                  D2D1::RectF(40.0f, 0.0f, 160.0f, 48.0f), br);

    D2D1_RECT_F hailTag = D2D1::RectF(162.0f, 14.0f, 266.0f, 34.0f);
    br->SetColor(D2D1::ColorF(pal.paper_bg, 1.0f));
    rt->FillRectangle(hailTag, br);
    draw_rect_1px(rt, br, hailTag, pal.border, 1.0f);
    br->SetColor(D2D1::ColorF(pal.ink_muted, 1.0f));
    const wchar_t* hailTxt = L"HAIL THAN TAI";
    rt->DrawTextW(hailTxt, (UINT32)wcslen(hailTxt), g_fmtTag, hailTag, br);

    // Compact Dark/Light Theme Button right beside MOTION
    {
        float hv = g_hover_anim[HIT_THEME];
        D2D1_RECT_F tr = g_rects.themeBtn;
        bool isDark = cfg.dark_mode;
        br->SetColor(D2D1::ColorF(hv > 0.25f ? pal.ink : (isDark ? pal.paper_bg : pal.surface_muted), 1.0f));
        rt->FillRectangle(tr, br);
        draw_rect_1px(rt, br, tr, (hv > 0.25f || isDark) ? pal.border_strong : pal.border, 1.0f);
        draw_theme_icon(rt, br, tr, isDark, hv > 0.25f ? pal.ink_on_active : pal.ink);
    }

    // Header MOTION toggle
    {
        float hv = g_hover_anim[HIT_MOTION];
        D2D1_RECT_F mr = g_rects.motionBtn;
        bool on = cfg.motion_enabled;
        br->SetColor(D2D1::ColorF(on ? pal.ink : (hv > 0.2f ? pal.surface : pal.surface_muted), 1.0f));
        rt->FillRectangle(mr, br);
        draw_rect_1px(rt, br, mr, (on || hv > 0.2f) ? pal.border_strong : pal.border, 1.0f);
        fill_px(rt, br, mr.left + 8.0f, mr.top + 9.0f, 5.0f, 5.0f,
                on ? pal.pixel_mint : pal.ink_subtle, 1.0f);
        br->SetColor(D2D1::ColorF(on ? pal.ink_on_active : pal.ink_muted, 1.0f));
        const wchar_t* mTxt = on ? L"MOTION" : L"STATIC";
        rt->DrawTextW(mTxt, (UINT32)wcslen(mTxt), g_fmtTag,
                      D2D1::RectF(mr.left + 15.0f, mr.top, mr.right - 4.0f, mr.bottom), br);
    }

    // Minimize Button
    {
        float hv = g_hover_anim[HIT_MIN];
        if (hv > 0.05f) {
            br->SetColor(D2D1::ColorF(pal.surface_muted, hv));
            rt->FillRectangle(g_rects.minBtn, br);
        }
        draw_rect_1px(rt, br, g_rects.minBtn, hv > 0.3f ? pal.border_strong : pal.border, 1.0f);
        fill_px(rt, br, g_rects.minBtn.left + 7.0f, g_rects.minBtn.top + 15.0f, 10.0f, 2.0f, pal.ink, 1.0f);
    }

    // Close Button
    {
        float hv = g_hover_anim[HIT_CLOSE];
        if (hv > 0.05f) {
            br->SetColor(D2D1::ColorF(pal.accent_vermilion, hv));
            rt->FillRectangle(g_rects.closeBtn, br);
        }
        draw_rect_1px(rt, br, g_rects.closeBtn, hv > 0.3f ? pal.accent_vermilion : pal.border, 1.0f);
        UINT32 xCol = (hv > 0.45f) ? 0xFFFFFF : pal.ink;
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        br->SetColor(D2D1::ColorF(xCol, 1.0f));
        rt->DrawLine(D2D1::Point2F(g_rects.closeBtn.left + 7.0f, g_rects.closeBtn.top + 7.0f),
                     D2D1::Point2F(g_rects.closeBtn.right - 7.0f, g_rects.closeBtn.bottom - 7.0f), br, 1.6f);
        rt->DrawLine(D2D1::Point2F(g_rects.closeBtn.right - 7.0f, g_rects.closeBtn.top + 7.0f),
                     D2D1::Point2F(g_rects.closeBtn.left + 7.0f, g_rects.closeBtn.bottom - 7.0f), br, 1.6f);
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    }

    // 3. Hero Stage (Clean Architectural Drafting Canvas)
    float heroDy = (1.0f - pHero) * 10.0f;

    {
        D2D1_RECT_F badge = D2D1::RectF(24.0f, 60.0f + heroDy, 114.0f, 82.0f + heroDy);
        br->SetColor(D2D1::ColorF(pal.surface, pHero));
        rt->FillRectangle(badge, br);
        draw_rect_1px(rt, br, badge, pal.border_strong, pHero);

        bool pulseOn = !cfg.motion_enabled || (std::fmod(g_time, 1.1f) < 0.72f) || (st.phase == GhostPhase::Connected);
        fill_px(rt, br, badge.left + 8.0f, badge.top + 8.0f, 5.0f, 5.0f, statusAccent, pulseOn ? pHero : 0.25f * pHero);

        br->SetColor(D2D1::ColorF(pal.ink, pHero));
        rt->DrawTextW(statusTag, (UINT32)wcslen(statusTag), g_fmtTag,
                      D2D1::RectF(badge.left + 14.0f, badge.top, badge.right - 4.0f, badge.bottom), br);
    }

    {
        std::wstring spec = std::wstring(res_short_label(cfg.res)) + L" \u00B7 " +
                            std::to_wstring(cfg.fps) + L"FPS \u00B7 " +
                            codec_short_label(cfg.codec);
        br->SetColor(D2D1::ColorF(pal.ink_muted, pHero));
        rt->DrawTextW(spec.c_str(), (UINT32)spec.size(), g_fmtRight,
                      D2D1::RectF(200.0f, 60.0f + heroDy, 436.0f, 82.0f + heroDy), br);
    }

    for (const auto& sp : g_sparkles) {
        if (!sp.active) continue;
        float t = sp.life / sp.max_life;
        float a = (1.0f - t) * pHero;
        draw_pixel_sparkle(rt, br, sp.x, sp.y + heroDy, 2.0f, sp.color, a);
    }

    // Crisp Corner Viewfinder Brackets
    {
        float hv = g_hover_anim[HIT_HERO];
        UINT32 bCol = (hv > 0.25f) ? pal.ink : pal.grid_major;
        D2D1_RECT_F hr = offset_rect(g_rects.heroHit, 0.0f, heroDy);
        float arm = 10.0f + 4.0f * hv;
        fill_px(rt, br, hr.left, hr.top, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.left, hr.top, 2.0f, arm, bCol, pHero);
        fill_px(rt, br, hr.right - arm, hr.top, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.right - 2.0f, hr.top, 2.0f, arm, bCol, pHero);
        fill_px(rt, br, hr.left, hr.bottom - 2.0f, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.left, hr.bottom - arm, 2.0f, arm, bCol, pHero);
        fill_px(rt, br, hr.right - arm, hr.bottom - 2.0f, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.right - 2.0f, hr.bottom - arm, 2.0f, arm, bCol, pHero);

        if (hv > 0.35f && st.phase != GhostPhase::PairingPin) {
            br->SetColor(D2D1::ColorF(pal.ink_muted, hv * pHero));
            const wchar_t* hint = L"CLICK TO RESTART";
            rt->DrawTextW(hint, (UINT32)wcslen(hint), g_fmtTag,
                          D2D1::RectF(hr.left, hr.bottom - 18.0f, hr.right, hr.bottom - 3.0f), br);
        }
    }

    if (st.phase == GhostPhase::PairingPin && !st.pin.empty()) {
        D2D1_RECT_F pinBox = D2D1::RectF(118.0f, 100.0f + heroDy, 342.0f, 192.0f + heroDy);
        br->SetColor(D2D1::ColorF(pal.accent_vermilion, pHero));
        rt->FillRectangle(offset_rect(pinBox, 4.0f, 4.0f), br);
        br->SetColor(D2D1::ColorF(cfg.dark_mode ? pal.surface : pal.ink, pHero));
        rt->FillRectangle(pinBox, br);
        if (cfg.dark_mode) {
            draw_rect_1px(rt, br, pinBox, pal.border_strong, pHero);
        }

        fill_px(rt, br, pinBox.left + 6.0f, pinBox.top + 6.0f, 4.0f, 4.0f, pal.accent_vermilion, pHero);
        fill_px(rt, br, pinBox.right - 10.0f, pinBox.top + 6.0f, 4.0f, 4.0f, pal.accent_vermilion, pHero);

        br->SetColor(D2D1::ColorF(pal.ink_subtle, pHero));
        const wchar_t* pLbl = L"AIRPLAY PIN";
        rt->DrawTextW(pLbl, (UINT32)wcslen(pLbl), g_fmtTag,
                      D2D1::RectF(pinBox.left, pinBox.top + 8.0f, pinBox.right, pinBox.top + 24.0f), br);

        std::wstring wpin = utf8_to_wide(st.pin);
        br->SetColor(D2D1::ColorF(0xFFFFFF, pHero));
        rt->DrawTextW(wpin.c_str(), (UINT32)wpin.size(), g_fmtPin,
                      D2D1::RectF(pinBox.left, pinBox.top + 22.0f, pinBox.right, pinBox.bottom - 4.0f), br);
    } else {
        float bob = cfg.motion_enabled ? std::round(std::sin(g_time * 3.0f) * 3.0f) : 0.0f;
        float hoverLift = -3.0f * g_hover_anim[HIT_HERO] + 2.0f * g_press_anim[HIT_HERO];
        int frame = cfg.motion_enabled ? ((int)(g_time * 2.2f) % 2) : 0;
        bool blink = cfg.motion_enabled && (std::fmod(g_time, 3.8f) > 3.66f);

        int eye_dx = 0, eye_dy = 0;
        if (g_mouse_inside) {
            float dx = g_mouse_x - 230.0f;
            float dy = g_mouse_y - 142.0f;
            if (dx > 28.0f) eye_dx = 1; else if (dx < -28.0f) eye_dx = -1;
            if (dy > 32.0f) eye_dy = 1; else if (dy < -32.0f) eye_dy = -1;
        }

        draw_hero_pixel_ghost(rt, br, pal, 230.0f, 140.0f + bob + hoverLift + heroDy, 5.0f,
                              st.phase, frame, blink, eye_dx, eye_dy, pHero);

        if (st.phase == GhostPhase::Connected) {
            bool audioOn = cfg.audio_enabled && cfg.volume_percent > 0;
            for (int b = 0; b < 5; ++b) {
                float wave = cfg.motion_enabled
                    ? (0.35f + 0.65f * std::abs(std::sin(g_time * 6.5f + b * 1.15f)))
                    : 0.6f;
                if (!audioOn) wave = 0.15f;
                float bh = std::round(4.0f + wave * 18.0f);
                fill_px(rt, br, 286.0f + b * 5.0f, 162.0f - bh + heroDy, 3.0f, bh,
                        audioOn ? pal.pixel_mint : pal.border, pHero);
            }
        }
    }

    std::wstring headline = L"Waiting for AirPlay\u2026";
    std::wstring subline  = L"Control Center \u2192 Screen Mirroring \u2192 \u201CGhost\u201D";

    if (st.phase == GhostPhase::Starting) {
        headline = L"Starting Ghost Engine\u2026";
        subline  = L"Initializing hardware-accelerated D3D11 + WASAPI";
    } else if (st.phase == GhostPhase::PairingPin) {
        headline = L"Enter PIN on Device";
        subline  = L"Type the 4-digit code shown above to pair";
    } else if (st.phase == GhostPhase::Connected) {
        headline = st.client_name.empty() ? L"Mirroring Active" : utf8_to_wide(st.client_name);
        std::wstring model = st.client_model.empty() ? L"Live AirPlay Stream" : utf8_to_wide(st.client_model);
        subline = model + L" \u00B7 Alt+Enter for Fullscreen";
    } else if (st.phase == GhostPhase::Error) {
        headline = L"Engine Needs Attention";
        subline  = st.error_msg.empty() ? L"Click the Ghost mascot or RESTART below" : utf8_to_wide(st.error_msg);
    }

    br->SetColor(D2D1::ColorF(pal.ink, pHero));
    rt->DrawTextW(headline.c_str(), (UINT32)headline.size(), g_fmtTitle,
                  D2D1::RectF(24.0f, 218.0f + heroDy, 436.0f, 250.0f + heroDy), br);

    br->SetColor(D2D1::ColorF(pal.ink_muted, pHero));
    rt->DrawTextW(subline.c_str(), (UINT32)subline.size(), g_fmtSub,
                  D2D1::RectF(24.0f, 252.0f + heroDy, 436.0f, 278.0f + heroDy), br);

    br->SetColor(D2D1::ColorF(pal.border, 1.0f));
    rt->DrawLine(D2D1::Point2F(0.0f, 296.5f), D2D1::Point2F(LOGICAL_W, 296.5f), br, 1.0f);

    // 4. Group 1: Quick Presets
    {
        float dy = (1.0f - pPreset) * 8.0f;
        br->SetColor(D2D1::ColorF(pal.ink_subtle, pPreset));
        const wchar_t* hdr = L"QUICK PRESET";
        rt->DrawTextW(hdr, (UINT32)wcslen(hdr), g_fmtMicro,
                      D2D1::RectF(24.0f, 308.0f + dy, 200.0f, 323.0f + dy), br);

        if (cfg.preset == GhostPreset::Custom) {
            br->SetColor(D2D1::ColorF(pal.pixel_cyan, pPreset));
            const wchar_t* cTag = L"CUSTOM TUNING";
            rt->DrawTextW(cTag, (UINT32)wcslen(cTag), g_fmtRight,
                          D2D1::RectF(240.0f, 308.0f + dy, 436.0f, 323.0f + dy), br);
        }

        draw_preset_btn(rt, br, pal, HIT_PRESET_0, g_rects.presetBtn[0], dy, pPreset,
                        L"1080p LOW-LAT", cfg.preset == GhostPreset::LowLatency1080p60);
        draw_preset_btn(rt, br, pal, HIT_PRESET_1, g_rects.presetBtn[1], dy, pPreset,
                        L"1440p SHARP", cfg.preset == GhostPreset::Sharp1440p60);
        draw_preset_btn(rt, br, pal, HIT_PRESET_2, g_rects.presetBtn[2], dy, pPreset,
                        L"4K H.265", cfg.preset == GhostPreset::Ultra4K60H265);
    }

    // 5. Group 2: Resolution Segmented Bar
    {
        float dy = (1.0f - pRes) * 8.0f;
        br->SetColor(D2D1::ColorF(pal.ink_subtle, pRes));
        const wchar_t* hdr = L"RESOLUTION";
        rt->DrawTextW(hdr, (UINT32)wcslen(hdr), g_fmtMicro,
                      D2D1::RectF(24.0f, 374.0f + dy, 200.0f, 389.0f + dy), br);

        std::wstring dims = utf8_to_wide(res_dimensions(cfg.res));
        br->SetColor(D2D1::ColorF(pal.ink_muted, pRes));
        rt->DrawTextW(dims.c_str(), (UINT32)dims.size(), g_fmtRight,
                      D2D1::RectF(260.0f, 374.0f + dy, 436.0f, 389.0f + dy), br);

        static const wchar_t* resLabels[4] = { L"720p", L"1080p", L"1440p", L"4K UHD" };
        draw_segmented_bar(rt, br, pal, g_rects.resBar, g_rects.resSeg, HIT_RES_0, 4,
                           static_cast<int>(cfg.res), resLabels, dy, pRes);
    }

    // 6. Group 3: FPS + Codec Segmented Bars
    {
        float dy = (1.0f - pFps) * 8.0f;
        br->SetColor(D2D1::ColorF(pal.ink_subtle, pFps));
        const wchar_t* fpsHdr = L"FRAME RATE";
        rt->DrawTextW(fpsHdr, (UINT32)wcslen(fpsHdr), g_fmtMicro,
                      D2D1::RectF(24.0f, 438.0f + dy, 240.0f, 453.0f + dy), br);

        const wchar_t* codecHdr = L"VIDEO CODEC";
        rt->DrawTextW(codecHdr, (UINT32)wcslen(codecHdr), g_fmtMicro,
                      D2D1::RectF(262.0f, 438.0f + dy, 436.0f, 453.0f + dy), br);

        static const wchar_t* fpsLabels[3] = { L"30 HZ", L"60 HZ", L"120 HZ" };
        int fpsIdx = (cfg.fps == 30) ? 0 : (cfg.fps == 60 ? 1 : 2);
        draw_segmented_bar(rt, br, pal, g_rects.fpsBar, g_rects.fpsSeg, HIT_FPS_0, 3,
                           fpsIdx, fpsLabels, dy, pFps);

        static const wchar_t* codecLabels[2] = { L"H.264", L"H.265" };
        int codecIdx = (cfg.codec == GhostCodec::H265) ? 1 : 0;
        draw_segmented_bar(rt, br, pal, g_rects.codecBar, g_rects.codecSeg, HIT_CODEC_0, 2,
                           codecIdx, codecLabels, dy, pFps);
    }

    // 7. Group 4: Option Switches
    {
        float dy = (1.0f - pOpt) * 8.0f;
        br->SetColor(D2D1::ColorF(pal.ink_subtle, pOpt));
        const wchar_t* hdr = L"OPTIONS";
        rt->DrawTextW(hdr, (UINT32)wcslen(hdr), g_fmtMicro,
                      D2D1::RectF(24.0f, 504.0f + dy, 200.0f, 519.0f + dy), br);

        bool audioOn = cfg.audio_enabled && cfg.volume_percent > 0;
        draw_switch_tile(rt, br, pal, HIT_VIEW, g_rects.viewTile, dy, pOpt,
                         L"DISPLAY", cfg.fullscreen ? L"FULL" : L"WINDOW",
                         cfg.fullscreen, pal.pixel_cyan);
        draw_switch_tile(rt, br, pal, HIT_VSYNC, g_rects.vsyncTile, dy, pOpt,
                         L"VSYNC", cfg.vsync ? L"ON" : L"OFF",
                         cfg.vsync, pal.pixel_mint);
        draw_switch_tile(rt, br, pal, HIT_PIN, g_rects.pinTile, dy, pOpt,
                         L"PIN LOCK", cfg.pin_enabled ? L"ON" : L"OFF",
                         cfg.pin_enabled, pal.accent_vermilion);
        draw_switch_tile(rt, br, pal, HIT_AUDIO, g_rects.audioTile, dy, pOpt,
                         L"AUDIO", audioOn ? L"ON" : L"MUTE",
                         audioOn, pal.pixel_mint);
    }

    // 8. Group 5: 20-Segment Pixel WASAPI Volume Fader
    {
        float dy = (1.0f - pVol) * 8.0f;
        float hv = std::max(g_hover_anim[HIT_VOL], g_dragging_vol ? 1.0f : 0.0f);
        D2D1_RECT_F vr = offset_rect(g_rects.volBox, 0.0f, dy);

        if (hv > 0.02f) {
            D2D1_RECT_F sh = offset_rect(vr, 2.0f * hv, 2.0f * hv);
            br->SetColor(D2D1::ColorF(pal.shadow_ink, pVol * hv * 0.85f));
            rt->FillRectangle(sh, br);
        }

        br->SetColor(D2D1::ColorF(pal.surface, pVol));
        rt->FillRectangle(vr, br);
        draw_rect_1px(rt, br, vr, (hv > 0.25f || g_dragging_vol) ? pal.border_strong : pal.border, pVol);

        bool audioOn = cfg.audio_enabled && cfg.volume_percent > 0;
        br->SetColor(D2D1::ColorF(audioOn ? pal.ink : pal.accent_vermilion, pVol));
        const wchar_t* vLbl = audioOn ? L"WASAPI" : L"MUTED";
        rt->DrawTextW(vLbl, (UINT32)wcslen(vLbl), g_fmtMicro,
                      D2D1::RectF(vr.left + 12.0f, vr.top, vr.left + 84.0f, vr.bottom), br);

        constexpr int SEGMENTS = 20;
        float trackL = g_rects.volTrack.left;
        float trackW = g_rects.volTrack.right - g_rects.volTrack.left;
        float segGap = 4.0f;
        float segW = std::floor((trackW - (SEGMENTS - 1) * segGap) / SEGMENTS);
        float targetVol = audioOn ? g_vol_visual : 0.0f;
        int filledCount = (int)std::round(targetVol * SEGMENTS);

        for (int i = 0; i < SEGMENTS; ++i) {
            float sx = trackL + i * (segW + segGap);
            bool active = (i < filledCount);
            bool isHead = (i == filledCount - 1) && active;
            float baseH = isHead ? 22.0f : 16.0f;
            if (active && st.phase == GhostPhase::Connected && cfg.motion_enabled) {
                baseH += std::round(std::sin(g_time * 9.0f + i * 0.65f) * 2.0f);
            }
            float sy = (vr.top + vr.bottom) * 0.5f - baseH * 0.5f;
            UINT32 col = pal.surface_muted;
            if (active) {
                col = (i >= 17) ? pal.accent_vermilion : pal.ink;
            }
            fill_px(rt, br, sx, sy, segW, baseH, col, pVol);
            if (!active) {
                draw_rect_1px(rt, br, D2D1::RectF(sx, sy, sx + segW, sy + baseH), pal.border, pVol * 0.7f);
            }
        }

        std::wstring pctTxt = std::to_wstring(audioOn ? cfg.volume_percent : 0) + L"%";
        br->SetColor(D2D1::ColorF(audioOn ? pal.ink : pal.ink_muted, pVol));
        rt->DrawTextW(pctTxt.c_str(), (UINT32)pctTxt.size(), g_fmtRight,
                      D2D1::RectF(374.0f, vr.top, vr.right - 12.0f, vr.bottom), br);
    }

    // 9. Architectural Colophon Footer (y: 652..704)
    br->SetColor(D2D1::ColorF(pal.surface, 1.0f));
    rt->FillRectangle(D2D1::RectF(0.0f, 652.0f, LOGICAL_W, LOGICAL_H), br);
    br->SetColor(D2D1::ColorF(pal.border, 1.0f));
    rt->DrawLine(D2D1::Point2F(0.0f, 652.5f), D2D1::Point2F(LOGICAL_W, 652.5f), br, 1.0f);

    std::wstring footerTxt;
    UINT32 footerCol = pal.ink_muted;
    if (g_pending_apply) {
        footerTxt = L"APPLYING NEW SETTINGS\u2026";
        footerCol = pal.accent_vermilion;
    } else if (st.lan_ip.empty()) {
        footerTxt = L"UXPLAY 1.74 \u00B7 D3D11 + WASAPI";
    } else {
        footerTxt = L"LAN " + utf8_to_wide(st.lan_ip) + L" \u00B7 UXPLAY 1.74";
    }
    br->SetColor(D2D1::ColorF(footerCol, 1.0f));
    rt->DrawTextW(footerTxt.c_str(), (UINT32)footerTxt.size(), g_fmtFooter,
                  D2D1::RectF(24.0f, 652.0f, 326.0f, LOGICAL_H), br);

    {
        float hv = g_hover_anim[HIT_RESTART];
        float pr = g_press_anim[HIT_RESTART];
        float lift = -2.0f * hv + 2.0f * pr;

        if (hv > 0.02f) {
            D2D1_RECT_F sh = offset_rect(g_rects.restartBtn, 3.0f * hv, 3.0f * hv);
            br->SetColor(D2D1::ColorF(pal.accent_vermilion, hv));
            rt->FillRectangle(sh, br);
        }

        D2D1_RECT_F rr = offset_rect(g_rects.restartBtn, lift, lift);
        br->SetColor(D2D1::ColorF(hv > 0.45f ? pal.ink : pal.surface, 1.0f));
        rt->FillRectangle(rr, br);
        draw_rect_1px(rt, br, rr, pal.border_strong, 1.0f);

        br->SetColor(D2D1::ColorF(hv > 0.45f ? pal.ink_on_active : pal.ink, 1.0f));
        const wchar_t* rTxt = L"RESTART >";
        rt->DrawTextW(rTxt, (UINT32)wcslen(rTxt), g_fmtTag, rr, br);
    }

    draw_rect_1px(rt, br, D2D1::RectF(0.0f, 0.0f, LOGICAL_W, LOGICAL_H),
                  cfg.dark_mode ? pal.border : pal.ink, 1.0f);
}

static void render_ui(HWND hwnd) {
    if (FAILED(init_d2d(hwnd))) return;

    GhostConfig cfg;
    GhostState st;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cfg = g_cfg;
        st = g_state;
    }

    draw_scene(g_rt, g_br, cfg, st);
    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        discard_d2d_targets();
    }
}

static void render_ui_to_dc(HWND hwnd, HDC hdc) {
    if (FAILED(init_d2d(hwnd)) || !g_d2dFactory) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = std::max(1, (int)(rc.right - rc.left));
    int h = std::max(1, (int)(rc.bottom - rc.top));

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memDC || !dib) {
        if (dib) DeleteObject(dib);
        if (memDC) DeleteDC(memDC);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, dib);

    float dpi = get_window_dpi(hwnd);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpi, dpi
    );
    ID2D1DCRenderTarget* dcRt = nullptr;
    if (SUCCEEDED(g_d2dFactory->CreateDCRenderTarget(&props, &dcRt)) && dcRt) {
        if (SUCCEEDED(dcRt->BindDC(memDC, &rc))) {
            dcRt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
            ID2D1SolidColorBrush* br = nullptr;
            if (SUCCEEDED(dcRt->CreateSolidColorBrush(D2D1::ColorF(Tok::INK, 1.0f), &br)) && br) {
                GhostConfig cfg;
                GhostState st;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    cfg = g_cfg;
                    st = g_state;
                }
                draw_scene(dcRt, br, cfg, st);
                dcRt->EndDraw();
                br->Release();
            }
        }
        dcRt->Release();
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
}

// ============================================================================
// Animation Update Loop (Real QPC Delta-Time + Auto-Idle Power Save)
// ============================================================================
static void ensure_anim_timer(HWND hwnd) {
    if (!g_anim_running && !g_window_minimized && IsWindowVisible(hwnd)) {
        QueryPerformanceCounter(&g_qpc_prev);
        SetTimer(hwnd, TIMER_ANIM, 16, nullptr);
        g_anim_running = true;
    }
}

static void stop_anim_timer(HWND hwnd) {
    if (g_anim_running) {
        KillTimer(hwnd, TIMER_ANIM);
        g_anim_running = false;
    }
}

static void tick_animation(HWND hwnd) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = static_cast<float>(now.QuadPart - g_qpc_prev.QuadPart) /
               static_cast<float>(g_qpc_freq.QuadPart);
    g_qpc_prev = now;
    dt = std::clamp(dt, 0.001f, 0.05f);

    bool motionOn = true;
    float targetVol = 0.85f;
    GhostPhase phase = GhostPhase::Waiting;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        motionOn = g_cfg.motion_enabled;
        targetVol = (g_cfg.audio_enabled && g_cfg.volume_percent > 0)
            ? (g_cfg.volume_percent / 100.0f) : 0.0f;
        phase = g_state.phase;
    }

    if (phase != g_prev_phase) {
        if (phase == GhostPhase::Connected) {
            spawn_sparkles(230.0f, 140.0f, 10, Tok::PIXEL_MINT);
        } else if (phase == GhostPhase::Error) {
            spawn_sparkles(230.0f, 140.0f, 8, Tok::ACCENT_VERMILION);
        }
        g_prev_phase = phase;
    }

    bool any_transition_active = false;

    if (g_intro_t < 0.70f) {
        g_intro_t += dt;
        any_transition_active = true;
    }

    const float hoverRate = dt / 0.16f;
    const float pressRate = dt / 0.09f;
    for (int i = 1; i < HIT_COUNT; ++i) {
        float th = (g_hover_id == i) ? 1.0f : 0.0f;
        float tp = (g_pressed_id == i) ? 1.0f : 0.0f;
        if (std::abs(g_hover_anim[i] - th) > 0.005f) {
            g_hover_anim[i] += std::clamp(th - g_hover_anim[i], -hoverRate, hoverRate);
            any_transition_active = true;
        } else {
            g_hover_anim[i] = th;
        }
        if (std::abs(g_press_anim[i] - tp) > 0.005f) {
            g_press_anim[i] += std::clamp(tp - g_press_anim[i], -pressRate, pressRate);
            any_transition_active = true;
        } else {
            g_press_anim[i] = tp;
        }
    }

    if (std::abs(g_vol_visual - targetVol) > 0.003f) {
        g_vol_visual += (targetVol - g_vol_visual) * std::min(1.0f, dt * 18.0f);
        any_transition_active = true;
    } else {
        g_vol_visual = targetVol;
    }

    if (motionOn) {
        g_time += dt;
        for (auto& s : g_sparkles) {
            if (!s.active) continue;
            s.life += dt;
            if (s.life >= s.max_life) {
                s.active = false;
                continue;
            }
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.vx *= (1.0f - dt * 2.5f);
            s.vy *= (1.0f - dt * 2.5f);
        }
        any_transition_active = true;
    }

    InvalidateRect(hwnd, nullptr, FALSE);

    if (!motionOn && !any_transition_active) {
        stop_anim_timer(hwnd);
    }
}

static void update_volume_from_mouse(float dipX) {
    float left = g_rects.volTrack.left;
    float right = g_rects.volTrack.right;
    float t = (dipX - left) / (right - left);
    int pct = (int)std::round(std::clamp(t, 0.0f, 1.0f) * 100.0f);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cfg.volume_percent = pct;
        g_cfg.audio_enabled = (pct > 0);
    }
    apply_wasapi_volume();
}

static void schedule_engine_apply(HWND hwnd) {
    g_pending_apply = true;
    SetTimer(hwnd, TIMER_APPLY_CFG, 420, nullptr);
    ensure_anim_timer(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ============================================================================
// Realtime Live Video-Window Synchronizer + Skinning (150ms Ground-Truth Sync)
// Instantly reflects AirPlay Connect & Disconnect AND skins the D3D11 window!
// ============================================================================
struct VidEnumCtx {
    DWORD pid;
    HWND found;
};

static BOOL CALLBACK enum_uxplay_vid_cb(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<VidEnumCtx*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    wchar_t title[128] = {};
    GetWindowTextW(hwnd, title, 128);
    if (wcslen(title) == 0) return TRUE;
    ctx->found = hwnd;
    return FALSE;
}

static void sync_live_video_window(HWND hwnd) {
    DWORD pid = 0;
    GhostPhase phase = GhostPhase::Starting;
    std::string clientName;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        pid = g_uxplay_pid;
        phase = g_state.phase;
        clientName = g_state.client_name;
    }
    if (pid == 0) {
        g_styled_vid_hwnd = nullptr;
        g_styled_vid_title.clear();
        g_had_video_window = false;
        return;
    }

    HWND target = g_styled_vid_hwnd;
    if (!target || !IsWindow(target) || !IsWindowVisible(target)) {
        VidEnumCtx ctx = { pid, nullptr };
        EnumWindows(enum_uxplay_vid_cb, reinterpret_cast<LPARAM>(&ctx));
        target = ctx.found;
    }

    if (target) {
        bool isNewWindow = (target != g_styled_vid_hwnd);
        g_had_video_window = true;

        // If the mirroring window appeared before log parsing caught up -> flip LIVE instantly!
        if (phase != GhostPhase::Connected) {
            {
                std::lock_guard<std::mutex> lk(g_mu);
                g_state.phase = GhostPhase::Connected;
                g_state.pin.clear();
                g_state.error_msg.clear();
            }
            apply_wasapi_volume();
            ensure_anim_timer(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        std::wstring desiredTitle = clientName.empty()
            ? L"GHOST MIRROR \u00B7 Live Display"
            : (L"GHOST MIRROR \u00B7 " + utf8_to_wide(clientName));

        if (isNewWindow) {
            g_styled_vid_hwnd = target;

            BOOL dark = TRUE;
            DwmSetWindowAttribute(target, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            DWORD corner = DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(target, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            COLORREF captionCol = RGB(0x11, 0x11, 0x11);
            COLORREF textCol    = RGB(0xFF, 0xFF, 0xFF);
            COLORREF borderCol  = RGB(0x11, 0x11, 0x11);
            DwmSetWindowAttribute(target, DWMWA_CAPTION_COLOR, &captionCol, sizeof(captionCol));
            DwmSetWindowAttribute(target, DWMWA_TEXT_COLOR, &textCol, sizeof(textCol));
            DwmSetWindowAttribute(target, DWMWA_BORDER_COLOR, &borderCol, sizeof(borderCol));

            if (g_appIcon) {
                SendMessageTimeoutW(target, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon, SMTO_ABORTIFHUNG, 150, nullptr);
            }
            if (g_appIconSmall || g_appIcon) {
                SendMessageTimeoutW(target, WM_SETICON, ICON_SMALL,
                                    (LPARAM)(g_appIconSmall ? g_appIconSmall : g_appIcon),
                                    SMTO_ABORTIFHUNG, 150, nullptr);
            }
            apply_wasapi_volume();
        }

        if (isNewWindow || g_styled_vid_title != desiredTitle) {
            SetWindowTextW(target, desiredTitle.c_str());
            g_styled_vid_title = desiredTitle;
        }
    } else if (g_had_video_window) {
        // Mirroring window just closed -> instant <150ms disconnect detection!
        g_had_video_window = false;
        g_styled_vid_hwnd = nullptr;
        g_styled_vid_title.clear();
        if (phase == GhostPhase::Connected) {
            {
                std::lock_guard<std::mutex> lk(g_mu);
                g_state.phase = GhostPhase::Waiting;
                g_state.client_name.clear();
                g_state.client_model.clear();
                g_state.pin.clear();
                g_state.error_msg.clear();
            }
            ensure_anim_timer(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
}

// ============================================================================
// System Tray Integration
// ============================================================================
static void setup_tray_icon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_appIconSmall ? g_appIconSmall : (g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION));
    wcscpy_s(g_nid.szTip, L"GHOST MIRROR \u2014 hail than tai");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void toggle_dark_mode(HWND hwnd) {
    bool isDark = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cfg.dark_mode = !g_cfg.dark_mode;
        isDark = g_cfg.dark_mode;
    }
    BOOL darkAttr = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkAttr, sizeof(darkAttr));
    ensure_anim_timer(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void show_tray_menu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    bool motionOn = true;
    bool darkOn = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        motionOn = g_cfg.motion_enabled;
        darkOn = g_cfg.dark_mode;
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Show Ghost Mirror");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RESTART, L"Restart AirPlay Engine");
    AppendMenuW(menu, MF_STRING | (darkOn ? MF_CHECKED : 0), ID_TRAY_THEME, L"Dark Blueprint Mode");
    AppendMenuW(menu, MF_STRING | (motionOn ? MF_CHECKED : 0), ID_TRAY_MOTION, L"Ambient Pixel Motion");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Quit Ghost Mirror");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

// ============================================================================
// Win32 Window Procedure
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            BOOL dark = FALSE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            DWORD corner = DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            if (g_appIcon) {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
            }
            if (g_appIconSmall || g_appIcon) {
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(g_appIconSmall ? g_appIconSmall : g_appIcon));
            }

            BOOL animAllowed = TRUE;
            SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animAllowed, 0);
            {
                std::lock_guard<std::mutex> lk(g_mu);
                g_cfg.motion_enabled = (animAllowed != FALSE);
            }

            QueryPerformanceFrequency(&g_qpc_freq);
            QueryPerformanceCounter(&g_qpc_prev);
            init_particles();
            ensure_anim_timer(hwnd);
            SetTimer(hwnd, TIMER_VID_SKIN, 150, nullptr); // 150ms realtime connect/disconnect + window skinning
            setup_tray_icon(hwnd);
            start_uxplay_engine();
            return 0;
        }
        case WM_STATE_CHANGED: {
            ensure_anim_timer(hwnd);
            sync_live_video_window(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_APPLY_WASAPI: {
            apply_wasapi_volume();
            sync_live_video_window(hwnd);
            SetTimer(hwnd, TIMER_WASAPI, 750, nullptr);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == TIMER_ANIM) {
                tick_animation(hwnd);
            } else if (wParam == TIMER_WASAPI) {
                KillTimer(hwnd, TIMER_WASAPI);
                apply_wasapi_volume();
                sync_live_video_window(hwnd);
            } else if (wParam == TIMER_VID_SKIN) {
                sync_live_video_window(hwnd);
            } else if (wParam == TIMER_APPLY_CFG) {
                KillTimer(hwnd, TIMER_APPLY_CFG);
                g_pending_apply = false;
                start_uxplay_engine();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                g_window_minimized = true;
                stop_anim_timer(hwnd);
            } else {
                g_window_minimized = false;
                if (g_rt) {
                    UINT w = std::max<UINT>(1, LOWORD(lParam));
                    UINT h = std::max<UINT>(1, HIWORD(lParam));
                    g_rt->Resize(D2D1::SizeU(w, h));
                }
                ensure_anim_timer(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            discard_d2d_targets();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            render_ui(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            render_ui_to_dc(hwnd, reinterpret_cast<HDC>(wParam));
            return 0;
        }
        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (hit_test_ui(px_to_dip(hwnd, pt.x), px_to_dip(hwnd, pt.y)) != HIT_NONE) {
                return HTCLIENT;
            }
            return HTCAPTION;
        }
        case WM_LBUTTONDOWN: {
            float x = px_to_dip(hwnd, GET_X_LPARAM(lParam));
            float y = px_to_dip(hwnd, GET_Y_LPARAM(lParam));
            int h = hit_test_ui(x, y);
            g_pressed_id = h;
            ensure_anim_timer(hwnd);
            if (h == HIT_VOL) {
                g_dragging_vol = true;
                SetCapture(hwnd);
                update_volume_from_mouse(x);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            float x = px_to_dip(hwnd, GET_X_LPARAM(lParam));
            float y = px_to_dip(hwnd, GET_Y_LPARAM(lParam));
            g_mouse_x = x;
            g_mouse_y = y;

            if (!g_mouse_inside) {
                g_mouse_inside = true;
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }

            if (g_dragging_vol) {
                update_volume_from_mouse(x);
                ensure_anim_timer(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int h = hit_test_ui(x, y);
            if (h != g_hover_id) {
                g_hover_id = h;
                ensure_anim_timer(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, h != HIT_NONE ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE: {
            g_mouse_inside = false;
            if (g_hover_id != HIT_NONE) {
                g_hover_id = HIT_NONE;
                ensure_anim_timer(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            float x = px_to_dip(hwnd, GET_X_LPARAM(lParam));
            float y = px_to_dip(hwnd, GET_Y_LPARAM(lParam));
            g_pressed_id = HIT_NONE;
            ensure_anim_timer(hwnd);

            if (g_dragging_vol) {
                g_dragging_vol = false;
                ReleaseCapture();
                update_volume_from_mouse(x);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int h = hit_test_ui(x, y);
            if (h == HIT_CLOSE) {
                DestroyWindow(hwnd);
            } else if (h == HIT_MIN) {
                ShowWindow(hwnd, SW_MINIMIZE);
            } else if (h == HIT_MOTION) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.motion_enabled = !g_cfg.motion_enabled;
                }
                ensure_anim_timer(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (h == HIT_THEME) {
                toggle_dark_mode(hwnd);
            } else if (h == HIT_HERO || h == HIT_RESTART) {
                KillTimer(hwnd, TIMER_APPLY_CFG);
                g_pending_apply = false;
                spawn_sparkles(230.0f, 140.0f, 10, Tok::ACCENT_VERMILION);
                start_uxplay_engine();
            } else if (h >= HIT_PRESET_0 && h <= HIT_PRESET_2) {
                GhostPreset target = static_cast<GhostPreset>(h - HIT_PRESET_0);
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    if (g_cfg.preset != target) {
                        apply_preset(g_cfg, target);
                        changed = true;
                    }
                }
                if (changed) schedule_engine_apply(hwnd);
            } else if (h >= HIT_RES_0 && h <= HIT_RES_3) {
                GhostRes target = static_cast<GhostRes>(h - HIT_RES_0);
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    if (g_cfg.res != target) {
                        g_cfg.res = target;
                        sync_preset_from_config(g_cfg);
                        changed = true;
                    }
                }
                if (changed) schedule_engine_apply(hwnd);
            } else if (h >= HIT_FPS_0 && h <= HIT_FPS_2) {
                static constexpr int kFpsVals[3] = { 30, 60, 120 };
                int target = kFpsVals[h - HIT_FPS_0];
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    if (g_cfg.fps != target) {
                        g_cfg.fps = target;
                        sync_preset_from_config(g_cfg);
                        changed = true;
                    }
                }
                if (changed) schedule_engine_apply(hwnd);
            } else if (h == HIT_CODEC_0 || h == HIT_CODEC_1) {
                GhostCodec target = (h == HIT_CODEC_1) ? GhostCodec::H265 : GhostCodec::H264;
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    if (g_cfg.codec != target) {
                        g_cfg.codec = target;
                        sync_preset_from_config(g_cfg);
                        changed = true;
                    }
                }
                if (changed) schedule_engine_apply(hwnd);
            } else if (h == HIT_VIEW) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.fullscreen = !g_cfg.fullscreen;
                }
                schedule_engine_apply(hwnd);
            } else if (h == HIT_VSYNC) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.vsync = !g_cfg.vsync;
                    sync_preset_from_config(g_cfg);
                }
                schedule_engine_apply(hwnd);
            } else if (h == HIT_PIN) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.pin_enabled = !g_cfg.pin_enabled;
                }
                schedule_engine_apply(hwnd);
            } else if (h == HIT_AUDIO) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.audio_enabled = !g_cfg.audio_enabled;
                    if (g_cfg.audio_enabled && g_cfg.volume_percent == 0) {
                        g_cfg.volume_percent = 85;
                    }
                }
                apply_wasapi_volume();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (wParam == 'R') {
                KillTimer(hwnd, TIMER_APPLY_CFG);
                g_pending_apply = false;
                spawn_sparkles(230.0f, 140.0f, 10, Tok::ACCENT_VERMILION);
                ensure_anim_timer(hwnd);
                start_uxplay_engine();
            } else if (wParam == 'M') {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.audio_enabled = !g_cfg.audio_enabled;
                    if (g_cfg.audio_enabled && g_cfg.volume_percent == 0) g_cfg.volume_percent = 85;
                }
                apply_wasapi_volume();
                ensure_anim_timer(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wParam == 'T') {
                toggle_dark_mode(hwnd);
            } else if (wParam == VK_SPACE) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.motion_enabled = !g_cfg.motion_enabled;
                }
                ensure_anim_timer(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
            return 0;
        }
        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            } else if (lParam == WM_RBUTTONUP) {
                show_tray_menu(hwnd);
            }
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_TRAY_SHOW:
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    break;
                case ID_TRAY_RESTART:
                    start_uxplay_engine();
                    break;
                case ID_TRAY_THEME:
                    toggle_dark_mode(hwnd);
                    break;
                case ID_TRAY_MOTION: {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.motion_enabled = !g_cfg.motion_enabled;
                    ensure_anim_timer(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }
        case WM_DESTROY: {
            g_quitting.store(true);
            stop_anim_timer(hwnd);
            KillTimer(hwnd, TIMER_WASAPI);
            KillTimer(hwnd, TIMER_VID_SKIN);
            KillTimer(hwnd, TIMER_APPLY_CFG);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            stop_uxplay_engine();
            release_all_d2d();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    enable_per_monitor_dpi();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    HANDLE singleMutex = CreateMutexW(nullptr, TRUE, L"Local\\GhostMirrorReceiverSingleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"GhostMirrorWndClass", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    g_job = CreateJobObjectW(nullptr, nullptr);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    g_appIcon = reinterpret_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_GHOST_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    g_appIconSmall = reinterpret_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_GHOST_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!g_appIcon) {
        g_appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (!g_appIconSmall) {
        g_appIconSmall = g_appIcon;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = g_appIcon;
    wc.hIconSm = g_appIconSmall;
    wc.lpszClassName = L"GhostMirrorWndClass";
    RegisterClassExW(&wc);

    HDC screenDC = GetDC(nullptr);
    float sysDpi = screenDC ? static_cast<float>(GetDeviceCaps(screenDC, LOGPIXELSX)) : 96.0f;
    if (screenDC) ReleaseDC(nullptr, screenDC);

    int winW = (int)std::round(LOGICAL_W * sysDpi / 96.0f);
    int winH = (int)std::round(LOGICAL_H * sysDpi / 96.0f);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"GHOST MIRROR \u2014 hail than tai",
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, winW, winH,
        nullptr, nullptr, hInstance, nullptr
    );

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_job) CloseHandle(g_job);
    if (singleMutex) CloseHandle(singleMutex);
    CoUninitialize();
    return 0;
}
