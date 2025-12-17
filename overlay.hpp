#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <functional>
#include <concepts>

template <typename T>
concept com_obj = std::is_base_of<IUnknown, T>::value;

template <com_obj T>
static inline void SafeRelease(T** ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

const wchar_t* const kOverlayClassName = L"OverlayWndClass";

class OverlayWindow {
  public:
    OverlayWindow();
    ~OverlayWindow();

    bool Init(HINSTANCE hInstance);
    bool RecreateDeviceResources();
    void Destroy();
    void Show();
    void Hide();
    void Move(int x, int y);
    void Resize(int width, int height);
    void Render();
    void PreRender(const std::function<bool()>& condition, const std::function<void()>& onFrame);
    void SetColor(const D2D1_COLOR_F& color);
    void SetGradient(const D2D1_COLOR_F& start, const D2D1_COLOR_F& end, float angleDeg = 0.f, bool rotating = false, float rotationSpeed = 90.f);
    HWND GetHwnd() const {
        return hwnd;
    }

    inline void SetBorderThickness(float bt) {
        borderThickness = bt;
        thicknessOuter = std::floor(bt / 2.0f);
        thicknessInner = bt - thicknessOuter;
    };

  private:
    void CreateGradientBrushes();
    bool CreateRenderTarget(UINT width, UINT height);

    HWND hwnd = nullptr;
    HINSTANCE hInst = nullptr;
    ID2D1Factory* d2dFactory = nullptr;
    ID2D1HwndRenderTarget* renderTarget = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    ID2D1SolidColorBrush* fadeBrush = nullptr;

    ID2D1LinearGradientBrush* gradientBrushOuter = nullptr;
    ID2D1LinearGradientBrush* gradientBrushInner = nullptr;
    ID2D1GradientStopCollection* gradientStopsOuter = nullptr;
    ID2D1GradientStopCollection* gradientStopsInner = nullptr;

    bool gradient = false;
    bool rotating = false;
    float rotationSpeed = 120.f;
    float gradientAngleDeg = 0.0f;
    D2D1_COLOR_F solidColor{};

    D2D1_RECT_F outerRect{};
    D2D1_RECT_F innerRect{};

    D2D1_ROUNDED_RECT outerRounded{};
    D2D1_ROUNDED_RECT innerRounded{};

    const float default_radius = 8.0f;
    float m_radius = default_radius;
    float borderThickness = 4.0f;
    float thicknessOuter = std::floor(borderThickness / 2.0f);
    float thicknessInner = borderThickness - thicknessOuter;

    D2D1_COLOR_F gradientStart{};
    D2D1_COLOR_F gradientEnd{};

    int lastWidth = 0;
    int lastHeight = 0;
    bool visible = false;
    bool vsync = true;
};