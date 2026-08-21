#pragma once

#include <windows.h>
#include <string>

namespace Rectify12::Settings {
    inline constexpr wchar_t EffectsKey[] = L"Software\\Rectify12\\Effects";
    inline constexpr wchar_t ValueEnabled[] = L"Enabled";
    inline constexpr wchar_t ValueReplaceGenericDark[] = L"ReplaceGenericDark";
    inline constexpr wchar_t ValueBackdrop[] = L"Backdrop";

    enum class Backdrop : DWORD {
        Mica = 1,
        Acrylic = 2,
        MicaAlt = 3
    };

    struct EffectsPreferences {
        bool enabled = true;
        bool replaceGenericDark = true;
        Backdrop backdrop = Backdrop::Mica;
    };

    inline DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) {
        DWORD value = fallback;
        DWORD size = sizeof(value);
        if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
            return fallback;
        }
        return value;
    }

    inline EffectsPreferences LoadEffectsPreferences() {
        EffectsPreferences prefs;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, EffectsKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
            return prefs;
        }

        prefs.enabled = ReadDword(key, ValueEnabled, 1) != 0;
        prefs.replaceGenericDark = ReadDword(key, ValueReplaceGenericDark, 1) != 0;

        const DWORD backdrop = ReadDword(key, ValueBackdrop, static_cast<DWORD>(Backdrop::Mica));
        switch (backdrop) {
        case static_cast<DWORD>(Backdrop::Acrylic):
            prefs.backdrop = Backdrop::Acrylic;
            break;
        case static_cast<DWORD>(Backdrop::MicaAlt):
            prefs.backdrop = Backdrop::MicaAlt;
            break;
        case static_cast<DWORD>(Backdrop::Mica):
        default:
            prefs.backdrop = Backdrop::Mica;
            break;
        }

        RegCloseKey(key);
        return prefs;
    }

    inline bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
        return RegSetValueExW(
            key,
            name,
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value)) == ERROR_SUCCESS;
    }

    inline bool SaveEffectsPreferences(const EffectsPreferences& prefs) {
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(
                HKEY_CURRENT_USER,
                EffectsKey,
                0,
                nullptr,
                0,
                KEY_SET_VALUE,
                nullptr,
                &key,
                &disposition) != ERROR_SUCCESS) {
            return false;
        }

        const bool success =
            WriteDword(key, ValueEnabled, prefs.enabled ? 1 : 0) &&
            WriteDword(key, ValueReplaceGenericDark, prefs.replaceGenericDark ? 1 : 0) &&
            WriteDword(key, ValueBackdrop, static_cast<DWORD>(prefs.backdrop));

        RegCloseKey(key);

        if (success) {
            // Future Rectify12 effects hosts can listen for this and reload without a reboot.
            SendMessageTimeoutW(
                HWND_BROADCAST,
                WM_SETTINGCHANGE,
                0,
                reinterpret_cast<LPARAM>(L"Rectify12Effects"),
                SMTO_ABORTIFHUNG,
                1000,
                nullptr);
        }

        return success;
    }
}
