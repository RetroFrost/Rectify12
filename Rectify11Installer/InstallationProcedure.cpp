#include "InstallationProcedure.h"
#include "Navigation.h"
#include "ProductInfo.h"

#include <filesystem>
#include <string>
#include <vector>

wchar_t path[MAX_PATH];
wchar_t cmd[1024];

std::wstring copy_list[] = {
L"%r11files%\\Mods|%ProgramData%\\Windhawk\\Engine\\Mods|NONE",
L"%r11files%\\Rectify11|%systemroot%\\Rectify12|INSTALLICONS",
L"%r11files%\\System32|%systemroot%\\System32|NONE",
L"%r11files%\\themes|%systemroot%\\resources\\themes|INSTALLTHEMES",
L"%r11files%\\wallpapers|%systemroot%\\web\\wallpaper\\rectified|INSTALLTHEMES",
L"%r11files%\\cursors|%systemroot%\\cursors|INSTALLTHEMES",
L"%r11files%\\media|%systemroot%\\media\\rectified|INSTALLTHEMES",
};

std::wstring install_list[] = {
L"msiexec.exe /i \"%r11files%\\SecureUxTheme_x64.msi\" /quiet /norestart|INSTALLTHEMES|AMD64",
L"msiexec.exe /i \"%r11files%\\SecureUxTheme_ARM64.msi\" /quiet /norestart|INSTALLTHEMES|ARM64",
L"%r11files%\\windhawk_setup_offline.exe /S|NONE",
L"%r11files%\\SymChk\\symchk.exe \"%systemroot%\\Explorer.exe\" /s SRV*%programdata%\\Windhawk\\Engine\\symbols\\*http://msdl.microsoft.com/download/symbols|NONE",
L"%r11files%\\SymChk\\symchk.exe \"%systemroot%\\system32\\Shlwapi.dll\" /s SRV*%programdata%\\Windhawk\\Engine\\symbols\\*http://msdl.microsoft.com/download/symbols|NONE"
};

std::wstring mod_list[] = {
L"%r11files%\\Regs\\resourcepatch.reg|INSTALLICONS|AMD64",
L"%r11files%\\Regs\\resourcepatchARM.reg|INSTALLICONS|ARM64",
L"%r11files%\\Regs\\soundWH.reg|INSTALLTHEMES|AMD64",
L"%r11files%\\Regs\\soundWHARM.reg|INSTALLTHEMES|ARM64",
L"%r11files%\\Regs\\winvershutdown.reg|INSTALLWINVERSHUTDOWN|AMD64",
L"%r11files%\\Regs\\winvershutdownARM.reg|INSTALLWINVERSHUTDOWN|ARM64",
L"%r11files%\\Regs\\titlebarfix.reg|INSTALLTHEMES|AMD64",
L"%r11files%\\Regs\\titlebarfixARM.reg|INSTALLTHEMES|ARM64",
L"%r11files%\\Regs\\topbar.reg|INSTALLEXP|AMD64",
L"%r11files%\\Regs\\topbarARM.reg|INSTALLEXP|ARM64",
L"%r11files%\\Regs\\Light.reg|LIGHTTHEME",
L"%r11files%\\Regs\\Dark.reg|DARKTHEME",
L"%r11files%\\Regs\\sound.reg|INSTALLTHEMES",
L"%r11files%\\Regs\\fonts.reg|NONE",
L"%r11files%\\Regs\\ASDF.reg|INSTALLASDF"
};

namespace {
    bool IsSuccessfulProcessExit(DWORD exitCode) {
        return exitCode == ERROR_SUCCESS ||
            exitCode == ERROR_SUCCESS_REBOOT_REQUIRED ||
            exitCode == ERROR_SUCCESS_REBOOT_INITIATED;
    }

    bool ExpandInstallPath(std::wstring& value) {
        constexpr wchar_t marker[] = L"%r11files%";
        std::size_t position = 0;
        while ((position = value.find(marker, position)) != std::wstring::npos) {
            value.replace(position, std::size(marker) - 1, r11dir);
            position += wcslen(r11dir);
        }

        const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (required == 0) {
            InstallationLogger.WriteLine(L"Failed to expand environment variables. Win32 error: " + std::to_wstring(GetLastError()));
            return false;
        }

        std::vector<wchar_t> expanded(required, L'\0');
        const DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
        if (written == 0 || written > expanded.size()) {
            InstallationLogger.WriteLine(L"Expanded path did not fit the allocated buffer. Win32 error: " + std::to_wstring(GetLastError()));
            return false;
        }

        value.assign(expanded.data());
        return true;
    }

    bool ConditionsAllow(const std::vector<std::wstring>& fields, std::size_t firstCondition, bool& valid) {
        valid = true;
        for (std::size_t i = firstCondition; i < fields.size(); ++i) {
            const auto condition = InstallFlags.find(fields[i]);
            if (condition == InstallFlags.end()) {
                InstallationLogger.WriteLine(L"Unknown installer condition: " + fields[i]);
                valid = false;
                return false;
            }
            if (!condition->second) return false;
        }
        return true;
    }

    bool SetRegistryString(HKEY key, const wchar_t* name, const wchar_t* value) {
        const DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
        return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes) == ERROR_SUCCESS;
    }

    bool SetRegistryDword(HKEY key, const wchar_t* name, DWORD value) {
        return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
    }

    bool CopyRequiredInstallerFile(const wchar_t* filename) {
        wchar_t source[MAX_PATH]{};
        wchar_t destination[MAX_PATH]{};
        if (FAILED(StringCchPrintfW(source, ARRAYSIZE(source), L"%s\\%s", currdir, filename)) ||
            FAILED(StringCchPrintfW(destination, ARRAYSIZE(destination), L"%s\\%s", r11targetdir, filename))) {
            InstallationLogger.WriteLine(L"A required installer path exceeded the current installer buffer.");
            return false;
        }

        if (!CopyFileW(source, destination, FALSE)) {
            InstallationLogger.WriteLine(
                L"Failed to copy required installer file " + std::wstring(filename) +
                L". Win32 error: " + std::to_wstring(GetLastError()));
            return false;
        }
        return true;
    }
}

ProcessResult RunEXE(const wchar_t* exe, wchar_t* args, DWORD timeoutMilliseconds) {
    ProcessResult result;
    STARTUPINFOW startupInfo{};
    PROCESS_INFORMATION processInfo{};
    startupInfo.cb = sizeof(startupInfo);

    if (exe) InstallationLogger.WriteLine(L"Path: " + std::wstring(exe));
    if (args) InstallationLogger.WriteLine(L"Arguments: " + std::wstring(args));

    if (!CreateProcessW(exe, args, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
        result.error = GetLastError();
        InstallationLogger.WriteLine(L"Failed to create process. Win32 error: " + std::to_wstring(result.error));
        return result;
    }

    InstallationLogger.WriteLine(L"Created process successfully.");
    CloseHandle(processInfo.hThread);

    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMilliseconds);
    if (waitResult == WAIT_TIMEOUT) {
        InstallationLogger.WriteLine(L"Process timed out; terminating the child process.");
        TerminateProcess(processInfo.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(processInfo.hProcess, 5000);
        CloseHandle(processInfo.hProcess);
        result.error = ERROR_TIMEOUT;
        return result;
    }
    if (waitResult == WAIT_FAILED) {
        result.error = GetLastError();
        InstallationLogger.WriteLine(L"Failed while waiting for child process. Win32 error: " + std::to_wstring(result.error));
        CloseHandle(processInfo.hProcess);
        return result;
    }

    if (!GetExitCodeProcess(processInfo.hProcess, &result.exitCode)) {
        result.error = GetLastError();
        InstallationLogger.WriteLine(L"Could not read child-process exit code. Win32 error: " + std::to_wstring(result.error));
        CloseHandle(processInfo.hProcess);
        return result;
    }
    CloseHandle(processInfo.hProcess);

    result.success = IsSuccessfulProcessExit(result.exitCode);
    result.error = result.success ? ERROR_SUCCESS : result.exitCode;
    InstallationLogger.WriteLine(L"Process exited with code " + std::to_wstring(result.exitCode));
    return result;
}

bool extractFiles() {
    if (FAILED(StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"\"%s\\7z.exe\" x -aoa -o\"%s\" \"%s\\Files.7z\" -y", r11dir, r11dir, r11dir))) {
        InstallationLogger.WriteLine(L"Extraction command exceeded the command buffer.");
        return false;
    }
    InstallationLogger.WriteLine(L"Extracting files...");
    return RunEXE(nullptr, cmd).success;
}

bool MoveFileCmd(const wchar_t* src, const wchar_t* dest) {
    if (FAILED(StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"/c xcopy \"%s\" \"%s\" /e /i /y", src, dest)) ||
        FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\System32\\cmd.exe", windir))) {
        InstallationLogger.WriteLine(L"Copy command exceeded the installer buffer.");
        return false;
    }
    return RunEXE(path, cmd).success;
}

bool RegisterRegFile(const wchar_t* regpath) {
    if (FAILED(StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"/c reg import \"%s\"", regpath)) ||
        FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\System32\\cmd.exe", windir))) {
        InstallationLogger.WriteLine(L"Registry-import command exceeded the installer buffer.");
        return false;
    }
    return RunEXE(path, cmd).success;
}

std::vector<std::wstring> ParseDelimiterString(std::wstring ws) {
    std::vector<std::wstring> fields;
    std::size_t delimiter = 0;
    while ((delimiter = ws.find(L'|')) != std::wstring::npos) {
        fields.push_back(ws.substr(0, delimiter));
        ws.erase(0, delimiter + 1);
    }
    fields.push_back(std::move(ws));
    return fields;
}

bool MoveFilesToTarget() {
    InstallationLogger.WriteLine(L"Copying files...");
    for (const auto& entry : copy_list) {
        std::vector<std::wstring> fields = ParseDelimiterString(entry);
        if (fields.size() < 3) {
            InstallationLogger.WriteLine(L"Invalid copy-list entry: " + entry);
            return false;
        }

        bool validConditions = true;
        const bool shouldRun = ConditionsAllow(fields, 2, validConditions);
        if (!validConditions) return false;
        if (!shouldRun) continue;

        if (!ExpandInstallPath(fields[0]) || !ExpandInstallPath(fields[1])) return false;
        InstallationLogger.WriteLine(L"Copying files \"" + fields[0] + L"\" to \"" + fields[1] + L"\"");
        if (!MoveFileCmd(fields[0].c_str(), fields[1].c_str())) return false;
    }
    return true;
}

bool InstallPrograms() {
    InstallationLogger.WriteLine(L"Installing programs...");
    for (const auto& entry : install_list) {
        std::vector<std::wstring> fields = ParseDelimiterString(entry);
        if (fields.size() < 2) {
            InstallationLogger.WriteLine(L"Invalid program-list entry: " + entry);
            return false;
        }

        bool validConditions = true;
        const bool shouldRun = ConditionsAllow(fields, 1, validConditions);
        if (!validConditions) return false;
        if (!shouldRun) continue;

        if (!ExpandInstallPath(fields[0])) return false;
        if (FAILED(StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"/c \"%s\"", fields[0].c_str())) ||
            FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\System32\\cmd.exe", windir))) {
            InstallationLogger.WriteLine(L"Program-install command exceeded the installer buffer.");
            return false;
        }
        if (!RunEXE(path, cmd).success) return false;
    }
    return true;
}

bool InstallFonts() {
    if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\Fonts\\*", r11dir))) {
        InstallationLogger.WriteLine(L"Font search path exceeded the installer buffer.");
        return false;
    }

    WIN32_FIND_DATAW findData{};
    InstallationLogger.WriteLine(L"Installing fonts...");
    HANDLE findHandle = FindFirstFileW(path, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            InstallationLogger.WriteLine(L"No payload fonts were found.");
            return true;
        }
        InstallationLogger.WriteLine(L"Could not enumerate payload fonts. Win32 error: " + std::to_wstring(error));
        return false;
    }

    bool success = true;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;

        if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\Fonts\\%s", r11dir, findData.cFileName)) ||
            FAILED(StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"%s\\Fonts\\%s", windir, findData.cFileName))) {
            InstallationLogger.WriteLine(L"Font path exceeded the installer buffer.");
            success = false;
            break;
        }

        if (!CopyFileW(path, cmd, FALSE)) {
            InstallationLogger.WriteLine(L"Could not copy font. Win32 error: " + std::to_wstring(GetLastError()));
            success = false;
            break;
        }
        if (AddFontResourceW(cmd) == 0) {
            InstallationLogger.WriteLine(L"Windows could not load installed font: " + std::wstring(findData.cFileName));
            success = false;
            break;
        }
    } while (FindNextFileW(findHandle, &findData));

    const DWORD enumerationError = GetLastError();
    FindClose(findHandle);
    if (success && enumerationError != ERROR_NO_MORE_FILES) {
        InstallationLogger.WriteLine(L"Font enumeration ended unexpectedly. Win32 error: " + std::to_wstring(enumerationError));
        return false;
    }
    return success;
}

bool RegisterWHMods() {
    InstallationLogger.WriteLine(L"Registering Windhawk modules and registry files...");
    for (const auto& entry : mod_list) {
        std::vector<std::wstring> fields = ParseDelimiterString(entry);
        if (fields.size() < 2) {
            InstallationLogger.WriteLine(L"Invalid registry-list entry: " + entry);
            return false;
        }

        bool validConditions = true;
        const bool shouldRun = ConditionsAllow(fields, 1, validConditions);
        if (!validConditions) return false;
        if (!shouldRun) continue;

        if (!ExpandInstallPath(fields[0])) return false;
        if (!RegisterRegFile(fields[0].c_str())) return false;
    }
    return true;
}

bool FinaliseInstall() {
    InstallationLogger.WriteLine(L"Finalising Rectify12 installation...");

    std::error_code directoryError;
    std::filesystem::create_directories(std::filesystem::path(r11targetdir), directoryError);
    if (directoryError && !std::filesystem::exists(std::filesystem::path(r11targetdir))) {
        InstallationLogger.WriteLine(L"Could not create Rectify12 install directory: " + std::to_wstring(directoryError.value()));
        return false;
    }

    const wchar_t* requiredFiles[] = {
        L"Base.dll",
        L"Controls.dll",
        L"PageRes.dll",
        Rectify12::InstalledExecutable,
        L"Segoe_r11.ttf"
    };
    for (const auto* filename : requiredFiles) {
        if (!CopyRequiredInstallerFile(filename)) return false;
    }

    wchar_t installLocation[MAX_PATH]{};
    wchar_t installedExe[MAX_PATH]{};
    if (FAILED(StringCchPrintfW(installLocation, ARRAYSIZE(installLocation), L"%s\\%s", windir, Rectify12::InstallFolder)) ||
        FAILED(StringCchPrintfW(installedExe, ARRAYSIZE(installedExe), L"%s\\%s", installLocation, Rectify12::InstalledExecutable))) {
        InstallationLogger.WriteLine(L"Install registration path exceeded the current installer buffer.");
        return false;
    }

    HKEY uninstallKey = nullptr;
    DWORD disposition = 0;
    const LSTATUS uninstallCreate = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        Rectify12::UninstallRegistryPath,
        0,
        nullptr,
        0,
        KEY_SET_VALUE,
        nullptr,
        &uninstallKey,
        &disposition);
    if (uninstallCreate != ERROR_SUCCESS) {
        InstallationLogger.WriteLine(L"Failed to create Rectify12 uninstall registration. Win32 error: " + std::to_wstring(uninstallCreate));
        return false;
    }

    const std::wstring quotedUninstall = L"\"" + std::wstring(installedExe) + L"\"";
    const bool uninstallValuesWritten =
        SetRegistryString(uninstallKey, L"DisplayIcon", installedExe) &&
        SetRegistryString(uninstallKey, L"DisplayName", Rectify12::ProductName) &&
        SetRegistryString(uninstallKey, L"DisplayVersion", Rectify12::ProductVersion) &&
        SetRegistryString(uninstallKey, L"InstallLocation", installLocation) &&
        SetRegistryString(uninstallKey, L"Publisher", Rectify12::Publisher) &&
        SetRegistryString(uninstallKey, L"UninstallString", quotedUninstall.c_str()) &&
        SetRegistryDword(uninstallKey, L"NoModify", 1) &&
        SetRegistryDword(uninstallKey, L"NoRepair", 1);
    RegCloseKey(uninstallKey);

    if (!uninstallValuesWritten) {
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, Rectify12::UninstallRegistryPath);
        InstallationLogger.WriteLine(L"Failed to write one or more Rectify12 uninstall values.");
        return false;
    }

    HKEY productKey = nullptr;
    const LSTATUS productCreate = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"Software\\Rectify12",
        0,
        nullptr,
        0,
        KEY_SET_VALUE,
        nullptr,
        &productKey,
        &disposition);
    if (productCreate != ERROR_SUCCESS) {
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, Rectify12::UninstallRegistryPath);
        InstallationLogger.WriteLine(L"Failed to create Rectify12 product registration. Win32 error: " + std::to_wstring(productCreate));
        return false;
    }

    const bool productValuesWritten =
        SetRegistryString(productKey, L"Version", Rectify12::ProductVersion) &&
        SetRegistryString(productKey, L"InstallLocation", installLocation);
    RegCloseKey(productKey);

    if (!productValuesWritten) {
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, Rectify12::UninstallRegistryPath);
        InstallationLogger.WriteLine(L"Failed to write Rectify12 product registration.");
        return false;
    }

    return true;
}

void SetupComplete() {
    DWORD lpidProcess[1024] = {};
    DWORD cbNeeded = 0;
    HANDLE hExplorer = NULL;
    EnumProcesses(lpidProcess, sizeof(lpidProcess), &cbNeeded);

    wchar_t buffer[MAX_PATH]{};
    for (DWORD i = 0; i < cbNeeded / sizeof(DWORD); i++) {
        hExplorer = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, lpidProcess[i]);
        if (!hExplorer) continue;

        DWORD bufferSize = ARRAYSIZE(buffer);
        if (QueryFullProcessImageNameW(hExplorer, 0, buffer, &bufferSize)) {
            const wchar_t* executableName = wcsrchr(buffer, L'\\');
            executableName = executableName ? executableName + 1 : buffer;
            if (_wcsicmp(executableName, L"explorer.exe") == 0) break;
        }

        CloseHandle(hExplorer);
        hExplorer = NULL;
    }
    if (hExplorer) {
        TerminateProcess(hExplorer, 1);
        CloseHandle(hExplorer);
    }

    wchar_t cmd2[] = L"/c del /q %localappdata%\\microsoft\\windows\\explorer\\*.db";
    if (SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\System32\\cmd.exe", windir))) {
        RunEXE(path, cmd2, 60000);
    }

    HANDLE hToken = NULL;
    TOKEN_PRIVILEGES tkp{};

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) {
            tkp.PrivilegeCount = 1;
            tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, 0);
        }
        CloseHandle(hToken);
    }

    InstallationLogger.WriteLine(L"Setup complete, requesting restart...");

    // Do not force-close user applications. If an app blocks shutdown, Windows can ask the user what to do.
    ExitWindowsEx(
        EWX_REBOOT,
        SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED);
}
