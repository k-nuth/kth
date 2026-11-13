// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Deferred signature checks.
//
// A script interpreter cannot batch signatures by itself: CHECKSIG needs an
// answer before the next opcode. With NULLFAIL (every BCH block since
// November 2017) a non-empty signature that fails verification fails the
// whole script, so for those checks the interpreter may *assume* validity,
// record the check here, and let the block validator verify everything it
// collected in one lane-parallel batch at the end. A failed batch rejects
// the block exactly where a failed CHECKSIG would have.
//
// Legacy ECDSA CHECKMULTISIG tries keys in order and tolerates failed
// checks, so it must never defer; the interpreters open an allow_scope only
// around the opcodes where a failure is final.
//
// All state is thread-local: a validation worker calls begin(), runs its
// scripts, then end().

#ifndef KTH_SECP256K1_TURBO_DEFERRED_HPP
#define KTH_SECP256K1_TURBO_DEFERRED_HPP

#include <cstddef>
#include <cstdint>

namespace secp256k1_turbo::deferred {

// Starts (or restarts) collecting on the calling thread.
void begin();
// True between begin() and end() on this thread.
bool active();
// True when active and inside an allow_scope: the caller may record the
// check and report it as valid.
bool allowed();

class allow_scope {
public:
    explicit allow_scope(bool on);
    ~allow_scope();
    allow_scope(allow_scope const&) = delete;
    allow_scope& operator=(allow_scope const&) = delete;
private:
    bool prev_;
};

// Records one check. The ECDSA signature is compact r||s, already low-S
// normalized by the caller; the Schnorr signature is the 64-byte BCH form.
// pubkey is the serialized key as it appeared in the script (33 or 65 bytes;
// any other size is recorded as invalid).
void record_ecdsa(std::uint8_t const* sig64, std::uint8_t const* msg32, std::uint8_t const* pubkey, std::size_t pubkey_size);
void record_schnorr(std::uint8_t const* sig64, std::uint8_t const* msg32, std::uint8_t const* pubkey, std::size_t pubkey_size);

struct result {
    std::size_t total = 0;    // checks recorded
    std::size_t invalid = 0;  // checks that failed verification
};

// Verifies everything recorded on this thread with the best available
// backend, clears the collection and deactivates.
result end();

// Number of checks recorded so far on this thread.
std::size_t pending();

} // namespace secp256k1_turbo::deferred

#endif
