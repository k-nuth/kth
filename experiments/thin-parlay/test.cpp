#include "adapters.hpp"

#include <atomic>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
void check(bool v, std::source_location here = std::source_location::current()) {
    if (! v)
        throw std::runtime_error("thin adapter invariant at line " + std::to_string(here.line()));
}
struct Hash {
    std::shared_ptr<std::atomic<unsigned>> calls;
    unsigned salt = 0, mask = 65535;
    size_t operator()(unsigned k) const {
        if (calls)
            ++*calls;
        return (k & mask) ^ salt;
    }
};
struct Equal {
    unsigned mask = 65535;
    bool operator()(unsigned a, unsigned b) const {
        return (a & mask) == (b & mask);
    }
};
using M = kth::experimental::parlay_lookup_map<unsigned, unsigned, Hash, Equal>;
using S = kth::experimental::parlay_lookup_set<unsigned, Hash, Equal>;
static_assert(sizeof(M) == sizeof(M::native_type));
static_assert(sizeof(S) == sizeof(S::native_type));
struct CopyGuard {
    static inline unsigned copies = 0;
    unsigned value = 17;
    CopyGuard() = default;
    CopyGuard(CopyGuard const& other) : value(other.value) {
        ++copies;
    }
};
int main() {
    auto calls = std::make_shared<std::atomic<unsigned>>(0);
    M a(1, Hash{ calls, 19 }, Equal{}), b(1, Hash{ calls, 77, 0xffffff }, Equal{ 0xffffff });
    for (unsigned i = 0; i < 2000; ++i) {
        check(a.try_insert(i, i + 17));
        check(b.try_insert(i, i + 17));
    }
    for (unsigned i = 0; i < 2000; ++i) {
        check(a.visit(i + 65536, [&](auto v) { check(v == i + 17); }));
        check(! b.contains(i + 65536));
    }
    check(*calls > 0);
    unsigned seen = 0;
    a.for_each([&](auto k, auto v) {check(v==k+17);++seen; });
    check(seen == 2000);
    M counters(1, Hash{ calls, 31 }, Equal{});
    std::vector<std::jthread> workers;
    for (unsigned t = 0; t < 8; ++t)
        workers.emplace_back([&] {for(unsigned i=0;i<2000;++i)counters.transform(i%32,[](auto old){return old.value_or(0)+1;}); });
    workers.clear();
    unsigned sum = 0;
    counters.for_each([&](auto, auto v) { sum += v; });
    check(sum == 16000);
    using Strings = kth::experimental::parlay_lookup_map<unsigned, std::string, Hash, Equal>;
    Strings strings(1, Hash{ calls, 13 }, Equal{});
    for (unsigned i = 0; i < 32; ++i)
        strings.try_insert(i, std::to_string(i));
    std::atomic<bool> failed = false;
    workers.emplace_back([&] {for(unsigned i=32;i<10000;++i){strings.try_insert(i,std::to_string(i));if(i%2==0)strings.erase(i);} });
    for (unsigned r = 0; r < 50; ++r) {
        std::vector<unsigned> stable(32);
        strings.for_each([&](auto k, auto const& v) {if(v!=std::to_string(k))failed=true;if(k<32)++stable[k]; });
        for (auto n : stable)
            check(n == 1);
    }
    workers.clear();
    check(! failed);
    S set(1, Hash{ calls, 42 }, Equal{});
    check(set.insert(7));
    check(! set.insert(65543));
    check(set.contains(65543));
    check(set.erase(65543));
    check(set.slow_size() == 0);
    kth::experimental::parlay_lookup_map<unsigned, CopyGuard, Hash, Equal> no_old_value_copy(128, Hash{ calls, 53 }, Equal{});
    CopyGuard payload;
    check(no_old_value_copy.try_insert(1, payload));
    auto before = CopyGuard::copies;
    check(! no_old_value_copy.try_insert(1, payload));
    check(no_old_value_copy.erase(1));
    check(CopyGuard::copies == before);
    std::cout << "PASS: one-member map/set, per-instance hash/equality, growth, transforms, concurrent epoch traversal\n";
}
