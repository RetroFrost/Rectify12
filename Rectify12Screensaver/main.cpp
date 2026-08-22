#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>

namespace {
constexpr wchar_t kClassName[] = L"Rectify12ScreensaverWindow";
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameIntervalMs = 33;

struct State {
    bool preview = false;
    POINT initialCursor{};
    bool cursorCaptured = false;
    double phase = 0.0;
};

COLORREF Blend(COLORREF a, COLORREF b, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    const auto channel = [amount](BYTE x, BYTE y) {
        return static_cast<BYTE>(x + (y - x) * amount);
    };
    return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)), channel(GetBValue(a), GetBValue(b)));
}

void FillGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom) {
    const int height = std::max(1L, rect.bottom - rect.top);
    for (int y = 0; y < height; ++y) {
        const double t = static_cast<double>(y) / height;
        HBRUSH brush = CreateSolidBrush(Blend(top, bottom, t));
        RECT line{rect.left, rect.top + y, rect.right, rect.top + y + 1};
        FillRect(dc, &line, brush);
        DeleteObject(brush);
    }
}

void DrawWave(HDC dc, const RECT& rect, double phase, COLORREF color, int offset, int amplitude, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    bool started = false;
    for (int x = -40; x <= w + 40; x += 5) {
        const double nx = static_cast<double>(x) / std::max(1, w);
        const int y = rect.top + h / 2 + offset + static_cast<int>(std::sin(nx * 7.0 + phase) * amplitude + std::sin(nx * 2.5 - phase * .6) * amplitude * .45);
        if (!started) {
            MoveToEx(dc, rect.left + x, y, nullptr);
            started = true;
        } else {
            LineTo(dc, rect.left + x, y);
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawCenteredText(HDC dc, const RECT& rect, const wchar_t* text, int pointSize, int weight, COLORREF color, int yOffset) {
    const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    HFONT font = CreateFontW(-MulDiv(pointSize, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, L"Segoe UI Variable Display");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT target = rect;
    target.top += yOffset;
    DrawTextW(dc, text, -1, &target, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void Render(HWND window, State& state) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = std::max(1L, client.right);
    const int height = std::max(1L, client.bottom);

    PAINTSTRUCT paint{};
    HDC target = BeginPaint(window, &paint);
    HDC buffer = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);

    FillGradient(buffer, client, RGB(3, 12, 34), RGB(6, 5, 25));
    DrawWave(buffer, client, state.phase, RGB(35, 155, 255), height / 8, std::max(20, height / 7), std::max(2, height / 110));
    DrawWave(buffer, client, state.phase + 1.7, RGB(114, 70, 235), -height / 12, std::max(18, height / 8), std::max(2, height / 125));
    DrawWave(buffer, client, state.phase + 3.1, RGB(42, 224, 235), height / 5, std::max(15, height / 10), std::max(1, height / 170));

    const int markSize = state.preview ? std::max(34, height / 4) : std::max(90, height / 5);
    DrawCenteredText(buffer, client, L"12", markSize, FW_SEMIBOLD, RGB(210, 240, 255), height / 3 - markSize / 2);

    if (!state.preview) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t clock[16]{};
        wchar_t date[80]{};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, clock, ARRAYSIZE(clock));
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &time, nullptr, date, ARRAYSIZE(date), nullptr);
        DrawCenteredText(buffer, client, clock, 34, FW_LIGHT, RGB(240, 245, 255), height - 125);
        DrawCenteredText(buffer, client, date, 11, FW_NORMAL, RGB(175, 193, 218), height - 70);
    }

    BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window, &paint);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE:
        if (state && !state->preview) {
            GetCursorPos(&state->initialCursor);
            state->cursorCaptured = true;
            ShowCursor(FALSE);
        }
        SetTimer(window, kFrameTimer, kFrameIntervalMs, nullptr);
        return 0;
    case WM_TIMER:
        if (state) state->phase += 0.035;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        if (state) Render(window, *state);
        return 0;
    case WM_MOUSEMOVE:
        if (state && !state->preview && state->cursorCaptured) {
            POINT current{};
            GetCursorPos(&current);
            if (std::abs(current.x - state->initialCursor.x) > 3 || std::abs(current.y - state->initialCursor.y) > 3) DestroyWindow(window);
        }
        return 0;
    case WM_KEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (state && !state->preview) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kFrameTimer);
        if (state && !state->preview) ShowCursor(TRUE);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

HWND ParsePreviewParent(const std::wstring& commandLine) {
    const auto space = commandLine.find_first_of(L" \t");
    if (space == std::wstring::npos) return nullptr;
    const wchar_t* raw = commandLine.c_str() + space;
    while (*raw && std::iswspace(*raw)) ++raw;
    return reinterpret_cast<HWND>(static_cast<ULONG_PTR>(_wcstoui64(raw, nullptr, 10)));
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLineRaw, int) {
    std::wstring commandLine = commandLineRaw ? commandLineRaw : L"";
    std::transform(commandLine.begin(), commandLine.end(), commandLine.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    if (commandLine.starts_with(L"/c") || commandLine.starts_with(L"-c")) {
        MessageBoxW(nullptr, L"Rectify12 Frost currently uses the automatic aurora, clock, and date layout.", L"Rectify12 Screensaver", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    HWND parent = nullptr;
    State state;
    if (commandLine.starts_with(L"/p") || commandLine.starts_with(L"-p")) {
        parent = ParsePreviewParent(commandLine);
        state.preview = parent != nullptr;
        if (!state.preview) return ERROR_INVALID_PARAMETER;
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    if (!RegisterClassExW(&windowClass)) return GetLastError();

    RECT bounds{};
    DWORD style = WS_POPUP;
    if (state.preview) {
        GetClientRect(parent, &bounds);
        style = WS_CHILD | WS_VISIBLE;
    } else {
        bounds = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    }

    HWND window = CreateWindowExW(state.preview ? 0 : WS_EX_TOPMOST, kClassName, L"Rectify12 Frost",
        style | WS_VISIBLE, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, instance, &state);
    if (!window) return GetLastError();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

