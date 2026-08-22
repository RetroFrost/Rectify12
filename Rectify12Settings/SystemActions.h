#pragma once

#include <windows.h>
#include <shellapi.h>
#include <srrestoreptapi.h>
#include <strsafe.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "SrClient.lib")

namespace Rectify12::SystemActions {
    struct Result {
        bool success = false;
        DWORD error = ERROR_SUCCESS;
        std::wstring message;
    };

    inline std::wstring EnvironmentValue(const wchar_t* name) {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0) return {};

        std::vector<wchar_t> buffer(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0 || written >= buffer.size()) return {};
        return std::wstring(buffer.data(), written);
    }

    inline std::wstring WindowsDirectory() {
        std::vector<wchar_t> buffer(512, L'\0');
        for (;;) {
            const UINT written = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
            if (written == 0) return {};
            if (written < buffer.size()) return std::wstring(buffer.data(), written);
            if (buffer.size() >= 32768) return {};
            buffer.resize(std::min<std::size_t>(static_cast<std::size_t>(written) + 1, 32768), L'\0');
        }
    }

    inline Result RestartExplorer() {
        HWND shellWindow = GetShellWindow();
        if (!shellWindow) {
            return { false, ERROR_NOT_FOUND, L"Explorer shell window was not found." };
        }

        DWORD processId = 0;
        GetWindowThreadProcessId(shellWindow, &processId);
        if (!processId) {
            return { false, ERROR_NOT_FOUND, L"Could not resolve the Explorer process." };
        }

        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
        if (!process) {
            return { false, GetLastError(), L"Could not open Explorer for restart." };
        }

        const BOOL terminated = TerminateProcess(process, 0);
        const DWORD terminateError = terminated ? ERROR_SUCCESS : GetLastError();
        if (terminated) {
            WaitForSingleObject(process, 5000);
        }
        CloseHandle(process);

        if (!terminated) {
            return { false, terminateError, L"Explorer could not be stopped." };
        }

        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
            nullptr,
            L"open",
            L"explorer.exe",
            nullptr,
            nullptr,
            SW_SHOWNORMAL));

        if (result <= 32) {
            return { false, static_cast<DWORD>(result), L"Explorer stopped, but Windows could not start it again." };
        }

        return { true, ERROR_SUCCESS, L"Explorer restarted." };
    }

    inline Result CreateRestoreSnapshot(std::wstring_view description = L"Before Rectify12 changes") {
        RESTOREPOINTINFOW begin{};
        begin.dwEventType = BEGIN_SYSTEM_CHANGE;
        begin.dwRestorePtType = MODIFY_SETTINGS;
        begin.llSequenceNumber = 0;
        const std::size_t descriptionLength = std::min<std::size_t>(description.size(), ARRAYSIZE(begin.szDescription) - 1);
        const HRESULT copyResult = StringCchCopyNW(
            begin.szDescription,
            ARRAYSIZE(begin.szDescription),
            description.data(),
            descriptionLength);
        if (FAILED(copyResult)) {
            return { false, ERROR_INVALID_PARAMETER, L"The restore snapshot description could not be prepared." };
        }

        STATEMGRSTATUS status{};
        if (!SRSetRestorePointW(&begin, &status)) {
            const DWORD error = status.nStatus ? status.nStatus : GetLastError();
            return { false, error, L"Windows did not create a restore snapshot. System Protection may be disabled." };
        }

        RESTOREPOINTINFOW end = begin;
        end.dwEventType = END_SYSTEM_CHANGE;
        end.llSequenceNumber = status.llSequenceNumber;

        STATEMGRSTATUS endStatus{};
        if (!SRSetRestorePointW(&end, &endStatus)) {
            const DWORD error = endStatus.nStatus ? endStatus.nStatus : GetLastError();
            return { false, error, L"Windows created the restore point but could not finalise it." };
        }

        return { true, ERROR_SUCCESS, L"Restore snapshot created." };
    }

    inline constexpr wchar_t CompatibilityKey[] = L"Software\\Rectify12\\Compatibility";
    inline constexpr wchar_t ExclusionsValue[] = L"ExcludedExecutables";

    inline std::vector<std::wstring> LoadCompatibilityExclusions() {
        std::vector<std::wstring> result;
        DWORD bytes = 0;
        if (RegGetValueW(
                HKEY_CURRENT_USER,
                CompatibilityKey,
                ExclusionsValue,
                RRF_RT_REG_MULTI_SZ,
                nullptr,
                nullptr,
                &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
            return result;
        }

        std::vector<wchar_t> buffer((bytes / sizeof(wchar_t)) + 2, L'\0');
        if (RegGetValueW(
                HKEY_CURRENT_USER,
                CompatibilityKey,
                ExclusionsValue,
                RRF_RT_REG_MULTI_SZ,
                nullptr,
                buffer.data(),
                &bytes) != ERROR_SUCCESS) {
            return result;
        }

        for (const wchar_t* entry = buffer.data(); *entry; entry += wcslen(entry) + 1) {
            result.emplace_back(entry);
        }
        return result;
    }

    inline Result SaveCompatibilityExclusions(std::vector<std::wstring> exclusions) {
        exclusions.erase(
            std::remove_if(exclusions.begin(), exclusions.end(), [](const std::wstring& value) { return value.empty(); }),
            exclusions.end());
        std::sort(exclusions.begin(), exclusions.end(), [](const std::wstring& left, const std::wstring& right) {
            return _wcsicmp(left.c_str(), right.c_str()) < 0;
        });
        exclusions.erase(
            std::unique(exclusions.begin(), exclusions.end(), [](const std::wstring& left, const std::wstring& right) {
                return _wcsicmp(left.c_str(), right.c_str()) == 0;
            }),
            exclusions.end());

        std::vector<wchar_t> data;
        for (const auto& exclusion : exclusions) {
            data.insert(data.end(), exclusion.begin(), exclusion.end());
            data.push_back(L'\0');
        }
        // REG_MULTI_SZ must always end with two NUL characters, including an empty list.
        data.push_back(L'\0');
        if (data.size() == 1) data.push_back(L'\0');

        HKEY key = nullptr;
        DWORD disposition = 0;
        const LSTATUS createResult = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            CompatibilityKey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            &disposition);
        if (createResult != ERROR_SUCCESS) {
            return { false, static_cast<DWORD>(createResult), L"Could not open Rectify12 compatibility settings." };
        }

        const LSTATUS writeResult = RegSetValueExW(
            key,
            ExclusionsValue,
            0,
            REG_MULTI_SZ,
            reinterpret_cast<const BYTE*>(data.data()),
            static_cast<DWORD>(data.size() * sizeof(wchar_t)));
        RegCloseKey(key);

        if (writeResult != ERROR_SUCCESS) {
            return { false, static_cast<DWORD>(writeResult), L"Could not save compatibility exclusions." };
        }

        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            reinterpret_cast<LPARAM>(L"Rectify12Compatibility"),
            SMTO_ABORTIFHUNG,
            1000,
            nullptr);
        return { true, ERROR_SUCCESS, L"Compatibility exclusions saved." };
    }

    inline bool ReadDword(const wchar_t* subkey, const wchar_t* name, DWORD& value) {
        DWORD size = sizeof(value);
        return RegGetValueW(
                   HKEY_CURRENT_USER,
                   subkey,
                   name,
                   RRF_RT_REG_DWORD,
                   nullptr,
                   &value,
                   &size) == ERROR_SUCCESS;
    }

    inline LSTATUS WriteDword(const wchar_t* subkey, const wchar_t* name, DWORD value) {
        HKEY key = nullptr;
        DWORD disposition = 0;
        const LSTATUS createResult = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            subkey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            &disposition);
        if (createResult != ERROR_SUCCESS) return createResult;

        const LSTATUS result = RegSetValueExW(
            key,
            name,
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value));
        RegCloseKey(key);
        return result;
    }

    inline bool IsAllowedEffectSetting(std::wstring_view name, DWORD value) {
        if (name == L"Backdrop") return value >= 1 && value <= 3;
        if (name == L"Enabled" || name == L"ReplaceGenericDark" || name == L"PatchExplorer" || name == L"ExplorerSafeMode") {
            return value <= 1;
        }
        return false;
    }

    inline Result ExportSettings(const std::filesystem::path& file) {
        constexpr wchar_t EffectsKey[] = L"Software\\Rectify12\\Effects";
        const wchar_t* values[] = {
            L"Enabled",
            L"ReplaceGenericDark",
            L"PatchExplorer",
            L"ExplorerSafeMode",
            L"Backdrop"
        };

        std::wofstream out(file, std::ios::trunc);
        if (!out) return { false, ERROR_OPEN_FAILED, L"Could not create the Rectify12 settings file." };

        out << L"R12CFG1\n";
        for (const auto* name : values) {
            DWORD value = 0;
            if (ReadDword(EffectsKey, name, value)) {
                out << L"Effects." << name << L"=" << value << L"\n";
            }
        }

        const auto exclusions = LoadCompatibilityExclusions();
        out << L"Compatibility.ExcludedExecutables=";
        for (std::size_t i = 0; i < exclusions.size(); ++i) {
            if (i) out << L";";
            out << exclusions[i];
        }
        out << L"\n";
        out.flush();

        if (!out) return { false, ERROR_WRITE_FAULT, L"Rectify12 settings could not be completely written." };
        return { true, ERROR_SUCCESS, L"Rectify12 settings exported." };
    }

    inline Result ImportSettings(const std::filesystem::path& file) {
        std::wifstream in(file);
        if (!in) {
            std::error_code existsError;
            const bool exists = std::filesystem::exists(file, existsError);
            const DWORD openError = existsError
                ? static_cast<DWORD>(existsError.value())
                : (exists ? static_cast<DWORD>(ERROR_OPEN_FAILED) : static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
            return { false, openError, L"Could not open the Rectify12 settings file." };
        }

        std::wstring line;
        if (!std::getline(in, line) || line != L"R12CFG1") {
            return { false, ERROR_INVALID_DATA, L"This is not a supported Rectify12 settings file." };
        }

        constexpr wchar_t EffectsKey[] = L"Software\\Rectify12\\Effects";
        std::vector<std::wstring> exclusions;
        bool exclusionsPresent = false;

        while (std::getline(in, line)) {
            const auto split = line.find(L'=');
            if (split == std::wstring::npos) continue;
            const std::wstring key = line.substr(0, split);
            const std::wstring value = line.substr(split + 1);

            if (key.rfind(L"Effects.", 0) == 0) {
                const std::wstring valueName = key.substr(8);
                try {
                    std::size_t parsedCharacters = 0;
                    const unsigned long parsed = std::stoul(value, &parsedCharacters, 10);
                    if (parsedCharacters != value.size() || parsed > MAXDWORD) {
                        return { false, ERROR_INVALID_DATA, L"A Rectify12 effect setting contains an invalid DWORD value." };
                    }
                    const DWORD number = static_cast<DWORD>(parsed);
                    if (!IsAllowedEffectSetting(valueName, number)) {
                        return { false, ERROR_INVALID_DATA, L"The settings file contains an unsupported Rectify12 effect value." };
                    }
                    const LSTATUS writeResult = WriteDword(EffectsKey, valueName.c_str(), number);
                    if (writeResult != ERROR_SUCCESS) {
                        return { false, static_cast<DWORD>(writeResult), L"A Rectify12 effect setting could not be imported." };
                    }
                }
                catch (...) {
                    return { false, ERROR_INVALID_DATA, L"A Rectify12 effect setting contains an invalid number." };
                }
            }
            else if (key == L"Compatibility.ExcludedExecutables") {
                exclusionsPresent = true;
                std::size_t start = 0;
                while (start <= value.size()) {
                    const auto end = value.find(L';', start);
                    const auto item = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
                    if (!item.empty()) exclusions.push_back(item);
                    if (end == std::wstring::npos) break;
                    start = end + 1;
                }
            }
        }

        if (in.bad()) {
            return { false, ERROR_READ_FAULT, L"The Rectify12 settings file could not be completely read." };
        }

        if (exclusionsPresent) {
            const auto exclusionsResult = SaveCompatibilityExclusions(std::move(exclusions));
            if (!exclusionsResult.success) return exclusionsResult;
        }

        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            reinterpret_cast<LPARAM>(L"Rectify12Effects"),
            SMTO_ABORTIFHUNG,
            1000,
            nullptr);
        return { true, ERROR_SUCCESS, L"Rectify12 settings imported." };
    }

    struct HealthItem {
        std::wstring name;
        bool healthy;
        std::wstring detail;
    };

    inline std::vector<HealthItem> RunHealthCheck() {
        std::vector<HealthItem> items;

        const std::wstring windowsDirectory = WindowsDirectory();
        const std::filesystem::path rectifyRoot = windowsDirectory.empty()
            ? std::filesystem::path{}
            : std::filesystem::path(windowsDirectory) / L"Rectify12";
        std::error_code rectifyError;
        const bool rectifyExists = !rectifyRoot.empty() && std::filesystem::exists(rectifyRoot, rectifyError);
        items.push_back({
            L"Rectify12 installation",
            rectifyExists && !rectifyError,
            rectifyRoot.empty()
                ? L"Windows directory could not be resolved."
                : (rectifyError ? L"The install directory could not be inspected." : rectifyRoot.wstring())
        });

        wchar_t productVersion[128]{};
        DWORD productVersionBytes = sizeof(productVersion);
        const LSTATUS productVersionResult = RegGetValueW(
            HKEY_LOCAL_MACHINE,
            L"Software\\Rectify12",
            L"Version",
            RRF_RT_REG_SZ,
            nullptr,
            productVersion,
            &productVersionBytes);
        items.push_back({
            L"Rectify12 direct-patch registration",
            productVersionResult == ERROR_SUCCESS && productVersion[0] != L'\0',
            productVersionResult == ERROR_SUCCESS && productVersion[0] != L'\0'
                ? L"Registered Rectify12 version: " + std::wstring(productVersion)
                : L"Rectify12 product registration is missing or unreadable."
        });

        DWORD effectsEnabled = 0;
        const bool hasEffectsState = ReadDword(L"Software\\Rectify12\\Effects", L"Enabled", effectsEnabled);
        // A missing key is not corruption: Rectify12 intentionally has safe defaults.
        items.push_back({
            L"Effects settings",
            true,
            hasEffectsState ? L"Registry state is readable." : L"No explicit registry state; safe defaults are active."
        });

        const auto exclusions = LoadCompatibilityExclusions();
        items.push_back({ L"Compatibility store", true, std::to_wstring(exclusions.size()) + L" executable exclusion(s)." });

        const std::filesystem::path explorerPath = windowsDirectory.empty()
            ? std::filesystem::path{}
            : std::filesystem::path(windowsDirectory) / L"explorer.exe";
        std::error_code explorerError;
        const bool explorerExists = !explorerPath.empty() && std::filesystem::exists(explorerPath, explorerError);
        items.push_back({
            L"Windows Explorer",
            explorerExists && !explorerError,
            explorerPath.empty()
                ? L"Windows directory could not be resolved."
                : (explorerError ? L"Explorer could not be inspected." : explorerPath.wstring())
        });

        return items;
    }
}
