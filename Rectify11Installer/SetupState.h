#pragma once

#include "framework.h"
#include "ProductInfo.h"

#include <filesystem>
#include <string>

namespace Rectify12::SetupState {
    inline constexpr DWORD SchemaVersion = 1;
    inline constexpr wchar_t RegistryPath[] = L"SOFTWARE\\Rectify12\\Setup";
    inline constexpr wchar_t RunOncePath[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    inline constexpr wchar_t RunOnceValue[] = L"Rectify12Resume";
    inline constexpr wchar_t ResumeArgument[] = L"/rectify12-resume";

    enum class Stage : DWORD {
        None = 0,
        AwaitingAtlas = 10,
        AtlasRunning = 20,
        PostAtlas = 30,
        ApplyingRectify = 40,
        Complete = 100,
    };

    inline bool IsResumeCommandLine(const wchar_t* commandLine) {
        return commandLine && wcsstr(commandLine, ResumeArgument) != nullptr;
    }

    inline bool SetDword(HKEY key, const wchar_t* name, DWORD value) {
        return RegSetValueExW(
            key,
            name,
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value)) == ERROR_SUCCESS;
    }

    inline bool SetString(HKEY key, const wchar_t* name, const std::wstring& value) {
        const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        return RegSetValueExW(
            key,
            name,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            bytes) == ERROR_SUCCESS;
    }

    inline bool ReadDword(HKEY key, const wchar_t* name, DWORD& value) {
        DWORD bytes = sizeof(value);
        return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS;
    }

    inline bool ReadString(HKEY key, const wchar_t* name, std::wstring& value) {
        DWORD bytes = 0;
        if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
            bytes < sizeof(wchar_t)) {
            return false;
        }

        std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
        if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes) != ERROR_SUCCESS) {
            return false;
        }
        if (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
        value = std::move(buffer);
        return !value.empty();
    }

    inline bool SaveStage(Stage stage, const std::wstring& resumeExecutable = {}) {
        HKEY key = nullptr;
        DWORD disposition = 0;
        const LSTATUS create = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE,
            RegistryPath,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            &disposition);
        if (create != ERROR_SUCCESS) return false;

        bool success =
            SetDword(key, L"SchemaVersion", SchemaVersion) &&
            SetDword(key, L"Stage", static_cast<DWORD>(stage));
        if (success && !resumeExecutable.empty()) {
            success = SetString(key, L"ResumeExecutable", resumeExecutable);
        }
        RegCloseKey(key);
        return success;
    }

    inline bool LoadStage(Stage& stage, std::wstring* resumeExecutable = nullptr) {
        HKEY key = nullptr;
        const LSTATUS open = RegOpenKeyExW(HKEY_LOCAL_MACHINE, RegistryPath, 0, KEY_READ, &key);
        if (open != ERROR_SUCCESS) return false;

        DWORD schema = 0;
        DWORD rawStage = 0;
        const bool valid =
            ReadDword(key, L"SchemaVersion", schema) && schema == SchemaVersion &&
            ReadDword(key, L"Stage", rawStage);
        if (valid && resumeExecutable) {
            ReadString(key, L"ResumeExecutable", *resumeExecutable);
        }
        RegCloseKey(key);
        if (!valid) return false;

        switch (static_cast<Stage>(rawStage)) {
        case Stage::AwaitingAtlas:
        case Stage::AtlasRunning:
        case Stage::PostAtlas:
        case Stage::ApplyingRectify:
        case Stage::Complete:
            stage = static_cast<Stage>(rawStage);
            return true;
        default:
            return false;
        }
    }

    inline bool ClearRunOnce() {
        const LSTATUS result = RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, RunOncePath, RunOnceValue);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
    }

    inline bool ArmRunOnce(const std::wstring& executable) {
        HKEY key = nullptr;
        DWORD disposition = 0;
        const LSTATUS create = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE,
            RunOncePath,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            &disposition);
        if (create != ERROR_SUCCESS) return false;

        const std::wstring command = L"\"" + executable + L"\" " + ResumeArgument;
        const bool success = SetString(key, RunOnceValue, command);
        RegCloseKey(key);
        return success;
    }

    inline bool Clear() {
        const bool runOnceCleared = ClearRunOnce();
        const LSTATUS stateResult = RegDeleteTreeW(HKEY_LOCAL_MACHINE, RegistryPath);
        return runOnceCleared &&
            (stateResult == ERROR_SUCCESS || stateResult == ERROR_FILE_NOT_FOUND || stateResult == ERROR_PATH_NOT_FOUND);
    }

    inline bool GetResumeDirectory(std::filesystem::path& directory) {
        wchar_t programData[MAX_PATH]{};
        const DWORD expanded = ExpandEnvironmentStringsW(L"%ProgramData%", programData, ARRAYSIZE(programData));
        if (expanded == 0 || expanded > ARRAYSIZE(programData)) return false;
        directory = std::filesystem::path(programData) / L"Rectify12" / L"Setup";
        return true;
    }

    inline bool CopyIfPresent(const std::filesystem::path& source, const std::filesystem::path& destination, bool required) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(source, ec);
        if (ec) return false;
        if (!exists) return !required;

        std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        return !ec;
    }

    inline bool StageResumeHost(std::wstring& stagedExecutable) {
        std::filesystem::path directory;
        if (!GetResumeDirectory(directory)) return false;

        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) return false;

        wchar_t modulePath[MAX_PATH]{};
        const DWORD moduleLength = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
        if (moduleLength == 0 || moduleLength >= ARRAYSIZE(modulePath)) return false;

        const std::filesystem::path sourceExecutable(modulePath);
        const std::filesystem::path sourceDirectory = sourceExecutable.parent_path();
        const std::filesystem::path targetExecutable = directory / Rectify12::InstalledExecutable;

        if (!CopyIfPresent(sourceExecutable, targetExecutable, true)) return false;

        // These are the DirectUI/runtime files required for the installer host to start
        // after reboot. The payload itself will be staged separately by its owning stage.
        const wchar_t* requiredRuntimeFiles[] = {
            L"Base.dll",
            L"Controls.dll",
            L"PageRes.dll",
            L"Rectify12-cursors-jepricreations.zip",
        };
        for (const auto* filename : requiredRuntimeFiles) {
            if (!CopyIfPresent(sourceDirectory / filename, directory / filename, true)) return false;
        }

        // The installer font is desirable but not a reason to lose resume capability.
        CopyIfPresent(sourceDirectory / L"Segoe_r11.ttf", directory / L"Segoe_r11.ttf", false);

        // Atlas is optional. Preserve its verified inputs across a wizard-requested
        // reboot when they were supplied with the original package.
        CopyIfPresent(sourceDirectory / L"AME Wizard Beta.exe", directory / L"AME Wizard Beta.exe", false);
        CopyIfPresent(sourceDirectory / L"AtlasPlaybook_v0.5.0-hotfix.apbx",
            directory / L"AtlasPlaybook_v0.5.0-hotfix.apbx", false);

        stagedExecutable = targetExecutable.wstring();
        return true;
    }

    inline bool Arm(Stage stage) {
        std::wstring stagedExecutable;
        if (!StageResumeHost(stagedExecutable)) return false;
        if (!SaveStage(stage, stagedExecutable)) return false;
        if (!ArmRunOnce(stagedExecutable)) {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, RegistryPath);
            return false;
        }
        return true;
    }

}
