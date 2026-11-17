// Experimental operation adapters; each owns exactly one native container.
#pragma once
#include <functional>
#include <parlay_hash/unordered_map.h>
#include <parlay_hash/unordered_set.h>
#include <utility>
namespace kth::experimental {
template <class K, class V, class Hash = std::hash<K>, class Equal = std::equal_to<K>>
class parlay_lookup_map {
  public:
    using native_type = parlay::parlay_unordered_map<K, V, Hash, Equal>;
    explicit parlay_lookup_map(std::size_t n = 0, Hash const& hash = Hash{}, Equal const& equal = Equal{}) : map_(long(n), hash, equal) {}
    bool try_insert(K const& k, V const& v) {
        return ! map_.Insert(k, v, [](auto const&) { return 0; }).has_value();
    }
    bool erase(K const& k) {
        return map_.Remove(k, [](auto const&) { return 0; }).has_value();
    }
    template <class F>
    bool visit(K const& k, F&& f) const {
        return map_.Find(k, [&](auto const& e) {std::invoke(f,e.second);return 0; }).has_value();
    }
    bool contains(K const& k) const {
        return visit(k, [](auto const&) {});
    }
    // Pure transform: can run more than once; never mutate external state.
    template <class F>
    auto transform(K const& k, F&& f) {
        return map_.Upsert(k, std::forward<F>(f));
    }
    template <class F>
    void for_each(F&& f) const {
        map_.ForEach([&](auto const& e) { std::invoke(f, e.first, e.second); });
    }
    // Explicit name: native size scans the table. No fake constant-time contract.
    std::size_t slow_size() const {
        return std::size_t(map_.size());
    }

  private:
    mutable native_type map_;
};
template <class K, class Hash = std::hash<K>, class Equal = std::equal_to<K>>
class parlay_lookup_set {
  public:
    using native_type = parlay::parlay_unordered_set<K, Hash, Equal>;
    explicit parlay_lookup_set(std::size_t n = 0, Hash const& hash = Hash{}, Equal const& eq = Equal{}) : set_(long(n), hash, eq) {}
    bool insert(K const& k) {
        return set_.Insert(k);
    }
    bool contains(K const& k) const {
        return set_.Find(k);
    }
    bool erase(K const& k) {
        return set_.Remove(k);
    }
    template <class F>
    void for_each(F&& f) const {
        set_.ForEach([&](auto const& key) { std::invoke(f, key); });
    }
    std::size_t slow_size() const {
        return std::size_t(set_.size());
    }

  private:
    mutable native_type set_;
};
}  // namespace kth::experimental
