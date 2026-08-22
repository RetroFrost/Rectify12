#pragma once

#include "InstallationProcedure.h"
#include "Navigation.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Rectify12::Cursors {
    inline constexpr wchar_t BackupKey[] = L"Software\\Rectify12\\CursorBackup";
    inline constexpr wchar_t ExpectedArchiveHash[] = L"04C9A4797F02AB88FD5DF15A9377A32B3F66497F05CAF89460F3441968A7024C";

    struct CursorValue {
        const wchar_t* registryName;
        const wchar_t* backupName;
    };

    inline constexpr CursorValue CursorValues[] = {
        { nullptr, L"Default" },
        { L"AppStarting", L"AppStarting" },
        { L"Arrow", L"Arrow" },
        { L"Crosshair", L"Crosshair" },
        { L"Hand", L"Hand" },
        { L"Help", L"Help" },
        { L"IBeam", L"IBeam" },
        { L"No", L"No" },
        { L"NWPen", L"NWPen" },
        { L"SizeAll", L"SizeAll" },
        { L"SizeNESW", L"SizeNESW" },
        { L"SizeNS", L"SizeNS" },
        { L"SizeNWSE", L"SizeNWSE" },
        { L"SizeWE", L"SizeWE" },
        { L"UpArrow", L"UpArrow" },
        { L"Wait", L"Wait" },
        { L"Person", L"Person" },
        { L"Pin", L"Pin" },
    };

    inline bool WriteDword(HKEY key, const std::wstring& name, DWORD value) {
        return RegSetValueExW(
            key,
            name.c_str(),
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value)) == ERROR_SUCCESS;
    }

    inline bool WriteString(HKEY key, const std::wstring& name, const std::wstring& value) {
        const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        return RegSetValueExW(
            key,
            name.c_str(),
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            bytes) == ERROR_SUCCESS;
    }

    inline bool ReadDword(HKEY key, const std::wstring& name, DWORD& value) {
        DWORD bytes = sizeof(value);
        return RegGetValueW(key, nullptr, name.c_str(), RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS;
    }

    inline bool ReadString(HKEY key, const wchar_t* valueName, std::wstring& value) {
        DWORD bytes = 0;
        const LSTATUS sizeResult = RegGetValueW(
            key,
            nullptr,
            valueName,
            RRF_RT_REG_SZ,
            nullptr,
            nullptr,
            &bytes);
        if (sizeResult != ERROR_SUCCESS || bytes < sizeof(wchar_t)) return false;

        std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
        const LSTATUS readResult = RegGetValueW(
            key,
            nullptr,
            valueName,
            RRF_RT_REG_SZ,
            nullptr,
            buffer.data(),
            &bytes);
        if (readResult != ERROR_SUCCESS) return false;
        if (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
        value = std::move(buffer);
        return true;
    }

    inline bool BackupCurrentScheme() {
        HKEY existingBackup = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, BackupKey, 0, KEY_READ, &existingBackup) == ERROR_SUCCESS) {
            DWORD complete = 0;
            DWORD bytes = sizeof(complete);
            const bool alreadyComplete =
                RegGetValueW(existingBackup, nullptr, L"Complete", RRF_RT_REG_DWORD, nullptr, &complete, &bytes) == ERROR_SUCCESS &&
                complete == 1;
            RegCloseKey(existingBackup);
            if (alreadyComplete) return true;
        }

        HKEY cursorKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", 0, KEY_READ, &cursorKey) != ERROR_SUCCESS) {
            InstallationLogger.WriteLine(L"Could not open the current user's cursor settings for backup.");
            return false;
        }

        HKEY backupKey = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(
                HKEY_CURRENT_USER,
                BackupKey,
                0,
                nullptr,
                0,
                KEY_SET_VALUE,
                nullptr,
                &backupKey,
                &disposition) != ERROR_SUCCESS) {
            RegCloseKey(cursorKey);
            InstallationLogger.WriteLine(L"Could not create the Rectify12 cursor rollback snapshot.");
            return false;
        }

        bool success = WriteDword(backupKey, L"Version", 1) && WriteDword(backupKey, L"Complete", 0);
        for (const auto& item : CursorValues) {
            if (!success) break;

            std::wstring current;
            const bool present = ReadString(cursorKey, item.registryName, current);
            const std::wstring presentName = L"Present_" + std::wstring(item.backupName);
            const std::wstring valueName = L"Value_" + std::wstring(item.backupName);
            success = WriteDword(backupKey, presentName, present ? 1 : 0);
            if (success && present) success = WriteString(backupKey, valueName, current);
        }

        if (success) success = WriteDword(backupKey, L"Complete", 1);
        RegCloseKey(backupKey);
        RegCloseKey(cursorKey);

        if (!success) {
            RegDeleteTreeW(HKEY_CURRENT_USER, BackupKey);
            InstallationLogger.WriteLine(L"Cursor rollback snapshot could not be completed.");
        }
        return success;
    }

    inline bool ExpandVerifiedArchive(std::filesystem::path& extractedRoot) {
        const std::filesystem::path archive = std::filesystem::path(currdir) / L"Rectify12-cursors-jepricreations.zip";

        wchar_t programData[MAX_PATH]{};
        const DWORD expanded = ExpandEnvironmentStringsW(L"%ProgramData%", programData, ARRAYSIZE(programData));
        if (expanded == 0 || expanded > ARRAYSIZE(programData)) return false;
        extractedRoot = std::filesystem::path(programData) / L"Rectify12" / L"Assets" / L"Cursors";

        wchar_t powershell[MAX_PATH]{};
        if (FAILED(StringCchPrintfW(
                powershell,
                ARRAYSIZE(powershell),
                L"%s\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
                windir))) {
            return false;
        }

        std::wstring command =
            L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""
            L"$ErrorActionPreference='Stop';"
            L"$archive='" + archive.wstring() + L"';"
            L"if(-not (Test-Path -LiteralPath $archive)){throw 'Rectify12 cursor archive is missing'};"
            L"$hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToUpperInvariant();"
            L"if($hash -ne '" + std::wstring(ExpectedArchiveHash) + L"'){throw 'Rectify12 cursor archive hash mismatch'};"
            L"$dest='" + extractedRoot.wstring() + L"';"
            L"if(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest -Recurse -Force};"
            L"New-Item -ItemType Directory -Force -Path $dest | Out-Null;"
            L"Expand-Archive -LiteralPath $archive -DestinationPath $dest -Force;"
            L"if(-not (Test-Path -LiteralPath (Join-Path $dest 'light\\Install.inf')) -or -not (Test-Path -LiteralPath (Join-Path $dest 'dark\\Install.inf'))){throw 'Rectify12 cursor archive is incomplete'};"
            L"exit 0\"";

        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        const ProcessResult result = RunEXE(powershell, mutableCommand.data(), 120000);
        if (!result.success) {
            InstallationLogger.WriteLine(L"Could not verify/extract the Rectify12 cursor archive.");
            return false;
        }
        return true;
    }

    inline bool ApplyInf(const std::filesystem::path& infPath) {
        wchar_t rundll32[MAX_PATH]{};
        if (FAILED(StringCchPrintfW(rundll32, ARRAYSIZE(rundll32), L"%s\\System32\\rundll32.exe", windir))) {
            return false;
        }

        std::wstring arguments =
            L"setupapi.dll,InstallHinfSection DefaultInstall 132 \"" + infPath.wstring() + L"\"";
        std::vector<wchar_t> mutableArguments(arguments.begin(), arguments.end());
        mutableArguments.push_back(L'\0');
        const ProcessResult result = RunEXE(rundll32, mutableArguments.data(), 120000);
        if (!result.success) {
            InstallationLogger.WriteLine(L"Windows SetupAPI could not install the Rectify12 cursor scheme.");
            return false;
        }

        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
        return true;
    }

    inline bool Install(bool lightTheme) {
        if (!BackupCurrentScheme()) return false;

        std::filesystem::path extractedRoot;
        if (!ExpandVerifiedArchive(extractedRoot)) return false;

        const wchar_t* variant = lightTheme ? L"light" : L"dark";
        const std::filesystem::path infPath = extractedRoot / variant / L"Install.inf";
        std::error_code ec;
        if (!std::filesystem::exists(infPath, ec) || ec) {
            InstallationLogger.WriteLine(L"The verified cursor archive did not contain the expected Install.inf.");
            return false;
        }

        if (!ApplyInf(infPath)) return false;

        HKEY backupKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, BackupKey, 0, KEY_SET_VALUE, &backupKey) == ERROR_SUCCESS) {
            WriteString(backupKey, L"InstalledVariant", variant);
            RegCloseKey(backupKey);
        }

        InstallationLogger.WriteLine(
            lightTheme
                ? L"Applied JepriCreations W11 Cursor Light Free scheme."
                : L"Applied JepriCreations W11 Cursor Dark Free scheme.");
        return true;
    }

    inline bool Restore() {
        HKEY backupKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, BackupKey, 0, KEY_READ, &backupKey) != ERROR_SUCCESS) {
            return true;
        }

        DWORD version = 0;
        DWORD complete = 0;
        if (!ReadDword(backupKey, L"Version", version) || version != 1 ||
            !ReadDword(backupKey, L"Complete", complete) || complete != 1) {
            RegCloseKey(backupKey);
            InstallationLogger.WriteLine(L"Cursor rollback snapshot is incomplete; refusing to overwrite cursor settings.");
            return false;
        }

        HKEY cursorKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", 0, KEY_SET_VALUE, &cursorKey) != ERROR_SUCCESS) {
            RegCloseKey(backupKey);
            return false;
        }

        bool success = true;
        for (const auto& item : CursorValues) {
            const std::wstring presentName = L"Present_" + std::wstring(item.backupName);
            const std::wstring valueName = L"Value_" + std::wstring(item.backupName);
            DWORD present = 0;
            if (!ReadDword(backupKey, presentName, present)) {
                success = false;
                break;
            }

            if (present) {
                std::wstring value;
                if (!ReadString(backupKey, valueName.c_str(), value)) {
                    success = false;
                    break;
                }
                const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
                if (RegSetValueExW(
                        cursorKey,
                        item.registryName,
                        0,
                        REG_SZ,
                        reinterpret_cast<const BYTE*>(value.c_str()),
                        bytes) != ERROR_SUCCESS) {
                    success = false;
                    break;
                }
            }
            else {
                const wchar_t* registryName = item.registryName ? item.registryName : L"";
                const LSTATUS deleteResult = RegDeleteValueW(cursorKey, registryName);
                if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND) {
                    success = false;
                    break;
                }
            }
        }

        RegCloseKey(cursorKey);
        RegCloseKey(backupKey);

        if (!success) {
            InstallationLogger.WriteLine(L"Could not fully restore the pre-Rectify12 cursor scheme; rollback data was preserved.");
            return false;
        }

        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
        RegDeleteTreeW(HKEY_CURRENT_USER, BackupKey);
        InstallationLogger.WriteLine(L"Restored the cursor scheme saved before Rectify12 installation.");
        return true;
    }
}
