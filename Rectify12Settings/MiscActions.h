#pragma once

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Rectify12::MiscActions {
    struct Result {
        bool success = false;
        DWORD error = ERROR_SUCCESS;
        std::wstring message;
    };

    inline std::filesystem::path SystemExecutable(std::wstring_view relativePath) {
        std::vector<wchar_t> buffer(512, L'\0');
        for (;;) {
            const UINT written = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
            if (written == 0) return {};
            if (written < buffer.size()) {
                return std::filesystem::path(std::wstring(buffer.data(), written)) / relativePath;
            }
            if (buffer.size() >= 32768) return {};
            buffer.resize(static_cast<std::size_t>(written) + 1, L'\0');
        }
    }

    inline Result RunElevatedProcess(
        const std::filesystem::path& executable,
        std::wstring_view parameters,
        std::wstring_view successMessage,
        DWORD timeoutMilliseconds = 120000) {

        if (executable.empty() || !std::filesystem::exists(executable)) {
            return { false, ERROR_FILE_NOT_FOUND, L"The required Windows system tool could not be found." };
        }

        std::wstring parameterBuffer(parameters);
        SHELLEXECUTEINFOW info{ sizeof(info) };
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"runas";
        info.lpFile = executable.c_str();
        info.lpParameters = parameterBuffer.c_str();
        info.nShow = SW_HIDE;

        if (!ShellExecuteExW(&info)) {
            const DWORD error = GetLastError();
            return {
                false,
                error,
                error == ERROR_CANCELLED
                    ? L"Administrator approval was cancelled. No system setting was changed."
                    : L"Windows could not start the elevated system-settings action."
            };
        }

        if (!info.hProcess) {
            return { false, ERROR_INVALID_HANDLE, L"Windows started the action but did not return a process handle." };
        }

        const DWORD waitResult = WaitForSingleObject(info.hProcess, timeoutMilliseconds);
        if (waitResult == WAIT_TIMEOUT) {
            CloseHandle(info.hProcess);
            return { false, ERROR_TIMEOUT, L"The elevated system-settings action did not finish in time." };
        }
        if (waitResult != WAIT_OBJECT_0) {
            const DWORD error = GetLastError();
            CloseHandle(info.hProcess);
            return { false, error, L"Rectify12 could not wait for the elevated system-settings action." };
        }

        DWORD exitCode = ERROR_GEN_FAILURE;
        if (!GetExitCodeProcess(info.hProcess, &exitCode)) {
            const DWORD error = GetLastError();
            CloseHandle(info.hProcess);
            return { false, error, L"Rectify12 could not read the system-settings action result." };
        }
        CloseHandle(info.hProcess);

        if (exitCode != 0) {
            return { false, exitCode, L"Windows rejected the requested system setting. It may be managed by policy." };
        }

        return { true, ERROR_SUCCESS, std::wstring(successMessage) };
    }

    inline bool LongPathsEnabled() noexcept {
        DWORD value = 0;
        DWORD size = sizeof(value);
        return RegGetValueW(
                   HKEY_LOCAL_MACHINE,
                   L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                   L"LongPathsEnabled",
                   RRF_RT_REG_DWORD,
                   nullptr,
                   &value,
                   &size) == ERROR_SUCCESS && value == 1;
    }

    inline Result EnableLongPaths() {
        const auto regExe = SystemExecutable(L"reg.exe");
        return RunElevatedProcess(
            regExe,
            L"ADD \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem\" /v LongPathsEnabled /t REG_DWORD /d 1 /f",
            L"Win32 long-path support is enabled. Rectify12 executables are long-path-aware; a restart may be needed for already-running apps.");
    }

    inline Result OptimiseMicrosoftDefender() {
        const auto powershell = SystemExecutable(L"WindowsPowerShell\\v1.0\\powershell.exe");
        constexpr wchar_t command[] =
            L"-NoLogo -NoProfile -NonInteractive -Command \""
            L"$ErrorActionPreference='Stop'; "
            L"Set-MpPreference -ScanAvgCPULoadFactor 30 -EnableLowCpuPriority $true "
            L"-ScanOnlyIfIdleEnabled $true -DisableCpuThrottleOnIdleScans $false; "
            L"exit 0\"";

        return RunElevatedProcess(
            powershell,
            command,
            L"Microsoft Defender scheduled scans are tuned for lower foreground impact. Real-time protection and security scanning remain enabled.");
    }

    inline Result RestoreMicrosoftDefenderScanDefaults() {
        const auto powershell = SystemExecutable(L"WindowsPowerShell\\v1.0\\powershell.exe");
        constexpr wchar_t command[] =
            L"-NoLogo -NoProfile -NonInteractive -Command \""
            L"$ErrorActionPreference='Stop'; "
            L"Set-MpPreference -ScanAvgCPULoadFactor 50 -EnableLowCpuPriority $true "
            L"-ScanOnlyIfIdleEnabled $true -DisableCpuThrottleOnIdleScans $true; "
            L"exit 0\"";

        return RunElevatedProcess(
            powershell,
            command,
            L"The Defender scan settings changed by Rectify12 were returned to Microsoft's documented defaults.");
    }
}
