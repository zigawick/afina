#include "MapBasedStripedImpl.h"
#include "storage/GlobalLock/MapBasedGlobalLockImpl.h"
#include <algorithm>

#include <mutex>

namespace Afina {
namespace Backend {

bool MapBasedStripedImpl::Put(const std::string &key, const std::string &value) {
  auto res = hash_fn (key);
  return buckets[res%num_of_buckets].Put (key, value);
}

bool MapBasedStripedImpl::PutIfAbsent(const std::string &key, const std::string &value) {
  auto res = hash_fn (key);
  return buckets[res%num_of_buckets].PutIfAbsent (key, value);
}

bool MapBasedStripedImpl::Set(const std::string &key, const std::string &value) {
  auto res = hash_fn (key);
  return buckets[res%num_of_buckets].Set (key, value);
}

bool MapBasedStripedImpl::Delete(const std::string &key) {
  auto res = hash_fn (key);
  return buckets[res%num_of_buckets].Delete (key);
}

bool MapBasedStripedImpl::Get(const std::string &key, std::string &value) const {
  auto res = hash_fn (key);
  return buckets[res%num_of_buckets].Get (key, value);
}


MapBasedStripedImpl::MapBasedStripedImpl(size_t max_size) : _max_size(max_size)
{
  for (int i = 0; i< num_of_buckets; i++)
    {
      buckets.emplace_back (max_size / num_of_buckets);
    }
}

} // namespace Backend
} // namespace Afina
