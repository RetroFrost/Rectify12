#pragma once

#include <windows.h>

namespace Rectify12::RuntimePolicy {
    inline constexpr wchar_t Key[] = L"Software\\Rectify12\\Runtime";
    inline constexpr wchar_t EnabledValue[] = L"Enabled";

    inline bool IsEnabled() noexcept {
        DWORD value = 1;
        DWORD size = sizeof(value);
        if (RegGetValueW(
                HKEY_CURRENT_USER,
                Key,
                EnabledValue,
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &size) != ERROR_SUCCESS) {
            return true;
        }
        return value != 0;
    }

    inline bool SetEnabled(bool enabled) noexcept {
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(
                HKEY_CURRENT_USER,
                Key,
                0,
                nullptr,
                0,
                KEY_SET_VALUE,
                nullptr,
                &key,
                &disposition) != ERROR_SUCCESS) {
            return false;
        }

        const DWORD value = enabled ? 1u : 0u;
        const bool saved = RegSetValueExW(
            key,
            EnabledValue,
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value)) == ERROR_SUCCESS;
        RegCloseKey(key);

        if (saved) {
            SendMessageTimeoutW(
                HWND_BROADCAST,
                WM_SETTINGCHANGE,
                0,
                reinterpret_cast<LPARAM>(L"Rectify12Runtime"),
                SMTO_ABORTIFHUNG,
                1000,
                nullptr);
        }
        return saved;
    }
}
