#pragma once

#include <windows.h>

#include <cwchar>
#include <vector>

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

    inline bool IsCurrentProcessExcluded() noexcept {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) {
            return false;
        }

        const wchar_t* processName = wcsrchr(path, L'\\');
        processName = processName ? processName + 1 : path;

        DWORD bytes = 0;
        if (RegGetValueW(
                HKEY_CURRENT_USER,
                L"Software\\Rectify12\\Compatibility",
                L"ExcludedExecutables",
                RRF_RT_REG_MULTI_SZ,
                nullptr,
                nullptr,
                &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
            return false;
        }

        std::vector<wchar_t> entries((bytes / sizeof(wchar_t)) + 2, L'\0');
        if (RegGetValueW(
                HKEY_CURRENT_USER,
                L"Software\\Rectify12\\Compatibility",
                L"ExcludedExecutables",
                RRF_RT_REG_MULTI_SZ,
                nullptr,
                entries.data(),
                &bytes) != ERROR_SUCCESS) {
            return false;
        }

        for (const wchar_t* entry = entries.data(); *entry; entry += wcslen(entry) + 1) {
            if (_wcsicmp(entry, processName) == 0) {
                return true;
            }
        }
        return false;
    }

    inline bool ShouldApply() noexcept {
        return IsEnabled() && !IsCurrentProcessExcluded();
    }
}
