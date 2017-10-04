#include "MapBasedGlobalLockImpl.h"
#include <algorithm>


#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put (const std::string &key, const std::string &value) {
  {
    std::unique_lock<std::mutex> guard(_lock);
    auto old_it = _backend.find (key);

    if (old_it != _backend.end ())
      {
        auto list_it = std::find (_cache.begin (), _cache.end (), old_it);
        if (list_it == _cache.end ())
          return false; // something strange

        _cache.erase (list_it);
        _backend.erase (old_it);
      }
  }

  return PutIfAbsent (key, value);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
  if (_max_size <= _backend.size ())
    {
      auto it = _cache.back ();
      _backend.erase (it);
      _cache.pop_back ();
    }

  auto inserted = _backend.insert ({key, value});
  if (inserted.second)
    _cache.push_front (inserted.first);

  return inserted.second;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
  auto it = _backend.find (key);

  if (it == _backend.end ())
    return false;

  if (!update_usage (it))
    return false;

  it->second = value;
  return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::unique_lock<std::mutex> guard(_lock);
  auto it = _backend.find (key);

  if (it == _backend.end ())
    return false;

  auto list_it = std::find (_cache.begin (), _cache.end (), it);
  if (list_it == _cache.end ())
    return false; // something strange

  _cache.erase (list_it);
  _backend.erase (it);
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::unique_lock<std::mutex> guard(*const_cast<std::mutex *>(&_lock));
  auto it = _backend.find (key);

  if (it == _backend.end ())
    return false;

  value = it->second;
  return true;
}

bool MapBasedGlobalLockImpl::update_usage (std::map<std::string, std::string>::iterator it)
{
  auto list_it = std::find (_cache.begin (), _cache.end (), it);
  if (list_it == _cache.end ())
    return false; // something strange

  _cache.erase (list_it);
  _cache.push_front (it);
  return true;
}

} // namespace Backend
} // namespace Afina
