#include "framework.h"
#include "InstallerEngine.h"
#include "DirectUI/DirectUI.h"
#include "Navigation.h"
#include "InstallationProcedure.h"
#include "UninstallationProcedure.h"
#include "DriverUpdater.h"
#include "CursorInstaller.h"
#include "AtlasStage.h"
#include "SetupState.h"

using namespace DirectUI;

std::atomic_bool IEngineWrapper::animate = true;
std::atomic_bool IEngineWrapper::operationRunning = false;
std::atomic<int> IEngineWrapper::progressnum = 0;
std::atomic<int> IEngineWrapper::Ttime = 30;
std::wstring IEngineWrapper::currprogress;
std::mutex IEngineWrapper::progressMutex;
std::mutex IEngineWrapper::logMutex;

namespace {
    void SetProgressText(const wchar_t* text) {
        {
            std::lock_guard<std::mutex> lock(IEngineWrapper::progressMutex);
            IEngineWrapper::currprogress = text ? text : L"";
        }
        if (pwnd) PostMessageW(pwnd->GetHWND(), WM_UPDATEPROGRESS, 0, 0);
    }

    unsigned long FailOperation(const wchar_t* operation, const wchar_t* stage) {
        const std::wstring operationName = operation ? operation : L"Setup";
        const std::wstring stageName = stage ? stage : L"Unknown stage";
        InstallationLogger.WriteLine(operationName + L" stopped because a required stage failed: " + stageName);

        const std::wstring progress = operationName + L" failed. Review Installation.log before retrying.";
        SetProgressText(progress.c_str());

        // Do not create an endless sign-in retry loop after an actionable failure.
        // The versioned stage record remains available for a deliberate retry.
        Rectify12::SetupState::ClearRunOnce();

        IEngineWrapper::operationRunning.store(false);
        if (pwnd) PostMessageW(pwnd->GetHWND(), WM_SETUPFAILED, 0, 0);
        return ERROR_INSTALL_FAILURE;
    }
}

unsigned long IEngineWrapper::BeginMainAnim(LPVOID) {
    while (animate.load()) {
        if (pwnd) PostMessageW(pwnd->GetHWND(), WM_UPDATEANIMATIONFRAME, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return ERROR_SUCCESS;
}

unsigned long IEngineWrapper::BeginRestartAnim(LPVOID) {
    while (animate.load()) {
        if (pwnd) PostMessageW(pwnd->GetHWND(), WM_UPDATERESTARTANIMATIONFRAME, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return ERROR_SUCCESS;
}

unsigned long IEngineWrapper::BeginInstall(LPVOID) {
    bool expected = false;
    if (!operationRunning.compare_exchange_strong(expected, true)) {
        InstallationLogger.WriteLine(L"Installation start was rejected because another system operation is already running.");
        return ERROR_BUSY;
    }

    Rectify12::SetupState::Stage savedStage = Rectify12::SetupState::Stage::None;
    const bool resuming = Rectify12::SetupState::LoadStage(savedStage);

    // Rectify11 v4 Alpha is the required base. Rectify12 deliberately does not run
    // the inherited Rectify11 v3 payload extraction/copy/font/program/registry stages.
    // Rectify12-owned system patch stages are inserted here as they are migrated away
    // from the old Windhawk prototypes.
    if (!resuming) {
        SetProgressText(L"Preparing safe restart continuation...");
        if (!Rectify12::SetupState::Arm(Rectify12::SetupState::Stage::AwaitingAtlas)) {
            return FailOperation(L"Installation", L"Preparing restart continuation");
        }
        savedStage = Rectify12::SetupState::Stage::AwaitingAtlas;
    }

    if (savedStage == Rectify12::SetupState::Stage::AwaitingAtlas) {
        if (!Rectify12::SetupState::SaveStage(Rectify12::SetupState::Stage::AtlasRunning)) {
            return FailOperation(L"Installation", L"Saving the Atlas stage state");
        }
        SetProgressText(L"Checking optional AtlasOS stage...");
        if (!Rectify12::Atlas::RunVisibleOrSkip()) {
            return FailOperation(L"Installation", L"Running the visible AtlasOS stage");
        }
        if (!Rectify12::SetupState::SaveStage(Rectify12::SetupState::Stage::PostAtlas)) {
            return FailOperation(L"Installation", L"Saving the post-Atlas state");
        }
        savedStage = Rectify12::SetupState::Stage::PostAtlas;
    }
    else if (savedStage == Rectify12::SetupState::Stage::AtlasRunning) {
        InstallationLogger.WriteLine(L"Setup resumed after the visible Atlas stage; continuing with Rectify12-owned stages.");
        if (!Rectify12::SetupState::SaveStage(Rectify12::SetupState::Stage::PostAtlas)) {
            return FailOperation(L"Installation", L"Saving resumed setup state");
        }
        savedStage = Rectify12::SetupState::Stage::PostAtlas;
    }

    if (savedStage == Rectify12::SetupState::Stage::PostAtlas) {
        SetProgressText(L"Updating device drivers...");
        if (!Rectify12::Drivers::UpdateFromWindowsUpdate()) {
            return FailOperation(L"Installation", L"Updating device drivers");
        }

        SetProgressText(L"Installing Rectify12 cursors...");
        if (!Rectify12::Cursors::Install(InstallFlags[L"LIGHTTHEME"])) {
            return FailOperation(L"Installation", L"Installing Rectify12 cursors");
        }
        if (!Rectify12::SetupState::SaveStage(Rectify12::SetupState::Stage::ApplyingRectify)) {
            return FailOperation(L"Installation", L"Saving Rectify12 application state");
        }
    }

    SetProgressText(L"Finishing installation...");
    if (!FinaliseInstall()) return FailOperation(L"Installation", L"Finalising installation");

    if (!Rectify12::SetupState::SaveStage(Rectify12::SetupState::Stage::Complete) ||
        !Rectify12::SetupState::Clear()) {
        return FailOperation(L"Installation", L"Cleaning restart continuation state");
    }

    operationRunning.store(false);
    if (pwnd) PostMessageW(pwnd->GetHWND(), WM_SETUPCOMPLETE, 0, 0);
    return ERROR_SUCCESS;
}

unsigned long IEngineWrapper::BeginUninstall(LPVOID) {
    bool expected = false;
    if (!operationRunning.compare_exchange_strong(expected, true)) {
        InstallationLogger.WriteLine(L"Uninstallation start was rejected because another system operation is already running.");
        return ERROR_BUSY;
    }

    SetProgressText(L"Restoring system settings...");
    if (!RestoreDefenderSettingsIfNeeded()) {
        return FailOperation(L"Uninstallation", L"Restoring Microsoft Defender settings");
    }

    SetProgressText(L"Restoring cursor scheme...");
    if (!Rectify12::Cursors::Restore()) {
        return FailOperation(L"Uninstallation", L"Restoring cursor scheme");
    }

    SetProgressText(L"Removing Rectify12 tweaks...");
    if (!RemoveRectifyTweaks()) {
        return FailOperation(L"Uninstallation", L"Removing Rectify12 tweaks");
    }

    RemoveSecureUX();

    SetProgressText(L"Finishing uninstallation...");
    if (!FinaliseUninstall()) {
        return FailOperation(L"Uninstallation", L"Finalising uninstallation");
    }

    operationRunning.store(false);
    if (pwnd) PostMessageW(pwnd->GetHWND(), WM_SETUPCOMPLETE, 0, 0);
    return ERROR_SUCCESS;
}

unsigned long IEngineWrapper::BeginRestartCountdown(LPVOID) {
    int i = 30;
    Ttime.store(i);
    while (Ttime.load() > 0 && animate.load()) {
        if (pwnd) PostMessageW(pwnd->GetHWND(), WM_UPDATECOUNTDOWN, 0, 0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        Ttime.store(--i);
    }

    if (!animate.load()) return ERROR_CANCELLED;
    if (pwnd) PostMessageW(pwnd->GetHWND(), WM_UPDATECOUNTDOWN, 0, 0);
    SetupComplete();
    if (pwnd) PostMessageW(pwnd->GetHWND(), WM_CLOSE, 0, 0);
    return ERROR_SUCCESS;
}

void IEngineWrapper::StartThread(unsigned long (*func)(LPVOID lpParam)) {
    if (!func) return;

    if (ienThread) {
        const DWORD existingState = WaitForSingleObject(ienThread, 0);
        if (existingState == WAIT_TIMEOUT) {
            InstallationLogger.WriteLine(L"Refusing to start a second installer worker while the previous worker is still running.");
            return;
        }
        CloseHandle(ienThread);
        ienThread = nullptr;
    }

    animate.store(true);
    DWORD threadId = 0;
    ienThread = CreateThread(nullptr, 0, func, nullptr, 0, &threadId);
    if (!ienThread) {
        InstallationLogger.WriteLine(L"Failed to create installer worker thread. Win32 error: " + std::to_wstring(GetLastError()));
    }
}

void IEngineWrapper::StopThread() {
    animate.store(false);
    if (!ienThread) return;

    WaitForSingleObject(ienThread, 2000);
    CloseHandle(ienThread);
    ienThread = nullptr;
}
