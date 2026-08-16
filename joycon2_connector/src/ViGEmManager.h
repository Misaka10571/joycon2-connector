#pragma once
// ViGEm Manager - Singleton wrapping ViGEm client lifecycle
#include <ViGEm/Client.h>
#include <ViGEm/Common.h>
#include <iostream>
#include <atomic>
#include <cstdio>
#include <string>
#include "Logger.h"

class ViGEmManager {
public:
    static ViGEmManager& Instance() {
        static ViGEmManager inst;
        return inst;
    }

    bool Initialize() {
        if (client != nullptr) return true;

        APP_LOG_INFO("Initializing ViGEm client");
        client = vigem_alloc();
        if (!client) {
            APP_LOG_ERROR("ViGEm client allocation failed (driver may not be installed)");
            lastError.store(VIGEM_ERROR_BUS_NOT_FOUND);
            connected = false;
            return false;
        }

        auto ret = vigem_connect(client);
        if (!VIGEM_SUCCESS(ret)) {
            APP_LOG_ERROR("ViGEm connect failed (error code: 0x%08X)", static_cast<unsigned int>(ret));
            vigem_free(client);
            client = nullptr;
            lastError.store(ret);
            connected = false;
            return false;
        }

        APP_LOG_INFO("ViGEm client connected");
        lastError.store(VIGEM_ERROR_NONE);
        connected = true;
        return true;
    }

    void Shutdown() {
        if (client) {
            APP_LOG_INFO("Disconnecting ViGEm client");
            vigem_disconnect(client);
            vigem_free(client);
            client = nullptr;
        }
        connected = false;
    }

    bool IsConnected() const { return connected; }
    PVIGEM_CLIENT GetClient() const { return client; }

    PVIGEM_TARGET AllocDS4() {
        return vigem_target_ds4_alloc();
    }

    PVIGEM_TARGET AllocX360() {
        return vigem_target_x360_alloc();
    }

    VIGEM_ERROR AddTarget(PVIGEM_TARGET target) {
        if (!client) {
            VIGEM_ERROR error = lastError.load();
            return VIGEM_SUCCESS(error) ? VIGEM_ERROR_BUS_NOT_FOUND : error;
        }
        VIGEM_ERROR error = vigem_target_add(client, target);
        if (!VIGEM_SUCCESS(error)) lastError.store(error);
        return error;
    }

    static std::string DescribeError(VIGEM_ERROR error) {
        const char* name = "Unknown";
        switch (error) {
        case VIGEM_ERROR_NONE: name = "None"; break;
        case VIGEM_ERROR_BUS_NOT_FOUND: name = "BusNotFound"; break;
        case VIGEM_ERROR_NO_FREE_SLOT: name = "NoFreeSlot"; break;
        case VIGEM_ERROR_INVALID_TARGET: name = "InvalidTarget"; break;
        case VIGEM_ERROR_ALREADY_CONNECTED: name = "AlreadyConnected"; break;
        case VIGEM_ERROR_TARGET_UNINITIALIZED: name = "TargetUninitialized"; break;
        case VIGEM_ERROR_BUS_VERSION_MISMATCH: name = "BusVersionMismatch"; break;
        case VIGEM_ERROR_BUS_ACCESS_FAILED: name = "BusAccessFailed"; break;
        case VIGEM_ERROR_BUS_INVALID_HANDLE: name = "BusInvalidHandle"; break;
        case VIGEM_ERROR_INVALID_PARAMETER: name = "InvalidParameter"; break;
        case VIGEM_ERROR_NOT_SUPPORTED: name = "NotSupported"; break;
        case VIGEM_ERROR_WINAPI: name = "WinApiError"; break;
        case VIGEM_ERROR_TIMED_OUT: name = "TimedOut"; break;
        case VIGEM_ERROR_IS_DISPOSING: name = "IsDisposing"; break;
        default: break;
        }
        char buffer[96];
        sprintf_s(buffer, "ViGEm error 0x%08X - %s", static_cast<unsigned int>(error), name);
        return buffer;
    }

    static bool HasSuggestedSolution(VIGEM_ERROR error) {
        switch (error) {
        case VIGEM_ERROR_BUS_NOT_FOUND:
        case VIGEM_ERROR_NO_FREE_SLOT:
        case VIGEM_ERROR_BUS_VERSION_MISMATCH:
        case VIGEM_ERROR_BUS_ACCESS_FAILED:
        case VIGEM_ERROR_BUS_INVALID_HANDLE:
        case VIGEM_ERROR_TIMED_OUT:
        case VIGEM_ERROR_IS_DISPOSING:
            return true;
        default:
            return false;
        }
    }

    void RemoveTarget(PVIGEM_TARGET target) {
        if (client && target) {
            vigem_target_remove(client, target);
            vigem_target_free(target);
        }
    }

    ~ViGEmManager() {
        Shutdown();
    }

private:
    ViGEmManager() = default;
    PVIGEM_CLIENT client = nullptr;
    std::atomic<bool> connected{ false };
    std::atomic<VIGEM_ERROR> lastError{ VIGEM_ERROR_NONE };
};
