#pragma once

#ifndef MY_HEADERLOGGER_H
#define MY_HEADERLOGGER_H

#include "framework.h"

class Logger {
private:
	std::wfstream logfile;
	std::mutex mutex;
public:
	void StartLogger(const wchar_t* filename);
	void WriteLine(const std::wstring& line);
	void WriteLine(const std::wstring& line, int exitCode);
	~Logger();
};

#endif
