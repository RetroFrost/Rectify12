#pragma once

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <string>
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

    inline std::wstring CurrentExecutableName() noexcept {
        // MAX_PATH is not a safe limit for modern long-path aware processes.
        // Grow the buffer until GetModuleFileNameW can return the complete path.
        std::vector<wchar_t> path(512, L'\0');
        for (;;) {
            SetLastError(ERROR_SUCCESS);
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0) {
                return {};
            }

            if (length < path.size() - 1) {
                path[length] = L'\0';
                break;
            }

            if (path.size() >= 32768) {
                return {};
            }
            path.resize(std::min<std::size_t>(path.size() * 2, 32768), L'\0');
        }

        const wchar_t* processName = wcsrchr(path.data(), L'\\');
        processName = processName ? processName + 1 : path.data();
        return processName;
    }

    inline bool IsCurrentProcessExcluded() noexcept {
        const std::wstring processName = CurrentExecutableName();
        if (processName.empty()) {
            return false;
        }

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
            if (_wcsicmp(entry, processName.c_str()) == 0) {
                return true;
            }
        }
        return false;
    }

    inline bool ShouldApply() noexcept {
        return IsEnabled() && !IsCurrentProcessExcluded();
    }
}
