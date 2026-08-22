#pragma once

#include "framework.h"
#include "InstallationProcedure.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Rectify12::Atlas {
    inline constexpr wchar_t PlaybookName[] = L"AtlasPlaybook_v0.5.0-hotfix.apbx";
    inline constexpr wchar_t PlaybookHash[] = L"13C4D8E7FC0ED38C37C5942EBCABF7F636389972E6DE391A192AE649967602D8";
    inline constexpr wchar_t WizardName[] = L"AME Wizard Beta.exe";

    inline bool ReadWindowsBuild(DWORD& build) {
        wchar_t text[32]{};
        DWORD bytes = sizeof(text);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, text, &bytes) != ERROR_SUCCESS) return false;
        wchar_t* end = nullptr;
        const unsigned long parsed = wcstoul(text, &end, 10);
        if (end == text || !end || *end != L'\0') return false;
        build = static_cast<DWORD>(parsed);
        return true;
    }

    inline void ShowUnavailable(const std::wstring& reason) {
        const std::wstring detail = reason +
            L"\n\nRectify12 will continue without AtlasOS. No Atlas changes have been applied.";
        TaskDialog(nullptr, nullptr, L"Rectify12", L"AtlasOS is unavailable", detail.c_str(),
            TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
        InstallationLogger.WriteLine(L"AtlasOS stage skipped: " + reason);
    }

    inline bool VerifyPlaybook(const std::filesystem::path& playbook) {
        wchar_t powershell[MAX_PATH]{};
        if (FAILED(StringCchPrintfW(powershell, ARRAYSIZE(powershell),
                L"%s\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", windir))) return false;
        std::wstring command = L"-NoLogo -NoProfile -NonInteractive -Command \""
            L"$h=(Get-FileHash -Algorithm SHA256 -LiteralPath '" + playbook.wstring() +
            L"').Hash.ToUpperInvariant();if($h -ne '" + std::wstring(PlaybookHash) +
            L"'){throw 'Atlas Playbook hash mismatch'}\"";
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        return RunEXE(powershell, mutableCommand.data(), 120000).success;
    }

    inline bool LaunchVisible(const std::filesystem::path& wizard, const std::filesystem::path& playbook) {
        const std::wstring explanation =
            L"Rectify12 found the pinned AtlasOS v0.5.0 hotfix Playbook and AME Wizard.\n\n"
            L"AME Wizard will remain visible. Review its options, warnings, licence/consent screens, progress, "
            L"failures, and restart decisions. Rectify12 will wait for the visible wizard to finish.\n\n"
            L"Playbook: " + playbook.wstring();
        int pressed = IDCANCEL;
        const HRESULT prompt = TaskDialog(nullptr, nullptr, L"Rectify12",
            L"Ready to open the visible AtlasOS stage", explanation.c_str(),
            TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, &pressed);
        if (FAILED(prompt) || pressed != IDOK) {
            InstallationLogger.WriteLine(L"The user cancelled before the visible AtlasOS stage.");
            return false;
        }
        std::wstring arguments = L"\"" + playbook.wstring() + L"\"";
        std::vector<wchar_t> mutableArguments(arguments.begin(), arguments.end());
        mutableArguments.push_back(L'\0');
        const ProcessResult result = RunEXE(wizard.c_str(), mutableArguments.data(), 6 * 60 * 60 * 1000);
        if (!result.success) {
            InstallationLogger.WriteLine(L"AME Wizard did not finish successfully. Exit/error code: " +
                std::to_wstring(result.error));
            return false;
        }
        InstallationLogger.WriteLine(L"The visible AtlasOS stage finished successfully.");
        return true;
    }

    inline bool RunVisibleOrSkip() {
        DWORD build = 0;
        if (!ReadWindowsBuild(build)) {
            ShowUnavailable(L"The current Windows build could not be determined.");
            return true;
        }
        if (build != 26100 && build != 26200) {
            ShowUnavailable(L"The pinned AtlasOS v0.5.0 Playbook supports Windows builds 26100 and 26200 only; "
                L"detected build " + std::to_wstring(build) + L".");
            return true;
        }
        const std::filesystem::path packageRoot(currdir);
        const std::filesystem::path wizard = packageRoot / WizardName;
        const std::filesystem::path playbook = packageRoot / PlaybookName;
        std::error_code wizardError;
        std::error_code playbookError;
        if (!std::filesystem::exists(wizard, wizardError) || wizardError ||
            !std::filesystem::exists(playbook, playbookError) || playbookError) {
            ShowUnavailable(L"The package does not contain both " + std::wstring(WizardName) + L" and " +
                std::wstring(PlaybookName) + L".");
            return true;
        }
        if (!VerifyPlaybook(playbook)) {
            InstallationLogger.WriteLine(L"The AtlasOS stage was blocked because Playbook verification failed.");
            return false;
        }
        return LaunchVisible(wizard, playbook);
    }
}
