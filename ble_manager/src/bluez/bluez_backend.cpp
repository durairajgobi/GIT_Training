// SPDX-License-Identifier: MIT
// BlueZ backend: core plumbing and GATT client (central role).
#include <algorithm>
#include <cstdlib>

#include "blemgr/log.hpp"
#include "bluez_impl.hpp"

namespace blemgr {

using namespace gdbus;

GDBusNodeInfo* parseNodeXml(const std::string& xml) {
    GError* err = nullptr;
    GDBusNodeInfo* n = g_dbus_node_info_new_for_xml(xml.c_str(), &err);
    if (!n) {
        BLE_LOGE("introspection XML: %s", err->message);
        std::abort();
    }
    return n;
}

namespace {

struct PendingCall {
    std::shared_ptr<bool> alive;
    BluezBackend::Impl::ReplyFn fn;
};

void onCallReply(GObject* src, GAsyncResult* res, gpointer ud) {
    std::unique_ptr<PendingCall> p(static_cast<PendingCall*>(ud));
    GError* raw = nullptr;
    VariantPtr reply(g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &raw));
    ErrorPtr err(raw);
    if (!*p->alive) return;
    if (err && g_error_matches(err.get(), G_IO_ERROR, G_IO_ERROR_CANCELLED)) return;
    p->fn(reply.get(), toStatus(err.get()));
}

GVariant* emptyDict() { return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0); }

bool changedHas(GVariant* changed, const char* key) {
    VariantPtr v(g_variant_lookup_value(changed, key, nullptr));
    return v != nullptr;
}

}  // namespace

// ================================================================ Impl core

Status BluezBackend::Impl::start(IBackendEvents* events) {
    if (conn) return {Error::Busy, "already started"};
    ev = events;
    ctx = g_main_context_ref_thread_default();
    alive = std::make_shared<bool>(true);
    cancel = g_cancellable_new();

    GError* raw = nullptr;
    if (opts.busAddress.empty()) {
        conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, cancel, &raw);
    } else {
        conn = g_dbus_connection_new_for_address_sync(
            opts.busAddress.c_str(),
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, cancel, &raw);
    }
    if (!conn) {
        ErrorPtr err(raw);
        Status st{Error::NotReady, std::string("D-Bus connect: ") + err->message};
        stop();
        return st;
    }

    om = g_dbus_object_manager_client_new_sync(
        conn, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START, opts.busName.c_str(), "/",
        nullptr, nullptr, nullptr, cancel, &raw);
    if (!om) {
        ErrorPtr err(raw);
        Status st{Error::NotReady, std::string("BlueZ object manager: ") + err->message};
        stop();
        return st;
    }

    adapterPath = "/org/bluez/" + opts.adapter;

    signalIds.push_back(g_signal_connect(
        om, "object-added", G_CALLBACK(+[](GDBusObjectManager*, GDBusObject* obj, gpointer self) {
            GList* ifs = g_dbus_object_get_interfaces(obj);
            for (GList* l = ifs; l; l = l->next)
                static_cast<Impl*>(self)->onInterfaceAdded(G_DBUS_PROXY(l->data));
            g_list_free_full(ifs, g_object_unref);
        }),
        this));
    signalIds.push_back(g_signal_connect(
        om, "object-removed", G_CALLBACK(+[](GDBusObjectManager*, GDBusObject* obj, gpointer self) {
            static_cast<Impl*>(self)->onObjectRemoved(g_dbus_object_get_object_path(obj));
        }),
        this));
    signalIds.push_back(g_signal_connect(
        om, "interface-added",
        G_CALLBACK(+[](GDBusObjectManager*, GDBusObject*, GDBusInterface* i, gpointer self) {
            static_cast<Impl*>(self)->onInterfaceAdded(G_DBUS_PROXY(i));
        }),
        this));
    signalIds.push_back(g_signal_connect(
        om, "interface-removed",
        G_CALLBACK(+[](GDBusObjectManager*, GDBusObject* obj, GDBusInterface* i, gpointer self) {
            static_cast<Impl*>(self)->onInterfaceRemoved(
                g_dbus_object_get_object_path(obj),
                g_dbus_proxy_get_interface_name(G_DBUS_PROXY(i)));
        }),
        this));
    signalIds.push_back(g_signal_connect(
        om, "interface-proxy-properties-changed",
        G_CALLBACK(+[](GDBusObjectManagerClient*, GDBusObjectProxy*, GDBusProxy* p,
                       GVariant* changed, const gchar* const*, gpointer self) {
            static_cast<Impl*>(self)->onPropertiesChanged(p, changed);
        }),
        this));
    signalIds.push_back(g_signal_connect(
        om, "notify::name-owner", G_CALLBACK(+[](GObject* o, GParamSpec*, gpointer self) {
            gchar* owner =
                g_dbus_object_manager_client_get_name_owner(G_DBUS_OBJECT_MANAGER_CLIENT(o));
            auto* d = static_cast<Impl*>(self);
            if (owner) {
                BLE_LOGI("bluetoothd appeared (%s)", owner);
                d->registerAgent();
            } else {
                BLE_LOGW("bluetoothd vanished");
            }
            g_free(owner);
            d->emitAdapterState();
        }),
        this));

    gchar* owner = g_dbus_object_manager_client_get_name_owner(G_DBUS_OBJECT_MANAGER_CLIENT(om));
    if (!owner) BLE_LOGW("%s is not running; waiting for it", opts.busName.c_str());
    g_free(owner);

    emitAdapterState();

    // Replay devices the stack already knows (bonded, cached or connected).
    GList* objs = g_dbus_object_manager_get_objects(om);
    for (GList* l = objs; l; l = l->next) {
        std::string path = g_dbus_object_get_object_path(G_DBUS_OBJECT(l->data));
        if (!deviceUnderAdapter(path)) continue;
        auto dev = proxy(path, iface::kDevice);
        if (!dev) continue;
        DeviceInfo info = deviceInfo(dev.get());
        ev->onDeviceUpdated(info);
        if (info.connected) ev->onConnectionChanged(info.address, true);
        if (info.servicesResolved) ev->onServicesResolved(info.address, collectServices(path));
    }
    g_list_free_full(objs, g_object_unref);

    registerAgent();
    BLE_LOGI("BlueZ backend started on %s", adapterPath.c_str());
    return {};
}

void BluezBackend::Impl::stop() {
    if (alive) *alive = false;
    if (conn) {
        if (adv) {
            if (adv->registered)
                call(adapterPath, iface::kAdvManager, "UnregisterAdvertisement",
                     g_variant_new("(o)", (opts.objectRoot + "/advertisement0").c_str()),
                     [](GVariant*, const Status&) {});
            dropAdvertisementObject();
        }
        if (server) {
            if (server->registered)
                call(adapterPath, iface::kGattManager, "UnregisterApplication",
                     g_variant_new("(o)", server->rootPath.c_str()), [](GVariant*, const Status&) {});
            dropServerObjects();
        }
        unregisterAgent();
        g_dbus_connection_flush_sync(conn, nullptr, nullptr);
    }
    if (om) {
        for (gulong id : signalIds) g_signal_handler_disconnect(om, id);
        g_object_unref(om);
    }
    signalIds.clear();
    om = nullptr;
    if (cancel) {
        g_cancellable_cancel(cancel);
        g_object_unref(cancel);
    }
    cancel = nullptr;
    if (conn) g_object_unref(conn);
    conn = nullptr;
    if (ctx) g_main_context_unref(ctx);
    ctx = nullptr;
    charPaths.clear();
    charOwners.clear();
    ev = nullptr;
}

void BluezBackend::Impl::call(const std::string& path, const char* ifc, const char* method,
                              GVariant* params, ReplyFn fn, int timeoutMs) {
    if (!conn) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        fn(nullptr, {Error::NotReady, "backend not started"});
        return;
    }
    BLE_LOGD("call %s %s.%s", path.c_str(), ifc, method);
    g_dbus_connection_call(conn, opts.busName.c_str(), path.c_str(), ifc, method, params, nullptr,
                           G_DBUS_CALL_FLAGS_NONE, timeoutMs < 0 ? opts.callTimeoutMs : timeoutMs,
                           cancel, &onCallReply, new PendingCall{alive, std::move(fn)});
}

void BluezBackend::Impl::defer(std::function<void()> fn) {
    struct Deferred {
        std::shared_ptr<bool> alive;
        std::function<void()> fn;
    };
    GSource* s = g_idle_source_new();
    g_source_set_callback(
        s,
        [](gpointer p) -> gboolean {
            auto* d = static_cast<Deferred*>(p);
            if (*d->alive) d->fn();
            return G_SOURCE_REMOVE;
        },
        new Deferred{alive ? alive : std::make_shared<bool>(true), std::move(fn)},
        [](gpointer p) { delete static_cast<Deferred*>(p); });
    g_source_attach(s, ctx ? ctx : g_main_context_get_thread_default());
    g_source_unref(s);
}

void BluezBackend::Impl::deferStatus(const StatusCallback& cb, Status st) {
    if (!cb) return;
    defer([cb, st] { cb(st); });
}

void BluezBackend::Impl::setProperty(const std::string& path, const char* ifc, const char* name,
                                     GVariant* value, StatusCallback cb) {
    call(path, iface::kProps, "Set", g_variant_new("(ssv)", ifc, name, value),
         [cb](GVariant*, const Status& s) {
             if (cb) cb(s);
         });
}

// ======================================================== object manager ==

ObjPtr<GDBusProxy> BluezBackend::Impl::proxy(const std::string& path, const char* ifc) {
    if (!om) return nullptr;
    GDBusInterface* i = g_dbus_object_manager_get_interface(om, path.c_str(), ifc);
    return ObjPtr<GDBusProxy>(i ? G_DBUS_PROXY(i) : nullptr);
}

bool BluezBackend::Impl::deviceUnderAdapter(const std::string& path) const {
    const std::string prefix = adapterPath + "/dev_";
    return hasPrefix(path, prefix) && path.find('/', prefix.size()) == std::string::npos;
}

AdapterState BluezBackend::Impl::adapterState() {
    auto a = proxy(adapterPath, iface::kAdapter);
    if (!a) return AdapterState::Unavailable;
    return propBool(a.get(), "Powered") ? AdapterState::On : AdapterState::Off;
}

void BluezBackend::Impl::emitAdapterState() {
    if (ev) ev->onAdapterChanged(adapterState());
}

void BluezBackend::Impl::onInterfaceAdded(GDBusProxy* p) {
    if (!ev) return;
    std::string ifc = g_dbus_proxy_get_interface_name(p);
    std::string path = g_dbus_proxy_get_object_path(p);
    if (ifc == iface::kAdapter && path == adapterPath) {
        BLE_LOGI("adapter %s appeared", path.c_str());
        emitAdapterState();
        // Registrations do not survive a controller / daemon restart.
        if (adv) {
            adv->registered = false;
            call(adapterPath, iface::kAdvManager, "RegisterAdvertisement",
                 g_variant_new("(o@a{sv})", (opts.objectRoot + "/advertisement0").c_str(), emptyDict()),
                 [this](GVariant*, const Status& s) {
                     if (adv) adv->registered = s.ok();
                     if (!s.ok()) BLE_LOGW("re-advertise failed: %s", s.toString().c_str());
                 });
        }
        if (server) {
            server->registered = false;
            call(adapterPath, iface::kGattManager, "RegisterApplication",
                 g_variant_new("(o@a{sv})", server->rootPath.c_str(), emptyDict()),
                 [this](GVariant*, const Status& s) {
                     if (server) server->registered = s.ok();
                     if (!s.ok()) BLE_LOGW("GATT re-register failed: %s", s.toString().c_str());
                 });
        }
    } else if (ifc == iface::kDevice && deviceUnderAdapter(path)) {
        DeviceInfo info = deviceInfo(p);
        ev->onDeviceUpdated(info);
        if (info.connected) ev->onConnectionChanged(info.address, true);
    }
}

void BluezBackend::Impl::onObjectRemoved(const std::string& path) {
    if (!ev) return;
    if (path == adapterPath) {
        BLE_LOGW("adapter %s removed", path.c_str());
        emitAdapterState();
        return;
    }
    // Forget cached characteristic paths below the removed object.
    for (auto it = charOwners.begin(); it != charOwners.end();) {
        if (hasPrefix(it->first, path)) {
            charPaths.erase(it->second.address + "|" + it->second.ref.service + "|" +
                            it->second.ref.characteristic);
            it = charOwners.erase(it);
        } else {
            ++it;
        }
    }
    if (deviceUnderAdapter(path)) {
        if (auto a = addressFromPath(path)) ev->onDeviceRemoved(*a);
    }
}

void BluezBackend::Impl::onInterfaceRemoved(const std::string& path, const std::string& ifc) {
    if (ifc == iface::kAdapter && path == adapterPath) emitAdapterState();
}

void BluezBackend::Impl::onPropertiesChanged(GDBusProxy* p, GVariant* changed) {
    if (!ev) return;
    std::string ifc = g_dbus_proxy_get_interface_name(p);
    std::string path = g_dbus_proxy_get_object_path(p);

    if (ifc == iface::kAdapter) {
        if (path == adapterPath && changedHas(changed, "Powered")) emitAdapterState();
        return;
    }
    if (ifc == iface::kDevice) {
        if (!deviceUnderAdapter(path)) return;
        DeviceInfo info = deviceInfo(p);
        ev->onDeviceUpdated(info);
        if (changedHas(changed, "Connected")) ev->onConnectionChanged(info.address, info.connected);
        if (changedHas(changed, "ServicesResolved") && info.servicesResolved && info.connected)
            ev->onServicesResolved(info.address, collectServices(path));
        return;
    }
    if (ifc == iface::kGattChar) {
        VariantPtr v(g_variant_lookup_value(changed, "Value", G_VARIANT_TYPE_BYTESTRING));
        if (!v) return;
        auto it = charOwners.find(path);
        if (it == charOwners.end()) {
            auto a = addressFromPath(path);
            if (!a) return;
            collectServices(devicePath(adapterPath, *a));
            it = charOwners.find(path);
            if (it == charOwners.end()) return;
        }
        ev->onNotification(it->second.address, it->second.ref, toBytes(v.get()));
    }
}

DeviceInfo BluezBackend::Impl::deviceInfo(GDBusProxy* dev) {
    DeviceInfo d;
    std::string path = g_dbus_proxy_get_object_path(dev);
    d.address = normalizeAddress(propString(dev, "Address"))
                    .value_or(addressFromPath(path).value_or(std::string()));
    d.addressType = propString(dev, "AddressType") == "random" ? AddressType::Random
                                                               : AddressType::Public;
    d.name = propString(dev, "Name");
    d.rssi = propInt16(dev, "RSSI");
    d.txPower = propInt16(dev, "TxPower");
    for (const auto& u : propStrings(dev, "UUIDs"))
        d.serviceUuids.push_back(normalizeUuid(u).value_or(u));
    d.paired = propBool(dev, "Paired");
    d.trusted = propBool(dev, "Trusted");
    d.connected = propBool(dev, "Connected");
    d.servicesResolved = propBool(dev, "ServicesResolved");

    if (auto md = prop(dev, "ManufacturerData");
        md && g_variant_is_of_type(md.get(), G_VARIANT_TYPE("a{qv}"))) {
        GVariantIter it;
        g_variant_iter_init(&it, md.get());
        guint16 company;
        GVariant* val;
        while (g_variant_iter_loop(&it, "{qv}", &company, &val))
            d.manufacturerData[company] = toBytes(val);
    }
    if (auto sd = prop(dev, "ServiceData");
        sd && g_variant_is_of_type(sd.get(), G_VARIANT_TYPE("a{sv}"))) {
        GVariantIter it;
        g_variant_iter_init(&it, sd.get());
        const gchar* uuid;
        GVariant* val;
        while (g_variant_iter_loop(&it, "{&sv}", &uuid, &val))
            d.serviceData[normalizeUuid(uuid).value_or(uuid)] = toBytes(val);
    }
    return d;
}

std::vector<GattServiceInfo> BluezBackend::Impl::collectServices(const std::string& devPath) {
    auto addr = addressFromPath(devPath);
    if (!om || !addr) return {};

    struct CharEntry {
        std::string path, servicePath;
        GattCharacteristicInfo info;
    };
    std::map<std::string, GattServiceInfo> services;  // by object path
    std::vector<CharEntry> chars;
    const std::string prefix = devPath + "/";

    GList* objs = g_dbus_object_manager_get_objects(om);
    for (GList* l = objs; l; l = l->next) {
        GDBusObject* o = G_DBUS_OBJECT(l->data);
        std::string p = g_dbus_object_get_object_path(o);
        if (!hasPrefix(p, prefix)) continue;
        if (GDBusInterface* i = g_dbus_object_get_interface(o, iface::kGattService)) {
            ObjPtr<GDBusProxy> sp(G_DBUS_PROXY(i));
            GattServiceInfo s;
            std::string u = propString(sp.get(), "UUID");
            s.uuid = normalizeUuid(u).value_or(u);
            s.primary = propBool(sp.get(), "Primary");
            services[p] = std::move(s);
        }
        if (GDBusInterface* i = g_dbus_object_get_interface(o, iface::kGattChar)) {
            ObjPtr<GDBusProxy> cp(G_DBUS_PROXY(i));
            CharEntry c;
            c.path = p;
            c.servicePath = propString(cp.get(), "Service");
            std::string u = propString(cp.get(), "UUID");
            c.info.uuid = normalizeUuid(u).value_or(u);
            c.info.flags = propStrings(cp.get(), "Flags");
            chars.push_back(std::move(c));
        }
    }
    g_list_free_full(objs, g_object_unref);

    // Rebuild the path cache for this device.
    for (auto it = charOwners.begin(); it != charOwners.end();)
        it = hasPrefix(it->first, prefix) ? charOwners.erase(it) : std::next(it);
    for (auto it = charPaths.begin(); it != charPaths.end();)
        it = hasPrefix(it->first, *addr + "|") ? charPaths.erase(it) : std::next(it);

    std::sort(chars.begin(), chars.end(),
              [](const CharEntry& a, const CharEntry& b) { return a.path < b.path; });
    for (auto& c : chars) {
        auto s = services.find(c.servicePath);
        if (s == services.end()) continue;
        CharRef ref{s->second.uuid, c.info.uuid};
        charPaths.emplace(*addr + "|" + ref.service + "|" + ref.characteristic, c.path);  // first wins
        charOwners[c.path] = {*addr, ref};
        s->second.characteristics.push_back(std::move(c.info));
    }

    std::vector<GattServiceInfo> out;
    out.reserve(services.size());
    for (auto& [p, s] : services) out.push_back(std::move(s));
    return out;
}

std::optional<std::string> BluezBackend::Impl::findCharPath(const std::string& address,
                                                            const CharRef& ch) {
    const std::string key = address + "|" + ch.service + "|" + ch.characteristic;
    auto it = charPaths.find(key);
    if (it == charPaths.end()) {
        collectServices(devicePath(adapterPath, address));
        it = charPaths.find(key);
        if (it == charPaths.end()) return std::nullopt;
    }
    return it->second;
}

// ============================================================ BluezBackend

BluezBackend::BluezBackend(BluezOptions opts) : d_(std::make_unique<Impl>(std::move(opts))) {}
BluezBackend::~BluezBackend() { d_->stop(); }

Status BluezBackend::start(IBackendEvents* events) { return d_->start(events); }
void BluezBackend::stop() { d_->stop(); }

void BluezBackend::setPowered(bool on, StatusCallback cb) {
    d_->setProperty(d_->adapterPath, iface::kAdapter, "Powered", g_variant_new_boolean(on),
                    std::move(cb));
}

void BluezBackend::startDiscovery(const ScanFilter& f, StatusCallback cb) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "Transport", g_variant_new_string("le"));
    g_variant_builder_add(&b, "{sv}", "DuplicateData", g_variant_new_boolean(f.duplicateData));
    if (!f.serviceUuids.empty())
        g_variant_builder_add(&b, "{sv}", "UUIDs", fromStrings(f.serviceUuids));
    if (f.minRssi) g_variant_builder_add(&b, "{sv}", "RSSI", g_variant_new_int16(*f.minRssi));

    Impl* d = d_.get();
    d->call(d->adapterPath, iface::kAdapter, "SetDiscoveryFilter", g_variant_new("(a{sv})", &b),
            [d, cb](GVariant*, const Status& s) {
                if (!s.ok()) BLE_LOGW("SetDiscoveryFilter: %s (continuing)", s.toString().c_str());
                d->call(d->adapterPath, iface::kAdapter, "StartDiscovery", nullptr,
                        [cb](GVariant*, const Status& st) {
                            // InProgress: this client already has a discovery session.
                            cb(st.code() == Error::Busy ? Status{} : st);
                        });
            });
}

void BluezBackend::stopDiscovery(StatusCallback cb) {
    d_->call(d_->adapterPath, iface::kAdapter, "StopDiscovery", nullptr,
             [cb](GVariant*, const Status& s) { cb(s); });
}

void BluezBackend::connect(const std::string& address, StatusCallback cb) {
    const std::string path = devicePath(d_->adapterPath, address);
    if (!d_->proxy(path, iface::kDevice))
        return d_->deferStatus(cb, {Error::NotFound, address + " not known to BlueZ"});
    d_->call(path, iface::kDevice, "Connect", nullptr, [cb](GVariant*, const Status& s) { cb(s); });
}

void BluezBackend::disconnect(const std::string& address, StatusCallback cb) {
    const std::string path = devicePath(d_->adapterPath, address);
    if (!d_->proxy(path, iface::kDevice)) return d_->deferStatus(cb, {Error::NotFound, address});
    d_->call(path, iface::kDevice, "Disconnect", nullptr,
             [cb](GVariant*, const Status& s) { cb(s); });
}

void BluezBackend::pair(const std::string& address, StatusCallback cb) {
    const std::string path = devicePath(d_->adapterPath, address);
    if (!d_->proxy(path, iface::kDevice)) return d_->deferStatus(cb, {Error::NotFound, address});
    d_->call(path, iface::kDevice, "Pair", nullptr, [cb](GVariant*, const Status& s) { cb(s); },
             std::max(d_->opts.callTimeoutMs, 60000));
}

void BluezBackend::setTrusted(const std::string& address, bool trusted, StatusCallback cb) {
    d_->setProperty(devicePath(d_->adapterPath, address), iface::kDevice, "Trusted",
                    g_variant_new_boolean(trusted), std::move(cb));
}

void BluezBackend::removeDevice(const std::string& address, StatusCallback cb) {
    const std::string path = devicePath(d_->adapterPath, address);
    d_->call(d_->adapterPath, iface::kAdapter, "RemoveDevice", g_variant_new("(o)", path.c_str()),
             [cb](GVariant*, const Status& s) { cb(s); });
}

void BluezBackend::read(const std::string& address, const CharRef& ch, ReadCallback cb) {
    auto path = d_->findCharPath(address, ch);
    if (!path) {
        d_->defer([cb, ch] { cb({Error::NotFound, "characteristic " + ch.toString()}, {}); });
        return;
    }
    d_->call(*path, iface::kGattChar, "ReadValue", g_variant_new("(@a{sv})", emptyDict()),
             [cb](GVariant* reply, const Status& s) {
                 if (!s.ok() || !reply) return cb(s, {});
                 VariantPtr v(g_variant_get_child_value(reply, 0));
                 cb(s, toBytes(v.get()));
             });
}

void BluezBackend::write(const std::string& address, const CharRef& ch, const Bytes& value,
                         WriteType type, StatusCallback cb) {
    auto path = d_->findCharPath(address, ch);
    if (!path) return d_->deferStatus(cb, {Error::NotFound, "characteristic " + ch.toString()});
    GVariantBuilder o;
    g_variant_builder_init(&o, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&o, "{sv}", "type",
                          g_variant_new_string(type == WriteType::WithResponse ? "request" : "command"));
    d_->call(*path, iface::kGattChar, "WriteValue",
             g_variant_new("(@ay@a{sv})", fromBytes(value), g_variant_builder_end(&o)),
             [cb](GVariant*, const Status& s) { cb(s); });
}

void BluezBackend::setNotify(const std::string& address, const CharRef& ch, bool enable,
                             StatusCallback cb) {
    auto path = d_->findCharPath(address, ch);
    if (!path) return d_->deferStatus(cb, {Error::NotFound, "characteristic " + ch.toString()});
    d_->call(*path, iface::kGattChar, enable ? "StartNotify" : "StopNotify", nullptr,
             [cb, enable](GVariant*, const Status& s) {
                 cb(enable && s.code() == Error::Busy ? Status{} : s);  // already notifying
             });
}

void BluezBackend::startAdvertising(const AdvertisementData& adv, StatusCallback cb) {
    d_->startAdvertising(adv, std::move(cb));
}
void BluezBackend::stopAdvertising(StatusCallback cb) { d_->stopAdvertising(std::move(cb)); }
void BluezBackend::registerGattServer(const std::vector<LocalService>& s, StatusCallback cb) {
    d_->registerGattServer(s, std::move(cb));
}
void BluezBackend::unregisterGattServer(StatusCallback cb) { d_->unregisterGattServer(std::move(cb)); }
Status BluezBackend::notifyLocal(const CharRef& ch, const Bytes& v) { return d_->notifyLocal(ch, v); }

}  // namespace blemgr
