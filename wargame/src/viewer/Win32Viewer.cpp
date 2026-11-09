#include "viewer/Win32Viewer.h"

#ifdef _WIN32

#include "engine/Simulation.h"

// The wide-character API is used throughout this file, so the resource macros
// (IDC_ARROW and friends) must expand to their wide forms. Without this they
// expand to LPSTR and will not pass to LoadCursorW.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

// MSVC links these from here, so the build command needs no extra switches.
// MinGW takes them from its default link set.
#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

namespace wargame {

namespace {

// Colours are held as 0x00RRGGBB. With a 32-bit BI_RGB bitmap this is the byte
// order GDI expects on a little-endian machine, so the values can be written
// straight into the pixel buffer. Text and fills go through GDI's COLORREF,
// which is 0x00BBGGRR, hence the conversion.
COLORREF toColorRef(std::uint32_t rgb) {
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

const wchar_t* kClassName = L"WarGameViewerWindow";

class Win32Viewer : public Viewer {
  public:
    explicit Win32Viewer(const WindowStyle& style)
        : style_(style),
          cell_(style.cellPixels),
          statusHeight_(style.statusHeightPixels) {}

    ~Win32Viewer() override {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
        UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
    }

    void begin(const Simulation& sim) override;
    void frame(const Simulation& sim) override;
    void end(const Simulation& sim) override;

    bool stopRequested() const override { return closed_; }

  private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void pumpMessages();
    void paint(const Simulation& sim);
    void drawStatus(HDC dc, const Simulation& sim);

    WindowStyle style_;
    std::int32_t cell_ = 0;
    std::int32_t statusHeight_ = 0;
    std::int32_t rows_ = 0;
    std::int32_t cols_ = 0;
    std::int32_t width_ = 0;   // of the map area, in pixels
    std::int32_t height_ = 0;

    HWND hwnd_ = nullptr;
    bool closed_ = false;
    std::vector<std::uint32_t> pixels_;
};

LRESULT CALLBACK Win32Viewer::wndProc(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
    auto* self = reinterpret_cast<Win32Viewer*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            if (self != nullptr) {
                self->closed_ = true;
            }
            if (msg == WM_CLOSE) {
                DestroyWindow(hwnd);
                return 0;
            }
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            // Escape closes the window, which stops the run.
            if (wp == VK_ESCAPE && self != nullptr) {
                self->closed_ = true;
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;  // the whole client area is repainted every frame

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Win32Viewer::begin(const Simulation& sim) {
    const World& w = sim.world();
    rows_ = w.rows();
    cols_ = w.cols();
    width_ = cols_ * cell_;
    height_ = rows_ * cell_;

    pixels_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_),
                   style_.ground);

    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Win32Viewer::wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // Size the window so the client area is exactly the map plus the status
    // strip; otherwise the borders would eat into the picture.
    RECT want{0, 0, width_, height_ + statusHeight_};
    AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExW(
        0, kClassName, L"War game - plain battle", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, want.right - want.left,
        want.bottom - want.top, nullptr, nullptr, instance, nullptr);

    if (hwnd_ == nullptr) {
        closed_ = true;
        return;
    }

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    frame(sim);
}

void Win32Viewer::pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            closed_ = true;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Win32Viewer::paint(const Simulation& sim) {
    const World& w = sim.world();

    // Fill the pixel buffer one box at a time. At 64x64 boxes this is a few
    // tens of thousands of writes per frame — nothing, at a handful of frames
    // a second.
    for (std::int32_t r = 0; r < rows_; ++r) {
        for (std::int32_t c = 0; c < cols_; ++c) {
            const Cell& cellData = w.at(GridPos(r, c));

            std::uint32_t colour = style_.ground;
            if (cellData.blueCount > 0 && cellData.redCount > 0) {
                colour = style_.contact;
            } else if (cellData.blueCount > 0) {
                colour = style_.blue;
            } else if (cellData.redCount > 0) {
                colour = style_.red;
            }

            const std::int32_t x0 = c * cell_;
            const std::int32_t y0 = r * cell_;
            for (std::int32_t y = 0; y < cell_; ++y) {
                std::uint32_t* row =
                    &pixels_[static_cast<size_t>(y0 + y) *
                                 static_cast<size_t>(width_) +
                             static_cast<size_t>(x0)];
                for (std::int32_t x = 0; x < cell_; ++x) {
                    // A one-pixel gutter keeps the boxes readable as boxes.
                    const bool gutter =
                        (cell_ >= style_.gutterMinCellPixels) &&
                        (x == cell_ - 1 || y == cell_ - 1);
                    row[x] = (gutter && colour == style_.ground) ? style_.grid
                                                                 : colour;
                }
            }
        }
    }

    const HDC dc = GetDC(hwnd_);
    if (dc == nullptr) {
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_;  // negative: rows run top to bottom
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(dc, 0, statusHeight_, width_, height_, 0, 0, width_, height_,
                  pixels_.data(), &info, DIB_RGB_COLORS, SRCCOPY);

    drawStatus(dc, sim);
    ReleaseDC(hwnd_, dc);
}

void Win32Viewer::drawStatus(HDC dc, const Simulation& sim) {
    const World& w = sim.world();

    std::int32_t blue = 0;
    std::int32_t red = 0;
    std::int32_t contested = 0;
    for (std::int32_t i = 0; i < w.cellCount(); ++i) {
        const Cell& c = w.at(i);
        blue += c.blueCount;
        red += c.redCount;
        if (c.blueCount > 0 && c.redCount > 0) {
            ++contested;
        }
    }

    RECT bar{0, 0, width_, statusHeight_};
    HBRUSH brush = CreateSolidBrush(toColorRef(style_.statusBackground));
    FillRect(dc, &bar, brush);
    DeleteObject(brush);

    char text[192];
    std::snprintf(text, sizeof(text),
                  "  day %.2f    blue %d    red %d    boxes in contact %d",
                  sim.ticksToSeconds(sim.now()) / 86400.0, blue, red,
                  contested);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, toColorRef(style_.statusText));
    RECT textRect{0, 4, width_, statusHeight_};
    DrawTextA(dc, text, -1, &textRect, DT_LEFT | DT_SINGLELINE);
}

void Win32Viewer::frame(const Simulation& sim) {
    if (closed_ || hwnd_ == nullptr) {
        return;
    }
    pumpMessages();
    if (closed_) {
        return;
    }
    paint(sim);
}

void Win32Viewer::end(const Simulation& sim) {
    if (closed_ || hwnd_ == nullptr) {
        return;
    }
    paint(sim);
    SetWindowTextW(hwnd_, L"War game - finished (close the window to continue)");

    // Hold the final picture until the window is closed, so the result can
    // actually be looked at.
    while (!closed_) {
        pumpMessages();
        Sleep(static_cast<DWORD>(style_.closePollMs));
    }
}

}  // namespace

std::unique_ptr<Viewer> makeWindowViewer(const WindowStyle& style) {
    return std::make_unique<Win32Viewer>(style);
}

}  // namespace wargame

#else  // not Windows

namespace wargame {

std::unique_ptr<Viewer> makeWindowViewer(const WindowStyle&) {
    return nullptr;  // the factory falls back to the console viewer
}

}  // namespace wargame

#endif
