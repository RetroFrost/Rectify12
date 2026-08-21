#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>

#include "Settings.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")

namespace {
    constexpr int ID_EFFECTS_ENABLED = 1001;
    constexpr int ID_REPLACE_GENERIC_DARK = 1002;
    constexpr int ID_BACKDROP = 1003;
    constexpr int ID_APPLY = 1004;
    constexpr int ID_DEFAULTS = 1005;

    HWND gEffectsEnabled = nullptr;
    HWND gReplaceGenericDark = nullptr;
    HWND gBackdrop = nullptr;
    HFONT gUiFont = nullptr;

    bool IsDarkModeEnabled() {
        DWORD value = 0;
        DWORD size = sizeof(value);
        return RegGetValueW(
                   HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme",
                   RRF_RT_REG_DWORD,
                   nullptr,
                   &value,
                   &size) == ERROR_SUCCESS && value == 0;
    }

    void ApplyBackdrop(HWND hwnd) {
        const BOOL dark = IsDarkModeEnabled() ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        const MARGINS margins{ -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    }

    void ApplyControlTheme(HWND hwnd) {
        if (!hwnd) return;
        SetWindowTheme(hwnd, IsDarkModeEnabled() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        if (gUiFont) {
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
        }
    }

    void SetCheck(HWND hwnd, bool checked) {
        SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool GetCheck(HWND hwnd) {
        return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void LoadPreferencesIntoUi() {
        using namespace Rectify12::Settings;
        const EffectsPreferences prefs = LoadEffectsPreferences();

        SetCheck(gEffectsEnabled, prefs.enabled);
        SetCheck(gReplaceGenericDark, prefs.replaceGenericDark);

        int selection = 0;
        if (prefs.backdrop == Backdrop::Acrylic) selection = 1;
        else if (prefs.backdrop == Backdrop::MicaAlt) selection = 2;
        SendMessageW(gBackdrop, CB_SETCURSEL, selection, 0);
    }

    Rectify12::Settings::EffectsPreferences ReadPreferencesFromUi() {
        using namespace Rectify12::Settings;
        EffectsPreferences prefs;
        prefs.enabled = GetCheck(gEffectsEnabled);
        prefs.replaceGenericDark = GetCheck(gReplaceGenericDark);

        const int selection = static_cast<int>(SendMessageW(gBackdrop, CB_GETCURSEL, 0, 0));
        if (selection == 1) prefs.backdrop = Backdrop::Acrylic;
        else if (selection == 2) prefs.backdrop = Backdrop::MicaAlt;
        else prefs.backdrop = Backdrop::Mica;

        return prefs;
    }

    void ShowSaveResult(HWND hwnd, bool success) {
        MessageBoxW(
            hwnd,
            success
                ? L"Rectify12 effects preferences were saved. Components that support live reload will update automatically."
                : L"Rectify12 could not save the effects preferences.",
            L"Rectify12 Control Panel",
            MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR));
    }

    HWND AddStatic(HWND parent, const wchar_t* text, int x, int y, int width, int height, DWORD style = SS_LEFT) {
        HWND hwnd = CreateWindowExW(
            0,
            L"STATIC",
            text,
            WS_CHILD | WS_VISIBLE | style,
            x,
            y,
            width,
            height,
            parent,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        ApplyControlTheme(hwnd);
        return hwnd;
    }

    HWND AddCheckBox(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height) {
        HWND hwnd = CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            x,
            y,
            width,
            height,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),
            nullptr);
        ApplyControlTheme(hwnd);
        return hwnd;
    }

    HWND AddButton(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height) {
        HWND hwnd = CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            x,
            y,
            width,
            height,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),
            nullptr);
        ApplyControlTheme(hwnd);
        return hwnd;
    }

    void CreateControls(HWND hwnd) {
        NONCLIENTMETRICSW metrics{ sizeof(metrics) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            gUiFont = CreateFontIndirectW(&metrics.lfMessageFont);
        }

        HWND title = AddStatic(hwnd, L"Rectify12", 32, 28, 560, 44, SS_LEFT);
        if (gUiFont) {
            LOGFONTW titleFont{};
            GetObjectW(gUiFont, sizeof(titleFont), &titleFont);
            titleFont.lfHeight = -28;
            titleFont.lfWeight = FW_SEMIBOLD;
            HFONT largeFont = CreateFontIndirectW(&titleFont);
            if (largeFont) SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(largeFont), TRUE);
        }

        AddStatic(
            hwnd,
            L"Effects control how supported Microsoft-owned dark surfaces are rendered. The global effects host is being integrated separately; these preferences are the stable contract it will consume.",
            34,
            80,
            570,
            58);

        AddStatic(hwnd, L"Effects", 34, 156, 500, 30);
        gEffectsEnabled = AddCheckBox(hwnd, ID_EFFECTS_ENABLED, L"Enable Rectify12 effects", 38, 194, 300, 28);
        gReplaceGenericDark = AddCheckBox(hwnd, ID_REPLACE_GENERIC_DARK, L"Replace generic dark surfaces where supported", 38, 230, 390, 28);

        AddStatic(hwnd, L"Preferred backdrop", 38, 277, 190, 24);
        gBackdrop = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            38,
            304,
            260,
            180,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BACKDROP)),
            GetModuleHandleW(nullptr),
            nullptr);
        ApplyControlTheme(gBackdrop);
        SendMessageW(gBackdrop, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mica"));
        SendMessageW(gBackdrop, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Acrylic"));
        SendMessageW(gBackdrop, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mica Alt"));

        AddButton(hwnd, ID_DEFAULTS, L"Restore defaults", 332, 365, 128, 34);
        AddButton(hwnd, ID_APPLY, L"Apply", 474, 365, 128, 34);

        LoadPreferencesIntoUi();
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            ApplyBackdrop(hwnd);
            CreateControls(hwnd);
            return 0;

        case WM_SETTINGCHANGE:
            ApplyBackdrop(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case ID_APPLY:
                ShowSaveResult(hwnd, Rectify12::Settings::SaveEffectsPreferences(ReadPreferencesFromUi()));
                return 0;
            case ID_DEFAULTS: {
                Rectify12::Settings::EffectsPreferences defaults;
                const bool saved = Rectify12::Settings::SaveEffectsPreferences(defaults);
                LoadPreferencesIntoUi();
                ShowSaveResult(hwnd, saved);
                return 0;
            }
            default:
                break;
            }
            break;

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, IsDarkModeEnabled() ? RGB(242, 242, 242) : RGB(24, 24, 24));
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }

        case WM_ERASEBKGND:
            // DWM owns the client-area backdrop.
            return 1;

        case WM_DESTROY:
            if (gUiFont) {
                DeleteObject(gUiFont);
                gUiFont = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);

    constexpr wchar_t WindowClass[] = L"Rectify12ControlPanelWindow";

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WindowClass;

    if (!RegisterClassExW(&wc)) {
        return static_cast<int>(GetLastError());
    }

    HWND hwnd = CreateWindowExW(
        0,
        WindowClass,
        L"Rectify12 Control Panel",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        660,
        470,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        return static_cast<int>(GetLastError());
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
