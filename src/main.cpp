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
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>

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
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

constexpr UINT IDI_GHOST_ICON = 101;
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT ID_TRAY_SHOW = 1001;
constexpr UINT ID_TRAY_RESTART = 1002;
constexpr UINT ID_TRAY_EXIT = 1003;
constexpr UINT_PTR TIMER_ANIM = 1;

static HWND g_hwnd = nullptr;
static HICON g_appIcon = nullptr;
static ID2D1Factory* g_d2dFactory = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IDWriteFactory* g_dwFactory = nullptr;
static IDWriteTextFormat* g_fmtBrand = nullptr;
static IDWriteTextFormat* g_fmtHail = nullptr;
static IDWriteTextFormat* g_fmtTitle = nullptr;
static IDWriteTextFormat* g_fmtSub = nullptr;
static IDWriteTextFormat* g_fmtPin = nullptr;
static IDWriteTextFormat* g_fmtSection = nullptr;
static IDWriteTextFormat* g_fmtPill = nullptr;
static IDWriteTextFormat* g_fmtFooter = nullptr;

static std::mutex g_mu;
static GhostConfig g_cfg;
static GhostState g_state;

static HANDLE g_job = nullptr;
static HANDLE g_proc = nullptr;
static DWORD g_uxplay_pid = 0;
static std::atomic<bool> g_quitting{false};
static std::atomic<int> g_generation{0};

static float g_time = 0.0f;
static int g_tick_count = 0;
// Hit IDs:
// 1=close, 2=min, 3=ring, 4=preset, 5=res, 6=fps, 7=codec, 8=view, 9=vsync, 10=audio_mute, 11=pin, 12=restart, 13=vol_slider
static int g_hover_id = 0;
static bool g_dragging_vol = false;
static NOTIFYICONDATAW g_nid = {};

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

// Real-time WASAPI per-process volume & mute control (0ms latency, no stream restart required)
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
        // Keep WASAPI sink active so volume slider & mute work in real time without restarting
        cfg_copy.audio_enabled = true;
        g_state = GhostState{};
        g_state.phase = GhostPhase::Starting;
    }

    std::thread([my_gen, cfg_copy]() {
        std::wstring bin_dir, gst_dir;
        std::wstring exe_path = locate_uxplay_exe(bin_dir, gst_dir);
        if (exe_path.empty()) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_generation.load() == my_gen) {
                g_state.phase = GhostPhase::Error;
                g_state.error_msg = "uxplay.exe not found in ./engine or build folder";
            }
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
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
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
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
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);

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
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    if (g_generation.load() == my_gen) {
                        parse_uxplay_line(g_state, line);
                    }
                }
                if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
            }
        }
        CloseHandle(rPipe);

        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_generation.load() == my_gen && !g_quitting.load()) {
                if (g_state.phase != GhostPhase::Error) {
                    g_state.phase = GhostPhase::Error;
                    g_state.error_msg = "Engine stopped unexpectedly. Click ring to restart.";
                }
            }
        }
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    }).detach();
}

struct UiRects {
    D2D1_RECT_F closeBtn   = D2D1::RectF(396, 14, 426, 40);
    D2D1_RECT_F minBtn     = D2D1::RectF(362, 14, 392, 40);
    D2D1_RECT_F ringHit    = D2D1::RectF(130, 74, 310, 246);
    // Quick Preset Bar
    D2D1_RECT_F presetPill = D2D1::RectF(28, 356, 412, 394);
    // Advance Row 1: RES / FPS / CODEC
    D2D1_RECT_F resPill    = D2D1::RectF(28, 420, 150, 458);
    D2D1_RECT_F fpsPill    = D2D1::RectF(159, 420, 281, 458);
    D2D1_RECT_F codecPill  = D2D1::RectF(290, 420, 412, 458);
    // Advance Row 2: VIEW / VSYNC / AUDIO / PIN
    D2D1_RECT_F viewPill   = D2D1::RectF(28, 466, 118, 504);
    D2D1_RECT_F vsyncPill  = D2D1::RectF(126, 466, 216, 504);
    D2D1_RECT_F audioPill  = D2D1::RectF(224, 466, 314, 504);
    D2D1_RECT_F pinPill    = D2D1::RectF(322, 466, 412, 504);
    // Row 3: Interactive Volume Slider Box
    D2D1_RECT_F volBox     = D2D1::RectF(28, 514, 412, 554);
    D2D1_RECT_F volTrack   = D2D1::RectF(112, 531, 354, 537);
    // Footer restart
    D2D1_RECT_F restartBtn = D2D1::RectF(344, 582, 412, 608);
};

static UiRects g_rects;

static bool pt_in_rect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static int hit_test_ui(float x, float y) {
    if (pt_in_rect(x, y, g_rects.closeBtn))   return 1;
    if (pt_in_rect(x, y, g_rects.minBtn))     return 2;
    if (pt_in_rect(x, y, g_rects.ringHit))    return 3;
    if (pt_in_rect(x, y, g_rects.presetPill)) return 4;
    if (pt_in_rect(x, y, g_rects.resPill))    return 5;
    if (pt_in_rect(x, y, g_rects.fpsPill))    return 6;
    if (pt_in_rect(x, y, g_rects.codecPill))  return 7;
    if (pt_in_rect(x, y, g_rects.viewPill))   return 8;
    if (pt_in_rect(x, y, g_rects.vsyncPill))  return 9;
    if (pt_in_rect(x, y, g_rects.audioPill))  return 10;
    if (pt_in_rect(x, y, g_rects.pinPill))    return 11;
    if (pt_in_rect(x, y, g_rects.restartBtn)) return 12;
    if (pt_in_rect(x, y, g_rects.volBox))     return 13;
    return 0;
}

static void update_volume_from_mouse(float x) {
    float left = g_rects.volTrack.left;
    float right = g_rects.volTrack.right;
    float t = (x - left) / (right - left);
    int pct = (int)std::round(std::clamp(t, 0.0f, 1.0f) * 100.0f);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cfg.volume_percent = pct;
        g_cfg.audio_enabled = (pct > 0);
    }
    apply_wasapi_volume();
}

static HRESULT init_d2d(HWND hwnd) {
    if (!g_d2dFactory) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);
    }
    if (!g_dwFactory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_dwFactory));
        g_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &g_fmtBrand);
        g_dwFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &g_fmtHail);
        g_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 19.0f, L"en-us", &g_fmtTitle);
        g_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.2f, L"en-us", &g_fmtSub);
        g_dwFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 36.0f, L"en-us", &g_fmtPin);
        g_dwFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.8f, L"en-us", &g_fmtSection);
        g_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.8f, L"en-us", &g_fmtPill);
        g_dwFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &g_fmtFooter);

        if (g_fmtBrand) {
            g_fmtBrand->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        for (auto* f : {g_fmtHail, g_fmtTitle, g_fmtSub, g_fmtPin, g_fmtSection, g_fmtPill}) {
            if (f) {
                f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
        if (g_fmtFooter) {
            g_fmtFooter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (!g_rt) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;
        g_d2dFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &g_rt);
    }
    return g_rt ? S_OK : E_FAIL;
}

static void draw_pill(ID2D1SolidColorBrush* br, const D2D1_RECT_F& r, bool hovered, bool active, const wchar_t* text) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 9.0f, 9.0f);
    UINT32 bgHex = hovered ? 0x1E1E2A : (active ? 0x151923 : 0x121218);
    UINT32 borderHex = active ? 0x38BDF8 : (hovered ? 0x36384A : 0x22222E);
    float borderAlpha = active ? 0.55f : 1.0f;

    br->SetColor(D2D1::ColorF(bgHex, 1.0f));
    g_rt->FillRoundedRectangle(rr, br);

    br->SetColor(D2D1::ColorF(borderHex, borderAlpha));
    g_rt->DrawRoundedRectangle(rr, br, 1.0f);

    br->SetColor(D2D1::ColorF(active ? 0xF1F5F9 : 0x94A3B8, 1.0f));
    g_rt->DrawTextW(text, (UINT32)wcslen(text), g_fmtPill, r, br);
}

// Draws our signature Ghost Mirror emblem (dome head, cyber eyes, and AirPlay prism)
static void draw_ghost_emblem(ID2D1SolidColorBrush* br, D2D1_POINT_2F c, float scale, UINT32 accentHex) {
    // Ghost head dome + body
    float r = 18.0f * scale;
    D2D1_POINT_2F headC = D2D1::Point2F(c.x, c.y - 6.0f * scale);
    br->SetColor(D2D1::ColorF(0xE2E8F0, 0.95f));
    g_rt->FillEllipse(D2D1::Ellipse(headC, r, r), br);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(c.x - r, c.y - 6.0f * scale, c.x + r, c.y + 12.0f * scale),
        4.0f * scale, 4.0f * scale), br);

    // Cyber eyes
    br->SetColor(D2D1::ColorF(0x0B0B0E, 1.0f));
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - 6.5f * scale, c.y - 5.5f * scale), 3.6f * scale, 2.8f * scale), br);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + 6.5f * scale, c.y - 5.5f * scale), 3.6f * scale, 2.8f * scale), br);

    // Glowing cyan eye pupils
    br->SetColor(D2D1::ColorF(accentHex, 1.0f));
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - 6.5f * scale, c.y - 5.5f * scale), 1.7f * scale, 1.4f * scale), br);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x + 6.5f * scale, c.y - 5.5f * scale), 1.7f * scale, 1.4f * scale), br);

    // AirPlay prism bar beneath the ghost
    br->SetColor(D2D1::ColorF(accentHex, 0.9f));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(c.x - 9.0f * scale, c.y + 18.0f * scale, c.x + 9.0f * scale, c.y + 21.5f * scale),
        1.5f * scale, 1.5f * scale), br);
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

    g_rt->BeginDraw();
    g_rt->Clear(D2D1::ColorF(0x0B0B0E, 1.0f));

    ID2D1SolidColorBrush* br = nullptr;
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0xECECEF, 1.0f), &br);

    // Subtle outer frame border
    br->SetColor(D2D1::ColorF(0x1E1E28, 1.0f));
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(1.0f, 1.0f, 439.0f, 619.0f), 12.0f, 12.0f), br, 1.0f);

    UINT32 accentHex = 0x38BDF8; // Default cyber-ice
    if (st.phase == GhostPhase::Waiting)    accentHex = 0x38BDF8;
    if (st.phase == GhostPhase::Connected)  accentHex = 0x22D3EE;
    if (st.phase == GhostPhase::PairingPin) accentHex = 0xF59E0B;
    if (st.phase == GhostPhase::Error)      accentHex = 0xF43F5E;

    // Top-left Mini App Logo Badge + Brand Title
    D2D1_RECT_F logoBadge = D2D1::RectF(22.0f, 15.0f, 46.0f, 39.0f);
    br->SetColor(D2D1::ColorF(0x131620, 1.0f));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(logoBadge, 6.0f, 6.0f), br);
    br->SetColor(D2D1::ColorF(accentHex, 0.5f));
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(logoBadge, 6.0f, 6.0f), br, 1.0f);
    draw_ghost_emblem(br, D2D1::Point2F(34.0f, 26.5f), 0.42f, accentHex);

    br->SetColor(D2D1::ColorF(0xECECEF, 0.92f));
    const wchar_t* brand = L"G H O S T   M I R R O R";
    g_rt->DrawTextW(brand, (UINT32)wcslen(brand), g_fmtBrand, D2D1::RectF(54.0f, 14.0f, 232.0f, 40.0f), br);

    // Signature "HAIL THAN TAI" badge in header
    D2D1_RECT_F hailBadge = D2D1::RectF(232.0f, 17.0f, 350.0f, 37.0f);
    br->SetColor(D2D1::ColorF(0x141720, 1.0f));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(hailBadge, 6.0f, 6.0f), br);
    br->SetColor(D2D1::ColorF(accentHex, 0.35f));
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(hailBadge, 6.0f, 6.0f), br, 1.0f);
    br->SetColor(D2D1::ColorF(0xCBD5E1, 0.95f));
    const wchar_t* hailText = L"HAIL THAN TAI";
    g_rt->DrawTextW(hailText, (UINT32)wcslen(hailText), g_fmtHail, hailBadge, br);

    // Minimize & Close buttons
    if (g_hover_id == 2) {
        br->SetColor(D2D1::ColorF(0x1C1C26, 1.0f));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.minBtn, 6.0f, 6.0f), br);
    }
    br->SetColor(D2D1::ColorF(g_hover_id == 2 ? 0xECECEF : 0x6E7385, 1.0f));
    g_rt->DrawLine(D2D1::Point2F(371.0f, 27.0f), D2D1::Point2F(383.0f, 27.0f), br, 1.5f);

    if (g_hover_id == 1) {
        br->SetColor(D2D1::ColorF(0xF43F5E, 0.2f));
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.closeBtn, 6.0f, 6.0f), br);
    }
    br->SetColor(D2D1::ColorF(g_hover_id == 1 ? 0xF43F5E : 0x6E7385, 1.0f));
    g_rt->DrawLine(D2D1::Point2F(405.0f, 21.0f), D2D1::Point2F(417.0f, 33.0f), br, 1.5f);
    g_rt->DrawLine(D2D1::Point2F(417.0f, 21.0f), D2D1::Point2F(405.0f, 33.0f), br, 1.5f);

    // Center Hero: The Breathing Ghost Ring + Custom Ghost Logo
    D2D1_POINT_2F center = D2D1::Point2F(220.0f, 156.0f);
    float speed = (st.phase == GhostPhase::PairingPin) ? 3.2f : 1.6f;
    float wave = 0.5f + 0.5f * std::sin(g_time * speed);

    float outerR1 = 68.0f + wave * 10.0f;
    float outerR2 = 56.0f + (1.0f - wave) * 6.0f;

    br->SetColor(D2D1::ColorF(accentHex, 0.06f + 0.08f * (1.0f - wave)));
    g_rt->FillEllipse(D2D1::Ellipse(center, outerR1, outerR1), br);

    br->SetColor(D2D1::ColorF(accentHex, 0.16f + 0.14f * wave));
    g_rt->DrawEllipse(D2D1::Ellipse(center, outerR1, outerR1), br, 1.1f);

    br->SetColor(D2D1::ColorF(accentHex, 0.24f));
    g_rt->DrawEllipse(D2D1::Ellipse(center, outerR2, outerR2), br, 1.0f);

    // Core dark glass disc
    br->SetColor(D2D1::ColorF(0x111117, 1.0f));
    g_rt->FillEllipse(D2D1::Ellipse(center, 48.0f, 48.0f), br);

    // Crisp primary ring
    br->SetColor(D2D1::ColorF(accentHex, st.phase == GhostPhase::Connected ? 0.95f : (0.55f + 0.35f * wave)));
    g_rt->DrawEllipse(D2D1::Ellipse(center, 48.0f, 48.0f), br, st.phase == GhostPhase::Connected ? 2.4f : 1.8f);

    if (st.phase == GhostPhase::PairingPin && !st.pin.empty()) {
        std::wstring wpin = utf8_to_wide(st.pin);
        br->SetColor(D2D1::ColorF(0xFEF3C7, 1.0f));
        g_rt->DrawTextW(wpin.c_str(), (UINT32)wpin.size(), g_fmtPin, D2D1::RectF(140.0f, 126.0f, 300.0f, 186.0f), br);
    } else {
        draw_ghost_emblem(br, D2D1::Point2F(center.x, center.y - 1.0f + 2.0f * wave), 1.15f, accentHex);
    }

    // Subtle crest text right under the ring
    br->SetColor(D2D1::ColorF(0x64748B, 0.9f));
    const wchar_t* subCrest = L"\u2014  h a i l   t h a n   t a i  \u2014";
    g_rt->DrawTextW(subCrest, (UINT32)wcslen(subCrest), g_fmtSection, D2D1::RectF(24.0f, 238.0f, 416.0f, 256.0f), br);

    // Status Headline & Subline
    std::wstring headline = L"Waiting for AirPlay\u2026";
    std::wstring subline  = L"Control Center \u2192 Screen Mirroring \u2192 \"Ghost\"";

    if (st.phase == GhostPhase::Starting) {
        headline = L"Starting Ghost Engine\u2026";
        subline  = L"Initializing hardware-accelerated D3D11 pipeline";
    } else if (st.phase == GhostPhase::PairingPin) {
        headline = L"Enter PIN on Device";
        subline  = L"Type the 4-digit code shown inside the Ghost Ring";
    } else if (st.phase == GhostPhase::Connected) {
        headline = st.client_name.empty() ? L"Mirroring Active" : utf8_to_wide(st.client_name);
        std::string model_info = st.client_model.empty() ? "Live Stream" : st.client_model;
        subline  = utf8_to_wide(model_info + " \u00B7 " + res_dimensions(cfg.res) + "@" + std::to_string(cfg.fps) + "fps (Alt+Enter Fullscreen)");
    } else if (st.phase == GhostPhase::Error) {
        headline = L"Engine Needs Attention";
        subline  = st.error_msg.empty() ? L"Click the Ghost Ring to restart engine" : utf8_to_wide(st.error_msg);
    }

    br->SetColor(D2D1::ColorF(0xECECEF, 1.0f));
    g_rt->DrawTextW(headline.c_str(), (UINT32)headline.size(), g_fmtTitle, D2D1::RectF(24.0f, 260.0f, 416.0f, 290.0f), br);

    br->SetColor(D2D1::ColorF(0x8A8F9E, 1.0f));
    g_rt->DrawTextW(subline.c_str(), (UINT32)subline.size(), g_fmtSub, D2D1::RectF(24.0f, 292.0f, 416.0f, 330.0f), br);

    // Row 1: Quick Preset Pill
    std::wstring presetText = std::wstring(L"Preset: ") + preset_label(cfg.preset);
    draw_pill(br, g_rects.presetPill, g_hover_id == 4, cfg.preset != GhostPreset::Custom, presetText.c_str());

    // Section Header: ADVANCE TUNING
    br->SetColor(D2D1::ColorF(0x525665, 1.0f));
    const wchar_t* advHdr = L"A D V A N C E   C U S T O M   C O N T R O L S";
    g_rt->DrawTextW(advHdr, (UINT32)wcslen(advHdr), g_fmtSection, D2D1::RectF(28.0f, 398.0f, 412.0f, 416.0f), br);

    // Row 2: Advance RES / FPS / CODEC
    draw_pill(br, g_rects.resPill, g_hover_id == 5, true, res_label(cfg.res));

    std::wstring fpsText = L"FPS: " + std::to_wstring(cfg.fps);
    draw_pill(br, g_rects.fpsPill, g_hover_id == 6, true, fpsText.c_str());

    draw_pill(br, g_rects.codecPill, g_hover_id == 7, cfg.codec == GhostCodec::H265, codec_label(cfg.codec));

    // Row 3: VIEW / VSYNC / AUDIO / PIN
    draw_pill(br, g_rects.viewPill, g_hover_id == 8, cfg.fullscreen,
              cfg.fullscreen ? L"View: Full" : L"View: Win");

    draw_pill(br, g_rects.vsyncPill, g_hover_id == 9, !cfg.vsync,
              cfg.vsync ? L"VSync: ON" : L"VSync: OFF");

    bool audioOn = cfg.audio_enabled && cfg.volume_percent > 0;
    draw_pill(br, g_rects.audioPill, g_hover_id == 10, audioOn,
              audioOn ? L"Audio: ON" : L"Muted");

    draw_pill(br, g_rects.pinPill, g_hover_id == 11, cfg.pin_enabled,
              cfg.pin_enabled ? L"PIN: ON" : L"PIN: OFF");

    // Row 4: Interactive Volume Slider Box
    D2D1_ROUNDED_RECT volRR = D2D1::RoundedRect(g_rects.volBox, 9.0f, 9.0f);
    br->SetColor(D2D1::ColorF(g_hover_id == 13 || g_dragging_vol ? 0x181A24 : 0x121218, 1.0f));
    g_rt->FillRoundedRectangle(volRR, br);
    br->SetColor(D2D1::ColorF(g_dragging_vol ? 0x38BDF8 : 0x22222E, 1.0f));
    g_rt->DrawRoundedRectangle(volRR, br, 1.0f);

    // Volume label on left
    br->SetColor(D2D1::ColorF(audioOn ? 0xCBD5E1 : 0x64748B, 1.0f));
    const wchar_t* volLbl = audioOn ? L"VOL" : L"MUTE";
    g_rt->DrawTextW(volLbl, (UINT32)wcslen(volLbl), g_fmtPill, D2D1::RectF(36.0f, 514.0f, 104.0f, 554.0f), br);

    // Track background
    br->SetColor(D2D1::ColorF(0x232533, 1.0f));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.volTrack, 3.0f, 3.0f), br);

    // Active filled track + thumb
    float frac = audioOn ? (cfg.volume_percent / 100.0f) : 0.0f;
    float fillX = g_rects.volTrack.left + frac * (g_rects.volTrack.right - g_rects.volTrack.left);
    D2D1_RECT_F activeTrack = D2D1::RectF(g_rects.volTrack.left, g_rects.volTrack.top, fillX, g_rects.volTrack.bottom);
    br->SetColor(D2D1::ColorF(accentHex, 0.9f));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(activeTrack, 3.0f, 3.0f), br);

    D2D1_POINT_2F knobPt = D2D1::Point2F(fillX, 534.0f);
    br->SetColor(D2D1::ColorF(0xF8FAFC, 1.0f));
    g_rt->FillEllipse(D2D1::Ellipse(knobPt, g_dragging_vol ? 7.5f : 6.0f, g_dragging_vol ? 7.5f : 6.0f), br);
    br->SetColor(D2D1::ColorF(accentHex, 1.0f));
    g_rt->DrawEllipse(D2D1::Ellipse(knobPt, g_dragging_vol ? 7.5f : 6.0f, g_dragging_vol ? 7.5f : 6.0f), br, 1.5f);

    // Percentage label on right
    std::wstring pctTxt = std::to_wstring(audioOn ? cfg.volume_percent : 0) + L"%";
    br->SetColor(D2D1::ColorF(0xE2E8F0, 1.0f));
    g_rt->DrawTextW(pctTxt.c_str(), (UINT32)pctTxt.size(), g_fmtPill, D2D1::RectF(358.0f, 514.0f, 408.0f, 554.0f), br);

    // Subtle divider + Footer bar
    br->SetColor(D2D1::ColorF(0x191922, 1.0f));
    g_rt->DrawLine(D2D1::Point2F(28.0f, 568.0f), D2D1::Point2F(412.0f, 568.0f), br, 1.0f);

    br->SetColor(D2D1::ColorF(0x64748B, 1.0f));
    std::wstring footer = st.lan_ip.empty()
        ? L"UXPLAY 1.74 \u00B7 HAIL THAN TAI"
        : (L"LAN " + utf8_to_wide(st.lan_ip) + L" \u00B7 HAIL THAN TAI");
    g_rt->DrawTextW(footer.c_str(), (UINT32)footer.size(), g_fmtFooter, D2D1::RectF(28.0f, 580.0f, 340.0f, 608.0f), br);

    br->SetColor(D2D1::ColorF(g_hover_id == 12 ? 0xECECEF : 0x8A8F9E, 1.0f));
    const wchar_t* restartTxt = L"[Restart]";
    g_rt->DrawTextW(restartTxt, (UINT32)wcslen(restartTxt), g_fmtFooter, g_rects.restartBtn, br);

    br->Release();
    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        g_rt->Release();
        g_rt = nullptr;
    }
}

static void setup_tray_icon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"GHOST MIRROR \u2014 hail than tai");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void show_tray_menu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Show Ghost Mirror");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RESTART, L"Restart AirPlay Engine");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Quit Ghost Mirror");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            DWORD corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            if (g_appIcon) {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIcon);
            }
            SetTimer(hwnd, TIMER_ANIM, 16, nullptr);
            setup_tray_icon(hwnd);
            start_uxplay_engine();
            return 0;
        }
        case WM_TIMER: {
            if (wParam == TIMER_ANIM) {
                g_time += 0.016f;
                if (++g_tick_count % 30 == 0) {
                    apply_wasapi_volume();
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
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
        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (hit_test_ui((float)pt.x, (float)pt.y) != 0) {
                return HTCLIENT;
            }
            return HTCAPTION;
        }
        case WM_LBUTTONDOWN: {
            float x = (float)GET_X_LPARAM(lParam);
            float y = (float)GET_Y_LPARAM(lParam);
            int h = hit_test_ui(x, y);
            if (h == 13) {
                g_dragging_vol = true;
                SetCapture(hwnd);
                update_volume_from_mouse(x);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            float x = (float)GET_X_LPARAM(lParam);
            float y = (float)GET_Y_LPARAM(lParam);
            if (g_dragging_vol) {
                update_volume_from_mouse(x);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int h = hit_test_ui(x, y);
            if (h != g_hover_id) {
                g_hover_id = h;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, h != 0 ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_LBUTTONUP: {
            float x = (float)GET_X_LPARAM(lParam);
            float y = (float)GET_Y_LPARAM(lParam);
            if (g_dragging_vol) {
                g_dragging_vol = false;
                ReleaseCapture();
                update_volume_from_mouse(x);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int h = hit_test_ui(x, y);
            if (h == 1) {
                DestroyWindow(hwnd);
            } else if (h == 2) {
                ShowWindow(hwnd, SW_MINIMIZE);
            } else if (h == 3 || h == 12) {
                start_uxplay_engine();
            } else if (h == 4) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    apply_preset(g_cfg, next_preset(g_cfg.preset));
                }
                start_uxplay_engine();
            } else if (h == 5) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.res = next_res(g_cfg.res);
                    g_cfg.preset = GhostPreset::Custom;
                }
                start_uxplay_engine();
            } else if (h == 6) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.fps = next_fps(g_cfg.fps);
                    g_cfg.preset = GhostPreset::Custom;
                }
                start_uxplay_engine();
            } else if (h == 7) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.codec = next_codec(g_cfg.codec);
                    g_cfg.preset = GhostPreset::Custom;
                }
                start_uxplay_engine();
            } else if (h == 8) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.fullscreen = !g_cfg.fullscreen;
                }
                start_uxplay_engine();
            } else if (h == 9) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.vsync = !g_cfg.vsync;
                    g_cfg.preset = GhostPreset::Custom;
                }
                start_uxplay_engine();
            } else if (h == 10) {
                // Toggle Mute in real time without restarting stream!
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.audio_enabled = !g_cfg.audio_enabled;
                    if (g_cfg.audio_enabled && g_cfg.volume_percent == 0) {
                        g_cfg.volume_percent = 85;
                    }
                }
                apply_wasapi_volume();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (h == 11) {
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_cfg.pin_enabled = !g_cfg.pin_enabled;
                }
                start_uxplay_engine();
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
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }
        case WM_DESTROY: {
            g_quitting.store(true);
            KillTimer(hwnd, TIMER_ANIM);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            stop_uxplay_engine();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
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

    g_appIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_GHOST_ICON));
    if (!g_appIcon) {
        g_appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = g_appIcon;
    wc.hIconSm = g_appIcon;
    wc.lpszClassName = L"GhostMirrorWndClass";
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 440;
    int winH = 620;
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
