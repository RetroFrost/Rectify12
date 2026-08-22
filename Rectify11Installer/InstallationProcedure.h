#pragma once
#include "framework.h"

#ifndef MY_HEADERPROC_H
#define MY_HEADERPROC_H

struct ProcessResult {
    bool success = false;
    DWORD error = ERROR_SUCCESS;
    DWORD exitCode = 0;
};

bool extractFiles();
bool MoveFilesToTarget();
bool InstallPrograms();
bool RegisterRectifyTweaks();
void SetupComplete();
bool InstallFonts();
ProcessResult RunEXE(const wchar_t* exe, wchar_t* args, DWORD timeoutMilliseconds = 300000);
bool FinaliseInstall();

#endif
