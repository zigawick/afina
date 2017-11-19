#include "MapBasedSharedMutexImpl.h"
#include <algorithm>

#include <mutex>

namespace Afina {
namespace Backend {

struct writer_num {

    writer_num(std::atomic_int &w) : w_(w) { w_ += 1; }
    ~writer_num() {
        if (w_ > 0)
            w_ -= 1;
    }
    std::atomic_int &w_;
};

bool MapBasedSharedMutexImpl::Put(const std::string &key, const std::string &value) {
    {
        writer_num num(write_num);
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto old_it = _backend.find(key);

        if (old_it != _backend.end()) {
            auto list_it = std::find(_cache.begin(), _cache.end(), old_it);
            if (list_it == _cache.end())
                return false; // something strange

            _cache.erase(list_it);
            _backend.erase(old_it);
        }
        write_num -= 1;
    }

    return PutIfAbsent(key, value);
}

bool MapBasedSharedMutexImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    writer_num num(write_num);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (_max_size <= _backend.size()) {
        auto it = _cache.back();
        _backend.erase(it);
        _cache.pop_back();
    }
    auto inserted = _backend.insert({key, value});
    if (inserted.second)
        _cache.push_front(inserted.first);

    return inserted.second;
}

bool MapBasedSharedMutexImpl::Set(const std::string &key, const std::string &value) {
    writer_num num(write_num);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = _backend.find(key);

    if (it == _backend.end())
        return false;

    if (!update_usage(it))
        return false;

    it->second = value;
    return true;
}

bool MapBasedSharedMutexImpl::Delete(const std::string &key) {
    writer_num num(write_num);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = _backend.find(key);

    if (it == _backend.end())
        return false;

    auto list_it = std::find(_cache.begin(), _cache.end(), it);
    if (list_it == _cache.end())
        return false; // something strange

    _cache.erase(list_it);
    _backend.erase(it);
    return true;
}

bool MapBasedSharedMutexImpl::Get(const std::string &key, std::string &value) const {

    std::shared_lock<std::shared_mutex> lock(mutex_);
    cv.wait(lock, [this] { return write_num == 0; });
    auto it = _backend.find(key);

    if (it == _backend.end())
        return false;

    value = it->second;
    return true;
}

bool MapBasedSharedMutexImpl::update_usage(std::map<std::string, std::string>::iterator it) {
    auto list_it = std::find(_cache.begin(), _cache.end(), it);
    if (list_it == _cache.end())
        return false; // something strange

    _cache.erase(list_it);
    _cache.push_front(it);
    return true;
}

} // namespace Backend
} // namespace Afina
