#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
#include <strsafe.h>
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace Tray {
enum class OwnershipPolicy {
    Copy,   // default: clone handle => wrapper owns clone
    Borrow, // non-owning view (wrapper never frees)
    Adopt   // take ownership of passed handle
};

class Icon {
    HICON hIcon_ = nullptr;
    bool owns_ = false;

    static HICON cloneIcon(HICON src) {
        if (!src)
            return nullptr;
        return CopyIcon(src);
    }

  public:
    Icon() = default;

    // Default: COPY so we always own our handle safely
    explicit Icon(HICON h, OwnershipPolicy p = OwnershipPolicy::Copy) {
        switch (p) {
            case OwnershipPolicy::Copy:
                hIcon_ = cloneIcon(h);
                owns_ = (hIcon_ != nullptr);
                break;
            case OwnershipPolicy::Borrow:
                hIcon_ = h;
                owns_ = false;
                break;
            case OwnershipPolicy::Adopt:
                hIcon_ = h;
                owns_ = true;
                break;
        }
    }

    // Load from file => own
    explicit Icon(const std::wstring& path) {
        hIcon_ = reinterpret_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (!hIcon_)
            throw std::runtime_error("LoadImageW icon failed");
        owns_ = true;
    }
    explicit Icon(const wchar_t* path) : Icon(std::wstring(path)) {}

    // From resource (classic LoadIconW returns shared) => clone so we own
    explicit Icon(WORD resid) {
        HICON shared = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resid));
        if (!shared)
            throw std::runtime_error("LoadIconW resource failed");
        hIcon_ = cloneIcon(shared);
        if (!hIcon_)
            throw std::runtime_error("CopyIcon failed");
        owns_ = true;
    }

    // Copy => make our own clone so both objects are independent owners
    Icon(const Icon& rhs) {
        if (rhs.hIcon_) {
            hIcon_ = cloneIcon(rhs.hIcon_);
            owns_ = (hIcon_ != nullptr);
        }
    }
    Icon& operator=(const Icon& rhs) {
        if (this == &rhs)
            return *this;
        reset();
        if (rhs.hIcon_) {
            hIcon_ = cloneIcon(rhs.hIcon_);
            owns_ = (hIcon_ != nullptr);
        }
        return *this;
    }

    // Move => transfer
    Icon(Icon&& rhs) noexcept : hIcon_(rhs.hIcon_), owns_(rhs.owns_) {
        rhs.hIcon_ = nullptr;
        rhs.owns_ = false;
    }
    Icon& operator=(Icon&& rhs) noexcept {
        if (this == &rhs)
            return *this;
        reset();
        hIcon_ = rhs.hIcon_;
        owns_ = rhs.owns_;
        rhs.hIcon_ = nullptr;
        rhs.owns_ = false;
        return *this;
    }

    ~Icon() {
        reset();
    }

    void reset() {
        if (owns_ && hIcon_)
            DestroyIcon(hIcon_);
        hIcon_ = nullptr;
        owns_ = false;
    }

    operator HICON() const {
        return hIcon_;
    }
    HICON get() const {
        return hIcon_;
    }

    static Icon FromStock(SHSTOCKICONID id, bool small_icon = true) {
        HICON h = nullptr;

        // Try multiple SHGetStockIconInfo flags first
        SHSTOCKICONINFO sii{};
        sii.cbSize = sizeof(sii);
        const UINT tryFlags[] = {(UINT)(SHGSI_ICON | (small_icon ? SHGSI_SMALLICON : SHGSI_LARGEICON)), (UINT)(SHGSI_ICON | SHGSI_SHELLICONSIZE), (UINT)(SHGSI_ICON)};
        for (UINT f : tryFlags) {
            ZeroMemory(&sii, sizeof(sii));
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(SHGetStockIconInfo(id, f, &sii)) && sii.hIcon) {
                h = sii.hIcon; // returned HICON we own
                break;
            }
        }

        // Fallback: pull icon location and ExtractIconExW
        if (!h) {
            ZeroMemory(&sii, sizeof(sii));
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(SHGetStockIconInfo(id, SHGSI_ICONLOCATION, &sii)) && sii.szPath[0]) {
                HICON hLarge = nullptr, hSmall = nullptr;
                if (ExtractIconExW(sii.szPath, sii.iIcon, &hLarge, &hSmall, 1) > 0) {
                    h = small_icon ? (hSmall ? hSmall : hLarge) : (hLarge ? hLarge : hSmall);
                    // whichever we didn't pick, destroy it to avoid leaks
                    if (hSmall && hSmall != h)
                        DestroyIcon(hSmall);
                    if (hLarge && hLarge != h)
                        DestroyIcon(hLarge);
                }
            }
        }

        // Return RAII wrapper; adopt so we destroy it
        if (h)
            return Icon(h, OwnershipPolicy::Adopt);
        return Icon(); // empty; caller may choose a fallback
    }

    // Convert to 32bpp top-down DIB for MIIM_BITMAP glyphs
    HBITMAP toBitmap(int cx, int cy) const {
        if (!hIcon_)
            return nullptr;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = cx;
        bi.bmiHeader.biHeight = -cy; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        if (!screen)
            return nullptr;

        HBITMAP hbmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbmp) {
            ReleaseDC(nullptr, screen);
            return nullptr;
        }

        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(mem, hbmp);

        RECT rc{0, 0, cx, cy};
        HBRUSH clear = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(mem, &rc, clear);

        DrawIconEx(mem, 0, 0, hIcon_, cx, cy, 0, nullptr, DI_NORMAL);

        SelectObject(mem, old);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);

        return hbmp; // caller owns; in your code you wrap with Image(HBITMAP,true)
    }
};

//----------------------- Image (RAII, defaults to own via clone) -----------------------
class Image {
    HBITMAP hbmp_ = nullptr;
    bool owns_ = false;

    static HBITMAP cloneBitmap(HBITMAP src) {
        if (!src)
            return nullptr;
        // Copy with same size; 0,0 lets GDI pick original size; LR_CREATEDIBSECTION gives an ownable DIB
        return reinterpret_cast<HBITMAP>(CopyImage(src, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    }

  public:
    Image() = default;

    explicit Image(HBITMAP h, OwnershipPolicy p = OwnershipPolicy::Copy) {
        switch (p) {
            case OwnershipPolicy::Copy:
                hbmp_ = cloneBitmap(h);
                owns_ = (hbmp_ != nullptr);
                break;
            case OwnershipPolicy::Borrow:
                hbmp_ = h;
                owns_ = false;
                break;
            case OwnershipPolicy::Adopt:
                hbmp_ = h;
                owns_ = true;
                break;
        }
    }

    explicit Image(const std::wstring& path) {
        hbmp_ = reinterpret_cast<HBITMAP>(LoadImageW(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
        if (!hbmp_)
            throw std::runtime_error("LoadImageW bitmap failed");
        owns_ = true;
    }
    explicit Image(const wchar_t* path) : Image(std::wstring(path)) {}

    Image(const Image& rhs) {
        if (rhs.hbmp_) {
            hbmp_ = cloneBitmap(rhs.hbmp_);
            owns_ = (hbmp_ != nullptr);
        }
    }
    Image& operator=(const Image& rhs) {
        if (this == &rhs)
            return *this;
        reset();
        if (rhs.hbmp_) {
            hbmp_ = cloneBitmap(rhs.hbmp_);
            owns_ = (hbmp_ != nullptr);
        }
        return *this;
    }

    Image(Image&& rhs) noexcept : hbmp_(rhs.hbmp_), owns_(rhs.owns_) {
        rhs.hbmp_ = nullptr;
        rhs.owns_ = false;
    }
    Image& operator=(Image&& rhs) noexcept {
        if (this == &rhs)
            return *this;
        reset();
        hbmp_ = rhs.hbmp_;
        owns_ = rhs.owns_;
        rhs.hbmp_ = nullptr;
        rhs.owns_ = false;
        return *this;
    }

    ~Image() {
        reset();
    }

    void reset() {
        if (owns_ && hbmp_)
            DeleteObject(hbmp_);
        hbmp_ = nullptr;
        owns_ = false;
    }

    operator HBITMAP() const {
        return hbmp_;
    }
    HBITMAP get() const {
        return hbmp_;
    }
};
} // namespace Tray
