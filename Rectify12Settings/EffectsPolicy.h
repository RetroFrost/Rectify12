#pragma once

#include <windows.h>

namespace Rectify12::EffectsPolicy {
    inline constexpr wchar_t Key[] = L"Software\\Rectify12\\Effects";

    enum class Backdrop : DWORD {
        Mica = 1,
        Acrylic = 2,
        MicaAlt = 3,
    };

    struct State {
        bool enabled = true;
        bool replaceGenericDark = true;
        bool patchExplorer = true;
        Backdrop backdrop = Backdrop::Acrylic;
    };

    inline DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) noexcept {
        DWORD value = fallback;
        DWORD size = sizeof(value);
        return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
            ? value
            : fallback;
    }

    inline State Load() noexcept {
        State state;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, Key, 0, KEY_READ, &key) != ERROR_SUCCESS) {
            return state;
        }

        state.enabled = ReadDword(key, L"Enabled", 1) != 0;
        state.replaceGenericDark = ReadDword(key, L"ReplaceGenericDark", 1) != 0;
        state.patchExplorer = ReadDword(key, L"PatchExplorer", 1) != 0;
        switch (ReadDword(key, L"Backdrop", static_cast<DWORD>(Backdrop::Acrylic))) {
        case static_cast<DWORD>(Backdrop::Mica): state.backdrop = Backdrop::Mica; break;
        case static_cast<DWORD>(Backdrop::MicaAlt): state.backdrop = Backdrop::MicaAlt; break;
        case static_cast<DWORD>(Backdrop::Acrylic):
        default: state.backdrop = Backdrop::Acrylic; break;
        }
        RegCloseKey(key);
        return state;
    }

    inline bool WriteDword(HKEY key, const wchar_t* name, DWORD value) noexcept {
        return RegSetValueExW(
                   key,
                   name,
                   0,
                   REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value),
                   sizeof(value)) == ERROR_SUCCESS;
    }

    inline bool Save(const State& state) noexcept {
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition) != ERROR_SUCCESS) {
            return false;
        }

        const bool saved =
            WriteDword(key, L"Enabled", state.enabled ? 1u : 0u) &&
            WriteDword(key, L"ReplaceGenericDark", state.replaceGenericDark ? 1u : 0u) &&
            WriteDword(key, L"PatchExplorer", state.patchExplorer ? 1u : 0u) &&
            WriteDword(key, L"Backdrop", static_cast<DWORD>(state.backdrop));
        RegCloseKey(key);

        if (saved) {
            SendMessageTimeoutW(
                HWND_BROADCAST,
                WM_SETTINGCHANGE,
                0,
                reinterpret_cast<LPARAM>(L"Rectify12Effects"),
                SMTO_ABORTIFHUNG,
                1000,
                nullptr);
        }
        return saved;
    }
}
