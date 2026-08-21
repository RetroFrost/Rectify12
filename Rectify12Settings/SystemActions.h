#pragma once

#include <windows.h>
#include <shellapi.h>
#include <srrestoreptapi.h>
#include <strsafe.h>

#include <algorithm>
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

    inline Result RestartExplorer() {
        HWND shellWindow = GetShellWindow();
        if (!shellWindow) {
            return { false, ERROR_NOT_FOUND, L"Explorer shell window was not found." };
        }

        DWORD processId = 0;
        GetWindowThreadProcessId(shellWindow, &processId);
        if (!processId) {
            return { false, GetLastError(), L"Could not resolve the Explorer process." };
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
        StringCchCopyNW(begin.szDescription, ARRAYSIZE(begin.szDescription), description.data(), description.size());

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
        std::sort(exclusions.begin(), exclusions.end());
        exclusions.erase(std::unique(exclusions.begin(), exclusions.end()), exclusions.end());

        std::vector<wchar_t> data;
        for (const auto& exclusion : exclusions) {
            data.insert(data.end(), exclusion.begin(), exclusion.end());
            data.push_back(L'\0');
        }
        data.push_back(L'\0');

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

    inline bool WriteDword(const wchar_t* subkey, const wchar_t* name, DWORD value) {
        HKEY key = nullptr;
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition) != ERROR_SUCCESS) {
            return false;
        }
        const LSTATUS result = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }

    inline Result ExportSettings(const std::filesystem::path& file) {
        constexpr wchar_t EffectsKey[] = L"Software\\Rectify12\\Effects";
        const wchar_t* values[] = { L"Enabled", L"ReplaceGenericDark", L"PatchExplorer", L"Backdrop" };

        std::wofstream out(file, std::ios::trunc);
        if (!out) return { false, GetLastError(), L"Could not create the Rectify12 settings file." };

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

        return { true, ERROR_SUCCESS, L"Rectify12 settings exported." };
    }

    inline Result ImportSettings(const std::filesystem::path& file) {
        std::wifstream in(file);
        if (!in) return { false, GetLastError(), L"Could not open the Rectify12 settings file." };

        std::wstring line;
        if (!std::getline(in, line) || line != L"R12CFG1") {
            return { false, ERROR_INVALID_DATA, L"This is not a supported Rectify12 settings file." };
        }

        constexpr wchar_t EffectsKey[] = L"Software\\Rectify12\\Effects";
        std::vector<std::wstring> exclusions;
        while (std::getline(in, line)) {
            const auto split = line.find(L'=');
            if (split == std::wstring::npos) continue;
            const std::wstring key = line.substr(0, split);
            const std::wstring value = line.substr(split + 1);

            if (key.rfind(L"Effects.", 0) == 0) {
                try {
                    const DWORD number = static_cast<DWORD>(std::stoul(value));
                    if (!WriteDword(EffectsKey, key.c_str() + 8, number)) {
                        return { false, GetLastError(), L"A Rectify12 effect setting could not be imported." };
                    }
                }
                catch (...) {
                    return { false, ERROR_INVALID_DATA, L"A Rectify12 effect setting contains an invalid number." };
                }
            }
            else if (key == L"Compatibility.ExcludedExecutables") {
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

        const auto exclusionsResult = SaveCompatibilityExclusions(std::move(exclusions));
        if (!exclusionsResult.success) return exclusionsResult;

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

        wchar_t windowsDirectory[MAX_PATH]{};
        GetWindowsDirectoryW(windowsDirectory, ARRAYSIZE(windowsDirectory));
        const std::filesystem::path rectifyRoot = std::filesystem::path(windowsDirectory) / L"Rectify12";
        items.push_back({ L"Rectify12 installation", std::filesystem::exists(rectifyRoot), rectifyRoot.wstring() });

        wchar_t programData[MAX_PATH]{};
        const DWORD programDataLength = GetEnvironmentVariableW(L"ProgramData", programData, ARRAYSIZE(programData));
        const std::filesystem::path windhawkMods = programDataLength
            ? std::filesystem::path(programData) / L"Windhawk" / L"Engine" / L"Mods"
            : std::filesystem::path{};
        items.push_back({
            L"Windhawk modules",
            !windhawkMods.empty() && std::filesystem::exists(windhawkMods),
            windhawkMods.empty() ? L"ProgramData could not be resolved." : windhawkMods.wstring() });

        DWORD effectsEnabled = 0;
        const bool hasEffectsState = ReadDword(L"Software\\Rectify12\\Effects", L"Enabled", effectsEnabled);
        items.push_back({ L"Effects settings", hasEffectsState, hasEffectsState ? L"Registry state is readable." : L"Defaults will be used." });

        const auto exclusions = LoadCompatibilityExclusions();
        items.push_back({ L"Compatibility store", true, std::to_wstring(exclusions.size()) + L" executable exclusion(s)." });

        const std::filesystem::path explorerPath = std::filesystem::path(windowsDirectory) / L"explorer.exe";
        items.push_back({ L"Windows Explorer", std::filesystem::exists(explorerPath), explorerPath.wstring() });

        return items;
    }
}
