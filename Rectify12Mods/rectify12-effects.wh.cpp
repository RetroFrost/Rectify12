// ==WindhawkMod==
// @id              rectify12-effects
// @name            Rectify12 Effects
// @description     Applies Rectify12 Acrylic/Mica/Mica Alt backdrops to supported Microsoft desktop windows.
// @version         0.1.0
// @author          RetroFrost
// @github          https://github.com/RetroFrost/Rectify12
// @include         explorer.exe
// @include         control.exe
// @include         Taskmgr.exe
// @include         mmc.exe
// @include         regedit.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Rectify12 Effects

Early Rectify12 effects host. Acrylic is the Rectify12 default backdrop. The
mod deliberately starts with a conservative set of Microsoft desktop processes
and top-level captioned windows. It does **not** make arbitrary third-party
windows transparent and it does not hook paint APIs in this first version.

Preferences are read from:
`HKCU\\Software\\Rectify12\\Effects`

Changing a preference currently requires reopening the affected application or
reloading the mod. Live reload will be added after the first PC compatibility
pass.
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
        bool enabled = true;
        bool replaceGenericDark = true;
        Backdrop backdrop = Backdrop::Acrylic;
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
            prefs.enabled = ReadDword(key, L"Enabled", 1) != 0;
            prefs.replaceGenericDark = ReadDword(key, L"ReplaceGenericDark", 1) != 0;

            switch (ReadDword(key, L"Backdrop", static_cast<DWORD>(Backdrop::Acrylic))) {
            case static_cast<DWORD>(Backdrop::Acrylic):
                prefs.backdrop = Backdrop::Acrylic;
                break;
            case static_cast<DWORD>(Backdrop::MicaAlt):
                prefs.backdrop = Backdrop::MicaAlt;
                break;
            case static_cast<DWORD>(Backdrop::Mica):
                prefs.backdrop = Backdrop::Mica;
                break;
            default:
                prefs.backdrop = Backdrop::Acrylic;
                break;
            }
            RegCloseKey(key);
        }

        g_preferences = prefs;
        Wh_Log(
            L"Preferences: enabled=%d replaceGenericDark=%d backdrop=%u",
            prefs.enabled,
            prefs.replaceGenericDark,
            static_cast<unsigned>(prefs.backdrop));
    }

    bool AppsUseDarkMode() {
        DWORD appsUseLightTheme = 1;
        DWORD size = sizeof(appsUseLightTheme);
        if (RegGetValueW(
                HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme",
                RRF_RT_REG_DWORD,
                nullptr,
                &appsUseLightTheme,
                &size) != ERROR_SUCCESS) {
            return false;
        }
        return appsUseLightTheme == 0;
    }

    DWM_SYSTEMBACKDROP_TYPE PreferredBackdrop() {
        switch (g_preferences.backdrop) {
        case Backdrop::Acrylic:
            return DWMSBT_TRANSIENTWINDOW;
        case Backdrop::MicaAlt:
            return DWMSBT_TABBEDWINDOW;
        case Backdrop::Mica:
            return DWMSBT_MAINWINDOW;
        default:
            return DWMSBT_TRANSIENTWINDOW;
        }
    }

    bool IsCandidateWindow(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return false;
        if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;

        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        if (style & WS_CHILD) return false;
        if (!(style & WS_CAPTION)) return false;
        if (exStyle & WS_EX_TOOLWINDOW) return false;
        if (exStyle & WS_EX_LAYERED) return false;
        if (exStyle & WS_EX_NOREDIRECTIONBITMAP) return false;

        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);
        return processId == GetCurrentProcessId();
    }

    void RememberOriginalState(HWND hwnd) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_originalStates.contains(hwnd)) return;

        OriginalWindowState state;
        DWORD size = sizeof(state.backdrop);
        state.hasBackdrop = SUCCEEDED(DwmGetWindowAttribute(
            hwnd,
            DWMWA_SYSTEMBACKDROP_TYPE,
            &state.backdrop,
            size));

        size = sizeof(state.darkMode);
        state.hasDarkMode = SUCCEEDED(DwmGetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &state.darkMode,
            size));

        g_originalStates.emplace(hwnd, state);
    }

    void ApplyEffects(HWND hwnd) {
        if (!g_preferences.enabled || !g_preferences.replaceGenericDark) return;
        if (!AppsUseDarkMode()) return;
        if (!IsCandidateWindow(hwnd)) return;

        RememberOriginalState(hwnd);

        const BOOL darkMode = TRUE;
        DwmSetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &darkMode,
            sizeof(darkMode));

        const DWM_SYSTEMBACKDROP_TYPE backdrop = PreferredBackdrop();
        const HRESULT hr = DwmSetWindowAttribute(
            hwnd,
            DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop,
            sizeof(backdrop));

        if (SUCCEEDED(hr)) {
            wchar_t className[128]{};
            GetClassNameW(hwnd, className, ARRAYSIZE(className));
            Wh_Log(L"Applied backdrop %d to hwnd=%p class=%s", static_cast<int>(backdrop), hwnd, className);
        }
    }

    void ForgetWindow(HWND hwnd) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_originalStates.erase(hwnd);
    }

    void RestoreAllWindows() {
        std::vector<std::pair<HWND, OriginalWindowState>> states;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            states.reserve(g_originalStates.size());
            for (const auto& pair : g_originalStates) {
                states.push_back(pair);
            }
            g_originalStates.clear();
        }

        for (const auto& [hwnd, state] : states) {
            if (!IsWindow(hwnd)) continue;

            if (state.hasBackdrop) {
                DwmSetWindowAttribute(
                    hwnd,
                    DWMWA_SYSTEMBACKDROP_TYPE,
                    &state.backdrop,
                    sizeof(state.backdrop));
            }
            else {
                const DWM_SYSTEMBACKDROP_TYPE automatic = DWMSBT_AUTO;
                DwmSetWindowAttribute(
                    hwnd,
                    DWMWA_SYSTEMBACKDROP_TYPE,
                    &automatic,
                    sizeof(automatic));
            }

            if (state.hasDarkMode) {
                DwmSetWindowAttribute(
                    hwnd,
                    DWMWA_USE_IMMERSIVE_DARK_MODE,
                    &state.darkMode,
                    sizeof(state.darkMode));
            }
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

        if (hwnd) {
            ApplyEffects(hwnd);
        }
        return hwnd;
    }

    using DestroyWindow_t = decltype(&DestroyWindow);
    DestroyWindow_t DestroyWindow_Original = nullptr;

    BOOL WINAPI DestroyWindow_Hook(HWND hwnd) {
        ForgetWindow(hwnd);
        return DestroyWindow_Original(hwnd);
    }

    BOOL CALLBACK ApplyExistingWindow(HWND hwnd, LPARAM) {
        ApplyEffects(hwnd);
        return TRUE;
    }
}

BOOL Wh_ModInit() {
    Wh_Log(L"Rectify12 Effects initializing");
    LoadPreferences();

    if (!Wh_SetFunctionHook(
            reinterpret_cast<void*>(CreateWindowExW),
            reinterpret_cast<void*>(CreateWindowExW_Hook),
            reinterpret_cast<void**>(&CreateWindowExW_Original))) {
        Wh_Log(L"Failed to hook CreateWindowExW");
        return FALSE;
    }

    if (!Wh_SetFunctionHook(
            reinterpret_cast<void*>(DestroyWindow),
            reinterpret_cast<void*>(DestroyWindow_Hook),
            reinterpret_cast<void**>(&DestroyWindow_Original))) {
        Wh_Log(L"Failed to hook DestroyWindow");
        return FALSE;
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    EnumWindows(ApplyExistingWindow, 0);
}

void Wh_ModBeforeUninit() {
    RestoreAllWindows();
}

void Wh_ModUninit() {
    Wh_Log(L"Rectify12 Effects unloaded");
}
