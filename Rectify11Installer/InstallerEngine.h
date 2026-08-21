#pragma once
#define WM_UPDATEANIMATIONFRAME WM_USER+1
#define WM_UPDATERESTARTANIMATIONFRAME WM_USER+2
#define WM_UPDATEPROGRESS WM_USER+3
#define WM_UPDATECOUNTDOWN WM_USER+4
#define WM_SETUPCOMPLETE WM_USER+5

#ifndef MY_HEADER_H
#define MY_HEADER_H

#include "framework.h"

#endif

class IEngineWrapper {
private:
	HANDLE ienThread{};

public:
	static std::atomic_bool animate;
	static std::atomic<int> progressnum;
	static std::atomic<int> Ttime;
	static std::wstring currprogress;
	static std::mutex progressMutex;
	static std::mutex logMutex;
	void StartThread(unsigned long (*func)(LPVOID lpParam));
	void StopThread();
	static unsigned long BeginMainAnim(LPVOID lpParam);
	static unsigned long BeginRestartAnim(LPVOID lpParam);
	static unsigned long BeginInstall(LPVOID lpParam);
	static unsigned long BeginUninstall(LPVOID lpParam);
	static unsigned long BeginRestartCountdown(LPVOID lpParam);
};
