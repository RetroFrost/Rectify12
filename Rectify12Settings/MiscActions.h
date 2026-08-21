#pragma once

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
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
            if (written >= 32768 || buffer.size() >= 32768) return {};
            buffer.resize(std::min<std::size_t>(static_cast<std::size_t>(written) + 1, 32768), L'\0');
        }
    }

    inline Result RunElevatedProcess(
        const std::filesystem::path& executable,
        std::wstring_view parameters,
        std::wstring_view successMessage,
        DWORD timeoutMilliseconds = 120000) {

        std::error_code existsError;
        const bool executableExists = !executable.empty() && std::filesystem::exists(executable, existsError);
        if (!executableExists || existsError) {
            return {
                false,
                existsError ? static_cast<DWORD>(existsError.value()) : ERROR_FILE_NOT_FOUND,
                L"The required Windows system tool could not be found."
            };
        }

        std::wstring parameterBuffer(parameters);
        SHELLEXECUTEINFOW info{ sizeof(info) };
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
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
            return {
                false,
                ERROR_TIMEOUT,
                L"The elevated action is taking unusually long. It may still be running; do not start the same action again until it has finished."
            };
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
        if (LongPathsEnabled()) {
            return { true, ERROR_SUCCESS, L"Win32 long-path support is already enabled." };
        }

        const auto regExe = SystemExecutable(L"reg.exe");
        auto result = RunElevatedProcess(
            regExe,
            L"ADD \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem\" /v LongPathsEnabled /t REG_DWORD /d 1 /f",
            L"Win32 long-path support is enabled. A restart may be needed for already-running apps.");

        if (result.success && !LongPathsEnabled()) {
            return { false, ERROR_WRITE_FAULT, L"Windows reported success, but the long-path policy did not become enabled." };
        }
        return result;
    }

    inline bool DefenderBackupExists() noexcept {
        constexpr wchar_t keyPath[] = L"SOFTWARE\\Rectify12\\DefenderBackup";
        const wchar_t* requiredValues[] = {
            L"Version",
            L"ScanAvgCPULoadFactor",
            L"EnableLowCpuPriority",
            L"ScanOnlyIfIdleEnabled",
            L"DisableCpuThrottleOnIdleScans"
        };

        for (const auto* name : requiredValues) {
            DWORD value = 0;
            DWORD size = sizeof(value);
            if (RegGetValueW(
                    HKEY_LOCAL_MACHINE,
                    keyPath,
                    name,
                    RRF_RT_REG_DWORD,
                    nullptr,
                    &value,
                    &size) != ERROR_SUCCESS) {
                return false;
            }
            if (wcscmp(name, L"Version") == 0 && value != 1) return false;
        }
        return true;
    }

    inline Result OptimiseMicrosoftDefender() {
        const auto powershell = SystemExecutable(L"WindowsPowerShell\\v1.0\\powershell.exe");
        constexpr wchar_t command[] =
            L"-NoLogo -NoProfile -NonInteractive -Command \""
            L"$ErrorActionPreference='Stop'; "
            L"$k='HKLM:\\SOFTWARE\\Rectify12\\DefenderBackup'; "
            L"$required=@('Version','ScanAvgCPULoadFactor','EnableLowCpuPriority','ScanOnlyIfIdleEnabled','DisableCpuThrottleOnIdleScans'); "
            L"$validBackup=(Test-Path -LiteralPath $k); "
            L"if($validBackup){ $existing=Get-ItemProperty -LiteralPath $k; foreach($n in $required){ if($existing.PSObject.Properties.Name -notcontains $n){ $validBackup=$false; break } }; if($validBackup -and $existing.Version -ne 1){ $validBackup=$false } }; "
            L"if(-not $validBackup){ "
                L"if(Test-Path -LiteralPath $k){ Remove-Item -LiteralPath $k -Recurse -Force }; "
                L"$p=Get-MpPreference; "
                L"if($null -eq $p.ScanAvgCPULoadFactor){ throw 'Defender scan preferences are unavailable.' }; "
                L"New-Item -Path $k -Force | Out-Null; "
                L"New-ItemProperty -Path $k -Name ScanAvgCPULoadFactor -PropertyType DWord -Value ([int]$p.ScanAvgCPULoadFactor) -Force | Out-Null; "
                L"New-ItemProperty -Path $k -Name EnableLowCpuPriority -PropertyType DWord -Value ([int][bool]$p.EnableLowCpuPriority) -Force | Out-Null; "
                L"New-ItemProperty -Path $k -Name ScanOnlyIfIdleEnabled -PropertyType DWord -Value ([int][bool]$p.ScanOnlyIfIdleEnabled) -Force | Out-Null; "
                L"New-ItemProperty -Path $k -Name DisableCpuThrottleOnIdleScans -PropertyType DWord -Value ([int][bool]$p.DisableCpuThrottleOnIdleScans) -Force | Out-Null; "
                L"New-ItemProperty -Path $k -Name Version -PropertyType DWord -Value 1 -Force | Out-Null; "
            L"}; "
            L"Set-MpPreference -ScanAvgCPULoadFactor 30 -EnableLowCpuPriority $true "
                L"-ScanOnlyIfIdleEnabled $true -DisableCpuThrottleOnIdleScans $false; "
            L"$after=Get-MpPreference; "
            L"if($after.ScanAvgCPULoadFactor -ne 30 -or -not $after.EnableLowCpuPriority -or "
                L"-not $after.ScanOnlyIfIdleEnabled -or $after.DisableCpuThrottleOnIdleScans){ "
                L"throw 'Defender did not accept all Rectify12 scan settings.' "
            L"}; "
            L"exit 0\"";

        return RunElevatedProcess(
            powershell,
            command,
            L"Microsoft Defender scheduled scans are tuned for lower foreground impact. The previous scan settings were saved for exact restoration; real-time protection remains enabled.");
    }

    inline Result RestoreMicrosoftDefenderScanSettings() {
        if (!DefenderBackupExists()) {
            return { false, ERROR_NOT_FOUND, L"No complete Defender settings backup created by Rectify12 was found." };
        }

        const auto powershell = SystemExecutable(L"WindowsPowerShell\\v1.0\\powershell.exe");
        constexpr wchar_t command[] =
            L"-NoLogo -NoProfile -NonInteractive -Command \""
            L"$ErrorActionPreference='Stop'; "
            L"$k='HKLM:\\SOFTWARE\\Rectify12\\DefenderBackup'; "
            L"if(-not (Test-Path -LiteralPath $k)){ throw 'No Rectify12 Defender backup exists.' }; "
            L"$b=Get-ItemProperty -LiteralPath $k; "
            L"Set-MpPreference "
                L"-ScanAvgCPULoadFactor ([byte]$b.ScanAvgCPULoadFactor) "
                L"-EnableLowCpuPriority ([bool]([int]$b.EnableLowCpuPriority)) "
                L"-ScanOnlyIfIdleEnabled ([bool]([int]$b.ScanOnlyIfIdleEnabled)) "
                L"-DisableCpuThrottleOnIdleScans ([bool]([int]$b.DisableCpuThrottleOnIdleScans)); "
            L"$after=Get-MpPreference; "
            L"if($after.ScanAvgCPULoadFactor -ne [byte]$b.ScanAvgCPULoadFactor -or "
                L"$after.EnableLowCpuPriority -ne [bool]([int]$b.EnableLowCpuPriority) -or "
                L"$after.ScanOnlyIfIdleEnabled -ne [bool]([int]$b.ScanOnlyIfIdleEnabled) -or "
                L"$after.DisableCpuThrottleOnIdleScans -ne [bool]([int]$b.DisableCpuThrottleOnIdleScans)){ "
                L"throw 'Defender did not restore all saved settings.' "
            L"}; "
            L"Remove-Item -LiteralPath $k -Recurse -Force; "
            L"exit 0\"";

        return RunElevatedProcess(
            powershell,
            command,
            L"Microsoft Defender scan settings were restored to the exact values saved before Rectify12 optimisation.");
    }
}
