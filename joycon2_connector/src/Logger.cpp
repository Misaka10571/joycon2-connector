#include "Logger.h"

#include <shellapi.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_initialized{ false };
std::atomic<bool> g_debugEnabled{ false };
HANDLE g_logFile = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logLock{};
constexpr wchar_t LOG_FILE_NAME[] = L"joycon2_connector_debug.log";

const char* LevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:   return "DEBUG";
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error:   return "ERROR";
    default:                return "INFO";
    }
}

// Get the directory that contains the running executable.
std::wstring GetExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD copied = 0;
    for (;;) {
        copied = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (copied == 0) {
            return L".";
        }
        if (copied < path.size()) {
            break;
        }
        // Buffer too small: retry with more space.
        if (path.size() >= 32768) {
            return L".";
        }
        path.resize(path.size() * 2);
    }

    path.resize(copied);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slash);
}

bool HasDebugFlag() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        // Fallback: scan the raw command line.
        const wchar_t* cmd = GetCommandLineW();
        return cmd && wcsstr(cmd, L"--debug") != nullptr;
    }

    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && _wcsicmp(argv[i], L"--debug") == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

void WriteAll(HANDLE file, const char* data, DWORD length) {
    DWORD offset = 0;
    while (offset < length) {
        DWORD written = 0;
        if (!WriteFile(file, data + offset, length - offset, &written, nullptr) || written == 0) {
            return;
        }
        offset += written;
    }
}

// Used from normal Log() calls: take the lock, append and flush.
void AppendLineLocked(const char* data, DWORD length) {
    if (g_logFile == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_logLock);
    WriteAll(g_logFile, data, length);
    FlushFileBuffers(g_logFile);
    LeaveCriticalSection(&g_logLock);
}

// Used from the crash filter: avoid blocking if another thread owns the lock.
void AppendLineCrash(const char* data, DWORD length) {
    if (g_logFile == INVALID_HANDLE_VALUE) return;
    if (TryEnterCriticalSection(&g_logLock)) {
        WriteAll(g_logFile, data, length);
        FlushFileBuffers(g_logFile);
        LeaveCriticalSection(&g_logLock);
    } else {
        // The crashed thread may already hold the lock, or another thread is
        // writing. Fall back to an unsynchronized write so the crash record
        // is not lost.
        WriteAll(g_logFile, data, length);
        FlushFileBuffers(g_logFile);
    }
}

std::string BuildLine(LogLevel level, const char* fmt, va_list args) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    char prefix[96];
    sprintf_s(prefix,
              "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] [%lu:%lu] ",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              LevelName(level), GetCurrentProcessId(), GetCurrentThreadId());

    va_list argsCopy;
    va_copy(argsCopy, args);
    int messageLength = _vscprintf(fmt, argsCopy);
    va_end(argsCopy);

    std::string line(prefix);
    if (messageLength > 0) {
        std::vector<char> buffer(static_cast<size_t>(messageLength) + 1);
        vsnprintf_s(buffer.data(), buffer.size(), _TRUNCATE, fmt, args);
        line.append(buffer.data());
    } else if (messageLength < 0) {
        line += "<log format error>";
    }

    line += "\r\n";
    return line;
}

void LogV(LogLevel level, const char* fmt, va_list args, bool fromCrashFilter) {
    if (!g_debugEnabled.load(std::memory_order_relaxed) || !fmt) return;

    std::string line = BuildLine(level, fmt, args);
    if (fromCrashFilter) {
        AppendLineCrash(line.data(), static_cast<DWORD>(line.size()));
    } else {
        AppendLineLocked(line.data(), static_cast<DWORD>(line.size()));
    }
}

const char* ExceptionCodeName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    case 0xE06D7363:                         return "CPP_EXCEPTION";
    default:                                 return "UNKNOWN";
    }
}

std::string BuildCrashPrefix() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    char prefix[96];
    sprintf_s(prefix,
              "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [ERROR] [%lu:%lu] ",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
              GetCurrentProcessId(), GetCurrentThreadId());
    return prefix;
}

void LogExceptionRecord(_EXCEPTION_POINTERS* exceptionInfo) {
    if (!exceptionInfo || !exceptionInfo->ExceptionRecord) return;

    const EXCEPTION_RECORD* er = exceptionInfo->ExceptionRecord;
    std::string details = BuildCrashPrefix();

    char header[256];
    sprintf_s(header,
              "Unhandled exception: %s (0x%08lX), flags=0x%08lX, address=0x%p",
              ExceptionCodeName(er->ExceptionCode), er->ExceptionCode,
              er->ExceptionFlags, er->ExceptionAddress);
    details += header;

    DWORD paramCount = er->NumberParameters;
    if (paramCount > static_cast<DWORD>(EXCEPTION_MAXIMUM_PARAMETERS)) {
        paramCount = EXCEPTION_MAXIMUM_PARAMETERS;
    }
    for (DWORD i = 0; i < paramCount; ++i) {
        char param[64];
        sprintf_s(param, ", param[%lu]=0x%llX", i,
                  static_cast<unsigned long long>(er->ExceptionInformation[i]));
        details += param;
    }

    CONTEXT* ctx = exceptionInfo->ContextRecord;
#if defined(_M_X64)
    if (ctx) {
        char regs[384];
        sprintf_s(regs,
                  " | rip=0x%p rsp=0x%p rbp=0x%p rax=0x%llX rbx=0x%llX rcx=0x%llX rdx=0x%llX rsi=0x%llX rdi=0x%llX",
                  reinterpret_cast<void*>(ctx->Rip), reinterpret_cast<void*>(ctx->Rsp),
                  reinterpret_cast<void*>(ctx->Rbp),
                  static_cast<unsigned long long>(ctx->Rax),
                  static_cast<unsigned long long>(ctx->Rbx),
                  static_cast<unsigned long long>(ctx->Rcx),
                  static_cast<unsigned long long>(ctx->Rdx),
                  static_cast<unsigned long long>(ctx->Rsi),
                  static_cast<unsigned long long>(ctx->Rdi));
        details += regs;
    }
#elif defined(_M_IX86)
    if (ctx) {
        char regs[320];
        sprintf_s(regs,
                  " | eip=0x%p esp=0x%p ebp=0x%p eax=0x%08lX ebx=0x%08lX ecx=0x%08lX edx=0x%08lX esi=0x%08lX edi=0x%08lX",
                  reinterpret_cast<void*>(ctx->Eip), reinterpret_cast<void*>(ctx->Esp),
                  reinterpret_cast<void*>(ctx->Ebp),
                  static_cast<unsigned long>(ctx->Eax),
                  static_cast<unsigned long>(ctx->Ebx),
                  static_cast<unsigned long>(ctx->Ecx),
                  static_cast<unsigned long>(ctx->Edx),
                  static_cast<unsigned long>(ctx->Esi),
                  static_cast<unsigned long>(ctx->Edi));
        details += regs;
    }
#endif

    details += "\r\n";
    AppendLineCrash(details.data(), static_cast<DWORD>(details.size()));
}

} // namespace

void Logger::Init() {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    if (!HasDebugFlag()) {
        return;
    }

    std::wstring logPath = GetExecutableDirectory();
    if (logPath != L".") {
        logPath += L"\\";
    }
    logPath += LOG_FILE_NAME;

    HANDLE file = CreateFileW(logPath.c_str(),
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    InitializeCriticalSection(&g_logLock);
    g_logFile = file;
    g_debugEnabled.store(true, std::memory_order_release);

    APP_LOG_INFO("==============================================================");
    APP_LOG_INFO("Debug logging session started (log: %S)", logPath.c_str());
    APP_LOG_INFO("==============================================================");

    SetUnhandledExceptionFilter(Logger::UnhandledExceptionFilter);
}

void Logger::Shutdown() {
    if (!g_initialized.load(std::memory_order_relaxed)) {
        return;
    }

    g_debugEnabled.store(false, std::memory_order_release);

    // Only flush here. Detached BLE/update threads may still be inside a log
    // call, so closing the handle or deleting the critical section could race
    // with them. The OS reclaims the handle and critical section at process exit.
    if (g_logFile != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_logFile);
    }
}

bool Logger::IsDebugEnabled() {
    return g_debugEnabled.load(std::memory_order_relaxed) && g_logFile != INVALID_HANDLE_VALUE;
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(level, fmt, args, false);
    va_end(args);
}

LONG WINAPI Logger::UnhandledExceptionFilter(_EXCEPTION_POINTERS* exceptionInfo) {
    if (g_debugEnabled.load(std::memory_order_relaxed)) {
        LogExceptionRecord(exceptionInfo);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void Logger::OnTerminate() {
    if (!g_debugEnabled.load(std::memory_order_relaxed)) {
        std::abort();
    }

    std::string message = BuildCrashPrefix() + "Unhandled C++ exception: ";
    auto exception = std::current_exception();
    if (exception) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& e) {
            message += e.what();
        } catch (...) {
            message += "unknown type";
        }
    } else {
        message += "no exception object available";
    }

    message += "\r\n";
    AppendLineCrash(message.data(), static_cast<DWORD>(message.size()));
    std::abort();
}
