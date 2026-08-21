// Rectify12 installer entry point. The source filename is retained temporarily
// to keep the inherited Visual Studio project layout stable during the rewrite.
#include "framework.h"
#include "Rectify11Installer.h"
#include "resource.h"

#include "DirectUI/DirectUI.h"

#include "Initr11.h"
#include "InitUninst.h"
#include "Navigation.h"
#include "InstallerEngine.h"
#include "InstallationProcedure.h"
#include "Logger.h"
#include "MiscWindow.h"
#include "EffectsEngine.h"
#include "ProductInfo.h"

using namespace DirectUI;
using namespace std;

TouchButton* defenderbtn;
TouchButton* browsebtn;

RichText* waitAnimation;
RichText* restartWaitAnimation;

RichText* progressmeter;
RichText* Countdown;

DUIXmlParser* pParser;
HINSTANCE hinst;

HWNDElement* HElement;

Value* V;
WNDPROC WndProc;

NativeHWNDHost* pwnd;
Element* pMain;

vector<Element*> pageArr;
vector<Element*> animArr;

Element* progressbar;
TouchButton* Nxt;
TouchButton* Bck;

TouchButton* notes;
TouchButton* credits;

Logger MainLogger;
Logger NavLogger;
Logger InstallationLogger;

bool uninstall = false;

wchar_t currdir[MAX_PATH] = {};
// r11dir still refers to the inherited payload folder packaged beside the installer.
// It will be renamed when the payload archive itself is migrated to Rectify12.
wchar_t r11dir[MAX_PATH] = {};
wchar_t r11targetdir[MAX_PATH] = {};
wchar_t windir[MAX_PATH] = {};

int nxt = 1;
int curr = 0;
int currframe = 112;
unsigned long dKey;

std::map<std::wstring, bool> InstallFlags;

void NavNext(Element* elem, Event* iev) {
    if (iev->type != TouchButton::Click) return;

    const bool isRestartPage =
        (!uninstall && curr == RESTARTPAGE) ||
        (uninstall && curr == (UNINSTALLRESTARTPAGE - MAXPAGE) + 1);
    if (isRestartPage) {
        // Prevent the countdown worker from issuing a second reboot request.
        IEngineWrapper::animate.store(false);
        IEngineWrapper::Ttime.store(0);
        SetupComplete();
        return;
    }

    Navigate();
}

void NavBack(Element* elem, Event* iev) {
    if (iev->type == TouchButton::Click) {
        if (curr == DEFENDERPAGE) {
            PostMessageW(pwnd->GetHWND(), WM_CLOSE, 0, 0);
            return;
        }
        NavigateBack();
    }
}

void NavISO(Element* elem, Event* iev) {
    if (iev->type == TouchButton::Click) {
        Navigate();
    }
}

void NavSYS(Element* elem, Event* iev) {
    if (iev->type == TouchButton::Click) {
        nxt = 4;
        Navigate();
    }
}

void NavFull(Element* elem, Event* iev) {
    if (iev->type == TouchButton::Click) {
        Navigate();
        InstallFlags[L"INSTALLICONS"] = true;
    }
}

void NavNone(Element* elem, Event* iev) {
    if (iev->type == TouchButton::Click) {
        Navigate();
        InstallFlags[L"INSTALLICONS"] = false;
    }
}

void HandleThemesChk(Element* elem, Event* iev) {
    TouchCheckBox* tch = (TouchCheckBox*)elem;
    if (iev->type == TouchButton::Click) {
        if (tch->GetCheckedState() == CheckedStateFlags_CHECKED) {
            tch->SetCheckedState(CheckedStateFlags_NONE);
            InstallFlags[L"INSTALLTHEMES"] = false;
        }
        else {
            tch->SetCheckedState(CheckedStateFlags_CHECKED);
            InstallFlags[L"INSTALLTHEMES"] = true;
        }
    }
}

void HandleAsdfChk(Element* elem, Event* iev) {
    TouchCheckBox* tch = (TouchCheckBox*)elem;
    if (iev->type == TouchButton::Click) {
        if (tch->GetCheckedState() == CheckedStateFlags_CHECKED) {
            tch->SetCheckedState(CheckedStateFlags_NONE);
            InstallFlags[L"INSTALLASDF"] = false;
        }
        else {
            tch->SetCheckedState(CheckedStateFlags_CHECKED);
            InstallFlags[L"INSTALLASDF"] = true;
        }
    }
}

void HandleWinverChk(Element* elem, Event* iev) {
    TouchCheckBox* tch = (TouchCheckBox*)elem;
    if (iev->type == TouchButton::Click) {
        if (tch->GetCheckedState() == CheckedStateFlags_CHECKED) {
            tch->SetCheckedState(CheckedStateFlags_NONE);
            InstallFlags[L"INSTALLWINVERSHUTDOWN"] = false;
        }
        else {
            tch->SetCheckedState(CheckedStateFlags_CHECKED);
            InstallFlags[L"INSTALLWINVERSHUTDOWN"] = true;
        }
    }
}

void HandleExplorerChk(Element* elem, Event* iev) {
    TouchCheckBox* tch = (TouchCheckBox*)elem;
    if (iev->type == TouchButton::Click) {
        if (tch->GetCheckedState() == CheckedStateFlags_CHECKED) {
            tch->SetCheckedState(CheckedStateFlags_NONE);
            InstallFlags[L"INSTALLEXP"] = false;
        }
        else {
            tch->SetCheckedState(CheckedStateFlags_CHECKED);
            InstallFlags[L"INSTALLEXP"] = true;
        }
    }
}

void HandleIconChk(Element* elem, Event* iev) {
    TouchCheckBox* tch = (TouchCheckBox*)elem;
    if (iev->type == TouchButton::Click) {
        if (tch->GetCheckedState() == CheckedStateFlags_CHECKED) {
            tch->SetCheckedState(CheckedStateFlags_NONE);
            InstallFlags[L"INSTALLICONS"] = false;
        }
        else {
            tch->SetCheckedState(CheckedStateFlags_CHECKED);
            InstallFlags[L"INSTALLICONS"] = true;
        }
    }
}

void SetBackdrop() {
    if (!pwnd) return;

    Rectify12::Effects::WindowEffectOptions options;
    options.immersiveDark = !GetUserAppMode();
    options.extendFrame = true;
    options.backdrop = Rectify12::Effects::BackdropKind::Acrylic;

    const HRESULT effectResult = Rectify12::Effects::ApplyWindowEffects(pwnd->GetHWND(), options);
    if (FAILED(effectResult)) {
        // Effects are an enhancement, not a reason to make the installer unusable.
        MainLogger.WriteLine(L"Rectify12 effects engine could not apply the preferred installer backdrop.", effectResult);
    }
}

void OpenCredits(Element* elem, Event* iev) {
    if (iev->type == TouchButton::Click && pMain) {
        InitMiscWindow(false, pMain->GetSheet(), hinst);
    }
}

bool DetectUninstall() {
    HKEY hKey;
    const DWORD lResult = RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        Rectify12::UninstallRegistryPath,
        0,
        KEY_READ,
        &hKey);

    if (lResult == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

LRESULT CALLBACK SubclassWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_SETTINGCHANGE: {
            if (lParam && wcscmp((LPCWSTR)lParam, L"ImmersiveColorSet") == 0) {
                HRESULT sheetResult = ChangeSheet();
                if (FAILED(sheetResult)) {
                    MainLogger.WriteLine(L"Failed to change stylesheet.", sheetResult);
                    return sheetResult;
                }
                SetBackdrop();
            }
            break;
        }
        case WM_UPDATEANIMATIONFRAME: {
            if (!waitAnimation) break;
            V = Value::CreateString((UCString)MAKEINTRESOURCE(currframe), hinst);
            if (V) waitAnimation->SetValue(RichText::ContentProp, 2, V);
            currframe++;
            if (currframe == 230) currframe = 112;
            break;
        }
        case WM_UPDATERESTARTANIMATIONFRAME: {
            if (!restartWaitAnimation) break;
            V = Value::CreateString((UCString)MAKEINTRESOURCE(currframe), hinst);
            if (V) restartWaitAnimation->SetValue(RichText::ContentProp, 2, V);
            currframe++;
            if (currframe == 230) currframe = 112;
            break;
        }
        case WM_UPDATEPROGRESS: {
            if (!progressmeter) break;
            std::wstring progressText;
            {
                std::lock_guard<std::mutex> lock(IEngineWrapper::progressMutex);
                progressText = IEngineWrapper::currprogress;
            }
            progressmeter->SetContentString((UCString)progressText.c_str());
            break;
        }
        case WM_UPDATECOUNTDOWN: {
            if (!Countdown) break;
            std::wstring ws = L"Restarting in: " + std::to_wstring(IEngineWrapper::Ttime.load()) + L" seconds";
            Countdown->SetContentString((UCString)ws.c_str());
            break;
        }
        case WM_SETUPCOMPLETE: {
            Navigate();
            break;
        }
        case WM_SETUPFAILED: {
            IEngineWrapper::animate.store(false);
            TaskDialog(
                hWnd,
                nullptr,
                L"Rectify12 setup",
                L"Setup operation stopped",
                L"A required setup step failed. Rectify12 will not continue to the success or restart screen. Review Installation.log, correct the reported problem, then retry setup.",
                TDCBF_OK_BUTTON,
                TD_ERROR_ICON,
                nullptr);
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            break;
        }
        case WM_MOVE: {
            SetWindowPos(hWnd, NULL, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }

    return WndProc ? CallWindowProc(WndProc, hWnd, uMsg, wParam, lParam) : DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    InstallFlags[L"NONE"] = true;
    InstallFlags[L"INSTALLICONS"] = true;
    InstallFlags[L"INSTALLTHEMES"] = true;
    InstallFlags[L"INSTALLASDF"] = true;
    InstallFlags[L"INSTALLEXP"] = true;
    InstallFlags[L"INSTALLWINVERSHUTDOWN"] = true;
    InstallFlags[L"AMD64"] = true;
    InstallFlags[L"ARM64"] = false;

    USHORT processMachine = 0;
    USHORT nativeMachine = 0;

    HANDLE hProcess = GetCurrentProcess();

    if (IsWow64Process2(hProcess, &processMachine, &nativeMachine)) {
        switch (nativeMachine) {
        case IMAGE_FILE_MACHINE_AMD64: {
            InstallFlags[L"AMD64"] = true;
            InstallFlags[L"ARM64"] = false;
            break;
        }
        case IMAGE_FILE_MACHINE_ARM64: {
            InstallFlags[L"AMD64"] = false;
            InstallFlags[L"ARM64"] = true;
            break;
        }
        }
    }

    HRESULT err = 0;
    if (GetCurrentDirectoryW(MAX_PATH, currdir) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    wstring ws(currdir);

    if (GetEnvironmentVariableW(L"systemroot", windir, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    // The payload directory still uses the inherited name until Files.7z is repackaged.
    if (FAILED(StringCchPrintfW(r11dir, MAX_PATH, L"%s\\Rectify11", currdir)) ||
        FAILED(StringCchPrintfW(r11targetdir, MAX_PATH, L"%s\\%s", windir, Rectify12::InstallFolder))) {
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }

    MainLogger.StartLogger((ws + L"\\Initialization.log").c_str());
    NavLogger.StartLogger((ws + L"\\Navigation.log").c_str());
    InstallationLogger.StartLogger((ws + L"\\Installation.log").c_str());

    wchar_t fPathOld[MAX_PATH]{};
    wchar_t fPath[MAX_PATH]{};
    if (SUCCEEDED(StringCchPrintfW(fPathOld, MAX_PATH, L"%s\\segoe_r11.ttf", currdir)) &&
        SUCCEEDED(StringCchPrintfW(fPath, MAX_PATH, L"%s\\segoe_r11.ttf", windir))) {
        if (!CopyFileW(fPathOld, fPath, FALSE)) {
            MainLogger.WriteLine(L"Could not stage the installer font. Win32 error: " + std::to_wstring(GetLastError()));
        }
        else if (AddFontResourceW(fPath) == 0) {
            MainLogger.WriteLine(L"Windows could not load the staged installer font.");
        }
    }

    hinst = hInstance;
    pageArr.push_back(NULL);
    animArr.push_back(NULL);

    uninstall = DetectUninstall();

    err = InitProcessPriv(14, NULL, NULL, false);
    MainLogger.WriteLine(L"InitProcessPriv() completed", err);
    if (FAILED(err)) {
        MainLogger.WriteLine(L"DirectUI initialization failed.");
        return err;
    }

    err = InitThread(2);
    MainLogger.WriteLine(L"InitThread() completed", err);
    if (FAILED(err)) {
        MainLogger.WriteLine(L"InitThread() failed.");
        return err;
    }

    err = RegisterAllControls();
    MainLogger.WriteLine(L"RegisterAllControls() completed", err);
    if (FAILED(err)) {
        MainLogger.WriteLine(L"Failed to register DirectUI controls.");
        return err;
    }

    err = NativeHWNDHost::Create((UCString)L"", NULL, NULL,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, WS_OVERLAPPED | WS_CAPTION | WS_MAXIMIZE, 0, &pwnd);
    MainLogger.WriteLine(L"NativeHWNDHost::Create() completed", err);
    if (FAILED(err) || !pwnd) {
        if (SUCCEEDED(err)) err = E_POINTER;
        MainLogger.WriteLine(L"Failed to create installer window.");
        return err;
    }

    SetBackdrop();
    SetLastError(ERROR_SUCCESS);
    WndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(pwnd->GetHWND(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubclassWindowProc)));
    if (!WndProc && GetLastError() != ERROR_SUCCESS) {
        err = HRESULT_FROM_WIN32(GetLastError());
        MainLogger.WriteLine(L"Failed to subclass installer window.", err);
        return err;
    }

    err = DUIXmlParser::Create(&pParser, NULL, NULL, NULL, NULL);
    MainLogger.WriteLine(L"DUIXmlParser::Create() completed", err);
    if (FAILED(err) || !pParser) {
        if (SUCCEEDED(err)) err = E_POINTER;
        MainLogger.WriteLine(L"Failed to create parser.");
        return err;
    }

    err = pParser->SetXMLFromResource((UINT)IDR_UIFILE1, hInstance, hInstance);
    MainLogger.WriteLine(L"pParser->SetXMLFromResource() complete", err);
    if (FAILED(err)) {
        MainLogger.WriteLine(L"Failed to set parser xml to resource. Please verify that all the resources are present and valid.");
        return err;
    }

    err = HWNDElement::Create(pwnd->GetHWND(), true, 0, NULL, &dKey, (Element**)&HElement);
    MainLogger.WriteLine(L"HWNDElement::Create() complete", err);
    if (FAILED(err) || !HElement) {
        if (SUCCEEDED(err)) err = E_POINTER;
        MainLogger.WriteLine(L"Failed to create host hwndelement");
        return err;
    }

    err = pParser->CreateElement((UCString)L"Main", HElement, NULL, NULL, &pMain);
    MainLogger.WriteLine(L"pParser->CreateElement() completed", err);
    if (FAILED(err) || !pMain) {
        if (SUCCEEDED(err)) err = E_POINTER;
        MainLogger.WriteLine(L"Failed to copy element from parser to HwndElement");
        return err;
    }

    pMain->SetVisible(true);
    pMain->EndDefer(dKey);
    pwnd->Host(pMain);

    if (!uninstall) {
        err = InitInstaller();
        MainLogger.WriteLine(L"InitInstaller() completed", err);
        if (FAILED(err)) {
            MainLogger.WriteLine(L"Installer Initialisation failed.");
            return err;
        }
    }
    else {
        err = InitUninstaller();
        MainLogger.WriteLine(L"InitUninstaller() completed", err);
        if (FAILED(err)) {
            MainLogger.WriteLine(L"Uninstaller Initialisation failed.");
            return err;
        }
    }
    Navigate();

    pwnd->ShowWindow(SW_SHOW);

    StartMessagePump();
    UnInitProcessPriv(NULL);

    return err;
}
