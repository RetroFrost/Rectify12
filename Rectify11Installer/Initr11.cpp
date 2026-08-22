#include "framework.h"
#include "Initr11.h"
#include "Rectify11Installer.h"
#include "Navigation.h"
#include "resource.h"
#include "DirectUI/DirectUI.h"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace std;
using namespace DirectUI;

HRESULT err = 0;

namespace {
    constexpr wchar_t Rectify11UninstallKey[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Rectify11";

    bool ReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* valueName, std::wstring& value) {
        DWORD bytes = 0;
        LSTATUS status = RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
        if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t)) return false;

        std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
        status = RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
        if (status != ERROR_SUCCESS) return false;

        if (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
        value = std::move(buffer);
        return !value.empty();
    }

    bool GetCurrentWindowsBuild(DWORD& build) {
        std::wstring buildText;
        if (!ReadRegistryString(
                HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                L"CurrentBuildNumber",
                buildText)) {
            return false;
        }

        wchar_t* end = nullptr;
        const unsigned long parsed = wcstoul(buildText.c_str(), &end, 10);
        if (!end || end == buildText.c_str() || *end != L'\0') return false;
        build = static_cast<DWORD>(parsed);
        return true;
    }

    bool IsSupportedRectify12Build(DWORD build) {
        // Windows 11 22H2 = 22621.x; 23H2 = 22631.x;
        // 24H2 = 26100.x; 25H2 = 26200.x.
        return build == 22621 || build == 22631 || build == 26100 || build == 26200;
    }

    bool HasRectify11V4Alpha(std::wstring& detectedVersion) {
        HKEY key = nullptr;
        LSTATUS status = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            Rectify11UninstallKey,
            0,
            KEY_READ | KEY_WOW64_64KEY,
            &key);
        if (status != ERROR_SUCCESS) {
            status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Rectify11UninstallKey, 0, KEY_READ, &key);
        }
        if (status != ERROR_SUCCESS) return false;

        wchar_t version[128]{};
        DWORD bytes = sizeof(version);
        status = RegGetValueW(key, nullptr, L"DisplayVersion", RRF_RT_REG_SZ, nullptr, version, &bytes);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS || version[0] == L'\0') return false;

        detectedVersion.assign(version);
        wchar_t* end = nullptr;
        const unsigned long major = wcstoul(version, &end, 10);
        return end != version && major >= 4;
    }

    bool RegistryKeyExists(HKEY root, const wchar_t* subkey) {
        HKEY key = nullptr;
        const LSTATUS status = RegOpenKeyExW(root, subkey, 0, KEY_READ, &key);
        if (status == ERROR_SUCCESS) RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }

    bool FindConflictingPatchedSystem(std::wstring& conflict) {
        // Use high-confidence markers only. Rectify11 is intentionally not checked here:
        // it is the required Rectify12 base and is explicitly allowed.
        if (RegistryKeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\AtlasOS")) {
            conflict = L"AtlasOS";
            return true;
        }

        wchar_t windowsDirectory[MAX_PATH]{};
        if (GetWindowsDirectoryW(windowsDirectory, ARRAYSIZE(windowsDirectory)) != 0) {
            std::filesystem::path atlasModules = std::filesystem::path(windowsDirectory) / L"AtlasModules";
            std::error_code ec;
            if (std::filesystem::exists(atlasModules, ec) && !ec) {
                conflict = L"AtlasOS";
                return true;
            }
        }

        wchar_t programFiles[MAX_PATH]{};
        if (ExpandEnvironmentStringsW(L"%ProgramFiles%", programFiles, ARRAYSIZE(programFiles)) != 0) {
            std::filesystem::path revisionTool =
                std::filesystem::path(programFiles) / L"Revision Tool" / L"revitool.exe";
            std::error_code ec;
            if (std::filesystem::exists(revisionTool, ec) && !ec) {
                conflict = L"ReviOS / Revision Tool";
                return true;
            }
        }

        return false;
    }

    bool ConfirmSystemPatchWarning() {
        int pressed = 0;
        const HRESULT result = TaskDialog(
            nullptr,
            nullptr,
            L"Rectify12",
            L"Back up your data before continuing",
            L"Rectify12 will apply an AtlasOS Playbook and patch Windows system components directly. "
            L"These changes can require restarts and may make an unsupported or interrupted Windows installation unbootable.\n\n"
            L"Save important files somewhere safe before continuing. Continue with Rectify12 setup?",
            TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
            TD_WARNING_ICON,
            &pressed);
        return SUCCEEDED(result) && pressed == IDYES;
    }

    int RunRectify12Preflight() {
        DWORD build = 0;
        if (!GetCurrentWindowsBuild(build) || !IsSupportedRectify12Build(build)) {
            const std::wstring detail =
                L"Rectify12 currently supports Windows 11 22H2 (build 22621), 23H2 (build 22631), "
                L"24H2 (build 26100), and 25H2 (build 26200).\n\n"
                L"Detected build: " + (build ? std::to_wstring(build) : std::wstring(L"unknown"));
            TaskDialog(
                nullptr,
                nullptr,
                L"Rectify12",
                L"Unsupported Windows version",
                detail.c_str(),
                TDCBF_OK_BUTTON,
                TD_ERROR_ICON,
                nullptr);
            MainLogger.WriteLine(L"Rectify12 preflight rejected Windows build " + std::to_wstring(build) + L".", -26100);
            return -26100;
        }

        std::wstring rectify11Version;
        if (!HasRectify11V4Alpha(rectify11Version)) {
            TaskDialog(
                nullptr,
                nullptr,
                L"Rectify12",
                L"Rectify11 v4 Alpha is required",
                L"Install Rectify11 v4 Alpha before running Rectify12. Rectify12 is designed to patch an existing Rectify11 v4 installation and will not continue without it.",
                TDCBF_OK_BUTTON,
                TD_ERROR_ICON,
                nullptr);
            MainLogger.WriteLine(L"Rectify12 preflight could not confirm a Rectify11 v4 prerequisite.", -4000);
            return -4000;
        }
        MainLogger.WriteLine(L"Detected Rectify11 prerequisite version " + rectify11Version + L".");

        std::wstring conflict;
        if (FindConflictingPatchedSystem(conflict)) {
            const std::wstring detail =
                L"Rectify12 detected an existing patched Windows environment: " + conflict +
                L".\n\nRemove that system modification and return Windows to the Rectify11 v4 base before running Rectify12.";
            TaskDialog(
                nullptr,
                nullptr,
                L"Rectify12",
                L"Conflicting system modification detected",
                detail.c_str(),
                TDCBF_OK_BUTTON,
                TD_ERROR_ICON,
                nullptr);
            MainLogger.WriteLine(L"Rectify12 preflight detected conflicting system modification: " + conflict, -4090);
            return -4090;
        }

        if (!ConfirmSystemPatchWarning()) {
            MainLogger.WriteLine(L"Rectify12 installation was cancelled at the system-patch backup warning.", ERROR_CANCELLED);
            return ERROR_CANCELLED;
        }

        return ERROR_SUCCESS;
    }
}

bool CheckVer(int build) {
    OSVERSIONINFOEXA vInfo{};
    vInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
    vInfo.dwBuildNumber = build;
    DWORDLONG dwlConditionMask = 0;
    const int op = VER_GREATER_EQUAL;
    VER_SET_CONDITION(dwlConditionMask, VER_BUILDNUMBER, op);
    return VerifyVersionInfoA(&vInfo, VER_BUILDNUMBER, dwlConditionMask) != FALSE;
}

bool GetUserAppMode() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);

    // Preserve the historical Rectify behaviour if the preference is unavailable:
    // dark mode is the safer visual default for the current installer resources.
    return result == ERROR_SUCCESS && value != 0;
}

int InitPages() {
    for (int i = 1; i < MAXPAGE; i++) {
        Element* page = nullptr;
        MainLogger.WriteLine(L"Setting XML parser from resource: " + to_wstring(IDR_UIFILE1 + i));
        err = pParser->SetXMLFromResource(IDR_UIFILE1 + i, hinst, hinst);
        if (FAILED(err)) {
            MainLogger.WriteLine(L"failed to set XML parser from resource: " + to_wstring(IDR_UIFILE1 + i), err);
            return err;
        }

        std::wstring n = L"page" + std::to_wstring(i);
        MainLogger.WriteLine(L"Finding Element with ID: " + n);
        Element* pContainer = pMain->FindDescendent(StrToID((UCString)n.c_str()));
        if (!pContainer) {
            err = -30;
            MainLogger.WriteLine(L"Element with ID: " + n + L" not found", err);
            return err;
        }

        MainLogger.WriteLine(L"Creating PageMain Element inside " + n);
        err = pParser->CreateElement((UCString)L"PageMain", pContainer, NULL, NULL, &page);
        if (FAILED(err) || !page) {
            if (SUCCEEDED(err)) err = -33;
            MainLogger.WriteLine(L"Cannot create PageMain Element inside " + n, err);
            return err;
        }

        std::wstring anim = L"animator" + std::to_wstring(i);
        MainLogger.WriteLine(L"Finding Element with ID: " + anim);
        Element* pAnimator = pMain->FindDescendent(StrToID((UCString)anim.c_str()));
        if (!pAnimator) {
            err = -31;
            MainLogger.WriteLine(L"Element with ID: " + anim + L" not found", err);
            return err;
        }

        MainLogger.WriteLine(L"Changing " + n + L"'s visibility");
        err = pContainer->SetVisible(false);
        if (FAILED(err)) {
            err = -32;
            MainLogger.WriteLine(L"Failed to change " + n + L"'s visibility", err);
            return err;
        }

        MainLogger.WriteLine(L"Adding page " + to_wstring(i) + L" to page list");
        animArr.push_back(pAnimator);
        pageArr.push_back(pContainer);
        MainLogger.WriteLine(L"\n");
    }
    MainLogger.WriteLine(L"InitPages() completed", err);
    return err;
}

int InitControls() {
    progressbar = pMain->FindDescendent(StrToID((UCString)L"progress"));
    Nxt = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"next"));
    Bck = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"back"));
    browsebtn = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"browsebtn"));

    TouchButton* SYS = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"PatchSystem"));
    TouchButton* ISO = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"PatchISO"));
    waitAnimation = (RichText*)pMain->FindDescendent(StrToID((UCString)L"WaitAnimation"));
    restartWaitAnimation = (RichText*)pMain->FindDescendent(StrToID((UCString)L"RestartWaitAnimation"));
    TouchButton* credits = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"credits"));
    TouchButton* Full = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"Full"));
    TouchButton* None = (TouchButton*)pMain->FindDescendent(StrToID((UCString)L"None"));
    progressmeter = (RichText*)pMain->FindDescendent(StrToID((UCString)L"R11Progress"));
    Countdown = (RichText*)pMain->FindDescendent(StrToID((UCString)L"RestartCountdown"));

    if (!progressbar || !Nxt || !Bck || !browsebtn || !SYS || !ISO || !waitAnimation ||
        !restartWaitAnimation || !credits || !Full || !None || !progressmeter || !Countdown) {
        MainLogger.WriteLine(L"One or more required installer controls are missing.", -35);
        return -35;
    }

    Nxt->AddListener(new EventListener(NavNext));
    Bck->AddListener(new EventListener(NavBack));
    SYS->AddListener(new EventListener(NavSYS));
    ISO->AddListener(new EventListener(NavISO));

    void (*fArr[])(Element*, Event*) = { HandleThemesChk, HandleAsdfChk, HandleWinverChk, HandleExplorerChk };
    for (int i = 0; i < 4; i++) {
        const std::wstring ws = L"chk" + std::to_wstring(i + 1);
        auto* tch = (TouchCheckBox*)pMain->FindDescendent(StrToID((UCString)ws.c_str()));
        if (!tch) {
            MainLogger.WriteLine(L"Required checkbox " + ws + L" was not found.", -36);
            return -36;
        }
        tch->AddListener(new EventListener(fArr[i]));
    }

    Full->AddListener(new EventListener(NavFull));
    None->AddListener(new EventListener(NavNone));
    credits->AddListener(new EventListener(OpenCredits));
    return S_OK;
}

int ChangeSheet() {
    MainLogger.WriteLine(L"Setting parser object to resource: " + to_wstring(IDR_UIFILE1));
    err = pParser->SetXMLFromResource((UINT)IDR_UIFILE1, hinst, hinst);
    if (FAILED(err)) {
        MainLogger.WriteLine(L"failed in setting parser object to resource: " + to_wstring(IDR_UIFILE1), err);
        return err;
    }
    Value* v{};
    StyleSheet* s{};

    if (GetUserAppMode()) {
        MainLogger.WriteLine(L"Light mode detected\nGetting Stylesheet information...");
        err = pParser->GetSheet((UCString)L"S", &v);
        InstallFlags[L"LIGHTTHEME"] = true;
        InstallFlags[L"DARKTHEME"] = false;
    }
    else {
        MainLogger.WriteLine(L"Dark mode detected\nGetting Stylesheet information...");
        err = pParser->GetSheet((UCString)L"Sdark", &v);
        InstallFlags[L"LIGHTTHEME"] = false;
        InstallFlags[L"DARKTHEME"] = true;
    }
    if (FAILED(err) || !v) {
        if (SUCCEEDED(err)) err = -34;
        MainLogger.WriteLine(L"Failed to get Stylesheet information.", err);
        return err;
    }
    MainLogger.WriteLine(L"Creating Stylesheet");
    s = v->GetStyleSheet();
    if (!s) {
        err = -34;
        MainLogger.WriteLine(L"Failed to create Stylesheet.", err);
        return err;
    }
    MainLogger.WriteLine(L"Applying stylesheet");
    err = pMain->SetSheet(s);
    if (FAILED(err)) {
        MainLogger.WriteLine(L"Failed to apply stylesheet", err);
        return err;
    }
    MainLogger.WriteLine(L"ChangeSheet() completed", err);
    return err;
}

int InitInstaller() {
    err = RunRectify12Preflight();
    if (err != ERROR_SUCCESS) return err;

    // Don't equate reachability of one website with system connectivity. Local
    // payload steps can still succeed behind proxies/firewalls, while individual
    // online child processes must report their own failures to the install engine.
    MainLogger.WriteLine(L"Initializing pages...\n\n\n");
    err = InitPages();
    if (FAILED(err)) {
        MainLogger.WriteLine(L"One or more pages failed to initialize correctly.\n", err);
        return err;
    }

    err = InitControls();
    if (FAILED(err)) {
        MainLogger.WriteLine(L"One or more controls failed to initialize correctly.\n", err);
        return err;
    }

    err = ChangeSheet();
    if (FAILED(err)) {
        MainLogger.WriteLine(L"Failed to change stylesheet.", err);
        return err;
    }
    return err;
}
