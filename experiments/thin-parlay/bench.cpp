#include "adapters.hpp"

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/network/hosts.hpp>

#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/version.hpp>
#ifdef BENCH_EXPECT_BOOST
static_assert(BOOST_VERSION == BENCH_EXPECT_BOOST, "wrong Boost headers selected");
#endif
#include <boost/unordered/concurrent_flat_set.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

namespace {
using U = uint64_t;
using Clock = std::chrono::steady_clock;
void check(bool b) {
    if (! b)
        throw std::runtime_error("benchmark invariant failed");
}
U random(U& x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}
kth::hash_digest hash(U i) {
    kth::hash_digest h{};
    std::memcpy(h.data(), &i, 8);
    U state = i + 0x9e3779b97f4a7c15ULL;
    for (unsigned j = 1; j < 4; ++j) {
        auto v = random(state);
        std::memcpy(h.data() + 8 * j, &v, 8);
    }
    return h;
}
U id(kth::hash_digest const& h) {
    U i;
    std::memcpy(&i, h.data(), 8);
    return i;
}
U low(kth::hash_digest const& h) {
    return id(h);
}
void increment(kth::hash_digest& h) {
    U x = low(h) + 1;
    std::memcpy(h.data(), &x, 8);
}
struct Pool {
    using K = kth::hash_digest;
    using V = kth::blockchain::mempool_entry;
    using H = kth::blockchain::salted_txid_hasher;
    using E = std::equal_to<K>;
    static constexpr bool is_set = false;
    static constexpr char const* name = "mempool";
    static K key(U i) {
        return hash(i);
    }
    static U index(K const& k) {
        return id(k);
    }
    static H hasher() {
        return H{ 19, 77 };
    }
    static V value(U i) {
        V v{};
        v.tx = std::make_shared<kth::domain::message::transaction>();
        v.fee = i + 1;
        v.size = uint32_t(i % 2048 + 64);
        v.sigchecks = uint32_t(i % 17);
        v.entry_id = i;
        return v;
    }
    static U consume(V const& v, V const& expected, bool modified) {
        auto tx = v.tx;
        check(tx && tx == expected.tx && v.size == expected.size && v.sigchecks == expected.sigchecks && v.entry_id == expected.entry_id);
        if (! modified)
            check(v.fee == expected.fee);
        return v.fee + v.size + v.sigchecks + bool(tx);
    }
    static void inc(V& v) {
        ++v.fee;
    }
    static U count(V const& v) {
        return v.fee;
    }
};
struct Spent {
    using K = kth::blockchain::outpoint_key;
    using V = kth::hash_digest;
    using H = kth::blockchain::salted_outpoint_hasher;
    using E = std::equal_to<K>;
    static constexpr bool is_set = false;
    static constexpr char const* name = "spent_by";
    static K key(U i) {
        return { hash(i), uint32_t(i % 4) };
    }
    static U index(K const& k) {
        return id(k.hash);
    }
    static H hasher() {
        return H{ 31, 91 };
    }
    static V value(U i) {
        return hash(i + 1);
    }
    static U consume(V const& v, V const& expected, bool modified) {
        V copy = v;
        asm volatile("" : "+m"(copy));
        check(std::equal(copy.begin() + 8, copy.end(), expected.begin() + 8));
        if (! modified)
            check(copy == expected);
        U sum = 0;
        for (unsigned j = 0; j < 4; ++j) {
            U x;
            std::memcpy(&x, copy.data() + j * 8, 8);
            sum += x;
        }
        return sum;
    }
    static void inc(V& v) {
        increment(v);
    }
    static U count(V const& v) {
        return low(v);
    }
};
// Same SipHash computation as salted_ip_hasher, with an explicit reproducible salt.
struct FixedIPHash {
    U k0 = 19, k1 = 77;
    size_t operator()(::asio::ip::address const& a) const noexcept {
        kth::sip_hasher h(k0, k1);
        if (a.is_v4()) {
            auto b = a.to_v4().to_bytes();
            h.write(b.data(), b.size());
        } else {
            auto b = a.to_v6().to_bytes();
            h.write(b.data(), b.size());
        }
        return size_t(h.finalize());
    }
};
struct Pending {
    using K = ::asio::ip::address;
    using H = FixedIPHash;
    using E = std::equal_to<K>;
    static constexpr bool is_set = true;
    static constexpr char const* name = "pending_ip";
    static K key(U i) {
        return ::asio::ip::address_v4(uint32_t(0x0b000001 + i));
    }
    static U index(K const& k) {
        return k.to_v4().to_uint() - 0x0b000001;
    }
    static H hasher() {
        return {};
    }
};
struct Hosts {
    using K = kth::network::hosts::address;
    using H = kth::network::address_hash;
    using E = kth::network::address_equal;
    static constexpr bool is_set = true;
    static constexpr char const* name = "host_address";
    static K key(U i) {
        return kth::infrastructure::config::authority(Pending::key(i), 8333).to_network_address();
    }
    static U index(K const& k) {
        auto const& b = k.ip();
        U ip = 0;
        for (unsigned j = 12; j < 16; ++j)
            ip = ip * 256 + b[j];
        return ip - 0x0b000001;
    }
    static H hasher() {
        return {};
    }
};
template <class K, class V, class H, class E>
class CfmMap {
    boost::concurrent_flat_map<K, V, H, E> map_;

  public:
    CfmMap(size_t n, H const& h, E const& e) : map_(0, h, e) {
        map_.reserve(n);
    }
    bool try_insert(K const& k, V const& v) {
        return map_.try_emplace(k, v);
    }
    bool erase(K const& k) {
        return map_.erase(k) != 0;
    }
    template <class F>
    bool visit(K const& k, F&& f) const {
        return map_.cvisit(k, [&](auto const& e) { f(e.second); }) != 0;
    }
    template <class F>
    bool update_existing(K const& k, F&& f) {
        return map_.visit(k, [&](auto& entry) { f(entry.second); }) != 0;
    }
    template <class F>
    void for_each(F&& f) const {
        map_.cvisit_all([&](auto const& e) { f(e.first, e.second); });
    }
    size_t slow_size() const {
        return map_.size();
    }
};
template <class K, class H, class E>
class CfmSet {
    boost::concurrent_flat_set<K, H, E> set_;

  public:
    CfmSet(size_t n, H const& h, E const& e) : set_(0, h, e) {
        set_.reserve(n);
    }
    bool insert(K const& k) {
        return set_.insert(k);
    }
    bool erase(K const& k) {
        return set_.erase(k) != 0;
    }
    bool contains(K const& k) const {
        return set_.contains(k);
    }
    template <class F>
    void for_each(F&& f) const {
        set_.cvisit_all(std::forward<F>(f));
    }
    size_t slow_size() const {
        return set_.size();
    }
};
#ifdef BENCH_PARLAY
template <class T>
using Map = kth::experimental::parlay_lookup_map<typename T::K, typename T::V, typename T::H, typename T::E>;
template <class T>
using Set = kth::experimental::parlay_lookup_set<typename T::K, typename T::H, typename T::E>;
constexpr char const* backend = "parlay";
#else
template <class T>
using Map = CfmMap<typename T::K, typename T::V, typename T::H, typename T::E>;
template <class T>
using Set = CfmSet<typename T::K, typename T::H, typename T::E>;
constexpr char const* backend = "cfm";
#endif
struct Stats {
    U ops = 0, checksum = 0;
    int64_t balance = 0;
    std::vector<double> latency;
};
template <class F>
Stats phase(char const* type, char const* name, size_t n, unsigned p, double seconds, bool finite, F f) {
    std::atomic<unsigned> ready{ 0 };
    std::atomic<bool> start{ false }, failed{ false };
    std::vector<std::jthread> workers;
    std::vector<Stats> stats(p);
    std::vector<std::string> errors(p);
    Clock::time_point begin;
    for (unsigned t = 0; t < p; ++t)
        workers.emplace_back([&, t] {
            Stats s;
            U rng = 0x123456789abcdefULL + t * 0x9e3779b97f4a7c15ULL;
            ready.fetch_add(1);
            ready.notify_one();
            start.wait(false);
            try {
                for (U i = t; finite ? i < n : true; i += p) {
                    bool sample = (s.ops % 1024 == 0);
                    auto before = sample ? Clock::now() : Clock::time_point{};
                    f(finite ? i : random(rng) & (n - 1), rng, s);
                    ++s.ops;
                    if (sample)
                        s.latency.push_back(std::chrono::duration<double, std::nano>(Clock::now() - before).count());
                    if (! finite && s.ops % 1024 == 0 && std::chrono::duration<double>(Clock::now() - begin).count() >= seconds)
                        break;
                }
            } catch (std::exception const& e) {
                failed = true;
                errors[t] = e.what();
            }
            stats[t] = std::move(s);
        });
    while (ready.load() != p) {
        auto old = ready.load();
        if (old != p)
            ready.wait(old);
    }
    begin = Clock::now();
    start = true;
    start.notify_all();
    workers.clear();
    double elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    if (failed) {
        for (auto const& e : errors)
            if (! e.empty())
                throw std::runtime_error(e);
    }
    Stats total;
    for (auto const& s : stats) {
        total.ops += s.ops;
        total.checksum += s.checksum;
        total.balance += s.balance;
        total.latency.insert(total.latency.end(), s.latency.begin(), s.latency.end());
    }
    std::sort(total.latency.begin(), total.latency.end());
    check(total.ops > 0);
    std::cout << "{\"backend\":\"" << backend << "\",\"type\":\"" << type << "\",\"phase\":\"" << name << "\",\"n\":" << n << ",\"threads\":" << p << ",\"ops\":" << total.ops << ",\"seconds\":" << elapsed << ",\"mops\":" << double(total.ops) / elapsed / 1e6 << ",\"p50_sample_ns\":" << total.latency[total.latency.size() / 2] << ",\"p99_sample_ns\":" << total.latency[(total.latency.size() - 1) * 99 / 100] << ",\"checksum\":" << total.checksum << ",\"balance\":" << total.balance << "}" << std::endl;
    return total;
}
template <class F>
void walk(char const* type, size_t n, F f) {
    auto start = Clock::now();
    U checksum = f();
    double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    std::cout << "{\"backend\":\"" << backend << "\",\"type\":\"" << type << "\",\"phase\":\"traverse\",\"n\":" << n << ",\"threads\":1,\"ops\":" << n << ",\"seconds\":" << elapsed << ",\"mops\":" << double(n) / elapsed / 1e6 << ",\"checksum\":" << checksum << "}" << std::endl;
}
template <class T>
void run_map(size_t n, unsigned p, double seconds) {
    std::vector<typename T::K> keys;
    std::vector<typename T::V> values;
    keys.reserve(2 * n);
    values.reserve(n);
    for (U i = 0; i < 2 * n; ++i)
        keys.push_back(T::key(i));
    for (U i = 0; i < n; ++i)
        values.push_back(T::value(i));
    Map<T> m(n, T::hasher(), typename T::E{});
    auto insert = [&](U i) { return m.try_insert(keys[i], values[i]); };
    phase(T::name, "insert", n, p, seconds, true, [&](U i, U&, Stats& s) {check(insert(i));++s.balance; });
    check(m.slow_size() == n);
    phase(T::name, "find_hit", n, p, seconds, false, [&](U i, U&, Stats& s) { check(m.visit(keys[i], [&](auto const& v) { s.checksum += T::consume(v, values[i], false); })); });
    phase(T::name, "find_miss", n, p, seconds, false, [&](U i, U&, Stats& s) {check(!m.visit(keys[i+n],[](auto const&){}));++s.checksum; });
    phase(T::name, "duplicate", n, p, seconds, false, [&](U i, U&, Stats& s) {check(!insert(i));++s.checksum; });
    auto mixed = phase(T::name, "mixed_50read_25insert_25erase", n, p, seconds, false, [&](U i, U& rng, Stats& s) {auto op=random(rng)%4;if(op<2)m.visit(keys[i],[&](auto const&v){s.checksum+=T::consume(v,values[i],false);});else if(op==2)s.balance+=insert(i);else s.balance-=m.erase(keys[i]); });
    check(int64_t(m.slow_size()) == int64_t(n) + mixed.balance);
    for (U i = 0; i < n; ++i)
        insert(i);
    check(m.slow_size() == n);
    U expected = 0;
    for (auto const& v : values)
        expected += T::count(v);
    for (bool hot : { false, true }) {
        auto updated = phase(T::name, hot ? "update_hot64_80pct" : "update_uniform", n, p, seconds, false, [&](U i, U& rng, Stats&) {
            if (hot && random(rng) % 5 != 0)
                i &= 63;
#ifdef BENCH_PARLAY
            m.transform(keys[i], [&](auto old) {auto v=old.value_or(values[i]);T::inc(v);return v; });
#else
            check(m.update_existing(keys[i], [](auto& value) { T::inc(value); }));
#endif
        });
        expected += updated.ops;
    }
    U sum = 0, seen = 0;
    walk(T::name, n, [&] {U checksum=0;m.for_each([&](auto const&k,auto const&v){auto i=T::index(k);check(i<n);checksum+=T::consume(v,values[i],true);sum+=T::count(v);++seen;});return checksum; });
    check(sum == expected && seen == n);
    auto before = Clock::now();
    U count = 0;
    for (unsigned i = 0; i < 5; ++i)
        count += m.slow_size();
    check(count == n * 5);
    const double size_ns = std::chrono::duration<double, std::nano>(Clock::now() - before).count() / 5;
    std::cout << "{\"backend\":\"" << backend << "\",\"type\":\"" << T::name << "\",\"phase\":\"size_latency\",\"n\":" << n << ",\"threads\":1,\"calls\":5,\"ns_per_call\":" << size_ns << "}" << std::endl;
    phase(T::name, "erase", n, p, seconds, true, [&](U i, U&, Stats& s) {check(m.erase(keys[i]));--s.balance; });
    check(m.slow_size() == 0);
}
template <class T>
void run_set(size_t n, unsigned p, double seconds) {
    std::vector<typename T::K> keys;
    keys.reserve(2 * n);
    for (U i = 0; i < 2 * n; ++i)
        keys.push_back(T::key(i));
    Set<T> s(n, T::hasher(), typename T::E{});
    phase(T::name, "insert", n, p, seconds, true, [&](U i, U&, Stats& r) {check(s.insert(keys[i]));++r.balance; });
    check(s.slow_size() == n);
    phase(T::name, "find_hit", n, p, seconds, false, [&](U i, U&, Stats& r) {check(s.contains(keys[i]));++r.checksum; });
    phase(T::name, "find_miss", n, p, seconds, false, [&](U i, U&, Stats& r) {check(!s.contains(keys[i+n]));++r.checksum; });
    phase(T::name, "duplicate", n, p, seconds, false, [&](U i, U&, Stats& r) {check(!s.insert(keys[i]));++r.checksum; });
    auto mixed = phase(T::name, "mixed_50read_25insert_25erase", n, p, seconds, false, [&](U i, U& rng, Stats& r) {auto op=random(rng)%4;if(op<2)r.checksum+=s.contains(keys[i]);else if(op==2)r.balance+=s.insert(keys[i]);else r.balance-=s.erase(keys[i]); });
    check(int64_t(s.slow_size()) == int64_t(n) + mixed.balance);
    for (U i = 0; i < n; ++i)
        s.insert(keys[i]);
    U seen = 0;
    walk(T::name, n, [&] {U checksum=0;s.for_each([&](auto const&k){auto i=T::index(k);check(i<n);checksum+=i;++seen;});return checksum; });
    check(seen == n);
    auto before = Clock::now();
    U count = 0;
    for (unsigned i = 0; i < 5; ++i)
        count += s.slow_size();
    check(count == n * 5);
    const double size_ns = std::chrono::duration<double, std::nano>(Clock::now() - before).count() / 5;
    std::cout << "{\"backend\":\"" << backend << "\",\"type\":\"" << T::name << "\",\"phase\":\"size_latency\",\"n\":" << n << ",\"threads\":1,\"calls\":5,\"ns_per_call\":" << size_ns << "}" << std::endl;
    phase(T::name, "erase", n, p, seconds, true, [&](U i, U&, Stats& r) {check(s.erase(keys[i]));--r.balance; });
    check(s.slow_size() == 0);
}
}  // namespace
int main(int argc, char** argv) try {
    check(argc == 5);
    std::string type = argv[1];
    size_t n = std::stoull(argv[2]);
    unsigned p = std::stoul(argv[3]);
    double seconds = std::stod(argv[4]);
    check(n >= 64 && n <= 1048576 && (n & (n - 1)) == 0 && p > 0 && p <= 16 && seconds > 0 && seconds <= 10);
    std::cout << "{\"phase\":\"build_info\",\"backend\":\"" << backend << "\",\"boost_version\":" << BOOST_VERSION << "}" << std::endl;
    if (type == "mempool")
        run_map<Pool>(n, p, seconds);
    else if (type == "spent_by")
        run_map<Spent>(n, p, seconds);
    else if (type == "pending_ip")
        run_set<Pending>(n, p, seconds);
    else if (type == "host_address")
        run_set<Hosts>(n, p, seconds);
    else
        throw std::runtime_error("unknown type");
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << "{\"phase\":\"process_peak_rss\",\"bytes\":" << (
#ifdef __APPLE__
                                                                     usage.ru_maxrss
#else
                                                                     usage.ru_maxrss * 1024L
#endif
                                                                     )
              << "}" << std::endl;
} catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
