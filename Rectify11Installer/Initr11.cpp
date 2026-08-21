#include "framework.h"
#include "Initr11.h"
#include "Rectify11Installer.h"
#include "Navigation.h"
#include "resource.h"
#include "DirectUI/DirectUI.h"

using namespace std;
using namespace DirectUI;

HRESULT err = 0;

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
    if (!CheckVer(21343)) {
        err = -21343;
        TaskDialog(NULL, NULL, L"Rectify12", L"Unsupported Windows version", L"Windows 10 build 21343 or newer is required.", TDCBF_OK_BUTTON, TD_ERROR_ICON, NULL);
        MainLogger.WriteLine(L"This Windows Build is not supported. Windows 10 Build 21343 and above is required.", err);
        return err;
    }

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
