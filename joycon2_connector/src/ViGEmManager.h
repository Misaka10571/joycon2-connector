#pragma once
// ViGEm Manager - Singleton wrapping ViGEm client lifecycle
#include <ViGEm/Client.h>
#include <ViGEm/Common.h>
#include <iostream>
#include <atomic>

class ViGEmManager {
public:
    static ViGEmManager& Instance() {
        static ViGEmManager inst;
        return inst;
    }

    bool Initialize() {
        if (client != nullptr) return true;

        client = vigem_alloc();
        if (!client) {
            connected = false;
            return false;
        }

        auto ret = vigem_connect(client);
        if (!VIGEM_SUCCESS(ret)) {
            vigem_free(client);
            client = nullptr;
            connected = false;
            return false;
        }

        connected = true;
        return true;
    }

    void Shutdown() {
        if (client) {
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

    bool AddTarget(PVIGEM_TARGET target) {
        if (!client) return false;
        return VIGEM_SUCCESS(vigem_target_add(client, target));
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
};
