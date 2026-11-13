// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <secp256k1_turbo/deferred.hpp>

#include <cstring>
#include <vector>

#include <secp256k1_turbo/batch_verify.hpp>

namespace secp256k1_turbo::deferred {

namespace {

struct item {
    std::uint8_t sig[64];
    std::uint8_t msg[32];
    std::uint8_t pubkey[65];
    std::size_t pubkey_size;  // 0 when the key cannot be valid
};

struct state {
    bool active = false;
    bool allowed = false;
    std::vector<item> ecdsa;
    std::vector<item> schnorr;
};

state& tls() {
    thread_local state s;
    return s;
}

void push(std::vector<item>& v, std::uint8_t const* sig64, std::uint8_t const* msg32, std::uint8_t const* pubkey, std::size_t pubkey_size) {
    item it;
    std::memcpy(it.sig, sig64, 64);
    std::memcpy(it.msg, msg32, 32);
    if (pubkey_size == 33 || pubkey_size == 65) {
        std::memcpy(it.pubkey, pubkey, pubkey_size);
        it.pubkey_size = pubkey_size;
    } else {
        it.pubkey_size = 0;
    }
    v.push_back(it);
}

std::size_t verify_all(std::vector<item> const& items, bool schnorr) {
    if (items.empty()) return 0;
    std::vector<signature_input> in;
    in.reserve(items.size());
    for (auto const& it : items) in.push_back({it.sig, it.msg, it.pubkey, it.pubkey_size});
    std::vector<std::uint8_t> out(items.size());
    backend const b = best_backend(in.size());
    std::size_t const valid = schnorr ? verify_schnorr_bch(in, out, b) : verify_ecdsa(in, out, b);
    return items.size() - valid;
}

} // namespace

void begin() {
    state& s = tls();
    s.ecdsa.clear();
    s.schnorr.clear();
    s.allowed = false;
    s.active = true;
}

bool active() { return tls().active; }
bool allowed() { state const& s = tls(); return s.active && s.allowed; }

allow_scope::allow_scope(bool on) : prev_(tls().allowed) { tls().allowed = on; }
allow_scope::~allow_scope() { tls().allowed = prev_; }

void record_ecdsa(std::uint8_t const* sig64, std::uint8_t const* msg32, std::uint8_t const* pubkey, std::size_t pubkey_size) {
    push(tls().ecdsa, sig64, msg32, pubkey, pubkey_size);
}

void record_schnorr(std::uint8_t const* sig64, std::uint8_t const* msg32, std::uint8_t const* pubkey, std::size_t pubkey_size) {
    push(tls().schnorr, sig64, msg32, pubkey, pubkey_size);
}

std::size_t pending() { state const& s = tls(); return s.ecdsa.size() + s.schnorr.size(); }

result end() {
    state& s = tls();
    result r;
    r.total = s.ecdsa.size() + s.schnorr.size();
    r.invalid = verify_all(s.ecdsa, false) + verify_all(s.schnorr, true);
    s.ecdsa.clear();
    s.schnorr.clear();
    s.allowed = false;
    s.active = false;
    return r;
}

} // namespace secp256k1_turbo::deferred
