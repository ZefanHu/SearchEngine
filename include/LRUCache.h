#ifndef __LRU_CACHE_H__
#define __LRU_CACHE_H__

#include <list>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <utility>
#include <cstddef>

namespace search
{

template <typename K, typename V>
class LRUCache
{
public:
    explicit LRUCache(size_t capacity)
        : _capacity(capacity)
    {
    }

    LRUCache(const LRUCache &) = delete;
    LRUCache &operator=(const LRUCache &) = delete;
    LRUCache(LRUCache &&) = delete;
    LRUCache &operator=(LRUCache &&) = delete;

    bool get(const K &key, V &value)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _map.find(key);
        if (it == _map.end())
        {
            return false;
        }

        _list.splice(_list.begin(), _list, it->second);
        value = it->second->second;
        return true;
    }

    void put(const K &key, const V &value)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _map.find(key);
        if (it != _map.end())
        {
            it->second->second = value;
            _list.splice(_list.begin(), _list, it->second);
            return;
        }

        _list.emplace_front(key, value);
        _map[key] = _list.begin();

        if (_list.size() > _capacity)
        {
            auto last = _list.end();
            --last;
            _map.erase(last->first);
            _list.pop_back();
        }
    }

    std::vector<std::pair<K, V>> getAllEntries()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return std::vector<std::pair<K, V>>(_list.begin(), _list.end());
    }

private:
    std::list<std::pair<K, V>> _list;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> _map;
    size_t _capacity;
    std::mutex _mutex;
};

} // namespace search

#endif
