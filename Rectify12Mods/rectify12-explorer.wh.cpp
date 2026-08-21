// ==WindhawkMod==
// @id              rectify12-explorer
// @name            Rectify12 Explorer Patch
// @description     Applies Rectify12 backdrop and dark-surface fixes specifically to File Explorer.
// @version         0.1.0
// @author          RetroFrost
// @github          https://github.com/RetroFrost/Rectify12
// @include         explorer.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Rectify12 Explorer Patch

Explorer-specific companion to Rectify12 Effects. The inherited
`windows-11-file-explorer-styler` mod is retained for its MicaBar/XAML styling;
this module owns the outer Explorer window/frame backdrop and dark-surface
policy so it can be disabled or rolled back independently.

Preferences are read from `HKCU\\Software\\Rectify12\\Effects`.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <dwmapi.h>

#include <mutex>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace {
    constexpr wchar_t kEffectsKey[] = L"Software\\Rectify12\\Effects";

    enum class Backdrop : DWORD {
        Mica = 1,
        Acrylic = 2,
        MicaAlt = 3,
    };

    struct Preferences {
        bool effectsEnabled = true;
        bool replaceGenericDark = true;
        bool patchExplorer = true;
        Backdrop backdrop = Backdrop::Mica;
    };

    struct OriginalWindowState {
        bool hasBackdrop = false;
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_AUTO;
        bool hasDarkMode = false;
        BOOL darkMode = FALSE;
    };

    Preferences g_preferences;
    std::mutex g_stateMutex;
    std::unordered_map<HWND, OriginalWindowState> g_originalStates;

    DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) {
        DWORD value = fallback;
        DWORD size = sizeof(value);
        if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
            return fallback;
        }
        return value;
    }

    void LoadPreferences() {
        Preferences prefs;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kEffectsKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
            prefs.effectsEnabled = ReadDword(key, L"Enabled", 1) != 0;
            prefs.replaceGenericDark = ReadDword(key, L"ReplaceGenericDark", 1) != 0;
            prefs.patchExplorer = ReadDword(key, L"PatchExplorer", 1) != 0;

            switch (ReadDword(key, L"Backdrop", static_cast<DWORD>(Backdrop::Mica))) {
            case static_cast<DWORD>(Backdrop::Acrylic):
                prefs.backdrop = Backdrop::Acrylic;
                break;
            case static_cast<DWORD>(Backdrop::MicaAlt):
                prefs.backdrop = Backdrop::MicaAlt;
                break;
            default:
                prefs.backdrop = Backdrop::Mica;
                break;
            }
            RegCloseKey(key);
        }
        g_preferences = prefs;
    }

    bool AppsUseDarkMode() {
        DWORD value = 1;
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

    DWM_SYSTEMBACKDROP_TYPE PreferredBackdrop() {
        switch (g_preferences.backdrop) {
        case Backdrop::Acrylic:
            return DWMSBT_TRANSIENTWINDOW;
        case Backdrop::MicaAlt:
            return DWMSBT_TABBEDWINDOW;
        case Backdrop::Mica:
        default:
            return DWMSBT_MAINWINDOW;
        }
    }

    bool IsExplorerFrame(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return false;
        if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;

        wchar_t className[128]{};
        if (!GetClassNameW(hwnd, className, ARRAYSIZE(className))) return false;

        return wcscmp(className, L"CabinetWClass") == 0 ||
               wcscmp(className, L"ExploreWClass") == 0;
    }

    void RememberOriginalState(HWND hwnd) {
        std::lock_guard lock(g_stateMutex);
        if (g_originalStates.contains(hwnd)) return;

        OriginalWindowState state;
        state.hasBackdrop = SUCCEEDED(DwmGetWindowAttribute(
            hwnd,
            DWMWA_SYSTEMBACKDROP_TYPE,
            &state.backdrop,
            sizeof(state.backdrop)));
        state.hasDarkMode = SUCCEEDED(DwmGetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &state.darkMode,
            sizeof(state.darkMode)));

        g_originalStates.emplace(hwnd, state);
    }

    void ApplyExplorerPatch(HWND hwnd) {
        if (!g_preferences.effectsEnabled || !g_preferences.patchExplorer) return;
        if (!IsExplorerFrame(hwnd)) return;

        RememberOriginalState(hwnd);

        const BOOL dark = AppsUseDarkMode() ? TRUE : FALSE;
        DwmSetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark,
            sizeof(dark));

        if (dark && g_preferences.replaceGenericDark) {
            const DWM_SYSTEMBACKDROP_TYPE backdrop = PreferredBackdrop();
            DwmSetWindowAttribute(
                hwnd,
                DWMWA_SYSTEMBACKDROP_TYPE,
                &backdrop,
                sizeof(backdrop));
        }

        // Keep Explorer's native control theming while allowing Windows' dark
        // Explorer theme to render compatible legacy controls correctly.
        SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);

        RedrawWindow(
            hwnd,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }

    void ForgetWindow(HWND hwnd) {
        std::lock_guard lock(g_stateMutex);
        g_originalStates.erase(hwnd);
    }

    void RestoreAllWindows() {
        std::vector<std::pair<HWND, OriginalWindowState>> states;
        {
            std::lock_guard lock(g_stateMutex);
            states.reserve(g_originalStates.size());
            for (const auto& state : g_originalStates) states.push_back(state);
            g_originalStates.clear();
        }

        for (const auto& [hwnd, state] : states) {
            if (!IsWindow(hwnd)) continue;

            const DWM_SYSTEMBACKDROP_TYPE backdrop = state.hasBackdrop
                ? state.backdrop
                : DWMSBT_AUTO;
            DwmSetWindowAttribute(
                hwnd,
                DWMWA_SYSTEMBACKDROP_TYPE,
                &backdrop,
                sizeof(backdrop));

            if (state.hasDarkMode) {
                DwmSetWindowAttribute(
                    hwnd,
                    DWMWA_USE_IMMERSIVE_DARK_MODE,
                    &state.darkMode,
                    sizeof(state.darkMode));
            }

            SetWindowTheme(hwnd, L"Explorer", nullptr);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        }
    }

    using CreateWindowExW_t = decltype(&CreateWindowExW);
    CreateWindowExW_t CreateWindowExW_Original = nullptr;

    HWND WINAPI CreateWindowExW_Hook(
        DWORD exStyle,
        LPCWSTR className,
        LPCWSTR windowName,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        HWND parent,
        HMENU menu,
        HINSTANCE instance,
        LPVOID parameter) {
        HWND hwnd = CreateWindowExW_Original(
            exStyle,
            className,
            windowName,
            style,
            x,
            y,
            width,
            height,
            parent,
            menu,
            instance,
            parameter);

        if (hwnd) ApplyExplorerPatch(hwnd);
        return hwnd;
    }

    using DestroyWindow_t = decltype(&DestroyWindow);
    DestroyWindow_t DestroyWindow_Original = nullptr;

    BOOL WINAPI DestroyWindow_Hook(HWND hwnd) {
        ForgetWindow(hwnd);
        return DestroyWindow_Original(hwnd);
    }

    BOOL CALLBACK ApplyExistingExplorerWindow(HWND hwnd, LPARAM) {
        ApplyExplorerPatch(hwnd);
        return TRUE;
    }
}

BOOL Wh_ModInit() {
    LoadPreferences();

    if (!Wh_SetFunctionHook(
            reinterpret_cast<void*>(CreateWindowExW),
            reinterpret_cast<void*>(CreateWindowExW_Hook),
            reinterpret_cast<void**>(&CreateWindowExW_Original))) {
        return FALSE;
    }

    if (!Wh_SetFunctionHook(
            reinterpret_cast<void*>(DestroyWindow),
            reinterpret_cast<void*>(DestroyWindow_Hook),
            reinterpret_cast<void**>(&DestroyWindow_Original))) {
        return FALSE;
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    EnumWindows(ApplyExistingExplorerWindow, 0);
}

void Wh_ModBeforeUninit() {
    RestoreAllWindows();
}

void Wh_ModUninit() {
    Wh_Log(L"Rectify12 Explorer Patch unloaded");
}
