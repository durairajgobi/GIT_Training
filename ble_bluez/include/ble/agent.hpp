// SPDX-License-Identifier: MIT
// org.bluez.Agent1 implementation, registered with org.bluez.AgentManager1.
// BlueZ calls it to confirm passkeys / authorise services during pairing.
#pragma once

#include <gio/gio.h>

#include <cstdint>
#include <functional>
#include <string>

namespace ble {

class Agent {
public:
    enum class Capability { DisplayOnly, DisplayYesNo, KeyboardOnly, NoInputNoOutput, KeyboardDisplay };

    Agent(GDBusConnection* conn, Capability cap = Capability::KeyboardDisplay,
          std::string path = "/com/example/ble/agent");
    ~Agent();
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // Exports the object and calls AgentManager1.RegisterAgent (and
    // RequestDefaultAgent if makeDefault). Throws on failure.
    void registerAgent(bool makeDefault = true);
    void unregisterAgent();

    const std::string& path() const { return path_; }
    static const char* capabilityString(Capability cap);

    // Hooks. Any hook left empty uses the default: accept / auto-generate.
    // Returning false (or throwing) rejects the request.
    std::function<bool(const std::string& device, uint32_t passkey)> onConfirm;
    std::function<bool(const std::string& device)> onAuthorize;
    std::function<bool(const std::string& device, const std::string& uuid)> onAuthorizeService;
    std::function<uint32_t(const std::string& device)> onRequestPasskey;
    std::function<std::string(const std::string& device)> onRequestPinCode;
    std::function<void(const std::string& device, uint32_t passkey, uint16_t entered)> onDisplayPasskey;
    std::function<void(const std::string& device, const std::string& pin)> onDisplayPinCode;
    std::function<void()> onCancel;

private:
    static void handleMethod(GDBusConnection*, const gchar* sender, const gchar* path,
                             const gchar* iface, const gchar* method, GVariant* params,
                             GDBusMethodInvocation* inv, gpointer self);
    void dispatch(const std::string& method, GVariant* params, GDBusMethodInvocation* inv);

    GDBusConnection* conn_;
    Capability cap_;
    std::string path_;
    guint regId_ = 0;
    bool registered_ = false;
};

}  // namespace ble
