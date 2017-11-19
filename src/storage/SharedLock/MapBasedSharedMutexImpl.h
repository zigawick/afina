#pragma once

#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <shared_mutex>
#include <atomic>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation with global lock
 *
 *
 */
class MapBasedSharedMutexImpl : public Afina::Storage {
public:
    MapBasedSharedMutexImpl(size_t max_size = 1024) : _max_size(max_size), write_num(0) {}
    ~MapBasedSharedMutexImpl() {}

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) const override;

private:
    bool update_usage(std::map<std::string, std::string>::iterator it);

    mutable std::shared_mutex mutex_;
    mutable std::atomic_int write_num;
    mutable std::condition_variable_any cv;

    size_t _max_size;

    std::map<std::string, std::string> _backend;
    std::list<std::map<std::string, std::string>::iterator> _cache;
};

} // namespace Backend
} // namespace Afina

