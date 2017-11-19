#pragma once

#include <afina/Storage.h>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <deque>

namespace Afina {
namespace Backend {
class MapBasedGlobalLockImpl;

/**
 * # Map based implementation with global lock
 *
 *
 */
class MapBasedStripedImpl : public Afina::Storage {
public:
    MapBasedStripedImpl(size_t max_size = 1024 * 8);
    ~MapBasedStripedImpl() {}

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
    std::deque<MapBasedGlobalLockImpl> buckets;

    size_t num_of_buckets = 8;
    size_t _max_size;

    std::hash<std::string> hash_fn;
};

} // namespace Backend
} // namespace Afina
