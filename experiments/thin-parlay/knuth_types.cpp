#include "adapters.hpp"

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/network/banlist.hpp>
#include <kth/network/hosts.hpp>

#include <iostream>
#include <stdexcept>
void check(bool v) {
    if (! v)
        throw std::runtime_error("Knuth type invariant");
}
int main() {
    using namespace kth;
    using namespace kth::blockchain;
    using namespace kth::experimental;
    using Pool = parlay_lookup_map<hash_digest, mempool_entry, salted_txid_hasher>;
    using Spent = parlay_lookup_map<outpoint_key, hash_digest, salted_outpoint_hasher>;
    using Pending = parlay_lookup_set<::asio::ip::address, salted_ip_hasher>;
    using Hosts = parlay_lookup_set<network::hosts::address, network::address_hash, network::address_equal>;
    static_assert(sizeof(Pool) == sizeof(Pool::native_type));
    static_assert(sizeof(Spent) == sizeof(Spent::native_type));
    static_assert(sizeof(Pending) == sizeof(Pending::native_type));
    static_assert(sizeof(Hosts) == sizeof(Hosts::native_type));
    Pool pool(128, salted_txid_hasher{ 19, 77 });
    Spent spent(128, salted_outpoint_hasher{ 31, 91 });
    Pending pending(128);
    Hosts hosts(128);
    hash_digest hash{};
    hash[0] = 7;
    mempool_entry entry{};
    entry.fee = 42;
    check(pool.try_insert(hash, entry));
    check(! pool.try_insert(hash, entry));
    check(pool.visit(hash, [](auto const& e) { check(e.fee == 42); }));
    outpoint_key out{ hash, 3 };
    check(spent.try_insert(out, hash));
    check(spent.visit(out, [&](auto const& v) { check(v == hash); }));
    auto ip = ::asio::ip::make_address("11.12.13.14");
    check(pending.insert(ip));
    check(pending.contains(ip));
    check(pending.erase(ip));
    auto address = infrastructure::config::authority(ip, 8333).to_network_address();
    check(hosts.insert(address));
    check(hosts.contains(address));
    check(hosts.erase(address));
    check(pool.erase(hash));
    check(spent.erase(out));
    std::cout << "PASS real Knuth mempool, spent-by, IP and host-address types; one member each\n";
}
