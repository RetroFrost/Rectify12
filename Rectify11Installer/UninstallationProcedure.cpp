#include "UninstallationProcedure.h"
#include "InstallationProcedure.h"
#include "resource.h"
#include "framework.h"
#include "Navigation.h"
#include "ProductInfo.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {
    constexpr wchar_t DefenderBackupKey[] = L"SOFTWARE\\Rectify12\\DefenderBackup";

    bool DeleteTreeIfPresent(HKEY root, const wchar_t* subkey, const wchar_t* description) {
        const LSTATUS result = RegDeleteTreeW(root, subkey);
        if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
            return true;
        }

        InstallationLogger.WriteLine(
            L"Could not remove " + std::wstring(description) + L". Win32 error: " + std::to_wstring(result));
        return false;
    }

    bool DeleteValueIfPresent(HKEY root, const wchar_t* subkey, const wchar_t* valueName, const wchar_t* description) {
        const LSTATUS result = RegDeleteKeyValueW(root, subkey, valueName);
        if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
            return true;
        }

        InstallationLogger.WriteLine(
            L"Could not remove " + std::wstring(description) + L". Win32 error: " + std::to_wstring(result));
        return false;
    }

    bool ReadBackupDword(HKEY key, const wchar_t* name, DWORD& value) {
        DWORD size = sizeof(value);
        return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
    }

    const wchar_t* PowerShellBool(DWORD value) {
        return value ? L"$true" : L"$false";
    }

    bool SetRunOnceTheme(const wchar_t* themeFile) {
        if (!themeFile || !*themeFile) return false;

        wchar_t themePath[MAX_PATH]{};
        if (FAILED(StringCchPrintfW(themePath, ARRAYSIZE(themePath), L"%s\\resources\\themes\\%s", windir, themeFile))) {
            InstallationLogger.WriteLine(L"Theme restore path exceeded the current installer buffer.");
            return false;
        }

        const std::wstring command = L"explorer.exe \"" + std::wstring(themePath) + L"\"";
        HKEY runOnce = nullptr;
        DWORD disposition = 0;
        const LSTATUS createResult = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &runOnce,
            &disposition);
        if (createResult != ERROR_SUCCESS) {
            InstallationLogger.WriteLine(L"Could not open RunOnce for theme restore. Win32 error: " + std::to_wstring(createResult));
            return false;
        }

        const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
        const LSTATUS writeResult = RegSetValueExW(
            runOnce,
            L"Rectify12ApplyTheme",
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            bytes);
        RegCloseKey(runOnce);

        if (writeResult != ERROR_SUCCESS) {
            InstallationLogger.WriteLine(L"Could not schedule Windows theme restore. Win32 error: " + std::to_wstring(writeResult));
            return false;
        }
        return true;
    }

    bool ScheduleInstallDirectoryRemoval() {
        const std::filesystem::path root(r11targetdir);
        std::error_code existsError;
        if (!std::filesystem::exists(root, existsError)) {
            if (existsError) {
                InstallationLogger.WriteLine(L"Could not inspect the Rectify12 install directory. Error: " + std::to_wstring(existsError.value()));
                return false;
            }
            return true;
        }

        std::vector<std::filesystem::path> entries;
        std::error_code iterateError;
        std::filesystem::recursive_directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            iterateError);
        const std::filesystem::recursive_directory_iterator end;

        while (!iterateError && iterator != end) {
            entries.push_back(iterator->path());
            iterator.increment(iterateError);
        }
        if (iterateError) {
            InstallationLogger.WriteLine(L"Could not enumerate Rectify12 files for reboot cleanup. Error: " + std::to_wstring(iterateError.value()));
            return false;
        }

        std::reverse(entries.begin(), entries.end());
        for (const auto& entry : entries) {
            if (!MoveFileExW(entry.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
                const DWORD error = GetLastError();
                if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
                    InstallationLogger.WriteLine(
                        L"Could not schedule deletion of " + entry.wstring() + L". Win32 error: " + std::to_wstring(error));
                    return false;
                }
            }
        }

        if (!MoveFileExW(root.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
                InstallationLogger.WriteLine(L"Could not schedule removal of the Rectify12 install directory. Win32 error: " + std::to_wstring(error));
                return false;
            }
        }
        return true;
    }
}

bool RestoreDefenderSettingsIfNeeded() {
    HKEY backupKey = nullptr;
    const LSTATUS openResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, DefenderBackupKey, 0, KEY_READ, &backupKey);
    if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    if (openResult != ERROR_SUCCESS) {
        InstallationLogger.WriteLine(L"Could not open the Rectify12 Defender rollback snapshot. Win32 error: " + std::to_wstring(openResult));
        return false;
    }

    DWORD version = 0;
    DWORD cpu = 0;
    DWORD lowPriority = 0;
    DWORD idleOnly = 0;
    DWORD disableIdleThrottle = 0;
    const bool complete =
        ReadBackupDword(backupKey, L"Version", version) && version == 1 &&
        ReadBackupDword(backupKey, L"ScanAvgCPULoadFactor", cpu) &&
        ReadBackupDword(backupKey, L"EnableLowCpuPriority", lowPriority) &&
        ReadBackupDword(backupKey, L"ScanOnlyIfIdleEnabled", idleOnly) &&
        ReadBackupDword(backupKey, L"DisableCpuThrottleOnIdleScans", disableIdleThrottle);
    RegCloseKey(backupKey);

    if (!complete || lowPriority > 1 || idleOnly > 1 || disableIdleThrottle > 1 || cpu > 100) {
        InstallationLogger.WriteLine(L"The Rectify12 Defender rollback snapshot is incomplete or invalid; refusing to erase it during uninstall.");
        return false;
    }

    wchar_t powershell[MAX_PATH]{};
    if (FAILED(StringCchPrintfW(
            powershell,
            ARRAYSIZE(powershell),
            L"%s\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
            windir))) {
        InstallationLogger.WriteLine(L"PowerShell path exceeded the current installer buffer.");
        return false;
    }

    std::wstring command =
        L"-NoLogo -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; "
        L"Set-MpPreference -ScanAvgCPULoadFactor " + std::to_wstring(cpu) +
        L" -EnableLowCpuPriority " + PowerShellBool(lowPriority) +
        L" -ScanOnlyIfIdleEnabled " + PowerShellBool(idleOnly) +
        L" -DisableCpuThrottleOnIdleScans " + PowerShellBool(disableIdleThrottle) +
        L"; $p=Get-MpPreference; if(([int]$p.ScanAvgCPULoadFactor -ne " + std::to_wstring(cpu) +
        L") -or ([bool]$p.EnableLowCpuPriority -ne " + PowerShellBool(lowPriority) +
        L") -or ([bool]$p.ScanOnlyIfIdleEnabled -ne " + PowerShellBool(idleOnly) +
        L") -or ([bool]$p.DisableCpuThrottleOnIdleScans -ne " + PowerShellBool(disableIdleThrottle) +
        L")){ throw 'Defender rollback verification failed.' }; exit 0\"";

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const ProcessResult result = RunEXE(powershell, mutableCommand.data(), 120000);
    if (!result.success) {
        InstallationLogger.WriteLine(L"Could not restore the Defender settings saved by Rectify12. The rollback snapshot will be kept.");
        return false;
    }

    InstallationLogger.WriteLine(L"Restored the exact Microsoft Defender scan settings saved before Rectify12 optimisation.");
    return true;
}

bool RemoveWHMods() {
    bool success = true;

    if (InstallFlags[L"INSTALLTHEMES"]) {
        InstallationLogger.WriteLine(L"Uninstalling sound hook...");
        success = DeleteTreeIfPresent(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Windhawk\\Engine\\Mods\\logon-logoff-shutdown-sounds",
            L"Rectify12 sound hook") && success;

        InstallationLogger.WriteLine(L"Uninstalling titlebar fix...");
        success = DeleteTreeIfPresent(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Windhawk\\Engine\\Mods\\local@titlebar-fix",
            L"Rectify12 titlebar fix") && success;
    }

    if (InstallFlags[L"INSTALLICONS"]) {
        InstallationLogger.WriteLine(L"Uninstalling resource redirect...");
        success = DeleteTreeIfPresent(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Windhawk\\Engine\\Mods\\icon-resource-redirect",
            L"Rectify12 icon resource redirect") && success;
    }

    if (InstallFlags[L"INSTALLASDF"]) {
        InstallationLogger.WriteLine(L"Uninstalling Accent Colorizer startup entry...");
        success = DeleteValueIfPresent(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"ASDF",
            L"Rectify12 Accent Colorizer startup entry") && success;
    }

    if (InstallFlags[L"INSTALLWINVERSHUTDOWN"]) {
        InstallationLogger.WriteLine(L"Uninstalling winver and shutdown enhancements...");
        success = DeleteTreeIfPresent(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Windhawk\\Engine\\Mods\\winvershutdown",
            L"Rectify12 winver/shutdown module") && success;
    }

    if (InstallFlags[L"INSTALLEXP"]) {
        InstallationLogger.WriteLine(L"Uninstalling Explorer tweaks...");
        success = DeleteTreeIfPresent(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Windhawk\\Engine\\Mods\\windows-11-file-explorer-styler",
            L"Rectify12 Explorer styler") && success;
    }

    return success;
}

void RemoveSecureUX() {
    if (InstallFlags[L"INSTALLTHEMES"]) {
        // SecureUxTheme can be shared with other custom themes, so Rectify12 must not
        // remove it blindly. A future ownership record will allow safe removal when
        // Rectify12 was the component that installed it.
        InstallationLogger.WriteLine(L"Leaving SecureUxTheme installed to avoid removing a shared dependency.");
    }
}

bool FinaliseUninstall() {
    if (InstallFlags[L"INSTALLTHEMES"]) {
        InstallationLogger.WriteLine(L"Restoring a Windows theme on next sign-in...");
        if (InstallFlags[L"LIGHTTHEME"] && !SetRunOnceTheme(L"aero.theme")) return false;
        if (InstallFlags[L"DARKTHEME"] && !SetRunOnceTheme(L"dark.theme")) return false;
    }

    if (!ScheduleInstallDirectoryRemoval()) {
        return false;
    }

    const LSTATUS uninstallDelete = RegDeleteTreeW(HKEY_LOCAL_MACHINE, Rectify12::UninstallRegistryPath);
    if (uninstallDelete != ERROR_SUCCESS && uninstallDelete != ERROR_FILE_NOT_FOUND && uninstallDelete != ERROR_PATH_NOT_FOUND) {
        InstallationLogger.WriteLine(L"Could not remove Rectify12 uninstall registration. Win32 error: " + std::to_wstring(uninstallDelete));
        return false;
    }

    const LSTATUS productDelete = RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Rectify12");
    if (productDelete != ERROR_SUCCESS && productDelete != ERROR_FILE_NOT_FOUND && productDelete != ERROR_PATH_NOT_FOUND) {
        InstallationLogger.WriteLine(L"Could not remove Rectify12 product registration. Win32 error: " + std::to_wstring(productDelete));
        return false;
    }

    // Per-user visual preferences are intentionally retained. This means a reinstall
    // restores the user's Rectify12 effects choices instead of silently resetting them.
    InstallationLogger.WriteLine(L"Rectify12 product registration removed; user visual preferences retained. Installed Rectify12 files are scheduled for deletion on reboot.");
    return true;
}
