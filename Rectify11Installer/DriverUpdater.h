#pragma once

#include "InstallationProcedure.h"
#include "Navigation.h"

#include <string>
#include <vector>

namespace Rectify12::Drivers {
    inline bool UpdateFromWindowsUpdate() {
        InstallationLogger.WriteLine(L"Checking Windows Update for applicable device-driver updates...");

        wchar_t powershell[MAX_PATH]{};
        if (FAILED(StringCchPrintfW(
                powershell,
                ARRAYSIZE(powershell),
                L"%s\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
                windir))) {
            InstallationLogger.WriteLine(L"PowerShell path exceeded the current installer buffer.");
            return false;
        }

        // Use the Windows Update Agent COM API that ships with Windows. This avoids
        // third-party driver databases and installs only drivers Microsoft currently
        // considers applicable to the machine.
        const std::wstring command =
            L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""
            L"$ErrorActionPreference='Stop';"
            L"$session=New-Object -ComObject Microsoft.Update.Session;"
            L"$searcher=$session.CreateUpdateSearcher();"
            L"$result=$searcher.Search(\"IsInstalled=0 and Type='Driver' and IsHidden=0\");"
            L"$updates=New-Object -ComObject Microsoft.Update.UpdateColl;"
            L"foreach($u in $result.Updates){if(-not $u.EulaAccepted){$u.AcceptEula()};[void]$updates.Add($u)};"
            L"if($updates.Count -eq 0){exit 0};"
            L"$downloader=$session.CreateUpdateDownloader();$downloader.Updates=$updates;"
            L"$download=$downloader.Download();if($download.ResultCode -ne 2 -and $download.ResultCode -ne 3){exit 2};"
            L"$installer=$session.CreateUpdateInstaller();$installer.Updates=$updates;"
            L"$installed=$installer.Install();if($installed.ResultCode -ne 2 -and $installed.ResultCode -ne 3){exit 3};"
            L"if($installed.RebootRequired){exit 3010};exit 0\"";

        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');

        // Driver discovery/download can legitimately take several minutes.
        const ProcessResult result = RunEXE(powershell, mutableCommand.data(), 1800000);
        if (!result.success) {
            InstallationLogger.WriteLine(
                L"Automatic driver update failed. Error/exit code: " + std::to_wstring(result.error));
            return false;
        }

        if (result.exitCode == ERROR_SUCCESS_REBOOT_REQUIRED ||
            result.exitCode == ERROR_SUCCESS_REBOOT_INITIATED) {
            InstallationLogger.WriteLine(L"Driver updates completed and requested a reboot.");
        }
        else {
            InstallationLogger.WriteLine(L"Applicable Windows Update driver updates are installed.");
        }
        return true;
    }
}
