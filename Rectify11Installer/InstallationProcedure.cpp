#include "InstallationProcedure.h"
#include "Navigation.h"
#include "ProductInfo.h"

wchar_t path[MAX_PATH];
wchar_t cmd[1024];

std::wstring copy_list[] = {
L"%r11files%\\Mods|%ProgramData%\\Windhawk\\Engine\\Mods|NONE",
L"%r11files%\\Rectify11|%systemroot%\\Rectify12|INSTALLICONS",
L"%r11files%\\System32|%systemroot%\\System32|NONE",
L"%r11files%\\themes|%systemroot%\\resources\\themes|INSTALLTHEMES",
L"%r11files%\\wallpapers|%systemroot%\\web\\wallpaper\\rectified|INSTALLTHEMES",
L"%r11files%\\cursors|%systemroot%\\cursors|INSTALLTHEMES",
L"%r11files%\\media|%systemroot%\\media\\rectified|INSTALLTHEMES",
};

std::wstring install_list[] = {
L"msiexec.exe /i \"%r11files%\\SecureUxTheme_x64.msi\" /quiet /norestart|INSTALLTHEMES|AMD64",
L"msiexec.exe /i \"%r11files%\\SecureUxTheme_ARM64.msi\" /quiet /norestart|INSTALLTHEMES|ARM64",
L"%r11files%\\windhawk_setup_offline.exe /S|NONE",
L"%r11files%\\SymChk\\symchk.exe \"%systemroot%\\Explorer.exe\" /s SRV*%programdata%\\Windhawk\\Engine\\symbols\\*http://msdl.microsoft.com/download/symbols|NONE",
L"%r11files%\\SymChk\\symchk.exe \"%systemroot%\\system32\\Shlwapi.dll\" /s SRV*%programdata%\\Windhawk\\Engine\\symbols\\*http://msdl.microsoft.com/download/symbols|NONE"
};

std::wstring mod_list[] = {
L"%r11files%\\Regs\\resourcepatch.reg|INSTALLICONS|AMD64",
L"%r11files%\\Regs\\resourcepatchARM.reg|INSTALLICONS|ARM64",
L"%r11files%\\Regs\\soundWH.reg|INSTALLTHEMES|AMD64",
L"%r11files%\\Regs\\soundWHARM.reg|INSTALLTHEMES|ARM64",
L"%r11files%\\Regs\\winvershutdown.reg|INSTALLWINVERSHUTDOWN|AMD64",
L"%r11files%\\Regs\\winvershutdownARM.reg|INSTALLWINVERSHUTDOWN|ARM64",
L"%r11files%\\Regs\\titlebarfix.reg|INSTALLTHEMES|AMD64",
L"%r11files%\\Regs\\titlebarfixARM.reg|INSTALLTHEMES|ARM64",
L"%r11files%\\Regs\\topbar.reg|INSTALLEXP|AMD64",
L"%r11files%\\Regs\\topbarARM.reg|INSTALLEXP|ARM64",
L"%r11files%\\Regs\\Light.reg|LIGHTTHEME",
L"%r11files%\\Regs\\Dark.reg|DARKTHEME",
L"%r11files%\\Regs\\sound.reg|INSTALLTHEMES",
L"%r11files%\\Regs\\fonts.reg|NONE",
L"%r11files%\\Regs\\ASDF.reg|INSTALLASDF"
};

void RunEXE(wchar_t exe[], wchar_t args[]) {
	STARTUPINFO startup_info{};
	PROCESS_INFORMATION process_info{};
	startup_info.cb = sizeof(STARTUPINFO);

	if (exe != NULL) InstallationLogger.WriteLine(L"Path: " + std::wstring(exe));
	if (args != NULL) InstallationLogger.WriteLine(L"Arguments: " + std::wstring(args));

	BOOL rv = CreateProcess(exe, args, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup_info, &process_info);
	if (rv) {
		InstallationLogger.WriteLine(L"Created process successfully.");
		CloseHandle(process_info.hThread);

		const DWORD waitResult = WaitForSingleObject(process_info.hProcess, 60000 * 5);
		if (waitResult == WAIT_TIMEOUT) {
			InstallationLogger.WriteLine(L"Process timed out after five minutes.");
		}
		else {
			DWORD exitCode = 0;
			if (GetExitCodeProcess(process_info.hProcess, &exitCode)) {
				InstallationLogger.WriteLine(L"Process exited with code " + std::to_wstring(exitCode));
			}
		}

		CloseHandle(process_info.hProcess);
	}
	else {
		InstallationLogger.WriteLine(L"Failed to create process. Win32 error: " + std::to_wstring(GetLastError()));
	}
}

void extractFiles() {
	StringCchPrintf(cmd, 1024, L"\"%s\\7z.exe\" x -aoa -o\"%s\" \"%s\\Files.7z\" -y", r11dir, r11dir, r11dir);
	InstallationLogger.WriteLine(L"Extracting files...");
	RunEXE(NULL, cmd);
}

void parseEnvironmentVariablePath(std::wstring& value) {
	int f = value.find(L'%');
	if (f != std::wstring::npos) {
		int t = value.find(L'%', f + 1);
		if (t != std::wstring::npos) {
			wchar_t tmp[MAX_PATH]{};
			std::wstring variableFromPath = value.substr(f + 1, t - f - 1);

			if (wcscmp(variableFromPath.c_str(), L"r11files") == 0) {
				value.replace(f, t - f + 1, r11dir);
			}
			else if (GetEnvironmentVariable(variableFromPath.c_str(), tmp, MAX_PATH) > 0) {
				value.replace(f, t - f + 1, tmp);
			}
		}
	}
}

void MoveFileCmd(const wchar_t* src, const wchar_t* dest) {
	StringCchPrintf(cmd, 1024, L"/c echo d | xcopy \"%s\" \"%s\" /e /y", src, dest);
	StringCchPrintf(path, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
	RunEXE(path, cmd);
}

void RegisterRegFile(const wchar_t* regpath) {
	StringCchPrintf(cmd, 1024, L"/c reg import \"%s\"", regpath);
	StringCchPrintf(path, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
	RunEXE(path, cmd);
}

std::vector<std::wstring> ParseDelimiterString(std::wstring ws) {
	std::vector<std::wstring> strlist;
	int del = 0;
	while ((del = ws.find('|')) != std::wstring::npos) {
		strlist.push_back(ws.substr(0, del));
		ws.erase(ws.begin(), ws.begin() + del + 1);
	}
	strlist.push_back(ws);
	return strlist;
}

void MoveFilesToTarget() {
	std::wstring ws;
	InstallationLogger.WriteLine(L"Copying files...");
	for (int i = 0; i < (sizeof(copy_list) / sizeof(std::wstring)); i++) {
		ws = copy_list[i];
		std::vector<std::wstring> pathlist(ParseDelimiterString(ws));
		bool alltrue = true;
		for (int j = 2; j < pathlist.size(); j++) {
			if (InstallFlags[pathlist[j]] == false) { alltrue = false; break; }
		}
		if (alltrue) {
			for (int j = 0; j < pathlist.size(); j++) {
				parseEnvironmentVariablePath(pathlist[j]);
			}
			InstallationLogger.WriteLine(L"Copying files \"" + pathlist[0] + L"\" to \"" + pathlist[1] + L"\" based on condition \"" + pathlist[2] + L"\"");
			MoveFileCmd(pathlist[0].c_str(), pathlist[1].c_str());
		}
	}
}

void InstallPrograms() {
	std::wstring ws;
	InstallationLogger.WriteLine(L"Installing programs...");
	for (int i = 0; i < (sizeof(install_list) / sizeof(std::wstring)); i++) {
		ws = install_list[i];
		std::vector<std::wstring> progpath(ParseDelimiterString(ws));
		bool alltrue = true;
		for (int j = 1; j < progpath.size(); j++) {
			if (InstallFlags[progpath[j]] == false) { alltrue = false; break; }
		}
		if (alltrue) {
			parseEnvironmentVariablePath(progpath[0]);
			StringCchPrintf(cmd, 1024, L"/c \"%s\"", progpath[0].c_str());
			StringCchPrintf(path, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
			RunEXE(path, cmd);
		}
	}
}

void InstallFonts() {
	StringCchPrintf(path, MAX_PATH, L"%s\\Fonts\\*", r11dir);

	WIN32_FIND_DATA FindFileData{};
	InstallationLogger.WriteLine(L"Installing fonts...");
	HANDLE hFind = FindFirstFile(path, &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE) {
		InstallationLogger.WriteLine(L"No payload fonts were found.");
		return;
	}

	do {
		if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			StringCchPrintf(path, MAX_PATH, L"%s\\Fonts\\%s", r11dir, FindFileData.cFileName);
			StringCchPrintf(cmd, MAX_PATH, L"%s\\Fonts\\%s", windir, FindFileData.cFileName);
			CopyFile(path, cmd, false);
			AddFontResource(cmd);
		}
	} while (FindNextFileW(hFind, &FindFileData));

	FindClose(hFind);
}

void RegisterWHMods() {
	std::wstring ws;
	InstallationLogger.WriteLine(L"Registering Windhawk modules and registry files...");
	for (int i = 0; i < (sizeof(mod_list) / sizeof(std::wstring)); i++) {
		ws = mod_list[i];
		std::vector<std::wstring> regpath(ParseDelimiterString(ws));
		bool alltrue = true;
		for (int j = 1; j < regpath.size(); j++) {
			if (InstallFlags[regpath[j]] == false) { alltrue = false; break; }
		}
		if (alltrue) {
			parseEnvironmentVariablePath(regpath[0]);
			RegisterRegFile(regpath[0].c_str());
		}
	}
}

namespace {
	bool SetRegistryString(HKEY key, const wchar_t* name, const wchar_t* value) {
		const DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
		return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes) == ERROR_SUCCESS;
	}

	bool SetRegistryDword(HKEY key, const wchar_t* name, DWORD value) {
		return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
	}
}

void FinaliseInstall() {
	InstallationLogger.WriteLine(L"Finalising Rectify12 installation...");

	wchar_t installLocation[MAX_PATH]{};
	wchar_t installedExe[MAX_PATH]{};
	StringCchPrintf(installLocation, MAX_PATH, L"%s\\%s", windir, Rectify12::InstallFolder);
	StringCchPrintf(installedExe, MAX_PATH, L"%s\\%s", installLocation, Rectify12::InstalledExecutable);

	HKEY uninstallKey = nullptr;
	DWORD disposition = 0;
	if (RegCreateKeyExW(
		HKEY_LOCAL_MACHINE,
		Rectify12::UninstallRegistryPath,
		0,
		nullptr,
		0,
		KEY_SET_VALUE,
		nullptr,
		&uninstallKey,
		&disposition) == ERROR_SUCCESS) {
		SetRegistryString(uninstallKey, L"DisplayIcon", installedExe);
		SetRegistryString(uninstallKey, L"DisplayName", Rectify12::ProductName);
		SetRegistryString(uninstallKey, L"DisplayVersion", Rectify12::ProductVersion);
		SetRegistryString(uninstallKey, L"InstallLocation", installLocation);
		SetRegistryString(uninstallKey, L"Publisher", Rectify12::Publisher);
		SetRegistryString(uninstallKey, L"UninstallString", installedExe);
		SetRegistryDword(uninstallKey, L"NoModify", 1);
		SetRegistryDword(uninstallKey, L"NoRepair", 1);
		RegCloseKey(uninstallKey);
	}
	else {
		InstallationLogger.WriteLine(L"Failed to create Rectify12 uninstall registration.");
	}

	HKEY productKey = nullptr;
	if (RegCreateKeyExW(
		HKEY_LOCAL_MACHINE,
		L"Software\\Rectify12",
		0,
		nullptr,
		0,
		KEY_SET_VALUE,
		nullptr,
		&productKey,
		&disposition) == ERROR_SUCCESS) {
		SetRegistryString(productKey, L"Version", Rectify12::ProductVersion);
		SetRegistryString(productKey, L"InstallLocation", installLocation);
		RegCloseKey(productKey);
	}

	StringCchPrintf(path, MAX_PATH, L"%s\\Base.dll", currdir);
	StringCchPrintf(cmd, MAX_PATH, L"%s\\Base.dll", r11targetdir);
	CopyFile(path, cmd, false);

	StringCchPrintf(path, MAX_PATH, L"%s\\Controls.dll", currdir);
	StringCchPrintf(cmd, MAX_PATH, L"%s\\Controls.dll", r11targetdir);
	CopyFile(path, cmd, false);

	StringCchPrintf(path, MAX_PATH, L"%s\\PageRes.dll", currdir);
	StringCchPrintf(cmd, MAX_PATH, L"%s\\PageRes.dll", r11targetdir);
	CopyFile(path, cmd, false);

	StringCchPrintf(path, MAX_PATH, L"%s\\%s", currdir, Rectify12::InstalledExecutable);
	StringCchPrintf(cmd, MAX_PATH, L"%s\\%s", r11targetdir, Rectify12::InstalledExecutable);
	if (!CopyFile(path, cmd, false)) {
		InstallationLogger.WriteLine(L"Failed to copy Rectify12 installer/uninstaller executable. Win32 error: " + std::to_wstring(GetLastError()));
	}

	StringCchPrintf(path, MAX_PATH, L"%s\\Segoe_r11.ttf", currdir);
	StringCchPrintf(cmd, MAX_PATH, L"%s\\Segoe_r11.ttf", r11targetdir);
	CopyFile(path, cmd, false);
}

void SetupComplete() {
	DWORD lpidProcess[1024] = {};
	DWORD cbNeeded = 0;
	HANDLE hExplorer = NULL;
	EnumProcesses(lpidProcess, sizeof(lpidProcess), &cbNeeded);

	wchar_t buffer[MAX_PATH]{};
	for (int i = 0; i < cbNeeded / sizeof(DWORD); i++) {
		hExplorer = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, lpidProcess[i]);
		if (!hExplorer) continue;

		DWORD bufferSize = MAX_PATH;
		if (QueryFullProcessImageNameW(hExplorer, 0, buffer, &bufferSize) && wcsstr(buffer, L"explorer.exe")) {
			break;
		}

		CloseHandle(hExplorer);
		hExplorer = NULL;
	}
	if (hExplorer) {
		TerminateProcess(hExplorer, 1);
		CloseHandle(hExplorer);
	}

	wchar_t cmd2[] = L"/c del %localappdata%\\microsoft\\windows\\explorer\\*.db";
	StringCchPrintf(path, MAX_PATH, L"%s\\System32\\cmd.exe", windir);
	RunEXE(path, cmd2);

	HANDLE hToken = NULL;
	TOKEN_PRIVILEGES tkp{};

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
		if (LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) {
			tkp.PrivilegeCount = 1;
			tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, 0);
		}
		CloseHandle(hToken);
	}

	InstallationLogger.WriteLine(L"Setup complete, requesting restart...");

	// Do not force-close user applications. If an app blocks shutdown, Windows can ask the user what to do.
	ExitWindowsEx(
		EWX_REBOOT,
		SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED);
}
