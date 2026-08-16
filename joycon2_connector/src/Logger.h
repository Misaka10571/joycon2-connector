#pragma once
// Logger - Optional file logging (enabled only with the --debug command line flag).
// Log files are written next to the executable as joycon2_connector_debug.log.
// Also installs an unhandled-exception filter so crashes are recorded in the same log.

#include <Windows.h>
#include <cstdarg>

enum class LogLevel {
    Debug = 0,
    Info,
    Warning,
    Error
};

class Logger {
public:
    // Parse the command line, open the log file (when --debug is present) and
    // install the crash handler. Safe to call multiple times.
    static void Init();

    // Flush the log file. The handle is intentionally left open until process
    // exit so detached worker threads can finish any in-flight log write safely.
    static void Shutdown();

    static bool IsDebugEnabled();

    static void Log(LogLevel level, const char* fmt, ...);

    // Installed by Init() when debug logging is active.
    static LONG WINAPI UnhandledExceptionFilter(_EXCEPTION_POINTERS* exceptionInfo);

    // Use with std::set_terminate so uncaught C++ exceptions are also logged.
    static void OnTerminate();
};

// Low-overhead macros: the body is skipped entirely when --debug is absent.
#define APP_LOG_DEBUG(...)   do { if (Logger::IsDebugEnabled()) Logger::Log(LogLevel::Debug,   __VA_ARGS__); } while (0)
#define APP_LOG_INFO(...)    do { if (Logger::IsDebugEnabled()) Logger::Log(LogLevel::Info,    __VA_ARGS__); } while (0)
#define APP_LOG_WARNING(...) do { if (Logger::IsDebugEnabled()) Logger::Log(LogLevel::Warning, __VA_ARGS__); } while (0)
#define APP_LOG_ERROR(...)   do { if (Logger::IsDebugEnabled()) Logger::Log(LogLevel::Error,   __VA_ARGS__); } while (0)
