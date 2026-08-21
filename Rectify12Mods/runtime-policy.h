#pragma once

#include <windows.h>

namespace Rectify12Runtime {
    inline bool IsEnabled() noexcept {
        DWORD value = 1;
        DWORD size = sizeof(value);
        if (RegGetValueW(
                HKEY_CURRENT_USER,
                L"Software\\Rectify12\\Runtime",
                L"Enabled",
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &size) != ERROR_SUCCESS) {
            return true;
        }
        return value != 0;
    }
}
