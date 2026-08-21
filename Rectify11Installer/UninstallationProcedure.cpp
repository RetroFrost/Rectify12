#include "UninstallationProcedure.h"
#include "InstallationProcedure.h"
#include "resource.h"
#include "framework.h"
#include "Navigation.h"
#include "ProductInfo.h"

wchar_t exepath[MAX_PATH];

void RemoveWHMods() {
	if (InstallFlags[L"INSTALLTHEMES"] == true) {
		InstallationLogger.WriteLine(L"Uninstalling sound hook...");
		StringCchPrintf(exepath, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
		wchar_t args[] = L"/c reg delete HKLM\\SOFTWARE\\Windhawk\\Engine\\Mods\\logon-logoff-shutdown-sounds /f";
		RunEXE(exepath, args);

		InstallationLogger.WriteLine(L"Uninstalling titlebar fix...");
		wchar_t args3[] = L"/c reg delete HKLM\\SOFTWARE\\Windhawk\\Engine\\Mods\\local@titlebar-fix /f";
		RunEXE(exepath, args3);
	}

	if (InstallFlags[L"INSTALLICONS"] == true) {
		InstallationLogger.WriteLine(L"Uninstalling resource redirect...");
		StringCchPrintf(exepath, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
		wchar_t args[] = L"/c reg delete HKLM\\SOFTWARE\\Windhawk\\Engine\\Mods\\icon-resource-redirect /f";
		RunEXE(exepath, args);
	}

	if (InstallFlags[L"INSTALLASDF"] == true) {
		InstallationLogger.WriteLine(L"Uninstalling Accent Colorizer startup entry...");
		StringCchPrintf(exepath, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
		wchar_t args[] = L"/c reg delete HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run /f /v ASDF";
		RunEXE(exepath, args);
	}

	if (InstallFlags[L"INSTALLWINVERSHUTDOWN"] == true) {
		InstallationLogger.WriteLine(L"Uninstalling winver and shutdown enhancements...");
		StringCchPrintf(exepath, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
		wchar_t args[] = L"/c reg delete HKLM\\SOFTWARE\\Windhawk\\Engine\\Mods\\winvershutdown /f";
		RunEXE(exepath, args);
	}

	if (InstallFlags[L"INSTALLEXP"] == true) {
		InstallationLogger.WriteLine(L"Uninstalling Explorer tweaks...");
		StringCchPrintf(exepath, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
		wchar_t args[] = L"/c reg delete HKLM\\SOFTWARE\\Windhawk\\Engine\\Mods\\windows-11-file-explorer-styler /f";
		RunEXE(exepath, args);
	}
}

void RemoveSecureUX() {
	if (InstallFlags[L"INSTALLTHEMES"] == true) {
		// SecureUxTheme can be shared with other custom themes, so Rectify12 must not
		// remove it blindly. A future ownership record will allow safe removal when
		// Rectify12 was the component that installed it.
		InstallationLogger.WriteLine(L"Leaving SecureUxTheme installed to avoid removing a shared dependency.");
	}
}

void FinaliseUninstall() {
	if (InstallFlags[L"INSTALLTHEMES"] == true) {
		InstallationLogger.WriteLine(L"Restoring a Windows theme on next sign-in...");

		if (InstallFlags[L"LIGHTTHEME"] == true) {
			StringCchPrintf(exepath, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
			wchar_t args[] = L"/c reg add HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce /v ApplyTheme /t REG_SZ /d \"cmd.exe /c %systemroot%\\resources\\themes\\aero.theme\"";
			RunEXE(exepath, args);
		}
		if (InstallFlags[L"DARKTHEME"] == true) {
			StringCchPrintf(exepath, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
			wchar_t args[] = L"/c reg add HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce /v ApplyTheme /t REG_SZ /d \"cmd.exe /c %systemroot%\\resources\\themes\\dark.theme\"";
			RunEXE(exepath, args);
		}
	}

	if (RegDeleteTreeW(HKEY_LOCAL_MACHINE, Rectify12::UninstallRegistryPath) != ERROR_SUCCESS) {
		InstallationLogger.WriteLine(L"Rectify12 uninstall registration was already absent or could not be removed.");
	}
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Rectify12");

	// Per-user visual preferences are intentionally retained. This means a reinstall
	// restores the user's Rectify12 effects choices instead of silently resetting them.
	InstallationLogger.WriteLine(L"Rectify12 product registration removed; user visual preferences retained.");
}
