#include "pch.hpp"
#include "overlay.hpp"
#include <Uxtheme.h>
#include <algorithm>

#include "tinylog.hpp"

OverlayWindow::OverlayWindow() {}

OverlayWindow::~OverlayWindow() {
    Destroy();
}

bool OverlayWindow::Init(HINSTANCE hInstance) {
    if (hwnd)
        Destroy();

    hInst = hInstance;

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory);

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW), CS_HREDRAW | CS_VREDRAW, DefWindowProcW, 0, 0, hInstance, nullptr, nullptr, nullptr, nullptr, kOverlayClassName, nullptr};
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST, // NO WS_EX_LAYERED
      kOverlayClassName,
      nullptr,
      WS_POPUP,
      0,
      0,
      1,
      1,
      nullptr,
      nullptr,
      hInstance,
      nullptr);

    MARGINS margins = {-1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    if (!CreateRenderTarget(1, 1))
        return false;

    SetColor((D2D1_COLOR_F)65535);

    return hwnd && renderTarget && brush;
}

void OverlayWindow::Destroy() {
    SafeRelease(&brush);
    SafeRelease(&fadeBrush);
    SafeRelease(&renderTarget);
    SafeRelease(&d2dFactory);

    SafeRelease(&gradientBrushOuter);
    SafeRelease(&gradientBrushInner);
    SafeRelease(&gradientStopsOuter);
    SafeRelease(&gradientStopsInner);

    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }

    visible = false;
    lastWidth = 0;
    lastHeight = 0;
}

bool OverlayWindow::CreateRenderTarget(UINT width, UINT height) {
    if (!d2dFactory) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory)))
            return false;
    }

    D2D1_RENDER_TARGET_PROPERTIES props =
      D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 0.0f, 0.0f);

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_NONE);

    return SUCCEEDED(d2dFactory->CreateHwndRenderTarget(props, hwndProps, &renderTarget));
}

bool OverlayWindow::RecreateDeviceResources() {
    SafeRelease(&brush);
    SafeRelease(&fadeBrush);
    SafeRelease(&gradientBrushOuter);
    SafeRelease(&gradientBrushInner);
    SafeRelease(&gradientStopsOuter);
    SafeRelease(&gradientStopsInner);
    SafeRelease(&renderTarget);

    if (!CreateRenderTarget(std::max(lastWidth, 1), std::max(lastHeight, 1))) {
        Destroy();
        return Init(hInst);
    }

    if (gradient) {
        CreateGradientBrushes();
    } else {
        SetColor(solidColor);
    }

    return renderTarget != nullptr;
}

void OverlayWindow::Show() {
    if (!visible) {
        static float ang = gradientAngleDeg;
        if (gradient && ang != gradientAngleDeg) {
            ang = gradientAngleDeg;
            CreateGradientBrushes();
        }
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        visible = true;
    }
}

void OverlayWindow::Hide() {
    if (visible) {
        ShowWindow(hwnd, SW_HIDE);
        visible = false;
    }
}

void OverlayWindow::Move(int x, int y) {
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void OverlayWindow::Resize(int width, int height) {
    if (width == lastWidth && height == lastHeight)
        return;
    lastWidth = width;
    lastHeight = height;

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, width, height, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);

    if (!renderTarget)
        return;

    renderTarget->Resize(D2D1::SizeU(width, height));
    renderTarget->SetDpi(96.0f, 96.0f);

    if (gradient && !rotating) {
        CreateGradientBrushes();
    }

    thicknessOuter = std::floor(borderThickness / 2.0f);
    thicknessInner = borderThickness - thicknessOuter;

    outerRounded.rect =
      D2D1::RectF(thicknessOuter * 0.5f, thicknessOuter * 0.5f, static_cast<float>(width) - thicknessOuter * 0.5f, static_cast<float>(height) - thicknessOuter * 0.5f);

    // Inner stroke is centered inward from the outer stroke
    innerRounded.rect = D2D1::RectF(thicknessOuter + thicknessInner * 0.5f,
      thicknessOuter + thicknessInner * 0.5f,
      static_cast<float>(width) - thicknessOuter - thicknessInner * 0.5f,
      static_cast<float>(height) - thicknessOuter - thicknessInner * 0.5f);

    Render();
}

void OverlayWindow::SetColor(const D2D1_COLOR_F& color) {
    solidColor = color;
    gradient = false;
    SafeRelease(&brush);
    SafeRelease(&fadeBrush);

    renderTarget->CreateSolidColorBrush(color, &brush);
    renderTarget->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, 0.5f), &fadeBrush);
}

void OverlayWindow::SetGradient(const D2D1_COLOR_F& start, const D2D1_COLOR_F& end, float angleDeg, bool r, float rotatingSpeed) {
    gradient = true;
    gradientStart = start;
    gradientEnd = end;
    gradientAngleDeg = angleDeg;
    rotating = r;
    rotationSpeed = rotatingSpeed;
}

void OverlayWindow::CreateGradientBrushes() {
    SafeRelease(&gradientBrushOuter);
    SafeRelease(&gradientBrushInner);
    SafeRelease(&gradientStopsOuter);
    SafeRelease(&gradientStopsInner);

    if (!renderTarget)
        return;

    D2D1_GRADIENT_STOP stopsOuter[2] = {
      {0.0f, D2D1::ColorF(gradientStart.r, gradientStart.g, gradientStart.b, 0.5f)}, {1.0f, D2D1::ColorF(gradientEnd.r, gradientEnd.g, gradientEnd.b, 0.5f)}};

    D2D1_GRADIENT_STOP stopsInner[2] = {
      {0.0f, D2D1::ColorF(gradientStart.r, gradientStart.g, gradientStart.b, 1.0f)}, {1.0f, D2D1::ColorF(gradientEnd.r, gradientEnd.g, gradientEnd.b, 1.0f)}};

    if (FAILED(renderTarget->CreateGradientStopCollection(stopsOuter, 2, &gradientStopsOuter)))
        return;
    if (FAILED(renderTarget->CreateGradientStopCollection(stopsInner, 2, &gradientStopsInner)))
        return;

    const float angleRad = gradientAngleDeg * (3.14159265f / 180.0f);
    const float cx = lastWidth * 0.5f;
    const float cy = lastHeight * 0.5f;
    const float radius = std::hypot(cx, cy);
    const float dx = std::cos(angleRad) * radius;
    const float dy = std::sin(angleRad) * radius;

    D2D1_POINT_2F p1 = {cx - dx, cy - dy};
    D2D1_POINT_2F p2 = {cx + dx, cy + dy};
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {p1, p2};

    renderTarget->CreateLinearGradientBrush(props, gradientStopsOuter, &gradientBrushOuter);
    renderTarget->CreateLinearGradientBrush(props, gradientStopsInner, &gradientBrushInner);
}

void OverlayWindow::Render() {
    if (!renderTarget || !brush || !fadeBrush)
        return;

    static auto lastTime = std::chrono::steady_clock::now();
    if (gradient && rotating) {
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        gradientAngleDeg += rotationSpeed * deltaTime;
        if (gradientAngleDeg > 360.0f)
            gradientAngleDeg -= 360.0f;

        CreateGradientBrushes();
    }

    renderTarget->BeginDraw();
    renderTarget->Clear();

    if (gradient && gradientBrushOuter)
        renderTarget->DrawRoundedRectangle(outerRounded, gradientBrushOuter, thicknessOuter);
    else
        renderTarget->DrawRoundedRectangle(outerRounded, fadeBrush, thicknessOuter);

    if (gradient && gradientBrushInner)
        renderTarget->DrawRoundedRectangle(innerRounded, gradientBrushInner, thicknessInner);
    else
        renderTarget->DrawRoundedRectangle(innerRounded, brush, thicknessInner);

    HRESULT hr = renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        LOG_W("Overlay render target lost; recreating");
        if (!RecreateDeviceResources()) {
            LOG_E("Failed to recreate overlay render target after device loss");
        }
    } else if (FAILED(hr)) {
        LOG_E("Overlay EndDraw failed: 0x{:X}", hr);
    }
}

void OverlayWindow::PreRender(const std::function<bool()>& condition, const std::function<void()>& onFrame) {
    Show();

    const UINT dpi = GetDpiForWindow(hwnd);
    m_radius = default_radius * (dpi / 96.0f);
    outerRounded.radiusX = m_radius;
    outerRounded.radiusY = m_radius;
    float innerR = m_radius - thicknessInner;
    if (innerR < 0.0f)
        innerR = 0.0f;
    innerRounded.radiusX = innerR;
    innerRounded.radiusY = innerR;

    while (condition()) {
        if (onFrame)
            onFrame();
        Render();
    }

    Hide();
}