#include "Logger.h"

void Logger::StartLogger(const wchar_t* filename) {
	logfile.open(filename, std::wfstream::binary | std::wfstream::out | std::wfstream::app);
	if (!logfile)
		logfile.open(filename, std::wfstream::binary | std::wfstream::trunc | std::wfstream::out);

	logfile << L"\n\n";
	const std::time_t now = std::time(nullptr);
	std::tm localTime{};
	localtime_s(&localTime, &now);

	wchar_t timestamp[64]{};
	if (std::wcsftime(timestamp, std::size(timestamp), L"%Y-%m-%d %H:%M:%S", &localTime) == 0) {
		wcscpy_s(timestamp, L"unknown time");
	}

	if (logfile.good()) {
		logfile << L"================ Logging started at " << timestamp << L" ================\n";
		logfile.flush();
	}
}

void Logger::WriteLine(const std::wstring& line) {
	std::lock_guard lock(mutex);
	if (logfile.is_open()) {
		logfile << line << L'\n';
		logfile.flush();
	}
}

void Logger::WriteLine(const std::wstring& line, int exitCode) {
	std::lock_guard lock(mutex);
	if (logfile.is_open()) {
		logfile << line << L" (exit code: " << std::to_wstring(exitCode) << L")\n";
		logfile.flush();
	}
}

Logger::~Logger() {
	if (logfile.is_open()) {
		logfile.close();
	}
}
