// A small dependency-injection container.
//
//   container.addSingleton<ILLMClient>([](ServiceContainer& c) {
//       return std::make_shared<AnthropicClient>(c.resolve<IHttpClient>(), opts);
//   });
//   auto llm = container.resolve<ILLMClient>();
//
// Singletons are built once on first resolve; transients are built on every
// resolve. Factories get the container so they can resolve their own
// dependencies (constructor injection).
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>

namespace wa {

class ResolutionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ServiceContainer {
public:
    template <typename T>
    using Factory = std::function<std::shared_ptr<T>(ServiceContainer&)>;

    template <typename T>
    void addSingleton(Factory<T> factory) { add<T>(std::move(factory), Lifetime::Singleton); }

    template <typename T>
    void addTransient(Factory<T> factory) { add<T>(std::move(factory), Lifetime::Transient); }

    // Register an already-built object as a singleton.
    template <typename T>
    void addInstance(std::shared_ptr<T> instance) {
        addSingleton<T>([instance](ServiceContainer&) { return instance; });
    }

    template <typename T>
    bool has() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return entries_.count(std::type_index(typeid(T))) > 0;
    }

    template <typename T>
    std::shared_ptr<T> resolve() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const std::type_index key(typeid(T));
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            throw ResolutionError(std::string("no service registered for ") + typeid(T).name());
        }
        if (it->second.lifetime == Lifetime::Singleton && it->second.instance) {
            return std::static_pointer_cast<T>(it->second.instance);
        }
        // Copy the factory: it may register services and invalidate `it`.
        auto factory = it->second.factory;

        if (resolving_.count(key)) {
            throw ResolutionError(std::string("circular dependency while resolving ") + typeid(T).name());
        }
        resolving_.insert(key);
        std::shared_ptr<void> obj;
        try {
            obj = factory(*this);
        } catch (...) {
            resolving_.erase(key);
            throw;
        }
        resolving_.erase(key);
        if (!obj) throw ResolutionError(std::string("factory returned null for ") + typeid(T).name());

        // Re-lookup: the factory may have registered services and rehashed the map.
        Entry& entry = entries_.at(key);
        if (entry.lifetime == Lifetime::Singleton) entry.instance = obj;
        return std::static_pointer_cast<T>(obj);
    }

private:
    enum class Lifetime { Singleton, Transient };
    struct Entry {
        std::function<std::shared_ptr<void>(ServiceContainer&)> factory;
        Lifetime lifetime;
        std::shared_ptr<void> instance;
    };

    template <typename T>
    void add(Factory<T> factory, Lifetime lifetime) {
        if (!factory) throw std::invalid_argument("factory must not be empty");
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        entries_[std::type_index(typeid(T))] = Entry{
            [f = std::move(factory)](ServiceContainer& c) -> std::shared_ptr<void> { return f(c); },
            lifetime, nullptr};
    }

    mutable std::recursive_mutex mutex_;
    std::unordered_map<std::type_index, Entry> entries_;
    std::unordered_set<std::type_index> resolving_;
};

}  // namespace wa
