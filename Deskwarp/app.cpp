#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QPushButton>
#include <QGuiApplication>
#include <QStyle>
#include <QLocale>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QSizePolicy>
#include <QPainter>
#include <QColor>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPixmap>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QSurfaceFormat>
#include <QRectF>
#include <QPointF>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QByteArray>
#include <QTimer>
#include <QScreen>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QtSvg/QSvgRenderer>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QPointer>
#include <QRegularExpression>
#include <QSharedMemory>
#include <QSystemSemaphore>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageLogContext>
#include <QtGlobal>
#include <QLoggingCategory>
#include <QShowEvent>
#include <QCloseEvent>
#include <cmath>
#include <algorithm>
#include <limits>
#include <exception>
#include <new>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <processthreadsapi.h>
#include <heapapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif


namespace cfg {
    constexpr int kGridW = 4;
    constexpr int kGridH = 4;
    constexpr int kTilesX = 32;
    constexpr int kTilesY = 32;
    inline float friction = 3.5f;
    inline float stiffness = 1.9f;
    inline float mass = 30.0f;
    inline float restVel = 0.02f;
    inline float restPos = 0.3f;
    constexpr int kStepIters = 3;
    constexpr int kRenderSleepMs = 8;
    constexpr int kCaptureSleepMs = 16;
    constexpr int kMaxSettleFrames = 1200;
    constexpr int kSteadyExit = 4;
    inline float boundRestitution = 0.18f;
    constexpr int kMinCaptionVisible = 80;
    constexpr int kMaxCaptionVisible = 320;
    constexpr UINT kBaseDpi = 96;
    constexpr float kMinScale = 0.25f;
    constexpr float kMaxScale = 8.0f;

    inline void applyRealismLevel(int level) {
        switch (level) {
        case 1:
            friction = 5.5f; stiffness = 3.2f; mass = 38.0f;
            restVel = 0.01f; restPos = 0.12f; boundRestitution = 0.08f;
            break;
        case 2:
            friction = 3.5f; stiffness = 1.9f; mass = 30.0f;
            restVel = 0.02f; restPos = 0.3f; boundRestitution = 0.18f;
            break;
        case 3:
            friction = 2.2f; stiffness = 1.2f; mass = 24.0f;
            restVel = 0.035f; restPos = 0.45f; boundRestitution = 0.28f;
            break;
        default:
            friction = 1.2f; stiffness = 0.6f; mass = 18.0f;
            restVel = 0.055f; restPos = 0.65f; boundRestitution = 0.42f;
            break;
        }
    }
}

typedef LONG NTSTATUS;
struct RTL_OSVERSIONINFOW_CUSTOM {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};
typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(RTL_OSVERSIONINFOW_CUSTOM*);

struct GridNode { float x, y, vx, vy, fx, fy; int immobile; };
struct GridSpring { int a, b; float restX, restY; };
struct MeshVertex { float x, y, u, v; };
struct ShaderConstants { float screenW, screenH, p1, p2, texW, texH, radius, p3; };
struct ScreenInfo { int vox, voy, w, h; };

namespace dpiapi {
    typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
    typedef UINT(WINAPI* PFN_GetDpiForSystem)(void);
    typedef int(WINAPI* PFN_GetSystemMetricsForDpi)(int, UINT);

    static PFN_GetDpiForMonitor pGetDpiForMonitor = nullptr;
    static PFN_GetDpiForWindow pGetDpiForWindow = nullptr;
    static PFN_GetDpiForSystem pGetDpiForSystem = nullptr;
    static PFN_GetSystemMetricsForDpi pGetSystemMetricsForDpi = nullptr;
    static std::atomic<bool> ready{ false };

    static void init() {
        HMODULE sh = LoadLibraryW(L"shcore.dll");
        if (sh) pGetDpiForMonitor = (PFN_GetDpiForMonitor)GetProcAddress(sh, "GetDpiForMonitor");
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) {
            pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(u, "GetDpiForWindow");
            pGetDpiForSystem = (PFN_GetDpiForSystem)GetProcAddress(u, "GetDpiForSystem");
            pGetSystemMetricsForDpi = (PFN_GetSystemMetricsForDpi)GetProcAddress(u, "GetSystemMetricsForDpi");
        }
        ready.store(true);
    }

    static UINT systemDpi() {
        if (pGetDpiForSystem) { UINT d = pGetDpiForSystem(); if (d) return d; }
        HDC dc = GetDC(NULL);
        UINT d = cfg::kBaseDpi;
        if (dc) { int v = GetDeviceCaps(dc, LOGPIXELSX); if (v > 0) d = (UINT)v; ReleaseDC(NULL, dc); }
        return d;
    }

    static UINT forMonitor(HMONITOR mon) {
        if (mon && pGetDpiForMonitor) {
            UINT dx = 0, dy = 0;
            if (SUCCEEDED(pGetDpiForMonitor(mon, 0, &dx, &dy)) && dx) return dx;
        }
        return systemDpi();
    }

    static UINT forPoint(POINT pt) {
        return forMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST));
    }

    static UINT forRect(const RECT& rc) {
        return forMonitor(MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST));
    }

    static UINT forWindow(HWND w) {
        if (w && pGetDpiForWindow) { UINT d = pGetDpiForWindow(w); if (d) return d; }
        return forMonitor(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST));
    }

    static int sysMetric(int idx, UINT dpi) {
        if (pGetSystemMetricsForDpi && dpi) return pGetSystemMetricsForDpi(idx, dpi);
        return GetSystemMetrics(idx);
    }

    static float clampScale(float s) {
        if (!(s == s)) return 1.0f;
        if (s < cfg::kMinScale) return cfg::kMinScale;
        if (s > cfg::kMaxScale) return cfg::kMaxScale;
        return s;
    }
}

static const char* kVsSource =
"cbuffer CB : register(b0) { float4 screen; float4 texInfo; };\n"
"struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };\n"
"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"VSOut main(VSIn i) {\n"
"  VSOut o;\n"
"  float x = (i.pos.x / screen.x)* 2.0 - 1.0;\n"
"  float y = 1.0 - (i.pos.y / screen.y)* 2.0;\n"
"  o.pos = float4(x, y, 0.0, 1.0);\n"
"  o.uv = i.uv;\n"
"  return o;\n"
"}\n";

static const char* kPsSource =
"cbuffer CB : register(b0) { float4 screen; float4 texInfo; };\n"
"Texture2D tex : register(t0);\n"
"SamplerState samp : register(s0);\n"
"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"float4 main(VSOut i) : SV_TARGET {\n"
"  float4 c = tex.Sample(samp, i.uv);\n"
"  if (texInfo.z > 0.0) {\n"
"    float2 px = i.uv* texInfo.xy;\n"
"    float r = texInfo.z;\n"
"    float a = 1.0;\n"
"    if (px.x < r && px.y < r) a = clamp(r - length(px - float2(r, r)), 0.0, 1.0);\n"
"    else if (px.x > texInfo.x - r && px.y < r) a = clamp(r - length(px - float2(texInfo.x - r, r)), 0.0, 1.0);\n"
"    else if (px.x < r && px.y > texInfo.y - r) a = clamp(r - length(px - float2(r, texInfo.y - r)), 0.0, 1.0);\n"
"    else if (px.x > texInfo.x - r && px.y > texInfo.y - r) a = clamp(r - length(px - float2(texInfo.x - r, texInfo.y - r)), 0.0, 1.0);\n"
"    c.a *= a;\n"
"  }\n"
"  return float4(c.rgb, c.a);\n"
"}\n";

static float Bernstein(int i, float t) {
    float u = 1.0f - t;
    if (i == 0) return u *u* u;
    if (i == 1) return 3.0f *u* u* t;
    if (i == 2) return 3.0f *u* t* t;
    return t *t* t;
}

static bool IsExcludedClass(HWND hw) {
    wchar_t cls[128];
    if (!GetClassNameW(hw, cls, 128)) return true;
    if (wcscmp(cls, L"Progman") == 0) return true;
    if (wcscmp(cls, L"WorkerW") == 0) return true;
    if (wcscmp(cls, L"Shell_TrayWnd") == 0) return true;
    if (wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) return true;
    if (wcscmp(cls, L"WobblyCtrl") == 0) return true;
    if (wcscmp(cls, L"WobblyOverlay") == 0) return true;
    return false;
}

class SoftBody {
public:
    std::vector<GridNode> nodes;
    std::vector<GridSpring> springs;
    int anchor = -1;
    float friction = 0.0f;
    float stiffness = 0.0f;
    float mass = 0.0f;
    float baseW = 0.0f, baseH = 0.0f;

    void build(float ox, float oy, float w, float h) {
        nodes.assign((size_t)cfg::kGridW* cfg::kGridH, GridNode{});
        springs.clear();
        anchor = -1;
        friction = cfg::friction;
        stiffness = cfg::stiffness;
        mass = cfg::mass;
        baseW = w; baseH = h;
        const float sx = w / float(cfg::kGridW - 1);
        const float sy = h / float(cfg::kGridH - 1);
        for (int r = 0; r < cfg::kGridH; ++r) {
            for (int c = 0; c < cfg::kGridW; ++c) {
                GridNode& n = nodes[(size_t)r* cfg::kGridW + c];
                n = GridNode{};
                n.x = ox + c* sx;
                n.y = oy + r* sy;
            }
        }
        for (int r = 0; r < cfg::kGridH; ++r) {
            for (int c = 0; c < cfg::kGridW; ++c) {
                int idx = r* cfg::kGridW + c;
                if (c < cfg::kGridW - 1) springs.push_back({ idx, idx + 1, sx, 0.0f });
                if (r < cfg::kGridH - 1) springs.push_back({ idx, idx + cfg::kGridW, 0.0f, sy });
            }
        }
    }

    void setRest(float w, float h) {
        baseW = w; baseH = h;
        const float sx = w / float(cfg::kGridW - 1);
        const float sy = h / float(cfg::kGridH - 1);
        for (auto& s : springs) {
            if (s.restX != 0.0f) s.restX = sx;
            if (s.restY != 0.0f) s.restY = sy;
        }
    }

    int nearest(float x, float y) const {
        float best = 1e30f; int bi = 0;
        for (int i = 0; i < (int)nodes.size(); ++i) {
            float d = fabsf(nodes[i].x - x) + fabsf(nodes[i].y - y);
            if (d < best) { best = d; bi = i; }
        }
        return bi;
    }

    void pin(float x, float y) {
        anchor = nearest(x, y);
        GridNode& n = nodes[anchor];
        n.immobile = 1; n.vx = n.vy = n.fx = n.fy = 0.0f;
    }

    void pinAt(float x, float y) {
        anchor = nearest(x, y);
        GridNode& n = nodes[anchor];
        n.x = x; n.y = y;
        n.immobile = 1; n.vx = n.vy = n.fx = n.fy = 0.0f;
    }

    void moveAnchor(float dx, float dy) {
        if (anchor >= 0) {
            nodes[anchor].x += dx;
            nodes[anchor].y += dy;
            float stepVx = dx / (float)cfg::kStepIters;
            float stepVy = dy / (float)cfg::kStepIters;
            nodes[anchor].vx = (nodes[anchor].vx *0.5f) + (stepVx* 0.5f);
            nodes[anchor].vy = (nodes[anchor].vy *0.5f) + (stepVy* 0.5f);
        }
    }

    void release() {
        if (anchor >= 0) { nodes[anchor].immobile = 0; anchor = -1; }
    }

    void integrate(int iters) {
        for (int it = 0; it < iters; ++it) {
            for (auto& s : springs) {
                float fx = stiffness* (nodes[s.b].x - nodes[s.a].x - s.restX);
                nodes[s.a].fx += fx; nodes[s.b].fx -= fx;
                float fy = stiffness* (nodes[s.b].y - nodes[s.a].y - s.restY);
                nodes[s.a].fy += fy; nodes[s.b].fy -= fy;
            }
            for (auto& n : nodes) {
                if (!n.immobile) {
                    n.fx -= friction* n.vx;
                    n.fy -= friction* n.vy;
                    n.vx += n.fx / mass;
                    n.vy += n.fy / mass;
                    n.x += n.vx;
                    n.y += n.vy;
                }
                n.fx = 0.0f; n.fy = 0.0f;
            }
        }
    }

    void clampTo(float minL, float maxL, float minT, float maxT, float restitution) {
        if (nodes.empty()) return;
        float rx = nodes[0].x;
        float ry = nodes[0].y;
        float corrX = 0.0f, corrY = 0.0f;
        int signX = 0, signY = 0;
        if (rx < minL) { corrX = minL - rx; signX = -1; }
        else if (rx > maxL) { corrX = maxL - rx; signX = 1; }
        if (ry < minT) { corrY = minT - ry; signY = -1; }
        else if (ry > maxT) { corrY = maxT - ry; signY = 1; }

        if (corrX != 0.0f || corrY != 0.0f) {
            float pullX = corrX* 0.2f;
            float pullY = corrY* 0.2f;
            for (auto& n : nodes) {
                n.x += pullX;
                n.y += pullY;
            }
        }

        if (signX != 0) {
            for (auto& n : nodes) {
                if ((signX > 0 && n.vx > 0.0f) || (signX < 0 && n.vx < 0.0f))
                    n.vx = -restitution* fabsf(n.vx);
            }
        }
        if (signY != 0) {
            for (auto& n : nodes) {
                if ((signY > 0 && n.vy > 0.0f) || (signY < 0 && n.vy < 0.0f))
                    n.vy = -restitution* fabsf(n.vy);
            }
        }
    }

    void relax() {
        const float sx = baseW / float(cfg::kGridW - 1);
        const float sy = baseH / float(cfg::kGridH - 1);
        float bx = nodes[0].x, by = nodes[0].y;
        for (size_t i = 0; i < nodes.size(); ++i) {
            int c = (int)(i % cfg::kGridW);
            int r = (int)(i / cfg::kGridW);
            nodes[i].x = bx + c* sx;
            nodes[i].y = by + r* sy;
            nodes[i].vx = nodes[i].vy = nodes[i].fx = nodes[i].fy = 0.0f;
        }
    }

    bool settled() const {
        const float sx = baseW / float(cfg::kGridW - 1);
        const float sy = baseH / float(cfg::kGridH - 1);
        float bx = nodes[0].x, by = nodes[0].y;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (fabsf(nodes[i].vx) > cfg::restVel || fabsf(nodes[i].vy) > cfg::restVel) return false;
            int c = (int)(i % cfg::kGridW);
            int r = (int)(i / cfg::kGridW);
            float ex = bx + c *sx, ey = by + r* sy;
            if (fabsf(nodes[i].x - ex) > cfg::restPos || fabsf(nodes[i].y - ey) > cfg::restPos) return false;
        }
        return true;
    }
};

class ScreenGrabber {
public:
    ScreenGrabber() = default;
    ~ScreenGrabber() { stop(); }
    ScreenGrabber(const ScreenGrabber&) = delete;
    ScreenGrabber& operator=(const ScreenGrabber&) = delete;

    bool prepare(HWND hwnd) {
        target_ = hwnd;
        captureDpi_ = dpiapi::forWindow(hwnd);
        if (captureDpi_ == 0) captureDpi_ = cfg::kBaseDpi;
        RECT fr;
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr)))) {
            if (!GetWindowRect(hwnd, &fr)) return false;
        }
        RECT wr;
        if (!GetWindowRect(hwnd, &wr)) return false;
        shadow_.x = fr.left - wr.left;
        shadow_.y = fr.top - wr.top;
        texW_ = fr.right - fr.left;
        texH_ = fr.bottom - fr.top;
        frame_ = fr; wnd_ = wr;
        if (texW_ <= 0 || texH_ <= 0) return false;
        size_t bytes = (size_t)texW_ *texH_* 4;
        buf0_.assign(bytes, 0);
        buf1_.assign(bytes, 0);
        ready_.store(-1);
        return true;
    }

    void captureInitial() {
        if (grabOnce(buf0_)) ready_.store(0);
    }

    void start() {
        run_.store(true);
        thread_ = std::thread([this] { loop(); });
    }

    void stop() {
        run_.store(false);
        if (thread_.joinable()) thread_.join();
    }

    bool hasReady() const { return ready_.load() >= 0; }

    bool copyReadyInto(void* dst, UINT rowPitch) {
        std::lock_guard<std::mutex> lk(mtx_);
        int idx = ready_.load();
        if (idx < 0) return false;
        const BYTE* src = (idx == 0) ? buf0_.data() : buf1_.data();
        if (rowPitch == (UINT)texW_* 4) {
            memcpy(dst, src, (size_t)texW_ *texH_* 4);
        } else {
            BYTE* d = (BYTE*)dst;
            for (int y = 0; y < texH_; ++y)
                memcpy(d + (size_t)y *rowPitch, src + (size_t)y* texW_ *4, (size_t)texW_* 4);
        }
        return true;
    }

    int texW() const { return texW_; }
    int texH() const { return texH_; }
    RECT frame() const { return frame_; }
    RECT wnd() const { return wnd_; }
    POINT shadowOffset() const { return shadow_; }
    UINT captureDpi() const { return captureDpi_; }

private:
    bool grabOnce(std::vector<BYTE>& out) {
        int ww = wnd_.right - wnd_.left;
        int wh = wnd_.bottom - wnd_.top;
        if (ww <= 0 || wh <= 0 || texW_ <= 0 || texH_ <= 0) return false;

        HDC screenDC = GetDC(NULL);
        HDC fullDC = CreateCompatibleDC(screenDC);
        HBITMAP fullBmp = CreateCompatibleBitmap(screenDC, ww, wh);
        HBITMAP fullOld = (HBITMAP)SelectObject(fullDC, fullBmp);
        HDC cropDC = CreateCompatibleDC(screenDC);
        HBITMAP cropBmp = CreateCompatibleBitmap(screenDC, texW_, texH_);
        HBITMAP cropOld = (HBITMAP)SelectObject(cropDC, cropBmp);

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = texW_;
        bi.bmiHeader.biHeight = -texH_;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        bool ok = false;
        if (PrintWindow(target_, fullDC, PW_RENDERFULLCONTENT)) {
            BitBlt(cropDC, 0, 0, texW_, texH_, fullDC, shadow_.x, shadow_.y, SRCCOPY);
            GetDIBits(cropDC, cropBmp, 0, texH_, out.data(), &bi, DIB_RGB_COLORS);
            uint32_t* px = reinterpret_cast<uint32_t*>(out.data());
            size_t n = (size_t)texW_* texH_;
            for (size_t i = 0; i < n; ++i) px[i] |= 0xFF000000;
            ok = true;
        }

        SelectObject(cropDC, cropOld);
        DeleteObject(cropBmp);
        DeleteDC(cropDC);
        SelectObject(fullDC, fullOld);
        DeleteObject(fullBmp);
        DeleteDC(fullDC);
        ReleaseDC(NULL, screenDC);
        return ok;
    }

    void loop() {
        int ww = wnd_.right - wnd_.left;
        int wh = wnd_.bottom - wnd_.top;

        HDC screenDC = GetDC(NULL);
        HDC fullDC = CreateCompatibleDC(screenDC);
        HBITMAP fullBmp = CreateCompatibleBitmap(screenDC, ww, wh);
        HBITMAP fullOld = (HBITMAP)SelectObject(fullDC, fullBmp);
        HDC cropDC = CreateCompatibleDC(screenDC);
        HBITMAP cropBmp = CreateCompatibleBitmap(screenDC, texW_, texH_);
        HBITMAP cropOld = (HBITMAP)SelectObject(cropDC, cropBmp);

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = texW_;
        bi.bmiHeader.biHeight = -texH_;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        std::vector<BYTE> local((size_t)texW_ *texH_* 4);
        uint32_t* px = reinterpret_cast<uint32_t*>(local.data());
        size_t n = (size_t)texW_* texH_;

        while (run_.load()) {
            if (!target_ || !IsWindow(target_)) break;
            if (PrintWindow(target_, fullDC, PW_RENDERFULLCONTENT)) {
                BitBlt(cropDC, 0, 0, texW_, texH_, fullDC, shadow_.x, shadow_.y, SRCCOPY);
                GetDIBits(cropDC, cropBmp, 0, texH_, local.data(), &bi, DIB_RGB_COLORS);
                for (size_t i = 0; i < n; ++i) px[i] |= 0xFF000000;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    int w = (ready_.load() == 0) ? 1 : 0;
                    if (w == 0) memcpy(buf0_.data(), local.data(), local.size());
                    else memcpy(buf1_.data(), local.data(), local.size());
                    ready_.store(w);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg::kCaptureSleepMs));
        }

        SelectObject(cropDC, cropOld);
        DeleteObject(cropBmp);
        DeleteDC(cropDC);
        SelectObject(fullDC, fullOld);
        DeleteObject(fullBmp);
        DeleteDC(fullDC);
        ReleaseDC(NULL, screenDC);
    }

    HWND target_ = NULL;
    RECT frame_ = {};
    RECT wnd_ = {};
    POINT shadow_ = {};
    int texW_ = 0, texH_ = 0;
    UINT captureDpi_ = cfg::kBaseDpi;
    std::vector<BYTE> buf0_;
    std::vector<BYTE> buf1_;
    std::atomic<int> ready_{ -1 };
    std::mutex mtx_;
    std::thread thread_;
    std::atomic<bool> run_{ false };
};

class GpuCompositor {
public:
    bool init(HWND hwnd, const ScreenInfo& si) {
        si_ = si;
        meshScratch_.resize((size_t)(cfg::kTilesX + 1)* (cfg::kTilesY + 1));

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
            levels, 3, D3D11_SDK_VERSION, &device_, &fl, &ctx_))) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device_.As(&dxgiDevice))) return false;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = si_.w;
        sd.Height = si_.h;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(factory->CreateSwapChainForComposition(device_.Get(), &sd, NULL, &swap_))) return false;

        if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcompDevice_)))) return false;
        if (FAILED(dcompDevice_->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget_))) return false;
        if (FAILED(dcompDevice_->CreateVisual(&dcompVisual_))) return false;
        dcompVisual_->SetContent(swap_.Get());
        dcompTarget_->SetRoot(dcompVisual_.Get());
        dcompDevice_->Commit();

        ComPtr<ID3D11Texture2D> backbuf;
        if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&backbuf)))) return false;
        if (FAILED(device_->CreateRenderTargetView(backbuf.Get(), NULL, &rtv_))) return false;

        ComPtr<ID3DBlob> vsb, psb, errb;
        if (FAILED(D3DCompile(kVsSource, strlen(kVsSource), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vsb, &errb))) return false;
        if (FAILED(D3DCompile(kPsSource, strlen(kPsSource), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &psb, &errb))) return false;
        if (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), NULL, &vs_))) return false;
        if (FAILED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), NULL, &ps_))) return false;

        D3D11_INPUT_ELEMENT_DESC ied[2];
        ied[0] = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        ied[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        if (FAILED(device_->CreateInputLayout(ied, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_))) return false;

        int nVerts = (cfg::kTilesX + 1)* (cfg::kTilesY + 1);
        D3D11_BUFFER_DESC vbd = {};
        vbd.ByteWidth = sizeof(MeshVertex)* nVerts;
        vbd.Usage = D3D11_USAGE_DYNAMIC;
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&vbd, NULL, &vb_))) return false;

        std::vector<UINT> indices;
        indices.reserve((size_t)cfg::kTilesX *cfg::kTilesY* 6);
        for (int j = 0; j < cfg::kTilesY; ++j) {
            for (int i = 0; i < cfg::kTilesX; ++i) {
                UINT i0 = j* (cfg::kTilesX + 1) + i;
                UINT i1 = i0 + 1;
                UINT i2 = i0 + (cfg::kTilesX + 1);
                UINT i3 = i2 + 1;
                indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
            }
        }
        D3D11_BUFFER_DESC ibd = {};
        ibd.ByteWidth = sizeof(UINT)* (UINT)indices.size();
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibsd = {}; ibsd.pSysMem = indices.data();
        if (FAILED(device_->CreateBuffer(&ibd, &ibsd, &ib_))) return false;

        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth = sizeof(ShaderConstants);
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&cbd, NULL, &cb_))) return false;

        D3D11_SAMPLER_DESC sad = {};
        sad.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sad.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sad.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sad.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sad.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateSamplerState(&sad, &sampler_))) return false;

        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device_->CreateBlendState(&bd, &blend_))) return false;

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = FALSE;
        rd.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rd, &rast_))) return false;

        return true;
    }

    void cleanup() {
        shadowSrv_.Reset(); shadowTex_.Reset();
        contentSrv_.Reset(); contentTex_.Reset();
        rast_.Reset(); blend_.Reset(); sampler_.Reset();
        cb_.Reset(); ib_.Reset(); vb_.Reset(); layout_.Reset();
        ps_.Reset(); vs_.Reset(); rtv_.Reset();
        dcompVisual_.Reset(); dcompTarget_.Reset(); dcompDevice_.Reset();
        swap_.Reset(); ctx_.Reset(); device_.Reset();
    }

    void setContentSize(int w, int h) {
        contentSrv_.Reset();
        contentTex_.Reset();
        texW_ = w; texH_ = h;
        if (w <= 0 || h <= 0) return;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, NULL, &contentTex_))) return;
        device_->CreateShaderResourceView(contentTex_.Get(), NULL, &contentSrv_);
    }

    void buildShadow() {
        int padX = (int)(si_.w* 0.02f);
        int padY = (int)(si_.h* 0.02f);
        int sw = texW_ + padX* 2;
        int sh = texH_ + padY* 2;
        shadowTex_.Reset();
        shadowSrv_.Reset();
        if (sw <= 0 || sh <= 0) return;

        std::vector<BYTE> data((size_t)sw *sh* 4);
        float sigmaX = (float)padX / 2.5f;
        float sigmaY = (float)padY / 2.5f;
        float root2 = 1.41421356f;

        std::vector<float> bx(sw), by(sh);
        for (int x = 0; x < sw; ++x)
            bx[x] = 0.5f *(std::erf((x - padX) / (sigmaX* root2)) - std::erf((x - (padX + texW_)) / (sigmaX* root2)));
        for (int y = 0; y < sh; ++y)
            by[y] = 0.5f *(std::erf((y - padY) / (sigmaY* root2)) - std::erf((y - (padY + texH_)) / (sigmaY* root2)));

        float opacity = 0.325f;
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                float a = bx[x] *by[y]* opacity;
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;
                size_t idx = ((size_t)y *sw + x)* 4;
                data[idx] = 0; data[idx + 1] = 0; data[idx + 2] = 0;
                data[idx + 3] = (BYTE)(a* 255.0f);
            }
        }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = sw; td.Height = sh;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data.data();
        initData.SysMemPitch = sw* 4;
        device_->CreateTexture2D(&td, &initData, &shadowTex_);
        device_->CreateShaderResourceView(shadowTex_.Get(), NULL, &shadowSrv_);
    }

    bool ready() const { return contentSrv_ && contentTex_ && shadowSrv_; }

    void updateContent(ScreenGrabber& grabber) {
        if (!contentTex_ || !grabber.hasReady()) return;
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(contentTex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            grabber.copyReadyInto(m.pData, m.RowPitch);
            ctx_->Unmap(contentTex_.Get(), 0);
        }
    }

    void drawScene(const GridNode* ctrl, bool win11, bool dropShadow, float scale) {
        float clear[4] = { 0, 0, 0, 0 };
        ctx_->ClearRenderTargetView(rtv_.Get(), clear);
        ID3D11RenderTargetView* rtvs[] = { rtv_.Get() };
        ctx_->OMSetRenderTargets(1, rtvs, NULL);

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)si_.w;
        vp.Height = (float)si_.h;
        vp.MaxDepth = 1.0f;
        ctx_->RSSetViewports(1, &vp);
        ctx_->RSSetState(rast_.Get());

        UINT stride = sizeof(MeshVertex), offset = 0;
        ID3D11Buffer* vbs[] = { vb_.Get() };
        ctx_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        ctx_->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(layout_.Get());
        ctx_->VSSetShader(vs_.Get(), NULL, 0);
        ID3D11Buffer* cbs[] = { cb_.Get() };
        ctx_->VSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetShader(ps_.Get(), NULL, 0);
        ID3D11SamplerState* samps[] = { sampler_.Get() };
        ctx_->PSSetSamplers(0, 1, samps);
        float bf[4] = { 0, 0, 0, 0 };
        ctx_->OMSetBlendState(blend_.Get(), bf, 0xFFFFFFFF);

        float padXpct = (si_.w* 0.02f) / (float)texW_;
        float padYpct = (si_.h* 0.02f) / (float)texH_;

        if (dropShadow)
            drawMesh(-padXpct, 1.0f + padXpct, -padYpct, 1.0f + padYpct, shadowSrv_.Get(), 0.0f, ctrl);
        drawMesh(0.0f, 1.0f, 0.0f, 1.0f, contentSrv_.Get(), win11 ? (8.0f* scale) : 0.0f, ctrl);
    }

    void present() { swap_->Present(1, 0); }

    int texW() const { return texW_; }
    int texH() const { return texH_; }

private:
    void drawMesh(float uMin, float uMax, float vMin, float vMax,
        ID3D11ShaderResourceView* srv, float radius, const GridNode* ctrl) {
        if (!srv) return;

        D3D11_MAPPED_SUBRESOURCE mc;
        if (SUCCEEDED(ctx_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
            ShaderConstants c = { (float)si_.w, (float)si_.h, 0, 0, (float)texW_, (float)texH_, radius, 0 };
            memcpy(mc.pData, &c, sizeof(c));
            ctx_->Unmap(cb_.Get(), 0);
        }

        float Bu[cfg::kTilesX + 1][4];
        float Bv[cfg::kTilesY + 1][4];
        for (int i = 0; i <= cfg::kTilesX; ++i) {
            float u = uMin + ((float)i / cfg::kTilesX)* (uMax - uMin);
            for (int k = 0; k < 4; ++k) Bu[i][k] = Bernstein(k, u);
        }
        for (int j = 0; j <= cfg::kTilesY; ++j) {
            float v = vMin + ((float)j / cfg::kTilesY)* (vMax - vMin);
            for (int k = 0; k < 4; ++k) Bv[j][k] = Bernstein(k, v);
        }

        for (int j = 0; j <= cfg::kTilesY; ++j) {
            for (int i = 0; i <= cfg::kTilesX; ++i) {
                float px = 0, py = 0;
                for (int jj = 0; jj < 4; ++jj) {
                    float bj = Bv[j][jj];
                    for (int ii = 0; ii < 4; ++ii) {
                        float bi = Bu[i][ii];
                        const GridNode& n = ctrl[jj* 4 + ii];
                        float w = bi* bj;
                        px += w* n.x;
                        py += w* n.y;
                    }
                }
                MeshVertex& vt = meshScratch_[(size_t)j* (cfg::kTilesX + 1) + i];
                vt.x = px - (float)si_.vox;
                vt.y = py - (float)si_.voy;
                vt.u = (float)i / cfg::kTilesX;
                vt.v = (float)j / cfg::kTilesY;
            }
        }

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, meshScratch_.data(), meshScratch_.size()* sizeof(MeshVertex));
            ctx_->Unmap(vb_.Get(), 0);
            ID3D11ShaderResourceView* srvs[] = { srv };
            ctx_->PSSetShaderResources(0, 1, srvs);
            ctx_->DrawIndexed(cfg::kTilesX *cfg::kTilesY* 6, 0, 0);
        }
    }

    ScreenInfo si_ = {};
    int texW_ = 0, texH_ = 0;
    std::vector<MeshVertex> meshScratch_;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vb_;
    ComPtr<ID3D11Buffer> ib_;
    ComPtr<ID3D11Buffer> cb_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Texture2D> contentTex_;
    ComPtr<ID3D11ShaderResourceView> contentSrv_;
    ComPtr<ID3D11Texture2D> shadowTex_;
    ComPtr<ID3D11ShaderResourceView> shadowSrv_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> rast_;
};

class WobblyEngine {
public:
    bool init(HWND overlay, const ScreenInfo& si, bool win11) {
        overlay_ = overlay;
        si_ = si;
        win11_ = win11;
        return gpu_.init(overlay, si);
    }

    void shutdown() {
        stopThreads();
        gpu_.cleanup();
    }

    bool isDragging() const { return dragging_.load(); }
    bool isSettling() const { return settling_.load(); }

    void beginDrag(HWND target, POINT pt) {
        stopThreads();
        dragging_.store(false);
        settling_.store(false);
        pendingSnap_ = 0;
        target_ = target;
        if (!IsWindow(target)) { target_ = NULL; return; }

        LONG style = GetWindowLongW(target, GWL_STYLE);
        bool maximized = (style & WS_MAXIMIZE) != 0;

        if (maximized) {
            RECT mf;
            if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &mf, sizeof(mf)))) {
                if (!GetWindowRect(target, &mf)) { target_ = NULL; return; }
            }
            int mfw = mf.right - mf.left;
            int mfh = mf.bottom - mf.top;

            captureLayeredState(target);
            SendMessageW(target, WM_SYSCOMMAND, SC_RESTORE, 0);

            if (!grabber_.prepare(target)) { target_ = NULL; return; }
            cacheGeometry();
            grabber_.captureInitial();
            gpu_.setContentSize(grabber_.texW(), grabber_.texH());
            gpu_.buildShadow();
            if (!gpu_.ready()) { target_ = NULL; return; }

            {
                std::lock_guard<std::mutex> lk(bodyMtx_);
                body_.build((float)mf.left, (float)mf.top, (float)mfw, (float)mfh);
                body_.setRest((float)grabber_.texW(), (float)grabber_.texH());
                body_.pinAt((float)pt.x, (float)pt.y);
            }
            { std::lock_guard<std::mutex> lk(mouseMtx_); lastMouse_ = pt; curMouse_ = pt; }

            showOverlay(true);
            dragging_.store(true);
            renderRun_.store(true);
            grabber_.start();
            renderThread_ = std::thread([this] { renderLoop(); });
            return;
        }

        WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
        if (GetWindowPlacement(target, &wp)) {
            RECT rc;
            if (GetWindowRect(target, &rc)) {
                int currentWidth = rc.right - rc.left;
                int currentHeight = rc.bottom - rc.top;
                int normalWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                int normalHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
                if (currentWidth != normalWidth || currentHeight != normalHeight) {
                    RECT mf;
                    if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &mf, sizeof(mf)))) {
                        mf = rc;
                    }
                    int mfw = mf.right - mf.left;
                    int mfh = mf.bottom - mf.top;

                    double relativeX = static_cast<double>(pt.x - rc.left) / currentWidth;
                    int newLeft = pt.x - static_cast<int>(relativeX* normalWidth);
                    int newTop = rc.top;

                    HMONITOR srcMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO srcMi; srcMi.cbSize = sizeof(srcMi);
                    RECT srcWork = {};
                    if (GetMonitorInfoW(srcMon, &srcMi)) srcWork = srcMi.rcWork;
                    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &srcWork, 0);

                    captureLayeredState(target);

                    wp.showCmd = SW_SHOWNORMAL;
                    wp.rcNormalPosition.left = newLeft - srcWork.left;
                    wp.rcNormalPosition.top = newTop - srcWork.top;
                    wp.rcNormalPosition.right = wp.rcNormalPosition.left + normalWidth;
                    wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + normalHeight;
                    SetWindowPlacement(target, &wp);

                    if (!grabber_.prepare(target)) { target_ = NULL; return; }
                    cacheGeometry();
                    grabber_.captureInitial();
                    gpu_.setContentSize(grabber_.texW(), grabber_.texH());
                    gpu_.buildShadow();
                    if (!gpu_.ready()) { target_ = NULL; return; }

                    RECT nf = grabber_.frame();
                    int nfw = nf.right - nf.left;
                    int nfh = nf.bottom - nf.top;
                    if (nfw <= 0) { nfw = mfw; }
                    if (nfh <= 0) { nfh = mfh; }

                    {
                        std::lock_guard<std::mutex> lk(bodyMtx_);
                        body_.build((float)nf.left, (float)nf.top, (float)nfw, (float)nfh);
                        body_.setRest((float)grabber_.texW(), (float)grabber_.texH());
                        body_.pinAt((float)pt.x, (float)pt.y);
                    }
                    { std::lock_guard<std::mutex> lk(mouseMtx_); lastMouse_ = pt; curMouse_ = pt; }

                    showOverlay(true);
                    dragging_.store(true);
                    renderRun_.store(true);
                    grabber_.start();
                    renderThread_ = std::thread([this] { renderLoop(); });
                    return;
                }
            }
        }

        if (!grabber_.prepare(target)) { target_ = NULL; return; }
        cacheGeometry();
        grabber_.captureInitial();
        gpu_.setContentSize(grabber_.texW(), grabber_.texH());
        gpu_.buildShadow();
        if (!gpu_.ready()) { target_ = NULL; return; }

        RECT fr = grabber_.frame();
        int fw = fr.right - fr.left;
        int fh = fr.bottom - fr.top;
        {
            std::lock_guard<std::mutex> lk(bodyMtx_);
            body_.build((float)fr.left, (float)fr.top, (float)fw, (float)fh);
            body_.setRest((float)grabber_.texW(), (float)grabber_.texH());
            body_.pin((float)pt.x, (float)pt.y);
        }
        { std::lock_guard<std::mutex> lk(mouseMtx_); lastMouse_ = pt; curMouse_ = pt; }

        captureLayeredState(target);
        showOverlay(true);
        dragging_.store(true);
        renderRun_.store(true);
        grabber_.start();
        renderThread_ = std::thread([this] { renderLoop(); });
    }

    void updateDrag(POINT pt) {
        std::lock_guard<std::mutex> lk(mouseMtx_);
        curMouse_ = pt;
    }

    void endDrag(POINT pt) {
        { std::lock_guard<std::mutex> lk(mouseMtx_); curMouse_ = pt; }

        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        mi.cbSize = sizeof(mi);
        pendingSnap_ = 0;
        if (GetMonitorInfoW(hMon, &mi)) {
            UINT mdpi = dpiapi::forMonitor(hMon);
            int edge = (int)(5.0f* ((float)mdpi / (float)cfg::kBaseDpi));
            if (edge < 5) edge = 5;
            bool hitLeft = (pt.x <= mi.rcMonitor.left + edge);
            bool hitRight = (pt.x >= mi.rcMonitor.right - edge);
            bool hitTop = (pt.y <= mi.rcMonitor.top + edge);
            bool hitBottom = (pt.y >= mi.rcMonitor.bottom - edge);

            if (hitTop && hitLeft) pendingSnap_ = 4;
            else if (hitBottom && hitLeft) pendingSnap_ = 5;
            else if (hitTop && hitRight) pendingSnap_ = 6;
            else if (hitBottom && hitRight) pendingSnap_ = 7;
            else if (hitTop) pendingSnap_ = 1;
            else if (hitLeft) pendingSnap_ = 2;
            else if (hitRight) pendingSnap_ = 3;
        }

        { std::lock_guard<std::mutex> lk(bodyMtx_); body_.release(); }
        dragging_.store(false);
        settling_.store(true);
    }

private:
    void cacheGeometry() {
        baseShadow_ = grabber_.shadowOffset();
        shadowOffset_ = baseShadow_;
        wndRect_ = grabber_.wnd();
        baseTexW_ = grabber_.texW();
        baseTexH_ = grabber_.texH();
        baseWndW_ = wndRect_.right - wndRect_.left;
        baseWndH_ = wndRect_.bottom - wndRect_.top;
        captureDpi_ = grabber_.captureDpi();
        if (captureDpi_ == 0) captureDpi_ = cfg::kBaseDpi;
        int capH = dpiapi::sysMetric(SM_CYCAPTION, captureDpi_) + dpiapi::sysMetric(SM_CYSIZEFRAME, captureDpi_) + dpiapi::sysMetric(SM_CXPADDEDBORDER, captureDpi_);
        if (capH < 24) capH = 24;
        if (baseTexH_ > 0 && capH > baseTexH_) capH = baseTexH_;
        baseCaptionH_ = capH;
        frameW_ = baseTexW_;
        frameH_ = baseTexH_;
        captionH_ = baseCaptionH_;
        curScale_ = 1.0f;
    }

    void applyScale(float s) {
        curScale_ = s;
        frameW_ = (int)(baseTexW_* s + 0.5f);
        frameH_ = (int)(baseTexH_* s + 0.5f);
        if (frameW_ < 1) frameW_ = 1;
        if (frameH_ < 1) frameH_ = 1;
        captionH_ = (int)(baseCaptionH_* s + 0.5f);
        if (captionH_ < 1) captionH_ = 1;
        if (frameH_ > 0 && captionH_ > frameH_) captionH_ = frameH_;
        shadowOffset_.x = (LONG)(baseShadow_.x* s + 0.5f);
        shadowOffset_.y = (LONG)(baseShadow_.y* s + 0.5f);
    }

    float scaleForPoint(POINT pt) {
        UINT dpi = dpiapi::forPoint(pt);
        UINT base = captureDpi_ ? captureDpi_ : cfg::kBaseDpi;
        return dpiapi::clampScale((float)dpi / (float)base);
    }

    void boundsFor(float rx, float ry, float& minL, float& maxL, float& minT, float& maxT) {
        RECT cap;
        cap.left = (LONG)floorf(rx);
        cap.top = (LONG)floorf(ry);
        cap.right = cap.left + (frameW_ > 0 ? frameW_ : 1);
        cap.bottom = cap.top + (captionH_ > 0 ? captionH_ : 1);

        RECT m;
        HMONITOR mon = MonitorFromRect(&cap, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            m = mi.rcWork;
        } else {
            m.left = si_.vox; m.top = si_.voy;
            m.right = si_.vox + si_.w; m.bottom = si_.voy + si_.h;
        }

        int visX = frameW_ / 4;
        UINT base = captureDpi_ ? captureDpi_ : cfg::kBaseDpi;
        UINT mdpi = dpiapi::forMonitor(mon);
        float vscale = (float)mdpi / (float)cfg::kBaseDpi;
        int minVis = (int)(cfg::kMinCaptionVisible* vscale + 0.5f);
        int maxVis = (int)(cfg::kMaxCaptionVisible* vscale + 0.5f);
        if (visX < minVis) visX = minVis;
        if (visX > maxVis) visX = maxVis;
        if (visX > frameW_) visX = frameW_;

        int visY = captionH_;
        int mh = m.bottom - m.top;
        if (visY > mh) visY = mh;
        if (visY < 1) visY = 1;

        minL = (float)(m.left - (frameW_ - visX));
        maxL = (float)(m.right - visX);
        minT = (float)m.top;
        maxT = (float)(m.bottom - visY);
        if (maxL < minL) maxL = minL;
        if (maxT < minT) maxT = minT;
        (void)base;
    }

    void captureLayeredState(HWND w) {
        origExStyle_ = GetWindowLongPtrW(w, GWL_EXSTYLE);
        origLayered_ = (origExStyle_ & WS_EX_LAYERED) != 0;
        if (origLayered_) {
            if (!GetLayeredWindowAttributes(w, NULL, &origAlpha_, &origFlags_)) {
                origAlpha_ = 255; origFlags_ = LWA_ALPHA;
            }
        } else {
            origAlpha_ = 255; origFlags_ = 0;
            SetWindowLongPtrW(w, GWL_EXSTYLE, origExStyle_ | WS_EX_LAYERED);
        }
        SetLayeredWindowAttributes(w, 0, 0, LWA_ALPHA);
    }

    void showOverlay(bool show) {
        if (!overlay_ || !IsWindow(overlay_)) return;
        HWND tb = FindWindowW(L"Shell_TrayWnd", NULL);
        HWND tb2 = FindWindowW(L"Shell_SecondaryTrayWnd", NULL);
        UINT flags = SWP_NOACTIVATE | SWP_NOSENDCHANGING;
        if (show) flags |= SWP_SHOWWINDOW;
        SetWindowPos(overlay_, HWND_TOPMOST, si_.vox, si_.voy, si_.w, si_.h - 1, flags);
        if (tb && IsWindowVisible(tb))
            SetWindowPos(tb, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        if (tb2 && IsWindowVisible(tb2))
            SetWindowPos(tb2, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    }

    void hideOverlay() {
        if (!overlay_ || !IsWindow(overlay_)) return;
        SetWindowPos(overlay_, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING | SWP_HIDEWINDOW);
    }

    void stopThreads() {
        renderRun_.store(false);
        if (renderThread_.joinable()) renderThread_.join();
        grabber_.stop();
    }

    void renderFrame() {
        if (!gpu_.ready()) return;
        gpu_.updateContent(grabber_);

        GridNode ctrl[16];
        {
            std::lock_guard<std::mutex> lk(bodyMtx_);
            if (body_.nodes.size() < 16) return;
            for (int i = 0; i < 16; ++i) ctrl[i] = body_.nodes[i];
        }

        BOOL ds = FALSE;
        SystemParametersInfoW(SPI_GETDROPSHADOW, 0, &ds, 0);

        gpu_.drawScene(ctrl, win11_, ds != FALSE, curScale_);
        showOverlay(false);
        gpu_.present();
    }

    void renderLoop() {
        int steady = 0;
        int frame = 0;
        while (renderRun_.load()) {
            ++frame;
            POINT cur;
            { std::lock_guard<std::mutex> lk(mouseMtx_); cur = curMouse_; }

            POINT anchorPt;
            {
                std::lock_guard<std::mutex> lk(bodyMtx_);
                int a = (body_.anchor >= 0 && body_.anchor < (int)body_.nodes.size()) ? body_.anchor : 0;
                anchorPt.x = (LONG)floorf(body_.nodes[a].x);
                anchorPt.y = (LONG)floorf(body_.nodes[a].y);
            }
            float scale = scaleForPoint(anchorPt);
            applyScale(scale);
            { std::lock_guard<std::mutex> lk(bodyMtx_); body_.setRest((float)frameW_, (float)frameH_); }

            if (dragging_.load()) {
                float dx = (float)(cur.x - lastMouse_.x);
                float dy = (float)(cur.y - lastMouse_.y);
                lastMouse_ = cur;
                { std::lock_guard<std::mutex> lk(bodyMtx_); body_.moveAnchor(dx, dy); body_.integrate(cfg::kStepIters); }
                steady = 0;
            } else if (settling_.load()) {
                bool rest;
                {
                    std::lock_guard<std::mutex> lk(bodyMtx_);
                    body_.integrate(cfg::kStepIters);
                    float minL, maxL, minT, maxT;
                    boundsFor(body_.nodes[0].x, body_.nodes[0].y, minL, maxL, minT, maxT);
                    body_.clampTo(minL, maxL, minT, maxT, cfg::boundRestitution);
                    rest = body_.settled();
                }
                if (rest || pendingSnap_ != 0) ++steady; else steady = 0;
                if (steady > cfg::kSteadyExit || frame > cfg::kMaxSettleFrames || pendingSnap_ != 0) break;
            }

            if (target_ && IsWindow(target_)) {
                float bx, by;
                { std::lock_guard<std::mutex> lk(bodyMtx_); bx = body_.nodes[0].x; by = body_.nodes[0].y; }
                int fl = (int)floorf(bx + 0.5f);
                int ft = (int)floorf(by + 0.5f);
                SetWindowPos(target_, NULL, fl - shadowOffset_.x, ft - shadowOffset_.y, 0, 0,
                    SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS);
            }

            renderFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg::kRenderSleepMs));
        }

        grabber_.stop();

        if (target_ && IsWindow(target_)) {
            POINT anchorPt;
            { std::lock_guard<std::mutex> lk(bodyMtx_); anchorPt.x = (LONG)floorf(body_.nodes[0].x); anchorPt.y = (LONG)floorf(body_.nodes[0].y); }
            float fscale = scaleForPoint(anchorPt);
            applyScale(fscale);

            float bx, by;
            {
                std::lock_guard<std::mutex> lk(bodyMtx_);
                body_.setRest((float)frameW_, (float)frameH_);
                body_.relax();
                float minL, maxL, minT, maxT;
                boundsFor(body_.nodes[0].x, body_.nodes[0].y, minL, maxL, minT, maxT);
                float rx = body_.nodes[0].x;
                float ry = body_.nodes[0].y;
                if (rx < minL) rx = minL; else if (rx > maxL) rx = maxL;
                if (ry < minT) ry = minT; else if (ry > maxT) ry = maxT;
                bx = rx; by = ry;
            }
            int fl = (int)floorf(bx + 0.5f);
            int ft = (int)floorf(by + 0.5f);
            int sox = (int)(baseShadow_.x* fscale + 0.5f);
            int soy = (int)(baseShadow_.y* fscale + 0.5f);
            int nl = fl - sox;
            int nt = ft - soy;
            int ww = (int)(baseWndW_* fscale + 0.5f);
            int wh = (int)(baseWndH_* fscale + 0.5f);
            if (ww < 1) ww = baseWndW_;
            if (wh < 1) wh = baseWndH_;
            SetWindowPos(target_, HWND_TOP, nl, nt, ww, wh, SWP_NOACTIVATE | SWP_NOSENDCHANGING);

            {
                WINDOWPLACEMENT fwp = { sizeof(WINDOWPLACEMENT) };
                if (GetWindowPlacement(target_, &fwp)) {
                    RECT finalRect = { nl, nt, nl + ww, nt + wh };
                    HMONITOR fmon = MonitorFromRect(&finalRect, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO fmi; fmi.cbSize = sizeof(fmi);
                    if (fmon && GetMonitorInfoW(fmon, &fmi)) {
                        OffsetRect(&finalRect, fmi.rcMonitor.left - fmi.rcWork.left, fmi.rcMonitor.top - fmi.rcWork.top);
                    }
                    fwp.flags = 0;
                    fwp.showCmd = SW_SHOWNORMAL;
                    fwp.rcNormalPosition = finalRect;
                    SetWindowPlacement(target_, &fwp);
                }
            }

            if (origLayered_) {
                SetLayeredWindowAttributes(target_, 0, origAlpha_, origFlags_);
                SetWindowPos(target_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
            } else {
                SetLayeredWindowAttributes(target_, 0, 255, LWA_ALPHA);
                SetWindowLongPtrW(target_, GWL_EXSTYLE, origExStyle_);
                SetWindowPos(target_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
                RedrawWindow(target_, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
            }
            SetForegroundWindow(target_);
            BringWindowToTop(target_);

            if (pendingSnap_ != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                auto sendKey = [](WORD vk, bool down) {
                    INPUT in = { 0 };
                    in.type = INPUT_KEYBOARD;
                    in.ki.wVk = vk;
                    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
                    SendInput(1, &in, sizeof(INPUT));
                };

                if (pendingSnap_ == 1) {
                    sendKey(VK_LWIN, true); sendKey(VK_UP, true); sendKey(VK_UP, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 2) {
                    sendKey(VK_LWIN, true); sendKey(VK_LEFT, true); sendKey(VK_LEFT, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 3) {
                    sendKey(VK_LWIN, true); sendKey(VK_RIGHT, true); sendKey(VK_RIGHT, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 4) {
                    sendKey(VK_LWIN, true); sendKey(VK_LEFT, true); sendKey(VK_LEFT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_UP, true); sendKey(VK_UP, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 5) {
                    sendKey(VK_LWIN, true); sendKey(VK_LEFT, true); sendKey(VK_LEFT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_DOWN, true); sendKey(VK_DOWN, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 6) {
                    sendKey(VK_LWIN, true); sendKey(VK_RIGHT, true); sendKey(VK_RIGHT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_UP, true); sendKey(VK_UP, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 7) {
                    sendKey(VK_LWIN, true); sendKey(VK_RIGHT, true); sendKey(VK_RIGHT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_DOWN, true); sendKey(VK_DOWN, false); sendKey(VK_LWIN, false);
                }
                pendingSnap_ = 0;
            }
        }

        hideOverlay();
        settling_.store(false);
        renderRun_.store(false);
        target_ = NULL;
    }

    SoftBody body_;
    GpuCompositor gpu_;
    ScreenGrabber grabber_;
    std::thread renderThread_;
    std::atomic<bool> renderRun_{ false };
    std::atomic<bool> dragging_{ false };
    std::atomic<bool> settling_{ false };
    HWND target_ = NULL;
    HWND overlay_ = NULL;
    ScreenInfo si_ = {};
    bool win11_ = false;
    POINT lastMouse_ = {};
    POINT curMouse_ = {};
    std::mutex mouseMtx_;
    std::mutex bodyMtx_;
    POINT shadowOffset_ = {};
    POINT baseShadow_ = {};
    RECT wndRect_ = {};
    int frameW_ = 0;
    int frameH_ = 0;
    int captionH_ = 0;
    int baseTexW_ = 0;
    int baseTexH_ = 0;
    int baseWndW_ = 0;
    int baseWndH_ = 0;
    int baseCaptionH_ = 0;
    UINT captureDpi_ = cfg::kBaseDpi;
    float curScale_ = 1.0f;
    LONG_PTR origExStyle_ = 0;
    BYTE origAlpha_ = 255;
    DWORD origFlags_ = 0;
    bool origLayered_ = false;
    int pendingSnap_ = 0;
};


#ifdef _WIN32
namespace {

std::wstring narrow_to_wide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), length);
    return wide;
}

std::wstring quote_command_line_arg(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring quoted = L"\"";
    for (const wchar_t ch : arg) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += L'"';
    return quoted;
}

std::wstring build_command_line_params(int argc, char* argv[]) {
    std::wstring params;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr || argv[i][0] == '\0') {
            continue;
        }
        if (!params.empty()) {
            params += L' ';
        }
        params += quote_command_line_arg(narrow_to_wide(argv[i]));
    }
    return params;
}

bool has_background_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::strcmp(argv[i], "--background") == 0) {
            return true;
        }
    }
    return false;
}

} // namespace
#endif

class WobblyController {
public:
    static bool ensureElevated(int argc, char* argv[]) {
        BOOL elevated = FALSE;
        HANDLE tok = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            TOKEN_ELEVATION e;
            DWORD cb = sizeof(e);
            if (GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &cb))
                elevated = e.TokenIsElevated;
            CloseHandle(tok);
        }
        if (!elevated) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
                const std::wstring params = build_command_line_params(argc, argv);
                SHELLEXECUTEINFOW sei = { 0 };
                sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                sei.lpVerb = L"runas";
                sei.lpFile = path;
                sei.lpParameters = params.empty() ? nullptr : params.c_str();
                sei.nShow = has_background_arg(argc, argv) ? SW_HIDE : SW_SHOWNORMAL;
                if (ShellExecuteExW(&sei)) return false;
            }
        }
        return true;
    }

    bool init(HINSTANCE hInst, HWND uiHwnd) {
        s_self = this;
        hInst_ = hInst;
        ui_hwnd_ = uiHwnd;
        detectWin11();

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
        com_init_ = SUCCEEDED(hr);

        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        dpiapi::init();

        si_.vox = GetSystemMetrics(SM_XVIRTUALSCREEN);
        si_.voy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        si_.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        si_.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (si_.w <= 0) si_.w = GetSystemMetrics(SM_CXSCREEN);
        if (si_.h <= 0) si_.h = GetSystemMetrics(SM_CYSCREEN);

        WNDCLASSW wc2 = {};
        wc2.lpfnWndProc = OverlayProc;
        wc2.hInstance = hInst;
        wc2.lpszClassName = L"WobblyOverlay";
        wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc2);

        overlay_ = CreateWindowExW(
            WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            L"WobblyOverlay", L"", WS_POPUP,
            si_.vox, si_.voy, si_.w, si_.h, NULL, NULL, hInst, NULL);

        if (!engine_.init(overlay_, si_, win11_)) return false;

        hook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, hInst, 0);
        return hook_ != NULL;
    }

    void shutdown() {
        if (hook_) { UnhookWindowsHookEx(hook_); hook_ = NULL; }
        engine_.shutdown();
        if (com_init_) CoUninitialize();
        s_self = nullptr;
    }

    void setEnabled(bool on) { enabled_.store(on); }
    bool isEnabled() const { return enabled_.load(); }

    void setRealismLevel(int level) {
        cfg::applyRealismLevel(level);
        realism_ = level;
    }

    int realismLevel() const { return realism_; }

    bool handleUiMessage(UINT msg, WPARAM, LPARAM) {
        if (msg != WM_USER + 1) return false;
        HWND target = pendingTarget_;
        POINT pt = pendingPt_;
        pendingTarget_ = NULL;
        dragRequested_.store(false);
        if (target && !engine_.isDragging() && !engine_.isSettling()) {
            SetWindowPos(target, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOSENDCHANGING);
            SetForegroundWindow(target);
            BringWindowToTop(target);
            engine_.beginDrag(target, pt);
        }
        return true;
    }

private:
    void detectWin11() {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (nt) {
            RtlGetVersionPtr p = (RtlGetVersionPtr)GetProcAddress(nt, "RtlGetVersion");
            if (p) {
                RTL_OSVERSIONINFOW_CUSTOM v = { 0 };
                v.dwOSVersionInfoSize = sizeof(v);
                if (p(&v) == 0 && v.dwBuildNumber >= 22000) win11_ = true;
            }
        }
    }

    LRESULT onMouse(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode >= 0 && enabled_.load()) {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
            if (wParam == WM_LBUTTONDOWN && !engine_.isDragging() && !engine_.isSettling() && pendingTarget_ == NULL) {
                POINT pt = ms->pt;
                HWND hw = WindowFromPoint(pt);
                if (hw) {
                    HWND top = GetAncestor(hw, GA_ROOT);
                    if (top && IsWindowVisible(top) && !IsExcludedClass(top) && top != overlay_) {
                        LONG style = GetWindowLongW(top, GWL_STYLE);
                        if (!(style & WS_MINIMIZE) && (style & WS_CAPTION)) {
                            DWORD_PTR result = 0;
                            SendMessageTimeoutW(top, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y), SMTO_ABORTIFHUNG, 80, &result);
                            if (result == HTCAPTION) {
                                LRESULT defResult = DefWindowProcW(top, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
                                if (defResult == HTMINBUTTON || defResult == HTMAXBUTTON || defResult == HTCLOSE || defResult == HTSYSMENU || defResult == HTHELP) {
                                    return CallNextHookEx(hook_, nCode, wParam, lParam);
                                }
                                RECT btnRect = { 0 };
                                if (SUCCEEDED(DwmGetWindowAttribute(top, 5, &btnRect, sizeof(btnRect)))) {
                                    if (btnRect.right > btnRect.left && btnRect.bottom > btnRect.top) {
                                        if (PtInRect(&btnRect, pt)) {
                                            return CallNextHookEx(hook_, nCode, wParam, lParam);
                                        }
                                        RECT wndRect;
                                        if (GetWindowRect(top, &wndRect)) {
                                            RECT screenBtnRect = {
                                                wndRect.left + btnRect.left,
                                                wndRect.top + btnRect.top,
                                                wndRect.left + btnRect.right,
                                                wndRect.top + btnRect.bottom
                                            };
                                            if (PtInRect(&screenBtnRect, pt)) {
                                                return CallNextHookEx(hook_, nCode, wParam, lParam);
                                            }
                                        }
                                    }
                                }
                                SetWindowPos(top, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                                HWND fg = GetForegroundWindow();
                                if (fg != top) {
                                    DWORD fgT = GetWindowThreadProcessId(fg, NULL);
                                    DWORD tgT = GetWindowThreadProcessId(top, NULL);
                                    DWORD myT = GetCurrentThreadId();
                                    if (fgT != 0 && fgT != myT) AttachThreadInput(myT, fgT, TRUE);
                                    if (tgT != 0 && tgT != myT && tgT != fgT) AttachThreadInput(myT, tgT, TRUE);
                                    DWORD lockTO = 0;
                                    SystemParametersInfoW(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &lockTO, 0);
                                    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, 0, SPIF_SENDCHANGE);
                                    SetForegroundWindow(top);
                                    BringWindowToTop(top);
                                    SetActiveWindow(top);
                                    SetFocus(top);
                                    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (PVOID)(ULONG_PTR)lockTO, SPIF_SENDCHANGE);
                                    if (tgT != 0 && tgT != myT && tgT != fgT) AttachThreadInput(myT, tgT, FALSE);
                                    if (fgT != 0 && fgT != myT) AttachThreadInput(myT, fgT, FALSE);
                                }
                                static DWORD lastClickTime = 0;
                                static HWND lastClickWnd = NULL;
                                static POINT lastClickPt = { 0, 0 };
                                DWORD now = GetTickCount();
                                bool dbl = false;
                                if (lastClickWnd == top && (now - lastClickTime) <= GetDoubleClickTime()) {
                                    int cx = GetSystemMetrics(SM_CXDOUBLECLK);
                                    int cy = GetSystemMetrics(SM_CYDOUBLECLK);
                                    int ddx = pt.x - lastClickPt.x;
                                    int ddy = pt.y - lastClickPt.y;
                                    if (ddx >= -(cx / 2) && ddx <= (cx / 2) && ddy >= -(cy / 2) && ddy <= (cy / 2))
                                        dbl = true;
                                }
                                lastClickWnd = top;
                                lastClickTime = now;
                                lastClickPt = pt;
                                if (dbl) {
                                    LONG cur = GetWindowLongW(top, GWL_STYLE);
                                    lastClickTime = 0;
                                    if (cur & WS_MAXIMIZEBOX) {
                                        WPARAM sc = (cur & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE;
                                        PostMessageW(top, WM_SYSCOMMAND, sc, 0);
                                    }
                                    return 1;
                                }
                                pendingTarget_ = top;
                                pendingPt_ = pt;
                                dragRequested_.store(false);
                                return 1;
                            }
                        }
                    }
                }
            } else if (wParam == WM_MOUSEMOVE) {
                if (pendingTarget_ != NULL && !engine_.isDragging() && !dragRequested_.load()) {
                    int dx = ms->pt.x - pendingPt_.x;
                    int dy = ms->pt.y - pendingPt_.y;
                    UINT mdpi = dpiapi::forPoint(pendingPt_);
                    float ds = (float)mdpi / (float)cfg::kBaseDpi;
                    int thresh = (int)(16.0f* ds* ds);
                    if (thresh < 16) thresh = 16;
                    if (dx *dx + dy* dy > thresh) {
                        dragRequested_.store(true);
                        if (ui_hwnd_) PostMessageW(ui_hwnd_, WM_USER + 1, 0, 0);
                    }
                }
                if (engine_.isDragging()) engine_.updateDrag(ms->pt);
            } else if (wParam == WM_LBUTTONUP) {
                if (pendingTarget_ != NULL && !engine_.isDragging()) {
                    pendingTarget_ = NULL;
                    dragRequested_.store(false);
                    return 1;
                }
                if (engine_.isDragging()) {
                    engine_.endDrag(ms->pt);
                    return 1;
                }
            }
        }
        return CallNextHookEx(hook_, nCode, wParam, lParam);
    }

    static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_SETCURSOR) { SetCursor(LoadCursor(NULL, IDC_ARROW)); return TRUE; }
        return DefWindowProcW(h, m, w, l);
    }
    static LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
        return s_self ? s_self->onMouse(n, w, l) : CallNextHookEx(NULL, n, w, l);
    }

    static WobblyController* s_self;
    HINSTANCE hInst_ = NULL;
    HWND ui_hwnd_ = NULL;
    HWND overlay_ = NULL;
    HHOOK hook_ = NULL;
    ScreenInfo si_ = {};
    bool win11_ = false;
    bool com_init_ = false;
    WobblyEngine engine_;
    std::atomic<bool> enabled_{ false };
    std::atomic<bool> dragRequested_{ false };
    HWND pendingTarget_ = NULL;
    POINT pendingPt_ = {};
    int realism_ = 3;
};

WobblyController* WobblyController::s_self = nullptr;
namespace {

    constexpr int    kWindowWidth        = 360;
    constexpr int    kWindowHeight       = 240;
    constexpr int    kToggleWidth        = 48;
    constexpr int    kToggleHeight       = 24;
    constexpr int    kSliderWidth        = 280;
    constexpr int    kSliderHeight       = 52;
    constexpr int    kSliderLevels       = 4;
    constexpr int    kToggleAnimMs       = 180;
    constexpr int    kSliderAnimMs       = 160;
    constexpr int    kIconButtonSz       = 32;
    constexpr double kToggleGapPx        = 4.0;

    [[nodiscard]] bool is_valid_hex_color(const QString& s) noexcept {
        static const QRegularExpression rx(QStringLiteral("^#[0-9A-Fa-f]{6}"));
        if (s.isEmpty()) {
            return false;
        }
        return rx.match(s).hasMatch();
    }

    [[nodiscard]] QString safe_hex_color(const QString& s, const QString& fallback) noexcept {
        if (is_valid_hex_color(s)) {
            return s;
        }
        if (is_valid_hex_color(fallback)) {
            return fallback;
        }
        return QStringLiteral("#ffffff");
    }

    [[nodiscard]] int clamp_channel(int v) noexcept {
        if (v < 0) {
            return 0;
        }
        if (v > 255) {
            return 255;
        }
        return v;
    }

    [[nodiscard]] double clamp_double(double v, double lo, double hi) noexcept {
        if (std::isnan(v) || std::isinf(v)) {
            return lo;
        }
        if (v < lo) {
            return lo;
        }
        if (v > hi) {
            return hi;
        }
        return v;
    }

    void secure_qt_message_handler(QtMsgType, const QMessageLogContext&, const QString&) noexcept {}

    [[noreturn]] void secure_terminate_handler() noexcept {
        std::_Exit(EXIT_FAILURE);
    }

    void secure_new_handler() {
        std::_Exit(EXIT_FAILURE);
    }

#ifdef _WIN32
    void apply_windows_mitigations() noexcept {
        ::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
        ::SetDllDirectoryW(L"");
        ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);

        PROCESS_MITIGATION_DEP_POLICY dep{};
        dep.Enable    = 1;
        dep.Permanent = 1;
        (void)::SetProcessMitigationPolicy(ProcessDEPPolicy, &dep, sizeof(dep));

        PROCESS_MITIGATION_ASLR_POLICY aslr{};
        aslr.EnableBottomUpRandomization = 1;
        aslr.EnableForceRelocateImages   = 1;
        aslr.EnableHighEntropy           = 1;
        aslr.DisallowStrippedImages      = 1;
        (void)::SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr, sizeof(aslr));

        PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY shcp{};
        shcp.RaiseExceptionOnInvalidHandleReference = 1;
        shcp.HandleExceptionsPermanentlyEnabled     = 1;
        (void)::SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &shcp, sizeof(shcp));

        PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY epd{};
        epd.DisableExtensionPoints = 1;
        (void)::SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &epd, sizeof(epd));

        PROCESS_MITIGATION_IMAGE_LOAD_POLICY ilp{};
        ilp.NoRemoteImages            = 1;
        ilp.NoLowMandatoryLabelImages = 1;
        ilp.PreferSystem32Images      = 1;
        (void)::SetProcessMitigationPolicy(ProcessImageLoadPolicy, &ilp, sizeof(ilp));

        PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
        cfg.EnableControlFlowGuard  = 1;
        cfg.EnableExportSuppression = 0;
        cfg.StrictMode              = 0;
        (void)::SetProcessMitigationPolicy(ProcessControlFlowGuardPolicy, &cfg, sizeof(cfg));
    }
#endif

    class SemaphoreGuard final {
    public:
        explicit SemaphoreGuard(QSystemSemaphore& s) noexcept : m_sem(s), m_acquired(false) {
            m_acquired = m_sem.acquire();
        }
        ~SemaphoreGuard() noexcept {
            if (m_acquired) {
                m_sem.release();
            }
        }
        SemaphoreGuard(const SemaphoreGuard&)            = delete;
        SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;
        SemaphoreGuard(SemaphoreGuard&&)                 = delete;
        SemaphoreGuard& operator=(SemaphoreGuard&&)      = delete;

        [[nodiscard]] bool acquired() const noexcept { return m_acquired; }

    private:
        QSystemSemaphore& m_sem;
        bool              m_acquired;
    };

    constexpr int kDonateWindowWidth  = 440;
    constexpr int kDonateWindowHeight = 280;

    inline const QString kLocalServerName = QStringLiteral("Deskwarp.LocalServer.v1");
    inline const QString kStartupArg      = QStringLiteral("--background");

    void center_window_on_screen(QWidget* widget) noexcept {
        if (widget == nullptr) {
            return;
        }
        QScreen* const screen = QGuiApplication::primaryScreen();
        if (screen == nullptr) {
            return;
        }
        const QRect avail = screen->availableGeometry();
        const int   x     = avail.x() + (avail.width()  - widget->width())  / 2;
        const int   y     = avail.y() + (avail.height() - widget->height()) / 2;
        widget->move(x, y);
    }

    namespace AppPersistence {

        [[nodiscard]] QSettings settings() {
            return QSettings(
                QStringLiteral("Deskwarp"),
                QStringLiteral("Deskwarp")
            );
        }

        [[nodiscard]] bool wobblyEnabled() {
            return settings().value(QStringLiteral("wobbly/enabled"), false).toBool();
        }

        void setWobblyEnabled(bool enabled) {
            settings().setValue(QStringLiteral("wobbly/enabled"), enabled);
            settings().sync();
        }

        [[nodiscard]] int wobblyRealism(int max_level) {
            return std::clamp(
                settings().value(QStringLiteral("wobbly/realism"), 1).toInt(),
                1,
                max_level
            );
        }

        void setWobblyRealism(int level) {
            settings().setValue(QStringLiteral("wobbly/realism"), level);
            settings().sync();
        }

        void ensureStartupRegistration() {
#ifdef _WIN32
            const QString exe_path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
            const QString entry    = QStringLiteral("\"%1\" %2").arg(exe_path, kStartupArg);
            
            QSettings run_key(
                QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                QSettings::NativeFormat
            );
            if (run_key.value(QStringLiteral("Deskwarp")).toString() != entry) {
                run_key.setValue(QStringLiteral("Deskwarp"), entry);
                run_key.sync();
            }

            QSettings approved_key(
                QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"),
                QSettings::NativeFormat
            );
            if (approved_key.contains(QStringLiteral("Deskwarp"))) {
                approved_key.remove(QStringLiteral("Deskwarp"));
                approved_key.sync();
            }
#endif
        }

        [[nodiscard]] bool notifyExistingInstance() {
            QLocalSocket socket;
            socket.connectToServer(kLocalServerName);
            if (!socket.waitForConnected(750)) {
                return false;
            }
            socket.write("show");
            socket.flush();
            socket.waitForBytesWritten(750);
            socket.disconnectFromServer();
            return true;
        }

    } // namespace AppPersistence

}

class ThemeColors final {
public:
    ThemeColors()                              = delete;
    ~ThemeColors()                             = delete;
    ThemeColors(const ThemeColors&)            = delete;
    ThemeColors& operator=(const ThemeColors&) = delete;
    ThemeColors(ThemeColors&&)                 = delete;
    ThemeColors& operator=(ThemeColors&&)      = delete;

    static void set_dark(bool dark) noexcept {
        s_dark = dark;
        refresh_accent();
    }

    [[nodiscard]] static bool is_dark() noexcept {
        return s_dark;
    }

    static void refresh_accent() noexcept {
        const QSettings reg(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"),
            QSettings::NativeFormat
        );
        const QVariant v = reg.value(QStringLiteral("ColorizationColor"));
        if (v.isValid() && v.canConvert<quint32>()) {
            bool ok = false;
            const quint32 val = v.toUInt(&ok);
            if (ok) {
                const int r = clamp_channel(static_cast<int>((val >> 16) & 0xFFu));
                const int g = clamp_channel(static_cast<int>((val >>  8) & 0xFFu));
                const int b = clamp_channel(static_cast<int>( val        & 0xFFu));
                s_accent = QColor(r, g, b);
                return;
            }
        }
        s_accent = s_dark ? QColor(0x60, 0xCD, 0xFF) : QColor(0x00, 0x78, 0xD4);
    }

    [[nodiscard]] static QColor window_bg()              noexcept { return s_dark ? QColor(QStringLiteral("#202020")) : QColor(QStringLiteral("#f3f3f3")); }
    [[nodiscard]] static QColor card_bg()                noexcept { return s_dark ? QColor(QStringLiteral("#2d2d2d")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor separator()              noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#e5e5e5")); }
    [[nodiscard]] static QColor title_text()             noexcept { return s_dark ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1a1a1a")); }
    [[nodiscard]] static QColor primary_text()           noexcept { return s_dark ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1a1a1a")); }
    [[nodiscard]] static QColor secondary_text()         noexcept { return s_dark ? QColor(QStringLiteral("#9a9a9a")) : QColor(QStringLiteral("#666666")); }
    [[nodiscard]] static QColor disabled_text()          noexcept { return s_dark ? QColor(QStringLiteral("#5a5a5a")) : QColor(QStringLiteral("#b0b0b0")); }
    [[nodiscard]] static QColor accent()                 noexcept { return s_accent; }
    [[nodiscard]] static QColor toggle_off()             noexcept { return s_dark ? QColor(QStringLiteral("#5a5a5a")) : QColor(QStringLiteral("#cccccc")); }
    [[nodiscard]] static QColor toggle_thumb()           noexcept { return QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor slider_track()           noexcept { return s_dark ? QColor(QStringLiteral("#5a5a5a")) : QColor(QStringLiteral("#cccccc")); }
    [[nodiscard]] static QColor slider_thumb_border()    noexcept { return s_dark ? QColor(QStringLiteral("#2d2d2d")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor slider_thumb_outline()   noexcept { return s_dark ? QColor(QStringLiteral("#2d2d2d")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor slider_text()            noexcept { return s_dark ? QColor(QStringLiteral("#e0e0e0")) : QColor(QStringLiteral("#333333")); }
    [[nodiscard]] static QColor disabled_accent()        noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#c0c0c0")); }
    [[nodiscard]] static QColor disabled_track()         noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#e0e0e0")); }
    [[nodiscard]] static QColor disabled_thumb()         noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#c0c0c0")); }
    [[nodiscard]] static QColor disabled_thumb_border()  noexcept { return s_dark ? QColor(QStringLiteral("#2d2d2d")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor disabled_thumb_outline() noexcept { return s_dark ? QColor(QStringLiteral("#2d2d2d")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor disabled_slider_text()   noexcept { return s_dark ? QColor(QStringLiteral("#5a5a5a")) : QColor(QStringLiteral("#b0b0b0")); }

    [[nodiscard]] static QString github_normal()  noexcept { return s_dark ? QStringLiteral("#5a5a5a") : QStringLiteral("#cccccc"); }
    [[nodiscard]] static QString github_pressed() noexcept { return s_dark ? QStringLiteral("#9a9a9a") : QStringLiteral("#999999"); }

private:
    static bool   s_dark;
    static QColor s_accent;
};

bool   ThemeColors::s_dark   = false;
QColor ThemeColors::s_accent = QColor(0x00, 0x78, 0xD4);

[[nodiscard]] static bool detect_system_dark_mode() noexcept {
    const QSettings reg(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat
    );
    const QVariant v = reg.value(QStringLiteral("AppsUseLightTheme"));
    if (!v.isValid() || !v.canConvert<int>()) {
        return false;
    }
    bool ok = false;
    const int value = v.toInt(&ok);
    if (!ok) {
        return false;
    }
    return value == 0;
}

[[nodiscard]] static QIcon load_application_icon() noexcept {
    const QString app_dir = QDir(QCoreApplication::applicationDirPath()).canonicalPath();
    if (app_dir.isEmpty()) {
        return QIcon();
    }
    const QString icon_path = QDir(app_dir).filePath(QStringLiteral("icon.ico"));
    const QFileInfo fi(icon_path);
    if (!fi.exists() || !fi.isFile()) {
        return QIcon();
    }
    const QString canonical_path = fi.canonicalFilePath();
    if (canonical_path.isEmpty() || !canonical_path.startsWith(app_dir)) {
        return QIcon();
    }
    const QIcon icon(canonical_path);
    if (icon.isNull() || icon.availableSizes().isEmpty()) {
        return QIcon();
    }
    return icon;
}

class GitHubButton final : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(GitHubButton)
public:
    explicit GitHubButton(QWidget* parent = nullptr)
        : QWidget(parent),
          m_url(QUrl(QStringLiteral("https://github.com/doebalov/Deskwarp"), QUrl::StrictMode)),
          m_svg_template(QStringLiteral(
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\" width=\"32\" height=\"32\">"
              "<path fill=\"{color}\" d=\"M8 0c4.42 0 8 3.58 8 8a8.013 8.013 0 0 1-5.45 7.59c-.4.08-.55-.17-.55-.38 "
              "0-.27-.01-1.13-.01-2.2 0-.75-.25-1.23-.54-1.48 1.78-.2 3.65-.88 3.65-3.95 0-.88-.31-1.59-.82-2.15"
              ".08-.2.36-1.02-.08-2.12 0 0-.67-.22-2.2.82-.64-.18-1.32-.27-2-.27-.68 0-1.36.09-2 .27-1.53-1.03-"
              "2.2-.82-2.2-.82-.44 1.1-.16 1.92-.08 2.12-.51.56-.82 1.28-.82 2.15 0 3.06 1.86 3.75 3.64 3.95-.23"
              ".2-.44.55-.51 1.07-.46.21-1.61.55-2.33-.66-.15-.24-.6-.83-1.23-.82-.67.01-.27.38.01.53.34.19.73.9"
              ".82 1.13.16.45.68 1.31 2.69.94 0 .67.01 1.3.01 1.49 0 .21-.15.45-.55.38A7.995 7.995 0 0 1 0 8c0-"
              "4.42 3.58-8 8-8Z\"/></svg>")),
          m_renderer(new QSvgRenderer(this)),
          m_pressed(false),
          m_current_color(safe_hex_color(ThemeColors::github_normal(), QStringLiteral("#cccccc")))
    {
        setFixedSize(kIconButtonSz, kIconButtonSz);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
        update_svg();
    }

    ~GitHubButton() override = default;

    void apply_theme() noexcept {
        const QString c = m_pressed ? ThemeColors::github_pressed() : ThemeColors::github_normal();
        m_current_color = safe_hex_color(c, QStringLiteral("#cccccc"));
        update_svg();
    }

protected:
    void leaveEvent(QEvent* event) override {
        if (!m_pressed) {
            m_current_color = safe_hex_color(ThemeColors::github_normal(), QStringLiteral("#cccccc"));
            update_svg();
        }
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            m_pressed       = true;
            m_current_color = safe_hex_color(ThemeColors::github_pressed(), QStringLiteral("#999999"));
            update_svg();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            const bool inside = rect().contains(event->pos());
            m_pressed         = false;
            m_current_color   = safe_hex_color(ThemeColors::github_normal(), QStringLiteral("#cccccc"));
            update_svg();
            if (inside
                && m_url.isValid()
                && m_url.scheme() == QStringLiteral("https")
                && m_url.host()   == QStringLiteral("github.com"))
            {
                QDesktopServices::openUrl(m_url);
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (m_renderer != nullptr && m_renderer->isValid()) {
            m_renderer->render(&painter);
        }
    }

private:
    void update_svg() noexcept {
        if (!is_valid_hex_color(m_current_color)) {
            m_current_color = QStringLiteral("#cccccc");
        }
        QString svg_data = m_svg_template;
        svg_data.replace(QStringLiteral("{color}"), m_current_color);
        const QByteArray bytes = svg_data.toUtf8();
        if (m_renderer != nullptr) {
            m_renderer->load(bytes);
            update();
        }
    }

    const QUrl     m_url;
    const QString  m_svg_template;
    QSvgRenderer* m_renderer;
    bool           m_pressed;
    QString        m_current_color;
};

class ToggleSwitch final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double thumbPosition READ getThumbPosition WRITE setThumbPosition)
    Q_DISABLE_COPY_MOVE(ToggleSwitch)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr, int width = kToggleWidth, int height = kToggleHeight)
        : QWidget(parent),
          m_width(std::clamp(width,  8, 4096)),
          m_height(std::clamp(height, 8, 4096)),
          m_state(false),
          m_radius(static_cast<double>(std::clamp(height, 8, 4096)) / 2.0),
          m_thumb_radius(m_radius - kToggleGapPx),
          m_thumb_x(m_radius),
          m_animation(new QPropertyAnimation(this, QByteArrayLiteral("thumbPosition"), this))
    {
        setFixedSize(m_width, m_height);
        if (m_animation != nullptr) {
            m_animation->setDuration(kToggleAnimMs);
            m_animation->setEasingCurve(QEasingCurve::InOutCubic);
        }
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    ~ToggleSwitch() override {
        if (m_animation != nullptr) {
            m_animation->stop();
        }
    }

    [[nodiscard]] bool isChecked() const noexcept {
        return m_state;
    }

    void setChecked(bool checked) noexcept {
        if (m_state == checked) {
            return;
        }
        m_state = checked;
        if (m_animation != nullptr) {
            m_animation->stop();
        }
        m_thumb_x = m_state
            ? (static_cast<double>(m_width) - m_radius)
            : m_radius;
        update();
    }

    [[nodiscard]] double getThumbPosition() const noexcept {
        return m_thumb_x;
    }

    void setThumbPosition(double value) noexcept {
        if (std::isnan(value) || std::isinf(value)) {
            return;
        }
        m_thumb_x = clamp_double(value, 0.0, static_cast<double>(m_width));
        update();
    }

    void apply_theme() noexcept {
        update();
    }

signals:
    void toggled(bool state);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            m_state = !m_state;
            const double target = m_state
                ? (static_cast<double>(m_width) - m_radius)
                : m_radius;
            if (m_animation != nullptr) {
                m_animation->stop();
                m_animation->setStartValue(m_thumb_x);
                m_animation->setEndValue(target);
                m_animation->start();
            } else {
                m_thumb_x = target;
                update();
            }
            emit toggled(m_state);
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const double r            = m_radius;
        const double w            = static_cast<double>(m_width);
        const double h            = static_cast<double>(m_height);
        const double total_travel = w - 2.0 * r;
        double       progress     = (total_travel > 0.0) ? (m_thumb_x - r) / total_travel : 0.0;
        progress                  = clamp_double(progress, 0.0, 1.0);
        const QColor on_color     = ThemeColors::accent();
        const QColor off_color    = ThemeColors::toggle_off();
        const int    cr           = clamp_channel(static_cast<int>(std::round(off_color.red()   + (on_color.red()   - off_color.red())   * progress)));
        const int    cg           = clamp_channel(static_cast<int>(std::round(off_color.green() + (on_color.green() - off_color.green()) * progress)));
        const int    cb           = clamp_channel(static_cast<int>(std::round(off_color.blue()  + (on_color.blue()  - off_color.blue())  * progress)));
        const QColor current_color(cr, cg, cb);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(current_color));
        QPainterPath path;
        path.addRoundedRect(QRectF(0.0, 0.0, w, h), r, r);
        painter.drawPath(path);
        painter.setBrush(QBrush(ThemeColors::toggle_thumb()));
        painter.drawEllipse(QPointF(m_thumb_x, h / 2.0), m_thumb_radius, m_thumb_radius);
    }

private:
    const int           m_width;
    const int           m_height;
    bool                m_state;
    const double        m_radius;
    const double        m_thumb_radius;
    double              m_thumb_x;
    QPropertyAnimation* m_animation;
};

class ModernSlider final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double thumbX READ getThumbX WRITE setThumbX)
    Q_DISABLE_COPY_MOVE(ModernSlider)
public:
    explicit ModernSlider(QWidget* parent = nullptr,
                          int width  = kSliderWidth,
                          int height = kSliderHeight,
                          int levels = kSliderLevels)
        : QWidget(parent),
          m_slider_width(std::clamp(width,  32, 8192)),
          m_slider_height(std::clamp(height, 16, 8192)),
          m_levels(std::clamp(levels, 2, 1024)),
          m_min_val(1),
          m_max_val(std::clamp(levels, 2, 1024)),
          m_enabled(true),
          m_padding_left(24),
          m_padding_right(24),
          m_track_y(24),
          m_track_height(4),
          m_thumb_radius(8),
          m_thumb_outer_radius(12),
          m_track_start(static_cast<double>(m_padding_left)),
          m_track_end(static_cast<double>(std::clamp(width, 32, 8192) - m_padding_right)),
          m_track_length(std::max(0.0,
              static_cast<double>(std::clamp(width, 32, 8192) - m_padding_right)
              - static_cast<double>(m_padding_left))),
          m_current_value(1),
          m_thumb_x(static_cast<double>(m_padding_left)),
          m_dragging(false),
          m_drag_offset_x(0.0),
          m_animation(new QPropertyAnimation(this, QByteArrayLiteral("thumbX"), this))
    {
        setFixedSize(m_slider_width, m_slider_height);
        m_thumb_x = value_to_x(m_current_value);
        if (m_animation != nullptr) {
            m_animation->setDuration(kSliderAnimMs);
            m_animation->setEasingCurve(QEasingCurve::OutCubic);
        }
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    ~ModernSlider() override {
        if (m_animation != nullptr) {
            m_animation->stop();
        }
    }

    void setSliderEnabled(bool enabled) noexcept {
        m_enabled = enabled;
        if (enabled) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
            if (m_dragging) {
                m_dragging = false;
                if (m_animation != nullptr) {
                    m_animation->stop();
                }
            }
        }
        update();
    }

    [[nodiscard]] bool isSliderEnabled() const noexcept {
        return m_enabled;
    }

    [[nodiscard]] double getThumbX() const noexcept {
        return m_thumb_x;
    }

    void setThumbX(double value) noexcept {
        if (std::isnan(value) || std::isinf(value)) {
            return;
        }
        m_thumb_x = clamp_double(value, m_track_start, m_track_end);
        update();
    }

    [[nodiscard]] int value() const noexcept {
        return m_current_value;
    }

    void setValueSilent(int value) noexcept {
        const int clamped = std::clamp(value, m_min_val, m_max_val);
        if (m_animation != nullptr) {
            m_animation->stop();
        }
        m_current_value = clamped;
        m_thumb_x       = value_to_x(m_current_value);
        m_dragging      = false;
        update();
    }

    void apply_theme() noexcept {
        update();
    }

signals:
    void valueChanged(int value);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (!m_enabled || event == nullptr) {
            QWidget::mousePressEvent(event);
            return;
        }
        if (event->button() == Qt::LeftButton) {
            const double pos_x = event->position().x();
            const double dist  = std::abs(pos_x - m_thumb_x);
            if (dist <= static_cast<double>(m_thumb_outer_radius) + 8.0) {
                m_dragging      = true;
                m_drag_offset_x = m_thumb_x - pos_x;
                if (m_animation != nullptr) {
                    m_animation->stop();
                }
            } else {
                m_dragging = false;
                animate_to_value(snap_value(x_to_value(pos_x)));
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!m_enabled || !m_dragging || event == nullptr) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        const double pos_x   = event->position().x() + m_drag_offset_x;
        const int    snapped = snap_value(x_to_value(pos_x));
        if (snapped != m_current_value) {
            animate_to_value(snapped);
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_enabled && event != nullptr && event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            animate_to_value(snap_value(x_to_value(m_thumb_x)));
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor accent;
        QColor track;
        QColor thumb;
        QColor thumb_border;
        QColor thumb_outline;
        QColor text;
        if (m_enabled) {
            accent        = ThemeColors::accent();
            track         = ThemeColors::slider_track();
            thumb         = ThemeColors::accent();
            thumb_border  = ThemeColors::slider_thumb_border();
            thumb_outline = ThemeColors::slider_thumb_outline();
            text          = ThemeColors::slider_text();
        } else {
            accent        = ThemeColors::disabled_accent();
            track         = ThemeColors::disabled_track();
            thumb         = ThemeColors::disabled_thumb();
            thumb_border  = ThemeColors::disabled_thumb_border();
            thumb_outline = ThemeColors::disabled_thumb_outline();
            text          = ThemeColors::disabled_slider_text();
        }
        const double ty = static_cast<double>(m_track_y);
        const double th = static_cast<double>(m_track_height);
        const double r  = th / 2.0;
        const double ts = m_track_start;
        const double te = m_track_end;
        painter.setPen(Qt::NoPen);

        const double outer_r = static_cast<double>(m_thumb_outer_radius);
        const double gap     = outer_r + kToggleGapPx;

        const double left_end = m_thumb_x - gap;
        if (left_end > ts - r) {
            painter.setBrush(QBrush(accent));
            QPainterPath left_path;
            left_path.addRoundedRect(QRectF(ts - r, ty - r, left_end - (ts - r), th), r, r);
            painter.drawPath(left_path);
        }

        const double right_start = m_thumb_x + gap;
        if (right_start < te + r) {
            painter.setBrush(QBrush(track));
            QPainterPath right_path;
            right_path.addRoundedRect(QRectF(right_start, ty - r, (te + r) - right_start, th), r, r);
            painter.drawPath(right_path);
        }

        const double inner_r = static_cast<double>(m_thumb_radius);
        const QPointF thumb_center(m_thumb_x, ty);
        painter.setBrush(QBrush(thumb_border));
        painter.setPen(QPen(thumb_outline, 1.0));
        painter.drawEllipse(thumb_center, outer_r, outer_r);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(thumb));
        painter.drawEllipse(thumb_center, inner_r, inner_r);
        const double label_y = ty + 24.0;
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        painter.setPen(QPen(text));
        const QFontMetrics fm = painter.fontMetrics();
        for (int i = 0; i < m_levels; ++i) {
            const int     val        = m_min_val + i;
            const double  lx         = value_to_x(val);
            const QString label_text = QString::number(val);
            const int     tw         = fm.horizontalAdvance(label_text);
            const int     th2        = fm.height();
            painter.drawText(QPointF(lx - static_cast<double>(tw) / 2.0,
                                     label_y + static_cast<double>(th2) / 4.0),
                             label_text);
        }
    }

private:
    [[nodiscard]] double value_to_x(double value) const noexcept {
        if (m_max_val == m_min_val) {
            return m_track_start;
        }
        const double ratio   = (value - static_cast<double>(m_min_val))
                               / static_cast<double>(m_max_val - m_min_val);
        const double clamped = clamp_double(ratio, 0.0, 1.0);
        return m_track_start + clamped * m_track_length;
    }

    [[nodiscard]] double x_to_value(double x) const noexcept {
        if (m_track_length <= 0.0) {
            return static_cast<double>(m_min_val);
        }
        const double ratio = clamp_double((x - m_track_start) / m_track_length, 0.0, 1.0);
        return static_cast<double>(m_min_val) + ratio * static_cast<double>(m_max_val - m_min_val);
    }

    [[nodiscard]] int snap_value(double value) const noexcept {
        if (std::isnan(value) || std::isinf(value)) {
            return m_min_val;
        }
        return std::clamp(static_cast<int>(std::round(value)), m_min_val, m_max_val);
    }

    void animate_to_value(int snapped_value) noexcept {
        const int prev = m_current_value;
        m_current_value     = std::clamp(snapped_value, m_min_val, m_max_val);
        const double target = value_to_x(m_current_value);
        if (m_animation != nullptr) {
            m_animation->stop();
            m_animation->setStartValue(m_thumb_x);
            m_animation->setEndValue(target);
            m_animation->start();
        } else {
            m_thumb_x = target;
            update();
        }
        if (m_current_value != prev) {
            emit valueChanged(m_current_value);
        }
    }

    const int           m_slider_width;
    const int           m_slider_height;
    const int           m_levels;
    const int           m_min_val;
    const int           m_max_val;
    bool                m_enabled;
    const int           m_padding_left;
    const int           m_padding_right;
    const int           m_track_y;
    const int           m_track_height;
    const int           m_thumb_radius;
    const int           m_thumb_outer_radius;
    const double        m_track_start;
    const double        m_track_end;
    const double        m_track_length;
    int                 m_current_value;
    double              m_thumb_x;
    bool                m_dragging;
    double              m_drag_offset_x;
    QPropertyAnimation* m_animation;
};

class Deskwarp final : public QMainWindow {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Deskwarp)
public:
    explicit Deskwarp(QWidget* parent = nullptr)
        : QMainWindow(parent),
          m_central_widget(nullptr),
          m_title_label(nullptr),
          m_wobbly_frame(nullptr),
          m_wobbly_label(nullptr),
          m_wobbly_toggle(nullptr),
          m_realism_label(nullptr),
          m_realism_slider(nullptr),
          m_github_btn(nullptr)
    {
        setWindowTitle(QStringLiteral("Deskwarp"));
        setFixedSize(kWindowWidth, kWindowHeight);
        const QIcon app_icon = load_application_icon();
        if (!app_icon.isNull()) {
            setWindowIcon(app_icon);
        }
        build_ui();
        apply_theme();
        load_persisted_settings();
    }

    ~Deskwarp() override {
        if (m_wobbly_initialized) {
            m_wobbly.shutdown();
            m_wobbly_initialized = false;
        }
    }

    [[nodiscard]] bool ensureWobblyEngine() {
        if (m_wobbly_initialized) {
            return true;
        }
        (void)winId();
        if (m_wobbly.init(GetModuleHandleW(nullptr), reinterpret_cast<HWND>(winId()))) {
            m_wobbly_initialized = true;
            apply_wobbly_runtime_state();
            return true;
        }
        return false;
    }

    void showMainWindow() {
        showNormal();
        raise();
        activateWindow();
        (void)ensureWobblyEngine();
    }

    void requestForceQuit() {
        m_force_quit = true;
        if (m_wobbly_initialized) {
            m_wobbly.setEnabled(false);
        }
        hide();
        close();
    }

signals:
    void hiddenToTray();

protected:
    void showEvent(QShowEvent* event) override {
        QMainWindow::showEvent(event);
        (void)ensureWobblyEngine();
    }

    void closeEvent(QCloseEvent* event) override {
        if (!m_force_quit) {
            event->ignore();
            hide();
            emit hiddenToTray();
            return;
        }
        if (m_wobbly_initialized) {
            m_wobbly.setEnabled(false);
            m_wobbly.shutdown();
            m_wobbly_initialized = false;
        }
        QMainWindow::closeEvent(event);
    }

    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
#ifdef _WIN32
        if (eventType == QByteArrayLiteral("windows_generic_MSG") && message != nullptr) {
            const MSG* const msg = static_cast<const MSG*>(message);
            if (msg != nullptr) {
                if (m_wobbly_initialized && m_wobbly.handleUiMessage(msg->message, msg->wParam, msg->lParam)) {
                    if (result) *result = 0;
                    return true;
                }
                if (msg->message == 0x0320) {
                    check_system_theme();
                } else if (msg->message == WM_SETTINGCHANGE && msg->lParam != 0) {
                    const wchar_t* const name = reinterpret_cast<const wchar_t*>(msg->lParam);
                    if (name != nullptr && ::lstrcmpW(name, L"ImmersiveColorSet") == 0) {
                        check_system_theme();
                    }
                }
            }
        }
#endif
        return QMainWindow::nativeEvent(eventType, message, result);
    }

private slots:
    void on_wobbly_toggled(bool state) {
        if (m_realism_slider != nullptr) {
            m_realism_slider->setSliderEnabled(state);
        }
        if (m_wobbly_initialized) {
            m_wobbly.setEnabled(state);
            if (state && m_realism_slider != nullptr) {
                m_wobbly.setRealismLevel(m_realism_slider->value());
            }
        }
        update_realism_label_style();
        if (!m_loading_settings) {
            AppPersistence::setWobblyEnabled(state);
        }
    }

    void on_realism_changed(int level) {
        cfg::applyRealismLevel(level);
        if (m_wobbly_initialized) {
            m_wobbly.setRealismLevel(level);
        }
        if (!m_loading_settings) {
            AppPersistence::setWobblyRealism(level);
        }
    }

private:
    void load_persisted_settings() {
        const bool wobbly_on = AppPersistence::wobblyEnabled();
        const int  realism   = AppPersistence::wobblyRealism(kSliderLevels);
        cfg::applyRealismLevel(realism);
        m_loading_settings = true;
        if (m_wobbly_toggle != nullptr) {
            m_wobbly_toggle->setChecked(wobbly_on);
        }
        if (m_realism_slider != nullptr) {
            m_realism_slider->setValueSilent(realism);
            m_realism_slider->setSliderEnabled(wobbly_on);
        }
        m_loading_settings = false;
        update_realism_label_style();
    }

    void apply_wobbly_runtime_state() {
        if (!m_wobbly_initialized) {
            return;
        }
        const bool enabled = m_wobbly_toggle != nullptr && m_wobbly_toggle->isChecked();
        const int  realism = m_realism_slider != nullptr ? m_realism_slider->value() : 1;
        m_wobbly.setEnabled(enabled);
        m_wobbly.setRealismLevel(realism);
    }

    void check_system_theme() {
        const bool current_dark = detect_system_dark_mode();
        ThemeColors::set_dark(current_dark);
        apply_theme();
    }

    void build_ui() {
        m_central_widget = new QWidget(this);
        setCentralWidget(m_central_widget);

        auto* const main_layout = new QVBoxLayout(m_central_widget);
        main_layout->setContentsMargins(0, 0, 0, 0);
        main_layout->setSpacing(0);

        m_title_label = new QLabel(QStringLiteral("Visual Effects"), m_central_widget);
        m_title_label->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 16));
        m_title_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_title_label->setTextInteractionFlags(Qt::NoTextInteraction);
        m_title_label->setTextFormat(Qt::PlainText);
        main_layout->addWidget(m_title_label);

        m_wobbly_frame = new QFrame(m_central_widget);
        auto* const wobbly_layout = new QVBoxLayout(m_wobbly_frame);
        wobbly_layout->setContentsMargins(16, 16, 16, 16);
        wobbly_layout->setSpacing(0);

        auto* const wobbly_top = new QHBoxLayout();
        wobbly_top->setContentsMargins(0, 0, 0, 0);
        wobbly_top->setSpacing(0);
        m_wobbly_label = new QLabel(QStringLiteral("Wobbly Windows"), m_wobbly_frame);
        m_wobbly_label->setFont(QFont(QStringLiteral("Segoe UI"), 11));
        m_wobbly_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_wobbly_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_wobbly_label->setTextFormat(Qt::PlainText);
        m_wobbly_label->setTextInteractionFlags(Qt::NoTextInteraction);
        wobbly_top->addWidget(m_wobbly_label);
        m_wobbly_toggle = new ToggleSwitch(m_wobbly_frame, kToggleWidth, kToggleHeight);
        wobbly_top->addWidget(m_wobbly_toggle);
        wobbly_layout->addLayout(wobbly_top);

        auto* const slider_container = new QVBoxLayout();
        slider_container->setContentsMargins(0, 8, 0, 0);
        slider_container->setSpacing(0);
        m_realism_label = new QLabel(QStringLiteral("Realism"), m_wobbly_frame);
        m_realism_label->setFont(QFont(QStringLiteral("Segoe UI"), 9));
        m_realism_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_realism_label->setTextFormat(Qt::PlainText);
        m_realism_label->setTextInteractionFlags(Qt::NoTextInteraction);
        slider_container->addWidget(m_realism_label);
        m_realism_slider = new ModernSlider(m_wobbly_frame, kSliderWidth, kSliderHeight, kSliderLevels);
        m_realism_slider->setSliderEnabled(false);
        slider_container->addWidget(m_realism_slider);
        wobbly_layout->addLayout(slider_container);
        main_layout->addWidget(m_wobbly_frame);

        main_layout->addStretch();

        auto* const bottom_layout = new QHBoxLayout();
        bottom_layout->setAlignment(Qt::AlignCenter);
        bottom_layout->setContentsMargins(0, 0, 0, 0);
        bottom_layout->setSpacing(0);
        m_github_btn = new GitHubButton(m_central_widget);
        bottom_layout->addWidget(m_github_btn);
        main_layout->addLayout(bottom_layout);
        main_layout->addStretch();

        connect(m_wobbly_toggle.data(), &ToggleSwitch::toggled,
                this, &Deskwarp::on_wobbly_toggled);
        connect(m_realism_slider.data(), &ModernSlider::valueChanged,
                this, &Deskwarp::on_realism_changed);
    }

    void apply_theme() {
        const QString win_bg        = safe_hex_color(ThemeColors::window_bg().name(),    QStringLiteral("#f3f3f3"));
        const QString card_bg       = safe_hex_color(ThemeColors::card_bg().name(),      QStringLiteral("#ffffff"));
        const QString title_color   = safe_hex_color(ThemeColors::title_text().name(),   QStringLiteral("#1a1a1a"));
        const QString primary_color = safe_hex_color(ThemeColors::primary_text().name(), QStringLiteral("#1a1a1a"));

        setStyleSheet(QStringLiteral("QMainWindow { background-color: %1; }").arg(win_bg));
        if (m_central_widget != nullptr) {
            m_central_widget->setStyleSheet(QStringLiteral("background-color: %1;").arg(win_bg));
        }
        if (m_title_label != nullptr) {
            m_title_label->setStyleSheet(
                QStringLiteral("color: %1; background-color: %2; "
                               "padding-left: 24px; padding-right: 24px; "
                               "padding-top: 20px; padding-bottom: 16px;")
                    .arg(title_color, win_bg));
        }
        if (m_wobbly_frame != nullptr) {
            m_wobbly_frame->setStyleSheet(
                QStringLiteral("QFrame { background-color: %1; border: none; "
                               "margin-left: 24px; margin-right: 24px; }")
                    .arg(card_bg));
        }
        if (m_wobbly_label != nullptr) {
            m_wobbly_label->setStyleSheet(
                QStringLiteral("color: %1; background-color: %2;")
                    .arg(primary_color, card_bg));
        }
        update_realism_label_style();
        repaint_accent_consumers();
    }

    void repaint_accent_consumers() noexcept {
        if (m_wobbly_toggle  != nullptr) { m_wobbly_toggle->apply_theme();  }
        if (m_realism_slider != nullptr) { m_realism_slider->apply_theme(); }
        if (m_github_btn     != nullptr) { m_github_btn->apply_theme();     }
    }

    void update_realism_label_style() {
        if (m_realism_label == nullptr || m_realism_slider == nullptr) {
            return;
        }
        const QString card_bg = safe_hex_color(ThemeColors::card_bg().name(), QStringLiteral("#ffffff"));
        const QString label_color = m_realism_slider->isSliderEnabled()
            ? safe_hex_color(ThemeColors::secondary_text().name(), QStringLiteral("#666666"))
            : safe_hex_color(ThemeColors::disabled_text().name(),  QStringLiteral("#b0b0b0"));
        m_realism_label->setStyleSheet(
            QStringLiteral("color: %1; background-color: %2;")
                .arg(label_color, card_bg));
    }

    QPointer<QWidget>      m_central_widget;
    QPointer<QLabel>       m_title_label;
    QPointer<QFrame>       m_wobbly_frame;
    QPointer<QLabel>       m_wobbly_label;
    QPointer<ToggleSwitch> m_wobbly_toggle;
    QPointer<QLabel>       m_realism_label;
    QPointer<ModernSlider> m_realism_slider;
    QPointer<GitHubButton> m_github_btn;
    WobblyController     m_wobbly;
    bool                 m_wobbly_initialized = false;
    bool                 m_force_quit         = false;
    bool                 m_loading_settings   = false;
};

struct Strings {
    QString title;
    QString body;
    QString button;
    bool rtl = false;
};

[[nodiscard]] Strings localized_strings() {
    const QLocale::Language lang = QLocale::system().language();
    switch (lang) {
    case QLocale::Russian:
        return {
            QString::fromUtf8("Поддержка"),
            QString::fromUtf8("Здравствуйте, мне 15 лет, эта программа абсолютно бесплатная. Я буду очень благодарен вам, если вы поможете накопить мне на хорошее рабочее место. Спасибо за установку Deskwarp :)"),
            QString::fromUtf8("Донат"), false };
    case QLocale::Ukrainian:
        return {
            QString::fromUtf8("Підтримка"),
            QString::fromUtf8("Вітаю, мені 15 років, ця програма абсолютно безкоштовна. Я буду дуже вдячний вам, якщо ви допоможете мені накопичити на гарне робоче місце. Дякую за встановлення Deskwarp :)"),
            QString::fromUtf8("Донат"), false };
    case QLocale::German:
        return {
            QString::fromUtf8("Unterstützung"),
            QString::fromUtf8("Hallo, ich bin 15 Jahre alt und dieses Programm ist völlig kostenlos. Ich wäre Ihnen sehr dankbar, wenn Sie mir helfen würden, für einen guten Arbeitsplatz zu sparen. Danke, dass Sie Deskwarp installiert haben :)"),
            QString::fromUtf8("Spenden"), false };
    case QLocale::French:
        return {
            QString::fromUtf8("Soutien"),
            QString::fromUtf8("Bonjour, j'ai 15 ans et ce programme est entièrement gratuit. Je vous serais très reconnaissant de m'aider à économiser pour un bon poste de travail. Merci d'avoir installé Deskwarp :)"),
            QString::fromUtf8("Faire un don"), false };
    case QLocale::Spanish:
        return {
            QString::fromUtf8("Apoyo"),
            QString::fromUtf8("Hola, tengo 15 años y este programa es completamente gratuito. Te estaría muy agradecido si me ayudaras a ahorrar para un buen espacio de trabajo. Gracias por instalar Deskwarp :)"),
            QString::fromUtf8("Donar"), false };
    case QLocale::Italian:
        return {
            QString::fromUtf8("Supporto"),
            QString::fromUtf8("Ciao, ho 15 anni e questo programma è completamente gratuito. Ti sarei molto grato se mi aiutassi a risparmiare per una buona postazione di lavoro. Grazie per aver installato Deskwarp :)"),
            QString::fromUtf8("Dona"), false };
    case QLocale::Portuguese:
        return {
            QString::fromUtf8("Apoio"),
            QString::fromUtf8("Olá, tenho 15 anos e este programa é totalmente gratuito. Ficaria muito grato se você me ajudasse a juntar dinheiro para um bom espaço de trabalho. Obrigado por instalar o Deskwarp :)"),
            QString::fromUtf8("Doar"), false };
    case QLocale::Polish:
        return {
            QString::fromUtf8("Wsparcie"),
            QString::fromUtf8("Cześć, mam 15 lat, a ten program jest całkowicie darmowy. Byłbym bardzo wdzięczny, gdybyś pomógł mi uzbierać na dobre stanowisko pracy. Dziękuję za zainstalowanie Deskwarp :)"),
            QString::fromUtf8("Wesprzyj"), false };
    case QLocale::Turkish:
        return {
            QString::fromUtf8("Destek"),
            QString::fromUtf8("Merhaba, 15 yaşındayım ve bu program tamamen ücretsiz. İyi bir çalışma alanı için para biriktirmeme yardımcı olursanız çok minnettar olurum. Deskwarp'ı yüklediğiniz için teşekkürler :)"),
            QString::fromUtf8("Bağış yap"), false };
    case QLocale::Dutch:
        return {
            QString::fromUtf8("Ondersteuning"),
            QString::fromUtf8("Hallo, ik ben 15 jaar oud en dit programma is volledig gratis. Ik zou je erg dankbaar zijn als je me zou helpen sparen voor een goede werkplek. Bedankt voor het installeren van Deskwarp :)"),
            QString::fromUtf8("Doneren"), false };
    case QLocale::Chinese:
        return {
            QString::fromUtf8("支持"),
            QString::fromUtf8("您好，我今年15岁，这个程序完全免费。如果您能帮助我攒钱购置一个好的工作环境，我将非常感激。感谢您安装 Deskwarp :)"),
            QString::fromUtf8("捐赠"), false };
    case QLocale::Japanese:
        return {
            QString::fromUtf8("サポート"),
            QString::fromUtf8("こんにちは、私は15歳です。このプログラムは完全に無料です。良い作業環境のための資金を貯めるのを手伝っていただけると、とても感謝します。Deskwarp をインストールしていただきありがとうございます :)"),
            QString::fromUtf8("寄付する"), false };
    case QLocale::Korean:
        return {
            QString::fromUtf8("후원"),
            QString::fromUtf8("안녕하세요, 저는 15살이고 이 프로그램은 완전히 무료입니다. 좋은 작업 공간을 마련할 수 있도록 도와주시면 정말 감사하겠습니다. Deskwarp를 설치해 주셔서 감사합니다 :)"),
            QString::fromUtf8("후원하기"), false };
    case QLocale::Arabic:
        return {
            QString::fromUtf8("الدعم"),
            QString::fromUtf8("مرحبًا، عمري 15 عامًا، وهذا البرنامج مجاني تمامًا. سأكون ممتنًا جدًا لك إذا ساعدتني في توفير المال لمكان عمل جيد. شكرًا لتثبيت Deskwarp :)"),
            QString::fromUtf8("تبرع"), true };
    case QLocale::English:
    default:
        return {
            QString::fromUtf8("Support"),
            QString::fromUtf8("Hello, I'm 15 years old, and this program is completely free. I would be very grateful if you could help me save up for a good workspace. Thank you for installing Deskwarp :)"),
            QString::fromUtf8("Donate"), false };
    }
}

[[nodiscard]] QString contrast_text_for(const QColor& bg) {
    const double luminance = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
    return luminance > 0.6 ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
}

class DonateWindow final : public QWidget {
public:
    explicit DonateWindow(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_QuitOnClose, false);
        build_ui();
        apply_theme();
        apply_dwm_theme();
        setFixedSize(kDonateWindowWidth, kDonateWindowHeight);
    }
protected:
#ifdef _WIN32
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
        if (eventType == QByteArrayLiteral("windows_generic_MSG") && message != nullptr) {
            const MSG* const msg = static_cast<const MSG*>(message);
            if (msg != nullptr) {
                if (msg->message == 0x0320) {
                    check_system_theme();
                } else if (msg->message == 0x001A && msg->lParam != 0) {
                    const wchar_t* const name = reinterpret_cast<const wchar_t*>(msg->lParam);
                    if (name != nullptr && ::lstrcmpW(name, L"ImmersiveColorSet") == 0) {
                        check_system_theme();
                    }
                }
            }
        }
        return QWidget::nativeEvent(eventType, message, result);
    }
#endif
private:
    void build_ui() {
        const Strings s = localized_strings();
        if (s.rtl) {
            setLayoutDirection(Qt::RightToLeft);
        }
        setWindowTitle(s.title);
        auto* const main_layout = new QVBoxLayout(this);
        main_layout->setContentsMargins(0, 0, 0, 0);
        main_layout->setSpacing(0);
        m_title_label = new QLabel(s.title, this);
        m_title_label->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 16));
        m_title_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        main_layout->addWidget(m_title_label);
        m_card = new QFrame(this);
        auto* const card_layout = new QVBoxLayout(m_card);
        card_layout->setContentsMargins(20, 18, 20, 18);
        card_layout->setSpacing(0);
        m_body_label = new QLabel(s.body, m_card);
        m_body_label->setFont(QFont(QStringLiteral("Segoe UI"), 11));
        m_body_label->setWordWrap(true);
        m_body_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        card_layout->addWidget(m_body_label);
        main_layout->addWidget(m_card, 1);
        m_separator = new QFrame(this);
        m_separator->setFixedHeight(1);
        main_layout->addWidget(m_separator);
        auto* const footer = new QWidget(this);
        auto* const footer_layout = new QHBoxLayout(footer);
        footer_layout->setContentsMargins(20, 14, 20, 16);
        footer_layout->setSpacing(0);
        footer_layout->addStretch(1);
        m_donate_button = new QPushButton(s.button, footer);
        m_donate_button->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 10));
        m_donate_button->setCursor(Qt::PointingHandCursor);
        m_donate_button->setMinimumHeight(36);
        m_donate_button->setMinimumWidth(120);
        QObject::connect(m_donate_button, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://dalink.to/doebalov")));
        });
        footer_layout->addWidget(m_donate_button);
        main_layout->addWidget(footer);
    }
    void apply_theme() {
        const QString win_bg = safe_hex_color(ThemeColors::window_bg().name(), "#f3f3f3");
        const QString card_bg = safe_hex_color(ThemeColors::card_bg().name(), "#ffffff");
        const QString sep_col = safe_hex_color(ThemeColors::separator().name(), "#e5e5e5");
        const QString title_c = safe_hex_color(ThemeColors::title_text().name(), "#1a1a1a");
        const QString text_c = safe_hex_color(ThemeColors::primary_text().name(), "#1a1a1a");
        const QColor accent = ThemeColors::accent();
        const QString accent_c = safe_hex_color(accent.name(), "#0078d4");
        const bool is_dark = ThemeColors::is_dark();
        const QString accent_hover = safe_hex_color(accent.lighter(is_dark ? 115 : 110).name(), "#1a86d9");
        const QString accent_press = safe_hex_color(accent.darker(115).name(), "#006cbe");
        const QString on_accent = contrast_text_for(accent);
        setStyleSheet(QStringLiteral("QWidget { background-color: %1; }").arg(win_bg));
        m_title_label->setStyleSheet(QStringLiteral("color: %1; background-color: %2; padding: 20px 24px 12px 24px;").arg(title_c, win_bg));
        m_card->setStyleSheet(QStringLiteral("QFrame { background-color: %1; border: none; margin-left: 24px; margin-right: 24px; }").arg(card_bg));
        m_body_label->setStyleSheet(QStringLiteral("color: %1; background-color: %2;").arg(text_c, card_bg));
        m_separator->setStyleSheet(QStringLiteral("QFrame { background-color: %1; border: none; margin-left: 24px; margin-right: 24px; }").arg(sep_col));
        m_donate_button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; color: %2; border: none; border-radius: 6px; padding: 8px 22px; } QPushButton:hover { background-color: %3; } QPushButton:pressed{ background-color: %4; }").arg(accent_c, on_accent, accent_hover, accent_press));
    }
    void apply_dwm_theme() {
#ifdef _WIN32
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const BOOL dark = ThemeColors::is_dark() ? TRUE : FALSE;
        ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
#endif
    }
    void check_system_theme() {
        apply_theme();
        apply_dwm_theme();
    }
    QLabel* m_title_label = nullptr;
    QFrame* m_card = nullptr;
    QLabel* m_body_label = nullptr;
    QFrame* m_separator = nullptr;
    QPushButton* m_donate_button = nullptr;
};

#include "app.moc"

int main(int argc, char* argv[]) {
    std::set_terminate(secure_terminate_handler);
    std::set_new_handler(secure_new_handler);
    qInstallMessageHandler(secure_qt_message_handler);

#ifdef _WIN32
    apply_windows_mitigations();
    ::SetCurrentProcessExplicitAppUserModelID(L"Deskwarp.utility.v1");
    if (!WobblyController::ensureElevated(argc, argv)) {
        return EXIT_SUCCESS;
    }
#endif

    try {
        QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

        QSurfaceFormat fmt;
        fmt.setSamples(4);
        fmt.setSwapInterval(1);
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        QSurfaceFormat::setDefaultFormat(fmt);

        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        app.setStyle(QStringLiteral("Fusion"));
        app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
        app.setApplicationName(QStringLiteral("Deskwarp"));
        app.setApplicationDisplayName(QStringLiteral("Deskwarp"));
        app.setOrganizationName(QStringLiteral("Deskwarp"));

        const bool background_launch = app.arguments().contains(kStartupArg);

        if (AppPersistence::notifyExistingInstance()) {
            return EXIT_SUCCESS;
        }

        QSystemSemaphore sem(QStringLiteral("Deskwarp.SingleInstance.Sem.v1"), 1);
        QSharedMemory shared(QStringLiteral("Deskwarp.SingleInstance.Mem.v1"));
        {
            SemaphoreGuard guard(sem);
            if (!guard.acquired()) {
                return EXIT_FAILURE;
            }
            const bool already_running = shared.attach();
            if (already_running) {
                shared.detach();
                if (AppPersistence::notifyExistingInstance()) {
                    return EXIT_SUCCESS;
                }
                return EXIT_SUCCESS;
            }
            if (!shared.create(1)) {
                return EXIT_FAILURE;
            }
        }

        QLocalServer local_server;
        QLocalServer::removeServer(kLocalServerName);
        if (!local_server.listen(kLocalServerName)) {
            return EXIT_FAILURE;
        }

        ThemeColors::set_dark(detect_system_dark_mode());
        ThemeColors::refresh_accent();

        const QIcon app_icon = load_application_icon();
        if (!app_icon.isNull()) {
            app.setWindowIcon(app_icon);
        }

        Deskwarp window;
        if (!app_icon.isNull()) {
            window.setWindowIcon(app_icon);
        }

        DonateWindow donateWindow;
        if (!app_icon.isNull()) {
            donateWindow.setWindowIcon(app_icon);
        }

        center_window_on_screen(&window);
        center_window_on_screen(&donateWindow);

        AppPersistence::ensureStartupRegistration();

        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            return EXIT_FAILURE;
        }

        QSystemTrayIcon tray_icon;
        if (!app_icon.isNull()) {
            tray_icon.setIcon(app_icon);
        }
        tray_icon.setToolTip(QStringLiteral("Deskwarp"));

        QMenu tray_menu;
        auto* const open_action = tray_menu.addAction(QStringLiteral("Open"));
        auto* const exit_action = tray_menu.addAction(QStringLiteral("Exit"));
        tray_icon.setContextMenu(&tray_menu);
        tray_icon.show();

        const auto show_windows = [&window, &donateWindow]() {
            center_window_on_screen(&window);
            center_window_on_screen(&donateWindow);
            window.showMainWindow();
            if (!donateWindow.isVisible()) {
                donateWindow.show();
            }
            donateWindow.raise();
        };

        QObject::connect(open_action, &QAction::triggered, &app, show_windows);
        QObject::connect(&tray_icon, &QSystemTrayIcon::activated, &app,
            [&window, &donateWindow](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    center_window_on_screen(&window);
                    center_window_on_screen(&donateWindow);
                    window.showMainWindow();
                    if (!donateWindow.isVisible()) {
                        donateWindow.show();
                    }
                    donateWindow.raise();
                }
            });

        QObject::connect(exit_action, &QAction::triggered, &app, [&]() {
            window.requestForceQuit();
            donateWindow.close();
            tray_icon.hide();
            app.quit();
        });

        QObject::connect(&window, &Deskwarp::hiddenToTray, &donateWindow, &QWidget::hide);

        QObject::connect(&local_server, &QLocalServer::newConnection, &app, [&local_server, show_windows]() {
            QLocalSocket* const client = local_server.nextPendingConnection();
            if (client == nullptr) {
                return;
            }
            if (client->waitForReadyRead(500)) {
                if (client->readAll() == QByteArrayLiteral("show")) {
                    show_windows();
                }
            }
            client->disconnectFromServer();
            client->deleteLater();
        });

        if (background_launch) {
            (void)window.winId();
            (void)window.ensureWobblyEngine();
        } else {
            window.show();
            donateWindow.show();
            (void)window.ensureWobblyEngine();
        }

        const int rc = app.exec();
        if (shared.isAttached()) {
            shared.detach();
        }
        local_server.close();
        QLocalServer::removeServer(kLocalServerName);
        return rc;
    } catch (const std::bad_alloc&) {
        return EXIT_FAILURE;
    } catch (const std::exception&) {
        return EXIT_FAILURE;
    } catch (...) {
        return EXIT_FAILURE;
    }
}