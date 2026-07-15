// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// block_pool's store: boost::bimap + upgrade_mutex vs boost::concurrent_flat_map.
//
// Both shapes implement the same five operations the pool actually performs,
// and are measured against the pool's real parameters:
//
//   pool size : blockchain::settings::reorganization_limit, 256
//   getdata   : up to max_inventory (50000); real asks are far smaller
//   writes    : block_organizer::organize, once per accepted block
//
// The bimap's `right` side is a multiset_of<size_t>, ordered by height. The
// only code that reads it walks every entry and filters by hand:
//
//     for (auto it: blocks_.right) {
//         if (it.first != 0 && it.first < minimum_height) { ... }
//
// so the ordering is paid for and not used. That is what makes a flat map a
// candidate at all.
//
//   ./kth_blockchain_bench_block_pool [writer-period-us]

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

namespace {

using hash_digest = std::array<uint8_t, 32>;
using block_ptr = std::shared_ptr<int>;   // stands in for block_const_ptr

struct hash_hasher {
    size_t operator()(hash_digest const& h) const noexcept {
        size_t out;
        std::memcpy(&out, h.data(), sizeof(out));
        return out;
    }
};

// Same shape as block_entry: a hash, an owning pointer, a child list. The
// search-key constructor is what `left.find(block_entry{hash})` costs.
struct entry {
    explicit entry(hash_digest const& h) : hash_(h) {}
    entry(hash_digest const& h, block_ptr b) : hash_(h), block_(std::move(b)) {}

    hash_digest const& hash() const { return hash_; }
    bool operator==(entry const& o) const { return hash_ == o.hash_; }

    hash_digest hash_;
    block_ptr block_;
    mutable std::vector<hash_digest> children_;
};

size_t hash_value(entry const& e) { return hash_hasher{}(e.hash()); }

// What the flat map stores per hash: everything block_entry holds bar the hash
// itself, which is the key.
struct value {
    block_ptr block;
    size_t height;
    std::vector<hash_digest> children;
};

struct inv {
    bool is_block_type() const { return type_ == 2; }
    hash_digest const& hash() const { return hash_; }
    uint32_t type_;
    hash_digest hash_;
};

// ---------------------------------------------------------------------------
// A: what the pool has today.

class bimap_pool {
public:
    using store = boost::bimaps::bimap<
        boost::bimaps::unordered_set_of<entry>,
        boost::bimaps::multiset_of<size_t>>;

    size_t size() const {
        std::shared_lock lock(mutex_);
        return blocks_.size();
    }

    void add(hash_digest const& hash, size_t height) {
        std::unique_lock lock(mutex_);
        if (blocks_.left.find(entry{hash}) == blocks_.left.end()) {
            blocks_.insert({entry{hash, std::make_shared<int>(1)}, height});
        }
    }

    bool exists(hash_digest const& hash) const {
        std::shared_lock lock(mutex_);
        return blocks_.left.find(entry{hash}) != blocks_.left.end();
    }

    void remove(std::vector<hash_digest> const& hashes) {
        std::unique_lock lock(mutex_);
        for (auto const& h : hashes) {
            auto it = blocks_.left.find(entry{h});
            if (it != blocks_.left.end()) {
                blocks_.left.erase(it);
            }
        }
    }

    // The ordered side, walked in full and filtered by hand.
    std::vector<hash_digest> below(size_t minimum_height) const {
        std::shared_lock lock(mutex_);
        std::vector<hash_digest> out;
        for (auto it : blocks_.right) {
            if (it.first != 0 && it.first < minimum_height) {
                out.push_back(it.second.hash());
            }
        }
        return out;
    }

    void filter(std::vector<inv>& invs) const {
        std::shared_lock lock(mutex_);
        auto const& left = blocks_.left;
        std::erase_if(invs, [&left](inv const& i) {
            return i.is_block_type() && left.find(entry{i.hash()}) != left.end();
        });
    }

private:
    store blocks_;
    mutable std::shared_mutex mutex_;
};

// ---------------------------------------------------------------------------
// B: the same five operations over a concurrent flat map, no mutex.

class cfm_pool {
public:
    size_t size() const { return blocks_.size(); }

    void add(hash_digest const& hash, size_t height) {
        blocks_.emplace(hash, value{std::make_shared<int>(1), height, {}});
    }

    bool exists(hash_digest const& hash) const {
        return blocks_.contains(hash);
    }

    void remove(std::vector<hash_digest> const& hashes) {
        for (auto const& h : hashes) {
            blocks_.erase(h);
        }
    }

    std::vector<hash_digest> below(size_t minimum_height) const {
        std::vector<hash_digest> out;
        blocks_.cvisit_all([&](auto const& kv) {
            if (kv.second.height != 0 && kv.second.height < minimum_height) {
                out.push_back(kv.first);
            }
        });
        return out;
    }

    void filter(std::vector<inv>& invs) const {
        std::erase_if(invs, [this](inv const& i) {
            return i.is_block_type() && blocks_.contains(i.hash());
        });
    }

private:
    boost::concurrent_flat_map<hash_digest, value, hash_hasher> blocks_;
};


// ---------------------------------------------------------------------------
// C: copy-on-write. The profile is 50k-lookup read bursts against a 256-entry
// store written once per accepted block, so readers take a snapshot and touch
// no synchronisation at all; writers copy the whole thing (~18KB) and swap.

class cow_pool {
public:
    using store = boost::unordered_flat_map<hash_digest, value, hash_hasher>;

    cow_pool() : blocks_(std::make_shared<store const>()) {}

    size_t size() const { return snapshot()->size(); }

    void add(hash_digest const& hash, size_t height) {
        std::lock_guard lock(write_mutex_);
        auto next = std::make_shared<store>(*snapshot());
        next->emplace(hash, value{std::make_shared<int>(1), height, {}});
        blocks_.store(std::move(next));
    }

    bool exists(hash_digest const& hash) const {
        return snapshot()->contains(hash);
    }

    void remove(std::vector<hash_digest> const& hashes) {
        std::lock_guard lock(write_mutex_);
        auto next = std::make_shared<store>(*snapshot());
        for (auto const& h : hashes) {
            next->erase(h);
        }
        blocks_.store(std::move(next));
    }

    std::vector<hash_digest> below(size_t minimum_height) const {
        auto const snap = snapshot();
        std::vector<hash_digest> out;
        for (auto const& [hash, v] : *snap) {
            if (v.height != 0 && v.height < minimum_height) {
                out.push_back(hash);
            }
        }
        return out;
    }

    void filter(std::vector<inv>& invs) const {
        auto const snap = snapshot();   // one atomic load for the whole pass
        std::erase_if(invs, [&snap](inv const& i) {
            return i.is_block_type() && snap->contains(i.hash());
        });
    }

private:
    std::shared_ptr<store const> snapshot() const { return blocks_.load(); }

    std::atomic<std::shared_ptr<store const>> blocks_;
    std::mutex write_mutex_;   // serialises writers against each other only
};

// ---------------------------------------------------------------------------

hash_digest make_hash(std::mt19937_64& rng) {
    hash_digest h{};
    for (size_t i = 0; i < h.size(); i += 8) {
        auto const v = rng();
        std::memcpy(h.data() + i, &v, 8);
    }
    return h;
}

constexpr size_t pool_size = 256;   // reorganization_limit

struct fixture {
    std::vector<hash_digest> in_pool;
    std::vector<hash_digest> absent;
    std::vector<inv> getdata_small;   // 500 -- a typical ask
    std::vector<inv> getdata_large;   // 50000 -- max_inventory
};

fixture make_fixture() {
    fixture f;
    std::mt19937_64 rng(42);
    for (size_t i = 0; i < pool_size; ++i) {
        f.in_pool.push_back(make_hash(rng));
    }
    for (size_t i = 0; i < pool_size; ++i) {
        f.absent.push_back(make_hash(rng));
    }
    auto build = [&](size_t n) {
        std::vector<inv> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            bool const hit = (i % 4) == 0;
            out.push_back({2u, hit ? f.in_pool[i % pool_size] : make_hash(rng)});
        }
        return out;
    };
    f.getdata_small = build(500);
    f.getdata_large = build(50000);
    return f;
}

template <typename Pool>
void fill(Pool& pool, fixture const& f) {
    for (size_t i = 0; i < f.in_pool.size(); ++i) {
        pool.add(f.in_pool[i], i + 1);
    }
}

// Writers on the real path: block_organizer::organize takes the exclusive lock
// once per accepted block.
template <typename Pool>
struct writer {
    writer(Pool& pool, fixture const& f, size_t period_us)
        : pool_(pool), f_(f), period_us_(period_us) {
        if (period_us_ == 0) {
            return;
        }
        thread_ = std::thread([this] {
            std::mt19937_64 r(7);
            while ( ! stop_.load(std::memory_order_relaxed)) {
                auto const i = r() % f_.in_pool.size();
                pool_.remove({f_.in_pool[i]});
                pool_.add(f_.in_pool[i], i + 1);
                std::this_thread::sleep_for(std::chrono::microseconds(period_us_));
            }
        });
    }

    ~writer() {
        stop_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    Pool& pool_;
    fixture const& f_;
    size_t period_us_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

template <typename Pool>
void run(ankerl::nanobench::Bench& bench, char const* name, fixture const& f, size_t period_us) {
    Pool pool;
    fill(pool, f);
    writer<Pool> w(pool, f, period_us);

    std::mt19937_64 rng(11);

    bench.run(std::string(name) + " exists (hit)", [&] {
        ankerl::nanobench::doNotOptimizeAway(pool.exists(f.in_pool[rng() % pool_size]));
    });

    bench.run(std::string(name) + " exists (miss)", [&] {
        ankerl::nanobench::doNotOptimizeAway(pool.exists(f.absent[rng() % pool_size]));
    });

    bench.run(std::string(name) + " add + remove", [&] {
        auto const& h = f.absent[rng() % pool_size];
        pool.add(h, 1);
        pool.remove({h});
    });

    bench.run(std::string(name) + " below (prune scan)", [&] {
        ankerl::nanobench::doNotOptimizeAway(pool.below(pool_size / 2));
    });

    bench.run(std::string(name) + " filter 500", [&] {
        auto v = f.getdata_small;
        pool.filter(v);
        ankerl::nanobench::doNotOptimizeAway(v.size());
    });

    bench.run(std::string(name) + " filter 50000", [&] {
        auto v = f.getdata_large;
        pool.filter(v);
        ankerl::nanobench::doNotOptimizeAway(v.size());
    });
}

} // namespace

int main(int argc, char** argv) {
    // Gap between writes. organize() fires once per accepted block: ~600s in
    // steady state, milliseconds apart during an IBD burst. 0 = no writer.
    size_t const period_us = (argc > 1) ? std::stoul(argv[1]) : 0;

    auto const f = make_fixture();

    ankerl::nanobench::Bench bench;
    bench.title(period_us == 0
        ? "block_pool store, no writer"
        : "block_pool store, writer every " + std::to_string(period_us) + "us")
        .relative(true)
        .minEpochIterations(50);

    run<bimap_pool>(bench, "bimap+mutex", f, period_us);
    run<cfm_pool>(bench, "concurrent ", f, period_us);
    run<cow_pool>(bench, "cow        ", f, period_us);
    return 0;
}
