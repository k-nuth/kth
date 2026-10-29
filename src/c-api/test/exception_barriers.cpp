// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Exception-barrier tests. Built only with -DKTH_CAPI_EXCEPTION_BARRIERS=ON
// as the separate `kth_capi_exception_test` executable, because it replaces
// the global allocation functions (and, on Linux, wraps `malloc` at link
// time) to make real C++ allocations fail on demand.
//
// Unless a test says "helper only", it calls exported, generated C-API
// functions linked against the real kth libraries. The injected fault is
// thrown from inside the real allocation the wrapper performs, so the C++
// exception travels through the generated code exactly as a production
// failure would. Faults are one-shot and armed only around the single call
// under test, on the calling thread, so Catch2's own allocations are never
// affected.

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/binary.h>
#include <kth/capi/chain/chain_sync.h>
#include <kth/capi/config/settings.h>
#include <kth/capi/hash_list.h>
#include <kth/capi/node.h>
#include <kth/capi/domain/chain/block.h>
#include <kth/capi/domain/chain/header.h>
#include <kth/capi/domain/chain/transaction_list.h>
#include <kth/capi/domain/message/merkle_block.h>
#include <kth/capi/domain/chain/input.h>
#include <kth/capi/domain/chain/input_list.h>
#include <kth/capi/domain/chain/output.h>
#include <kth/capi/domain/chain/output_list.h>
#include <kth/capi/domain/chain/output_point.h>
#include <kth/capi/domain/chain/script.h>
#include <kth/capi/domain/chain/transaction.h>
#include <kth/capi/domain/chain/utxo.h>
#include <kth/capi/domain/chain/utxo_list.h>
#include <kth/capi/domain/message/compact_block.h>
#include <kth/capi/error.h>
#include <kth/capi/handles.h>
#include <kth/capi/detail/mining_template.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/capi/primitives.h>
#include <kth/capi/chain/token_capability.h>
#include <kth/capi/domain/chain/token_data.h>
#include <kth/capi/hash.h>
#include <kth/capi/u32_list.h>
#include <kth/capi/u64_list.h>
#include <kth/capi/vm/big_number.h>
#include <kth/capi/vm/number.h>
#include <kth/capi/vm/program.h>
#include <kth/capi/wallet/bitcoin_uri.h>
#include <kth/capi/wallet/cashtoken_minting.h>
#include <kth/capi/wallet/coin_selection.h>
#include <kth/capi/wallet/coin_selection_result.h>
#include <kth/capi/wallet/payment_address.h>
#include <kth/capi/wallet/payment_address_list.h>
#include <kth/capi/wallet/wallet_manager.h>

#if ! defined(KTH_CAPI_EXCEPTION_BARRIERS)
#error "exception_barriers.cpp requires KTH_CAPI_EXCEPTION_BARRIERS"
#endif

// ---------------------------------------------------------------------------
// Fault injection
// ---------------------------------------------------------------------------

namespace {

enum class fault_kind { bad_alloc, runtime_error, logic_error, invalid_argument, custom };

struct custom_fault {};

// Armed state is per thread; counters are global.
struct new_fault_state {
    bool armed = false;
    fault_kind kind = fault_kind::bad_alloc;
    std::size_t skip = 0;   // allocations to let through before the fault
};

// Volatile: the compiler treats `malloc` as touching no other memory, so
// without it the arming stores can be optimized away when the code under
// test is inlined into this file.
struct malloc_fault_state {
    volatile bool armed = false;
    volatile std::size_t size = 0;
};

thread_local new_fault_state t_new_fault;
thread_local malloc_fault_state t_malloc_fault;

std::atomic<long> g_new_faults_fired{0};
std::atomic<long> g_malloc_faults_fired{0};
std::atomic<long> g_live_allocations{0};

[[noreturn]] void throw_fault(fault_kind kind) {
    switch (kind) {
        case fault_kind::bad_alloc:        throw std::bad_alloc();
        case fault_kind::runtime_error:    throw std::runtime_error("injected runtime_error");
        case fault_kind::logic_error:      throw std::logic_error("injected logic_error");
        case fault_kind::invalid_argument: throw std::invalid_argument("injected invalid_argument");
        case fault_kind::custom:           throw custom_fault{};
    }
    std::abort();
}

} // namespace

#if defined(KTH_CAPI_TEST_MALLOC_WRAP)
// Linked with -Wl,--wrap=malloc: every `malloc` reference in the executable,
// including the ones inside libc-api and the kth libraries, lands here.
extern "C" void* __real_malloc(std::size_t size);

extern "C" void* __wrap_malloc(std::size_t size) {
    if (t_malloc_fault.armed && size == t_malloc_fault.size) {
        t_malloc_fault.armed = false;
        ++g_malloc_faults_fired;
        errno = ENOMEM;
        return nullptr;
    }
    return __real_malloc(size);
}
#endif

namespace {

// Allocation used by the replaced `operator new`. It bypasses the malloc
// fault so only direct `malloc` callers (the C-API buffer helpers) see it.
void* raw_allocate(std::size_t size) {
#if defined(KTH_CAPI_TEST_MALLOC_WRAP)
    return __real_malloc(size != 0 ? size : 1);
#else
    return std::malloc(size != 0 ? size : 1);
#endif
}

} // namespace

void* operator new(std::size_t size) {
    if (t_new_fault.armed && t_new_fault.skip > 0) {
        --t_new_fault.skip;
    } else if (t_new_fault.armed) {
        // Disarm first: building the exception object may allocate.
        t_new_fault.armed = false;
        ++g_new_faults_fired;
        throw_fault(t_new_fault.kind);
    }
    void* p = raw_allocate(size);
    if (p == nullptr) throw std::bad_alloc();
    ++g_live_allocations;
    return p;
}

void operator delete(void* p) noexcept {
    if (p == nullptr) return;
    --g_live_allocations;
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    ::operator delete(p);
}

namespace {

// Arms a one-shot fault for the lifetime of the object.
class new_fault_scope {
public:
    explicit new_fault_scope(fault_kind kind, std::size_t skip = 0) {
        t_new_fault.kind = kind;
        t_new_fault.skip = skip;
        t_new_fault.armed = true;
    }
    ~new_fault_scope() { t_new_fault.armed = false; }
    new_fault_scope(new_fault_scope const&) = delete;
    new_fault_scope& operator=(new_fault_scope const&) = delete;
};

#if defined(KTH_CAPI_TEST_MALLOC_WRAP)
class malloc_fault_scope {
public:
    explicit malloc_fault_scope(std::size_t size) {
        t_malloc_fault.size = size;
        t_malloc_fault.armed = true;
    }
    ~malloc_fault_scope() { t_malloc_fault.armed = false; }
    malloc_fault_scope(malloc_fault_scope const&) = delete;
    malloc_fault_scope& operator=(malloc_fault_scope const&) = delete;
};
#endif

// Outcome of exactly one invocation of a C function.
struct invocation {
    int calls = 0;
    bool escaped = false;      // a C++ exception left the C function
    kth_error_code_t ec = kth_ec_success;
};

template <typename Call>
invocation invoke_once(Call&& call) {
    invocation r;
    try {
        ++r.calls;
        r.ec = call();
    } catch (...) {
        r.escaped = true;
    }
    return r;
}

// Runs `call` once with an allocation fault of `kind` armed for it.
template <typename Call>
invocation invoke_once_with_new_fault(fault_kind kind, Call&& call) {
    new_fault_scope scope(kind);
    return invoke_once(std::forward<Call>(call));
}

// Like the above, but lets `skip` allocations succeed first so the fault lands
// on a later allocation of the same call.
template <typename Call>
invocation invoke_once_with_new_fault_after(std::size_t skip, fault_kind kind, Call&& call) {
    new_fault_scope scope(kind, skip);
    return invoke_once(std::forward<Call>(call));
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

kth_hash_t const kPrev = {{
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30
}};

uint8_t const kBody[3] = { 0x51, 0x51, 0x93 };

#if defined(KTH_CURRENCY_BCH)
constexpr kth_script_flags_t kForkIdFlags = kth_script_flags_bch_sighash_forkid;
#else
constexpr kth_script_flags_t kForkIdFlags = kth_script_flags_bip143_rule;
#endif

kth_script_mut_t make_script() {
    kth_script_mut_t s = nullptr;
    REQUIRE(kth_domain_chain_script_construct_from_data(kBody, sizeof(kBody), 0, &s) == kth_ec_success);
    REQUIRE(s != nullptr);
    return s;
}

kth_input_list_mut_t make_inputs() {
    kth_input_list_mut_t list = kth_domain_chain_input_list_construct_default();
    kth_output_point_mut_t op = kth_domain_chain_output_point_construct_from_hash_index(&kPrev, 3);
    kth_script_mut_t sc = make_script();
    kth_input_mut_t in = kth_domain_chain_input_construct(op, sc, 0xfffffffeu);
    kth_domain_chain_input_list_push_back(list, in);
    kth_domain_chain_input_destruct(in);
    kth_domain_chain_script_destruct(sc);
    kth_domain_chain_output_point_destruct(op);
    return list;
}

kth_output_list_mut_t make_outputs() {
    kth_output_list_mut_t list = kth_domain_chain_output_list_construct_default();
    kth_script_mut_t sc = make_script();
    kth_output_mut_t out = kth_domain_chain_output_construct(12345ull, sc, nullptr);
    kth_domain_chain_output_list_push_back(list, out);
    kth_domain_chain_output_destruct(out);
    kth_domain_chain_script_destruct(sc);
    return list;
}

// Drives the real `kth_domain_chain_transaction_construct_move` with `kind`
// injected into its allocation, and checks the whole failure contract.
void check_construct_move_fault(fault_kind kind, kth_error_code_t expected) {
    long const live_before = g_live_allocations.load();
    kth_input_list_mut_t ins = make_inputs();
    kth_output_list_mut_t outs = make_outputs();
    long const live_with_lists = g_live_allocations.load();
    kth_transaction_mut_t out = nullptr;
    long const fired_before = g_new_faults_fired.load();

    auto const r = invoke_once_with_new_fault(kind, [&] {
        return kth_domain_chain_transaction_construct_move(7u, 99u, &ins, &outs, &out);
    });
    long const live_after = g_live_allocations.load();
    long const fired = g_new_faults_fired.load() - fired_before;

    REQUIRE(fired == 1);                 // the real allocation threw, once
    REQUIRE(r.calls == 1);
    REQUIRE_FALSE(r.escaped);            // no C++ exception left the C function
    REQUIRE(r.ec == expected);
    REQUIRE(out == nullptr);             // nothing published
    REQUIRE(ins == nullptr);             // consumed handles are nulled...
    REQUIRE(outs == nullptr);
    REQUIRE(live_with_lists > live_before);
    REQUIRE(live_after == live_before);  // ...and their storage was released

    // The fault was one-shot: the same call now succeeds.
    ins = make_inputs();
    outs = make_outputs();
    auto const ok = invoke_once([&] {
        return kth_domain_chain_transaction_construct_move(7u, 99u, &ins, &outs, &out);
    });
    REQUIRE(ok.calls == 1);
    REQUIRE_FALSE(ok.escaped);
    REQUIRE(ok.ec == kth_ec_success);
    REQUIRE(out != nullptr);
    REQUIRE(ins == nullptr);
    REQUIRE(outs == nullptr);
    kth_domain_chain_transaction_destruct(out);
}

} // namespace

// ---------------------------------------------------------------------------
// Boundary error codes
// ---------------------------------------------------------------------------

TEST_CASE("C-API exception barrier - boundary error codes are stable and distinct",
          "[C-API exception barrier]") {
    REQUIRE(kth_ec_out_of_memory == 10000);
    REQUIRE(kth_ec_unhandled_exception == 10001);
    REQUIRE(kth_ec_out_of_memory != kth_ec_unhandled_exception);
    REQUIRE(kth_ec_success == 0);
    REQUIRE(kth_ec_out_of_range < kth_ec_out_of_memory);
    REQUIRE(kth_ec_operand_size_mismatch < kth_ec_out_of_memory);
}

// ---------------------------------------------------------------------------
// Generated function + real C++ exceptions thrown from its allocation
// ---------------------------------------------------------------------------

TEST_CASE("C-API exception barrier - construct_move maps std::bad_alloc to out_of_memory",
          "[C-API exception barrier]") {
    check_construct_move_fault(fault_kind::bad_alloc, kth_ec_out_of_memory);
}

TEST_CASE("C-API exception barrier - construct_move maps std::runtime_error to unhandled_exception",
          "[C-API exception barrier]") {
    check_construct_move_fault(fault_kind::runtime_error, kth_ec_unhandled_exception);
}

TEST_CASE("C-API exception barrier - construct_move maps std::logic_error to unhandled_exception",
          "[C-API exception barrier]") {
    check_construct_move_fault(fault_kind::logic_error, kth_ec_unhandled_exception);
}

TEST_CASE("C-API exception barrier - construct_move maps std::invalid_argument to unhandled_exception",
          "[C-API exception barrier]") {
    check_construct_move_fault(fault_kind::invalid_argument, kth_ec_unhandled_exception);
}

TEST_CASE("C-API exception barrier - construct_move maps a non-std exception to unhandled_exception",
          "[C-API exception barrier]") {
    check_construct_move_fault(fault_kind::custom, kth_ec_unhandled_exception);
}

TEST_CASE("C-API exception barrier - from_data maps an injected exception and keeps the out slot null",
          "[C-API exception barrier]") {
    // A second generated function (different shape: borrowed bytes in, one
    // owned handle out) so the result is not specific to construct_move.
    uint8_t const wire[10] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    kth_transaction_mut_t out = nullptr;
    long const fired_before = g_new_faults_fired.load();
    auto const r = invoke_once_with_new_fault(fault_kind::bad_alloc, [&] {
        return kth_domain_chain_transaction_construct_from_data(wire, sizeof(wire), 1, &out);
    });
    REQUIRE(g_new_faults_fired.load() - fired_before == 1);
    REQUIRE(r.calls == 1);
    REQUIRE_FALSE(r.escaped);
    REQUIRE(r.ec == kth_ec_out_of_memory);
    REQUIRE(out == nullptr);

    auto const ok = invoke_once([&] {
        return kth_domain_chain_transaction_construct_from_data(wire, sizeof(wire), 1, &out);
    });
    REQUIRE_FALSE(ok.escaped);
    REQUIRE(ok.ec == kth_ec_success);
    REQUIRE(out != nullptr);
    kth_domain_chain_transaction_destruct(out);
}

// ---------------------------------------------------------------------------
// Without a fault the barrier is transparent
// ---------------------------------------------------------------------------

TEST_CASE("C-API exception barrier - domain errors keep their exact code",
          "[C-API exception barrier]") {
    uint8_t const truncated[3] = { 1, 0, 0 };
    kth_transaction_mut_t out = nullptr;
    auto const r = invoke_once([&] {
        return kth_domain_chain_transaction_construct_from_data(truncated, sizeof(truncated), 1, &out);
    });
    REQUIRE(r.calls == 1);
    REQUIRE_FALSE(r.escaped);
    REQUIRE(r.ec != kth_ec_success);
    REQUIRE(r.ec != kth_ec_out_of_memory);
    REQUIRE(r.ec != kth_ec_unhandled_exception);
    REQUIRE(r.ec == kth_ec_read_past_end_of_buffer);
    REQUIRE(out == nullptr);
}

TEST_CASE("C-API exception barrier - create_endorsement failure keeps the domain code and outputs untouched",
          "[C-API exception barrier]") {
    kth_input_list_mut_t ins = make_inputs();
    kth_output_list_mut_t outs = make_outputs();
    kth_transaction_mut_t tx = nullptr;
    REQUIRE(kth_domain_chain_transaction_construct_move(7u, 99u, &ins, &outs, &tx) == kth_ec_success);
    kth_script_mut_t prevout = make_script();

    kth_hash_t const zero_secret = {{ 0 }};   // not a valid secp256k1 secret
    uint8_t* out = nullptr;
    kth_size_t out_size = 0xdeadu;
    auto const r = invoke_once([&] {
        return kth_domain_chain_script_create_endorsement(
            &zero_secret, prevout, tx, 0, 0x41, kForkIdFlags, 1000,
            kth_endorsement_type_ecdsa, &out, &out_size);
    });
    REQUIRE(r.calls == 1);
    REQUIRE_FALSE(r.escaped);
    REQUIRE(r.ec != kth_ec_success);
    REQUIRE(r.ec != kth_ec_out_of_memory);
    REQUIRE(r.ec != kth_ec_unhandled_exception);
    REQUIRE(out == nullptr);
    REQUIRE(out_size == 0xdeadu);
    kth_domain_chain_script_destruct(prevout);
    kth_domain_chain_transaction_destruct(tx);
}

TEST_CASE("C-API exception barrier - create_endorsement success publishes an owned buffer",
          "[C-API exception barrier]") {
    kth_input_list_mut_t ins = make_inputs();
    kth_output_list_mut_t outs = make_outputs();
    kth_transaction_mut_t tx = nullptr;
    REQUIRE(kth_domain_chain_transaction_construct_move(7u, 99u, &ins, &outs, &tx) == kth_ec_success);
    kth_script_mut_t prevout = make_script();

    kth_hash_t secret = {{ 0 }};
    secret.hash[31] = 1;
    uint8_t* out = nullptr;
    kth_size_t out_size = 0;
    auto const r = invoke_once([&] {
        return kth_domain_chain_script_create_endorsement(
            &secret, prevout, tx, 0, 0x41, kForkIdFlags, 1000,
            kth_endorsement_type_ecdsa, &out, &out_size);
    });
    REQUIRE(r.calls == 1);
    REQUIRE_FALSE(r.escaped);
    REQUIRE(r.ec == kth_ec_success);
    REQUIRE(out != nullptr);
    REQUIRE(out_size > 0);
    kth_core_destruct_array(out);
    kth_domain_chain_script_destruct(prevout);
    kth_domain_chain_transaction_destruct(tx);
}

// ---------------------------------------------------------------------------
// Other classes: every exception type, at every allocation of the call
// ---------------------------------------------------------------------------
//
// For each function the same fault is injected at the 1st, 2nd, 3rd... heap
// allocation of the call, until the call completes without the fault firing.
// Wherever it fired, the C function must return the boundary code for that
// exception type, publish nothing (output slots keep their initial null),
// and leave no allocation behind. The whole attempt, fixtures included, is
// measured, so a leaked partially-built output shows up as `leaked != 0`.
// No Catch assertion runs inside a measured region (its own bookkeeping could
// allocate); checks are recorded and asserted afterwards.

namespace {

struct fault_case {
    fault_kind kind;
    kth_error_code_t expected;
};

constexpr fault_case kFaultCases[] = {
    { fault_kind::bad_alloc,        kth_ec_out_of_memory },
    { fault_kind::runtime_error,    kth_ec_unhandled_exception },
    { fault_kind::logic_error,      kth_ec_unhandled_exception },
    { fault_kind::invalid_argument, kth_ec_unhandled_exception },
    { fault_kind::custom,           kth_ec_unhandled_exception },
};

struct attempt_result {
    bool fired = false;
    int failed_check = 0;   // id of the first failed check, 0 if none
    long leaked = 0;        // C++ allocations still live after the attempt
};

class checks {
public:
    void operator()(bool condition, int id) {
        if ( ! condition && first_ == 0) first_ = id;
    }
    int first() const { return first_; }
private:
    int first_ = 0;
};

// Runs `call` once with the fault armed `skip` allocations in, and judges it.
template <typename Call, typename Untouched, typename Published>
attempt_result judge(fault_case fc, std::size_t skip, Call&& call,
                     Untouched&& untouched, Published&& published) {
    attempt_result r;
    checks chk;
    long const fired_before = g_new_faults_fired.load();
    invocation const inv = invoke_once_with_new_fault_after(skip, fc.kind, call);
    r.fired = g_new_faults_fired.load() != fired_before;
    chk(inv.calls == 1, 1);
    chk( ! inv.escaped, 2);             // no C++ exception left the C function
    if (r.fired) {
        chk(inv.ec == fc.expected, 3);  // exception type -> boundary code
        chk(untouched(), 4);            // nothing published
    } else {
        chk(inv.ec == kth_ec_success, 5);
        chk(published(), 6);
    }
    r.failed_check = chk.first();
    return r;
}

template <typename Attempt>
void sweep_allocation_failures(Attempt&& attempt, std::size_t limit = 512) {
    for (std::size_t skip = 0; skip < limit; ++skip) {
        attempt_result const r = attempt(skip);
        INFO("fault after " << skip << " allocations, failed check " << r.failed_check);
        REQUIRE(r.failed_check == 0);
        REQUIRE(r.leaked == 0);
        if ( ! r.fired) {
            REQUIRE(skip > 0);   // the call allocates at least once
            return;
        }
    }
    FAIL("the call never completed");
}

// Fixtures built without Catch assertions (they run in measured regions).
kth_script_mut_t quiet_script() {
    kth_script_mut_t s = nullptr;
    if (kth_domain_chain_script_construct_from_data(kBody, sizeof(kBody), 0, &s) != kth_ec_success) return nullptr;
    return s;
}

// A program with two `1` values on its main stack.
struct program_fixture {
    program_fixture() {
        script = quiet_script();
        program = script != nullptr ? kth_vm_program_construct_from_script(script) : nullptr;
        for (int i = 0; i < 3 && program != nullptr; ++i) kth_vm_program_push(program, 1);
    }
    ~program_fixture() {
        kth_vm_program_destruct(program);
        kth_domain_chain_script_destruct(script);
    }
    program_fixture(program_fixture const&) = delete;
    program_fixture& operator=(program_fixture const&) = delete;
    kth_script_mut_t script = nullptr;
    kth_program_mut_t program = nullptr;
};

char const* const kAddrDest = "bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz";
char const* const kAddrChange = "bitcoincash:qrhea03074073ff3zv9whh0nggxc7k03ssh8jv9mkx";

} // namespace

TEST_CASE("C-API exception barrier - number from_int contains every exception type",
          "[C-API exception barrier][vm]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                kth_number_mut_t out = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_vm_number_from_int(1234, &out); },
                    [&] { return out == nullptr; },
                    [&] { return out != nullptr && kth_vm_number_int64(out) == 1234; });
                kth_vm_number_destruct(out);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - number safe_add keeps its borrowed inputs and the out slot untouched",
          "[C-API exception barrier][vm]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                kth_number_mut_t x = nullptr;
                kth_number_mut_t y = nullptr;
                (void)kth_vm_number_from_int(40, &x);
                (void)kth_vm_number_from_int(2, &y);
                kth_number_mut_t out = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_vm_number_safe_add_number2(x, y, &out); },
                    [&] { return out == nullptr && kth_vm_number_int64(x) == 40
                                 && kth_vm_number_int64(y) == 2; },
                    [&] { return out != nullptr && kth_vm_number_int64(out) == 42; });
                kth_vm_number_destruct(out);
                kth_vm_number_destruct(y);
                kth_vm_number_destruct(x);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - header from_data contains every exception type",
          "[C-API exception barrier][chain]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                kth_hash_t const merkle = kPrev;
                kth_header_mut_t source = kth_domain_chain_header_construct(1u, &kPrev, &merkle, 1000u, 0x1d00ffffu, 7u);
                kth_size_t size = 0;
                uint8_t* raw = kth_domain_chain_header_to_data(source, 1, &size);
                kth_header_mut_t out = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_domain_chain_header_construct_from_data(raw, size, 1, &out); },
                    [&] { return out == nullptr; },
                    [&] { return out != nullptr && kth_domain_chain_header_equals(source, out) != 0; });
                kth_domain_chain_header_destruct(out);
                kth_core_destruct_array(raw);
                kth_domain_chain_header_destruct(source);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - binary parse_from contains every exception type",
          "[C-API exception barrier][core]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                kth_binary_mut_t out = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_core_binary_parse_from("101010101", &out); },
                    [&] { return out == nullptr; },
                    [&] { return out != nullptr && kth_core_binary_size(out) == 9u; });
                kth_core_binary_destruct(out);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - payment_address parse_from_simple contains every exception type",
          "[C-API exception barrier][wallet]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                kth_payment_address_mut_t out = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_wallet_payment_address_parse_from_simple(kAddrDest, &out); },
                    [&] { return out == nullptr; },
                    [&] { return out != nullptr; });
                kth_wallet_payment_address_destruct(out);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - bitcoin_uri parse_from contains every exception type",
          "[C-API exception barrier][wallet]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                kth_bitcoin_uri_mut_t out = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_wallet_bitcoin_uri_parse_from(
                              "bitcoin:113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD?amount=0.001&label=Donation", 1, &out); },
                    [&] { return out == nullptr; },
                    [&] { return out != nullptr; });
                kth_wallet_bitcoin_uri_destruct(out);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

// ---------------------------------------------------------------------------
// Several owned outputs: a fault after some of them were built must roll back
// ---------------------------------------------------------------------------

TEST_CASE("C-API exception barrier - program pop_binary publishes both outputs or neither",
          "[C-API exception barrier][vm][multi-output]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                program_fixture f;
                kth_number_mut_t a = nullptr;
                kth_number_mut_t b = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_vm_program_pop_binary(f.program, &a, &b); },
                    [&] { return a == nullptr && b == nullptr; },
                    [&] { return a != nullptr && b != nullptr
                                 && kth_vm_number_int64(a) == 1 && kth_vm_number_int64(b) == 1; });
                kth_vm_number_destruct(b);
                kth_vm_number_destruct(a);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - program pop_ternary publishes all three outputs or none",
          "[C-API exception barrier][vm][multi-output]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                program_fixture f;
                kth_number_mut_t a = nullptr;
                kth_number_mut_t b = nullptr;
                kth_number_mut_t c = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_vm_program_pop_ternary(f.program, &a, &b, &c); },
                    [&] { return a == nullptr && b == nullptr && c == nullptr; },
                    [&] { return a != nullptr && b != nullptr && c != nullptr; });
                kth_vm_number_destruct(c);
                kth_vm_number_destruct(b);
                kth_vm_number_destruct(a);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - program pop_big_ternary publishes all three outputs or none",
          "[C-API exception barrier][vm][multi-output]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                program_fixture f;
                kth_big_number_mut_t a = nullptr;
                kth_big_number_mut_t b = nullptr;
                kth_big_number_mut_t c = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_vm_program_pop_big_ternary(f.program, &a, &b, &c); },
                    [&] { return a == nullptr && b == nullptr && c == nullptr; },
                    [&] { return a != nullptr && b != nullptr && c != nullptr; });
                kth_vm_big_number_destruct(c);
                kth_vm_big_number_destruct(b);
                kth_vm_big_number_destruct(a);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

TEST_CASE("C-API exception barrier - coin_selection create_tx_template publishes all four outputs or none",
          "[C-API exception barrier][wallet][multi-output]") {
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            attempt_result r;
            {
                kth_utxo_list_mut_t utxos = kth_domain_chain_utxo_list_construct_default();
                uint64_t const amounts[] = { 100000u, 200000u, 300000u };
                for (uint32_t i = 0; i < 3; ++i) {
                    kth_output_point_mut_t op = kth_domain_chain_output_point_construct_from_hash_index(&kPrev, i);
                    kth_utxo_mut_t u = kth_domain_chain_utxo_construct(op, amounts[i], nullptr);
                    kth_domain_chain_utxo_list_push_back(utxos, u);
                    kth_domain_chain_utxo_destruct(u);
                    kth_domain_chain_output_point_destruct(op);
                }
                kth_payment_address_mut_t dest = nullptr;
                kth_payment_address_mut_t change = nullptr;
                (void)kth_wallet_payment_address_parse_from_simple(kAddrDest, &dest);
                (void)kth_wallet_payment_address_parse_from_simple(kAddrChange, &change);
                kth_payment_address_list_mut_t change_addresses = kth_wallet_payment_address_list_construct_default();
                kth_wallet_payment_address_list_push_back(change_addresses, change);

                kth_transaction_mut_t out_tx = nullptr;
                kth_u32_list_mut_t out_indices = nullptr;
                kth_payment_address_list_mut_t out_addresses = nullptr;
                kth_u64_list_mut_t out_amounts = nullptr;
                r = judge(fc, skip,
                    [&] { return kth_wallet_coin_selection_create_tx_template_default_ratios(
                              utxos, 50000u, dest, change_addresses,
                              kth_coin_selection_algorithm_largest_first,
                              &out_tx, &out_indices, &out_addresses, &out_amounts); },
                    [&] { return out_tx == nullptr && out_indices == nullptr
                                 && out_addresses == nullptr && out_amounts == nullptr; },
                    [&] { return out_tx != nullptr && out_indices != nullptr
                                 && out_addresses != nullptr && out_amounts != nullptr; });
                kth_domain_chain_transaction_destruct(out_tx);
                kth_core_u32_list_destruct(out_indices);
                kth_wallet_payment_address_list_destruct(out_addresses);
                kth_core_u64_list_destruct(out_amounts);
                kth_wallet_payment_address_list_destruct(change_addresses);
                kth_wallet_payment_address_destruct(change);
                kth_wallet_payment_address_destruct(dest);
                kth_domain_chain_utxo_list_destruct(utxos);
            }
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        });
    }
}

// ---------------------------------------------------------------------------
// Status wrappers generated from declarative specs
// ---------------------------------------------------------------------------
//
// The cashtoken builders and the seed decryption are generated by
// capi_status_wrappers.py. Each publishes exactly one owned handle through
// `out`: the fault is injected at the 1st, 2nd, 3rd... allocation of the call
// (conversions of the arguments, the builder itself, the heap promotion of its
// result), and the slot must stay null, with no allocation left behind,
// wherever it lands.

namespace {

kth_utxo_mut_t quiet_utxo(uint32_t index, uint64_t amount, kth_token_data_const_t token = nullptr) {
    kth_output_point_mut_t op = kth_domain_chain_output_point_construct_from_hash_index(&kPrev, index);
    kth_utxo_mut_t u = kth_domain_chain_utxo_construct(op, amount, token);
    kth_domain_chain_output_point_destruct(op);
    return u;
}

kth_utxo_list_mut_t quiet_utxo_list(kth_utxo_const_t a) {
    kth_utxo_list_mut_t list = kth_domain_chain_utxo_list_construct_default();
    kth_domain_chain_utxo_list_push_back(list, a);
    return list;
}

kth_hash_t const kCategory = {{
    0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01
}};

uint8_t const kCommitment[2] = { 0x01, 0x02 };

// Faults every kind at every allocation of `call`, which must publish one
// owned handle of type `Handle` through the pointer it is given.
template <typename Handle, typename Call, typename Destruct>
void sweep_single_owned_output(Call&& call, Destruct&& destruct) {
    {   // The fixture must really succeed, or the sweep proves nothing.
        Handle good = nullptr;
        REQUIRE(call(&good) == kth_ec_success);
        REQUIRE(good != nullptr);
        destruct(good);
    }
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            Handle out = nullptr;
            attempt_result r = judge(fc, skip,
                [&] { return call(&out); },
                [&] { return out == nullptr; },
                [&] { return out != nullptr; });
            destruct(out);
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        }, 4096);
    }
}

} // namespace

TEST_CASE("C-API exception barrier - cashtoken prepare_genesis_utxo publishes the result or nothing",
          "[C-API exception barrier][cashtoken]") {
    kth_payment_address_mut_t dest = nullptr;
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrDest, &dest) == kth_ec_success);
    kth_utxo_mut_t utxo = quiet_utxo(3, 50000u);
    sweep_single_owned_output<kth_cashtoken_prepare_genesis_result_mut_t>(
        [&](kth_cashtoken_prepare_genesis_result_mut_t* out) {
            return kth_wallet_cashtoken_prepare_genesis_utxo(utxo, dest, 10000u, nullptr, out);
        },
        [](auto h) { kth_wallet_cashtoken_prepare_genesis_result_destruct(h); });
    kth_domain_chain_utxo_destruct(utxo);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API exception barrier - cashtoken create_token_genesis publishes the result or nothing",
          "[C-API exception barrier][cashtoken]") {
    kth_payment_address_mut_t dest = nullptr;
    kth_payment_address_mut_t change = nullptr;
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrDest, &dest) == kth_ec_success);
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrChange, &change) == kth_ec_success);
    kth_utxo_mut_t genesis = quiet_utxo(0, 50000u);
    kth_utxo_mut_t fee = quiet_utxo(1, 200000u);
    kth_utxo_list_mut_t fees = quiet_utxo_list(fee);
    kth_cashtoken_nft_spec_mut_t nft = kth_wallet_cashtoken_nft_spec_construct(
        kth_token_capability_mut, kCommitment, sizeof(kCommitment));
    sweep_single_owned_output<kth_cashtoken_token_genesis_result_mut_t>(
        [&](kth_cashtoken_token_genesis_result_mut_t* out) {
            return kth_wallet_cashtoken_create_token_genesis(
                genesis, dest, 1, 1000u, nft, 1000u, fees, change, 0u, out);
        },
        [](auto h) { kth_wallet_cashtoken_token_genesis_result_destruct(h); });
    kth_wallet_cashtoken_nft_spec_destruct(nft);
    kth_domain_chain_utxo_list_destruct(fees);
    kth_domain_chain_utxo_destruct(fee);
    kth_domain_chain_utxo_destruct(genesis);
    kth_wallet_payment_address_destruct(change);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API exception barrier - cashtoken create_ft publishes the result or nothing",
          "[C-API exception barrier][cashtoken]") {
    kth_payment_address_mut_t dest = nullptr;
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrDest, &dest) == kth_ec_success);
    kth_utxo_mut_t genesis = quiet_utxo(0, 50000u);
    sweep_single_owned_output<kth_cashtoken_token_genesis_result_mut_t>(
        [&](kth_cashtoken_token_genesis_result_mut_t* out) {
            return kth_wallet_cashtoken_create_ft(genesis, dest, 1000000u, 1, nullptr, nullptr, 0u, out);
        },
        [](auto h) { kth_wallet_cashtoken_token_genesis_result_destruct(h); });
    kth_domain_chain_utxo_destruct(genesis);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API exception barrier - cashtoken create_token_mint publishes the result or nothing",
          "[C-API exception barrier][cashtoken]") {
    kth_payment_address_mut_t dest = nullptr;
    kth_payment_address_mut_t change = nullptr;
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrDest, &dest) == kth_ec_success);
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrChange, &change) == kth_ec_success);
    kth_token_data_mut_t minting = kth_domain_chain_token_make_non_fungible(
        &kCategory, kth_token_capability_minting, nullptr, 0);
    REQUIRE(minting != nullptr);
    kth_utxo_mut_t source = quiet_utxo(0, 100000u, minting);
    kth_utxo_mut_t fee = quiet_utxo(1, 200000u);
    kth_utxo_list_mut_t fees = quiet_utxo_list(fee);
    kth_cashtoken_nft_mint_request_list_mut_t requests =
        kth_wallet_cashtoken_nft_mint_request_list_construct_default();
    for (int i = 0; i < 2; ++i) {
        kth_cashtoken_nft_mint_request_mut_t request = kth_wallet_cashtoken_nft_mint_request_construct(
            dest, kCommitment, sizeof(kCommitment), kth_token_capability_none, 1000u);
        kth_wallet_cashtoken_nft_mint_request_list_push_back(requests, request);
        kth_wallet_cashtoken_nft_mint_request_destruct(request);
    }
    uint8_t const new_commitment[1] = { 0x07 };
    sweep_single_owned_output<kth_cashtoken_token_mint_result_mut_t>(
        [&](kth_cashtoken_token_mint_result_mut_t* out) {
            return kth_wallet_cashtoken_create_token_mint(
                source, requests, new_commitment, sizeof(new_commitment), dest, fees, change, 0u, out);
        },
        [](auto h) { kth_wallet_cashtoken_token_mint_result_destruct(h); });
    kth_wallet_cashtoken_nft_mint_request_list_destruct(requests);
    kth_domain_chain_utxo_list_destruct(fees);
    kth_domain_chain_utxo_destruct(fee);
    kth_domain_chain_utxo_destruct(source);
    kth_domain_chain_token_data_destruct(minting);
    kth_wallet_payment_address_destruct(change);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API exception barrier - cashtoken create_token_transfer and create_token_burn publish the result or nothing",
          "[C-API exception barrier][cashtoken]") {
    kth_payment_address_mut_t dest = nullptr;
    kth_payment_address_mut_t change = nullptr;
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrDest, &dest) == kth_ec_success);
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrChange, &change) == kth_ec_success);
    kth_token_data_mut_t fungible = kth_domain_chain_token_make_fungible(&kCategory, 1000u);
    REQUIRE(fungible != nullptr);
    kth_utxo_mut_t token_utxo = quiet_utxo(0, 100000u, fungible);
    kth_utxo_list_mut_t token_utxos = quiet_utxo_list(token_utxo);
    kth_utxo_mut_t fee = quiet_utxo(1, 200000u);
    kth_utxo_list_mut_t fees = quiet_utxo_list(fee);

    sweep_single_owned_output<kth_cashtoken_token_tx_result_mut_t>(
        [&](kth_cashtoken_token_tx_result_mut_t* out) {
            return kth_wallet_cashtoken_create_token_transfer(
                token_utxos, dest, 1, 400u, nullptr, fees, change, change, 1000u, out);
        },
        [](auto h) { kth_wallet_cashtoken_token_tx_result_destruct(h); });

    sweep_single_owned_output<kth_cashtoken_token_tx_result_mut_t>(
        [&](kth_cashtoken_token_tx_result_mut_t* out) {
            return kth_wallet_cashtoken_create_token_burn(
                token_utxo, 1, 1000u, 0, "burn", dest, fees, change, 1000u, out);
        },
        [](auto h) { kth_wallet_cashtoken_token_tx_result_destruct(h); });

    kth_domain_chain_utxo_list_destruct(fees);
    kth_domain_chain_utxo_destruct(fee);
    kth_domain_chain_utxo_list_destruct(token_utxos);
    kth_domain_chain_utxo_destruct(token_utxo);
    kth_domain_chain_token_data_destruct(fungible);
    kth_wallet_payment_address_destruct(change);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API exception barrier - cashtoken create_nft_collection publishes the result or nothing",
          "[C-API exception barrier][cashtoken]") {
    kth_payment_address_mut_t creator = nullptr;
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrDest, &creator) == kth_ec_success);
    kth_utxo_mut_t genesis = quiet_utxo(0, 1000000u);
    kth_cashtoken_nft_collection_item_list_mut_t items =
        kth_wallet_cashtoken_nft_collection_item_list_construct_default();
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t const commitment[1] = { static_cast<uint8_t>(i + 1) };
        kth_cashtoken_nft_collection_item_mut_t item =
            kth_wallet_cashtoken_nft_collection_item_construct(commitment, 1u, nullptr);
        kth_wallet_cashtoken_nft_collection_item_list_push_back(items, item);
        kth_wallet_cashtoken_nft_collection_item_destruct(item);
    }
    sweep_single_owned_output<kth_cashtoken_nft_collection_result_mut_t>(
        [&](kth_cashtoken_nft_collection_result_mut_t* out) {
            return kth_wallet_cashtoken_create_nft_collection(
                genesis, items, creator, 0, 0, 0u, nullptr, nullptr, 2u, 0u, out);
        },
        [](auto h) { kth_wallet_cashtoken_nft_collection_result_destruct(h); });
    kth_wallet_cashtoken_nft_collection_item_list_destruct(items);
    kth_domain_chain_utxo_destruct(genesis);
    kth_wallet_payment_address_destruct(creator);
}

TEST_CASE("C-API exception barrier - cashtoken builders keep ordinary domain errors and leave the slot null",
          "[C-API exception barrier][cashtoken]") {
    kth_payment_address_mut_t dest = nullptr;
    REQUIRE(kth_wallet_payment_address_parse_from_simple(kAddrDest, &dest) == kth_ec_success);
    kth_utxo_mut_t not_genesis = quiet_utxo(3, 50000u);   // outpoint index != 0
    kth_cashtoken_token_genesis_result_mut_t out = nullptr;
    auto const r = invoke_once([&] {
        return kth_wallet_cashtoken_create_token_genesis(
            not_genesis, dest, 1, 1000u, nullptr, 1000u, nullptr, nullptr, 0u, &out);
    });
    REQUIRE_FALSE(r.escaped);
    REQUIRE(r.ec != kth_ec_success);
    REQUIRE(r.ec != kth_ec_out_of_memory);
    REQUIRE(r.ec != kth_ec_unhandled_exception);
    REQUIRE(out == nullptr);
    kth_domain_chain_utxo_destruct(not_genesis);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API exception barrier - wallet decrypt_seed publishes the seed or nothing",
          "[C-API exception barrier][wallet]") {
    // The cipher has no integrity check, so any 96 bytes decrypt. Derive the
    // key once outside the sweep to warm whatever the first call initializes.
    kth_encrypted_seed_t encrypted;
    for (std::size_t i = 0; i < sizeof(encrypted.hash); ++i) encrypted.hash[i] = static_cast<uint8_t>(i * 7 + 3);
    {
        kth_longhash_t* warm = nullptr;
        REQUIRE(kth_wallet_decrypt_seed("correct horse", &encrypted, &warm) == kth_ec_success);
        REQUIRE(warm != nullptr);
        kth_longhash_destruct(warm);
    }
    for (auto const& fc : kFaultCases) {
        sweep_allocation_failures([&](std::size_t skip) {
            long const live_before = g_live_allocations.load();
            kth_longhash_t* out = nullptr;
            attempt_result r = judge(fc, skip,
                [&] { return kth_wallet_decrypt_seed("correct horse", &encrypted, &out); },
                [&] { return out == nullptr; },
                [&] { return out != nullptr; });
            kth_longhash_destruct(out);
            r.leaked = g_live_allocations.load() - live_before;
            return r;
        }, 4096);
    }
}

// ---------------------------------------------------------------------------
// Synchronous chain queries against a real node
// ---------------------------------------------------------------------------
//
// A regtest node runs in-process (chain only; inbound/outbound connections, peers and seeds disabled), so these
// calls cross the real executor: the wrapper creates the coroutine on the
// caller's thread, blocks on a future, and converts the answer on the caller's
// thread. Faults are thread-local, so they land on the caller-thread
// allocations only (the coroutine frame, the future's shared state, and the
// output conversions); the node's worker threads are never faulted, and
// nothing here claims otherwise.
//
// The v1 node cannot accept a block through `organize` (not implemented), so a
// fresh chain holds only the genesis header: the header, timestamp, hash and
// height queries succeed and are swept for their success path, while the
// queries that need a stored block body answer with their ordinary domain
// error, which is swept as well. The success-path staging of those (owned
// block, merkle/compact block, transaction, mining template) is covered at the
// helper level below.

namespace {

// An owned, uniquely named temporary directory (standard C++ only): the name
// carries a clock reading and a counter, and `create_directory` reports whether
// this call created it, so an existing directory is never adopted.
std::filesystem::path make_temp_directory() {
    static std::atomic<unsigned> counter{0};
    auto const base = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 1000; ++attempt) {
        auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto const candidate = base / ("kth-capi-sync-" + std::to_string(stamp) + "-"
                                       + std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec) && ! ec) return candidate;
    }
    return {};
}

struct sync_node {
    sync_node() {
        auto const root = make_temp_directory();
        if (root.empty()) return;
        dir = root.string();
        db_dir = (root / "db").string();
        hosts = (root / "hosts.cache").string();
        debug = (root / "debug.log").string();
        error = (root / "error.log").string();
        archive = (root / "archive").string();
        // `defaults` owns what kth_config_settings_default allocated and is
        // never modified, so kth_config_settings_destruct can release every
        // field (it deletes the inner members and then the struct itself, so
        // it needs a heap object). `settings` is a shallow working copy whose
        // fields are overridden below; the overrides are borrowed from the
        // strings above and are not freed.
        defaults.reset(new kth_settings(kth_config_settings_default(kth_network_regtest)));
        settings = *defaults;
        settings.database.directory = const_cast<char*>(db_dir.c_str());
        settings.database.db_max_size = 256ull << 20;
        settings.network.inbound_connections = 0;
        settings.network.outbound_connections = 0;
        settings.network.peer_count = 0;
        settings.network.seed_count = 0;
        settings.network.hosts_file = const_cast<char*>(hosts.c_str());
        settings.network.debug_file = const_cast<char*>(debug.c_str());
        settings.network.error_file = const_cast<char*>(error.c_str());
        settings.network.archive_directory = const_cast<char*>(archive.c_str());
        node = kth_node_construct(&settings, 0);
        if (node == nullptr) return;
        if (kth_node_initchain(node) == 0) return;
        // The start modules argument is ignored by the synchronous start.
        if (kth_node_init_run_sync(node, kth_start_modules_just_chain) != kth_ec_success) return;
        chain = kth_node_get_chain(node);
    }
    ~sync_node() {
        if (node != nullptr) {
            kth_node_close(node);
            kth_node_destruct(node);
        }
        if ( ! dir.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(dir, ignored);
        }
    }
    sync_node(sync_node const&) = delete;
    sync_node& operator=(sync_node const&) = delete;

    std::string dir, db_dir, hosts, debug, error, archive;
    std::unique_ptr<kth_settings, decltype(&kth_config_settings_destruct)> defaults{
        nullptr, &kth_config_settings_destruct};
    kth_settings settings{};
    kth_node_t node = nullptr;
    kth_chain_t chain = nullptr;
};

constexpr kth_size_t kUnset = 0xdeadbeefu;
constexpr uint64_t kUnset64 = 0xdeadbeefdeadbeefull;

// Judges one attempt of a call whose unfaulted outcome is `ok_code`. When the
// fault fires, `untouched()` must hold (an exception leaves every output as the
// caller had it); when it does not, `settled()` must hold (the unfaulted
// result: published outputs on success, the historical domain-error values
// otherwise).
template <typename Call, typename Untouched, typename Settled>
attempt_result judge_with(fault_case fc, std::size_t skip, kth_error_code_t ok_code, Call&& call,
                          Untouched&& untouched, Settled&& settled) {
    attempt_result r;
    checks chk;
    long const fired_before = g_new_faults_fired.load();
    invocation const inv = invoke_once_with_new_fault_after(skip, fc.kind, call);
    r.fired = g_new_faults_fired.load() != fired_before;
    chk(inv.calls == 1, 1);
    chk( ! inv.escaped, 2);
    if (r.fired) {
        chk(inv.ec == fc.expected, 3);
        chk(untouched(), 4);
    } else {
        chk(inv.ec == ok_code, 5);
        chk(settled(), 6);
    }
    r.failed_check = chk.first();
    return r;
}

// Sweeps every fault kind over every caller-thread allocation of `attempt`
// against a RUNNING node.
//
// It asserts exception containment, the boundary code and the state of the
// output slots, and nothing about leaks: the node's workers allocate and free
// concurrently (and release the coroutine frame only after the future has
// completed), so a process-wide count cannot attribute a difference to the
// call, and a free on one thread could cancel a leak on another. `leaked` is
// therefore never read here. Cleanup of owned outputs is measured, with exact
// counts and no worker threads alive, by the cashtoken/seed sweeps above and
// the "(helper only)" tests below.
template <typename Attempt>
void sweep_chain_call(Attempt&& attempt) {
    for (auto const& fc : kFaultCases) {
        bool completed = false;
        for (std::size_t skip = 0; skip < 4096 && ! completed; ++skip) {
            attempt_result const r = attempt(fc, skip);
            INFO("fault after " << skip << " allocations, failed check " << r.failed_check);
            REQUIRE(r.failed_check == 0);
            if ( ! r.fired) {
                REQUIRE(skip > 0);   // the call allocates at least once on this thread
                completed = true;
            }
        }
        REQUIRE(completed);
    }
}

bool is_domain_error(kth_error_code_t ec) {
    return ec != kth_ec_success && ec != kth_ec_out_of_memory && ec != kth_ec_unhandled_exception;
}

} // namespace

TEST_CASE("C-API sync header query returns an owned header on success (regression)",
          "[C-API exception barrier][chain][sync][regression]") {
    // The wrappers used to hand `std::error_code{}` (system category) to
    // `to_owned_if_success`, which compares against `kth::error::success`
    // (bitcoin category). The two are never equal, so a successful query
    // published a null header. This is a behaviour fix, not part of the
    // barrier mechanics.
    std::error_code const standard{};
    REQUIRE_FALSE(standard == kth::error::success);

    sync_node fx;
    REQUIRE(fx.chain != nullptr);
    kth_header_mut_t by_height = nullptr;
    kth_size_t height = kUnset;
    REQUIRE(kth_chain_sync_block_header_by_height(fx.chain, 0, &by_height, &height) == kth_ec_success);
    REQUIRE(by_height != nullptr);
    REQUIRE(height == 0);
    kth_hash_t const hash = kth_domain_chain_header_hash(by_height);

    kth_header_mut_t by_hash = nullptr;
    height = kUnset;
    REQUIRE(kth_chain_sync_block_header_by_hash(fx.chain, hash, &by_hash, &height) == kth_ec_success);
    REQUIRE(by_hash != nullptr);
    REQUIRE(height == 0);
    REQUIRE(kth_domain_chain_header_equals(by_height, by_hash) != 0);
    REQUIRE(kth_domain_chain_header_previous_block_hash(by_hash).hash[0] == 0);
    REQUIRE(kth_domain_chain_header_timestamp(by_hash) != 0);
    kth_domain_chain_header_destruct(by_hash);
    kth_domain_chain_header_destruct(by_height);
}

TEST_CASE("C-API exception barrier - sync chain queries that succeed publish every output or none against a real node",
          "[C-API exception barrier][chain][sync]") {
    sync_node fx;   // scoped: the node and its workers are gone when the test case ends
    REQUIRE(fx.chain != nullptr);
    kth_chain_t const chain = fx.chain;

    kth_header_mut_t genesis_header = nullptr;
    kth_size_t genesis_height = kUnset;
    REQUIRE(kth_chain_sync_block_header_by_height(chain, 0, &genesis_header, &genesis_height) == kth_ec_success);
    REQUIRE(genesis_header != nullptr);
    REQUIRE(genesis_height == 0);
    kth_hash_t const genesis_hash = kth_domain_chain_header_hash(genesis_header);
    kth_domain_chain_header_destruct(genesis_header);

    {   // last_height
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_size_t height = kUnset;
            return judge(fc, skip,
                [&] { return kth_chain_sync_last_height(chain, &height); },
                [&] { return height == kUnset; },
                [&] { return height == 0; });
        });
    }

    {   // block_header_by_height
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_header_mut_t header = nullptr;
            kth_size_t height = kUnset;
            attempt_result r = judge(fc, skip,
                [&] { return kth_chain_sync_block_header_by_height(chain, 0, &header, &height); },
                [&] { return header == nullptr && height == kUnset; },
                [&] { return header != nullptr && height == 0; });
            kth_domain_chain_header_destruct(header);
            return r;
        });
    }

    {   // block_header_by_hash
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_header_mut_t header = nullptr;
            kth_size_t height = kUnset;
            attempt_result r = judge(fc, skip,
                [&] { return kth_chain_sync_block_header_by_hash(chain, genesis_hash, &header, &height); },
                [&] { return header == nullptr && height == kUnset; },
                [&] { return header != nullptr && height == 0; });
            kth_domain_chain_header_destruct(header);
            return r;
        });
    }

    {   // block_by_height_timestamp
        kth_hash_t const marker = {{ 0x55 }};
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_hash_t hash = marker;
            uint32_t timestamp = 0xdeadu;
            return judge(fc, skip,
                [&] { return kth_chain_sync_block_by_height_timestamp(chain, 0, &hash, &timestamp); },
                [&] { return std::memcmp(hash.hash, marker.hash, 32) == 0 && timestamp == 0xdeadu; },
                [&] { return std::memcmp(hash.hash, genesis_hash.hash, 32) == 0 && timestamp != 0xdeadu; });
        });
    }

    {   // block_hash does not allocate, so it publishes on success and leaves the slot on not found
        kth_hash_t const marker = {{ 0x55 }};
        kth_hash_t hash = marker;
        long const fired_before = g_new_faults_fired.load();
        invocation r;
        {
            new_fault_scope scope(fault_kind::bad_alloc);
            r = invoke_once([&] { return kth_chain_sync_block_hash(chain, 0, &hash); });
        }
        REQUIRE(g_new_faults_fired.load() == fired_before);   // nothing to fault: it never allocates
        REQUIRE(r.ec == kth_ec_success);
        REQUIRE(std::memcmp(hash.hash, genesis_hash.hash, 32) == 0);

        hash = marker;
        auto const missing = invoke_once([&] { return kth_chain_sync_block_hash(chain, 1000000, &hash); });
        REQUIRE_FALSE(missing.escaped);
        REQUIRE(missing.ec == kth_ec_not_found);
        REQUIRE(std::memcmp(hash.hash, marker.hash, 32) == 0);
    }
}

TEST_CASE("C-API exception barrier - sync chain queries keep their domain errors and contain exceptions against a real node",
          "[C-API exception barrier][chain][sync]") {
    sync_node fx;   // scoped: the node and its workers are gone when the test case ends
    REQUIRE(fx.chain != nullptr);
    kth_chain_t const chain = fx.chain;

    kth_header_mut_t genesis_header = nullptr;
    kth_size_t genesis_height = kUnset;
    REQUIRE(kth_chain_sync_block_header_by_height(chain, 0, &genesis_header, &genesis_height) == kth_ec_success);
    kth_hash_t const genesis_hash = kth_domain_chain_header_hash(genesis_header);
    kth_domain_chain_header_destruct(genesis_header);

    // Handles the failed calls must leave alone: a stale non-null value in the
    // caller's slot shows whether the wrapper zeroed it (domain error) or never
    // touched it (exception).
    auto* const stale = reinterpret_cast<void*>(std::uintptr_t{1});

    {   // block_by_height and block_by_hash zero their outputs on a domain error
        kth_block_mut_t block = static_cast<kth_block_mut_t>(stale);
        kth_size_t height = kUnset;
        kth_error_code_t const expected = kth_chain_sync_block_by_height(chain, 0, &block, &height);
        REQUIRE(is_domain_error(expected));
        REQUIRE(block == nullptr);   // historical behaviour: zeroed
        REQUIRE(height == 0);
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_block_mut_t b = static_cast<kth_block_mut_t>(stale);
            kth_size_t h = kUnset;
            return judge_with(fc, skip, expected,
                [&] { return kth_chain_sync_block_by_height(chain, 0, &b, &h); },
                [&] { return b == stale && h == kUnset; },
                [&] { return b == nullptr && h == 0; });
        });
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_block_mut_t b = static_cast<kth_block_mut_t>(stale);
            kth_size_t h = kUnset;
            return judge_with(fc, skip, expected,
                [&] { return kth_chain_sync_block_by_hash(chain, genesis_hash, &b, &h); },
                [&] { return b == stale && h == kUnset; },
                [&] { return b == nullptr && h == 0; });
        });
    }

    {   // block_header_byhash_txs_size zeroes all four outputs on a domain error
        auto const run = [&](kth_header_mut_t* header, uint64_t* block_height, kth_hash_list_mut_t* hashes,
                             uint64_t* serialized) {
            return kth_chain_sync_block_header_byhash_txs_size(
                chain, genesis_hash, header, block_height, hashes, serialized);
        };
        kth_header_mut_t header = static_cast<kth_header_mut_t>(stale);
        kth_hash_list_mut_t hashes = static_cast<kth_hash_list_mut_t>(stale);
        uint64_t block_height = kUnset64;
        uint64_t serialized = kUnset64;
        kth_error_code_t const expected = run(&header, &block_height, &hashes, &serialized);
        REQUIRE(is_domain_error(expected));
        REQUIRE(header == nullptr);
        REQUIRE(hashes == nullptr);
        REQUIRE(block_height == 0);
        REQUIRE(serialized == 0);
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_header_mut_t h = static_cast<kth_header_mut_t>(stale);
            kth_hash_list_mut_t l = static_cast<kth_hash_list_mut_t>(stale);
            uint64_t bh = kUnset64;
            uint64_t sz = kUnset64;
            return judge_with(fc, skip, expected,
                [&] { return run(&h, &bh, &l, &sz); },
                [&] { return h == stale && l == stale && bh == kUnset64 && sz == kUnset64; },
                [&] { return h == nullptr && l == nullptr && bh == 0 && sz == 0; });
        });
    }

    {   // merkle_block and compact_block by height and by hash zero their outputs on a domain error
        auto const merkle = [&](bool by_hash, kth_merkle_block_mut_t* b, kth_size_t* h) {
            return by_hash ? kth_chain_sync_merkle_block_by_hash(chain, genesis_hash, b, h)
                           : kth_chain_sync_merkle_block_by_height(chain, 0, b, h);
        };
        auto const compact = [&](bool by_hash, kth_compact_block_mut_t* b, kth_size_t* h) {
            return by_hash ? kth_chain_sync_compact_block_by_hash(chain, genesis_hash, b, h)
                           : kth_chain_sync_compact_block_by_height(chain, 0, b, h);
        };
        for (bool by_hash : { false, true }) {
            kth_merkle_block_mut_t mb = static_cast<kth_merkle_block_mut_t>(stale);
            kth_compact_block_mut_t cb = static_cast<kth_compact_block_mut_t>(stale);
            kth_size_t height = kUnset;
            kth_error_code_t const merkle_expected = merkle(by_hash, &mb, &height);
            REQUIRE(is_domain_error(merkle_expected));
            REQUIRE(mb == nullptr);
            REQUIRE(height == 0);
            height = kUnset;
            kth_error_code_t const compact_expected = compact(by_hash, &cb, &height);
            REQUIRE(is_domain_error(compact_expected));
            REQUIRE(cb == nullptr);
            REQUIRE(height == 0);
            sweep_chain_call([&](fault_case fc, std::size_t skip) {
                kth_merkle_block_mut_t b = static_cast<kth_merkle_block_mut_t>(stale);
                kth_size_t h = kUnset;
                return judge_with(fc, skip, merkle_expected,
                    [&] { return merkle(by_hash, &b, &h); },
                    [&] { return b == stale && h == kUnset; },
                    [&] { return b == nullptr && h == 0; });
            });
            sweep_chain_call([&](fault_case fc, std::size_t skip) {
                kth_compact_block_mut_t b = static_cast<kth_compact_block_mut_t>(stale);
                kth_size_t h = kUnset;
                return judge_with(fc, skip, compact_expected,
                    [&] { return compact(by_hash, &b, &h); },
                    [&] { return b == stale && h == kUnset; },
                    [&] { return b == nullptr && h == 0; });
            });
        }
    }

    {   // transaction and transaction_position zero their outputs on a domain error
        kth_transaction_mut_t tx = static_cast<kth_transaction_mut_t>(stale);
        kth_size_t height = kUnset;
        kth_size_t index = kUnset;
        kth_error_code_t const expected = kth_chain_sync_transaction(chain, genesis_hash, 1, &tx, &height, &index);
        REQUIRE(is_domain_error(expected));
        REQUIRE(tx == nullptr);
        REQUIRE(height == 0);
        REQUIRE(index == 0);
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_transaction_mut_t t = static_cast<kth_transaction_mut_t>(stale);
            kth_size_t h = kUnset;
            kth_size_t i = kUnset;
            return judge_with(fc, skip, expected,
                [&] { return kth_chain_sync_transaction(chain, genesis_hash, 1, &t, &h, &i); },
                [&] { return t == stale && h == kUnset && i == kUnset; },
                [&] { return t == nullptr && h == 0 && i == 0; });
        });

        kth_size_t position = kUnset;
        kth_size_t pos_height = kUnset;
        kth_error_code_t const pos_expected =
            kth_chain_sync_transaction_position(chain, genesis_hash, 1, &position, &pos_height);
        REQUIRE(is_domain_error(pos_expected));
        REQUIRE(position == 0);
        REQUIRE(pos_height == 0);
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_size_t p = kUnset;
            kth_size_t h = kUnset;
            return judge_with(fc, skip, pos_expected,
                [&] { return kth_chain_sync_transaction_position(chain, genesis_hash, 1, &p, &h); },
                [&] { return p == kUnset && h == kUnset; },
                [&] { return p == 0 && h == 0; });
        });
    }

    {   // mining_template leaves both outputs untouched on a domain error and on an exception
        kth_mining_template_t probe{};
        kth_transaction_list_mut_t probe_txs = static_cast<kth_transaction_list_mut_t>(stale);
        kth_error_code_t const expected = kth_chain_sync_mining_template(chain, 0, &probe, &probe_txs);
        REQUIRE(is_domain_error(expected));
        REQUIRE(probe_txs == stale);   // historical behaviour: not touched on a domain error
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            kth_mining_template_t tmpl{};
            tmpl.height = kUnset;
            kth_transaction_list_mut_t txs = static_cast<kth_transaction_list_mut_t>(stale);
            return judge_with(fc, skip, expected,
                [&] { return kth_chain_sync_mining_template(chain, 0, &tmpl, &txs); },
                [&] { return tmpl.height == kUnset && txs == stale; },
                [&] { return tmpl.height == kUnset && txs == stale; });
        });
    }

    {   // organize_block and organize_transaction keep their int result and contain exceptions
        // A well-formed block to hand to organize: the v1 node answers that
        // single-block acceptance is not implemented.
        kth_hash_t const zero = {{ 0 }};
        kth_header_mut_t header = kth_domain_chain_header_construct(0x20000000u, &zero, &zero, 1u, 0x207fffffu, 0u);
        kth_transaction_list_mut_t no_txs = kth_domain_chain_transaction_list_construct_default();
        kth_block_mut_t block = kth_domain_chain_block_construct(header, no_txs);
        REQUIRE(block != nullptr);
        kth_domain_chain_transaction_list_destruct(no_txs);
        kth_domain_chain_header_destruct(header);
        int const expected = kth_chain_sync_organize_block(chain, block);
        REQUIRE(is_domain_error(static_cast<kth_error_code_t>(expected)));
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            return judge_with(fc, skip, static_cast<kth_error_code_t>(expected),
                [&] { return static_cast<kth_error_code_t>(kth_chain_sync_organize_block(chain, block)); },
                [&] { return true; },
                [&] { return true; });
        });
        kth_domain_chain_block_destruct(block);

        kth_input_list_mut_t ins = make_inputs();
        kth_output_list_mut_t outs = make_outputs();
        kth_transaction_mut_t tx = nullptr;
        REQUIRE(kth_domain_chain_transaction_construct_move(7u, 99u, &ins, &outs, &tx) == kth_ec_success);
        int const tx_expected = kth_chain_sync_organize_transaction(chain, tx);
        REQUIRE(is_domain_error(static_cast<kth_error_code_t>(tx_expected)));
        sweep_chain_call([&](fault_case fc, std::size_t skip) {
            return judge_with(fc, skip, static_cast<kth_error_code_t>(tx_expected),
                [&] { return static_cast<kth_error_code_t>(kth_chain_sync_organize_transaction(chain, tx)); },
                [&] { return true; },
                [&] { return true; });
        });
        kth_domain_chain_transaction_destruct(tx);
    }
}


// ---------------------------------------------------------------------------
// Staging helpers used by the generated wrappers (helper only)
// ---------------------------------------------------------------------------
//
// A synchronous query that succeeds needs a stored block body, which a v1 node
// cannot be given here. The two shapes the generated wrappers rely on to
// publish "all or nothing" are therefore driven directly, with real kth types,
// by the same faults.

namespace {

template <typename Operation>
void sweep_cpp_operation(Operation&& operation) {
    for (auto const& fc : kFaultCases) {
        bool completed = false;
        for (std::size_t skip = 0; skip < 512 && ! completed; ++skip) {
            long const live_before = g_live_allocations.load();
            long const fired_before = g_new_faults_fired.load();
            bool threw = false;
            {
                new_fault_scope scope(fc.kind, skip);
                try {
                    operation();
                    completed = true;
                } catch (...) {
                    threw = true;
                }
            }
            bool const fired = g_new_faults_fired.load() != fired_before;
            long const live_after = g_live_allocations.load();
            INFO("fault after " << skip << " allocations");
            REQUIRE(fired == threw);
            REQUIRE(live_after == live_before);
        }
        REQUIRE(completed);
    }
}

} // namespace

TEST_CASE("kth::stage_owned (helper only) frees the first staged object when the second promotion throws",
          "[C-API exception barrier][staging]") {
    // The shape of kth_chain_sync_block_header_byhash_txs_size: a header, then a
    // hash list, both promoted to the heap before anything is published.
    auto const header = std::make_shared<kth::domain::chain::header const>();
    auto const hashes = std::make_shared<std::vector<kth::hash_digest>>(3);
    void* published_header = nullptr;
    void* published_hashes = nullptr;
    sweep_cpp_operation([&] {
        auto header_owned = kth::stage_owned(kth::to_owned(header));
        auto hashes_owned = kth::stage_owned(kth::to_owned(hashes));
        published_header = header_owned.release();
        published_hashes = hashes_owned.release();
        kth::del<kth::domain::chain::header>(published_header);
        kth::del_list<kth::hash_digest>(published_hashes);
    });
}

TEST_CASE("kth::capi::fill_mining_template with local staging (helper only) publishes nothing when it throws",
          "[C-API exception barrier][staging]") {
    // The shape of the generated kth_chain_sync_mining_template: the shared,
    // unchanged helper fills local staging variables, and the caller's outputs
    // are written only once it has returned. This drives real kth objects with
    // allocation faults; it does not run a successful mining query (a v1 node
    // cannot be given a block to build a template on).
    kth::blockchain::mining_template tmpl{};
    tmpl.version = 0x20000000u;
    tmpl.height = 7;
    tmpl.bits = 0x207fffffu;
    tmpl.coinbase_value = 5000000000ull;
    for (int i = 0; i < 3; ++i) {
        tmpl.selection.txs.push_back(std::make_shared<kth::domain::message::transaction const>());
    }
    bool list_escaped_a_throw = false;
    bool wrong_result = false;
    sweep_cpp_operation([&] {
        kth_mining_template_t staged_header{};
        kth_transaction_list_mut_t staged_txs = nullptr;
        try {
            kth::capi::fill_mining_template(staged_header, staged_txs, tmpl);
        } catch (...) {
            list_escaped_a_throw = staged_txs != nullptr;
            throw;
        }
        wrong_result = wrong_result || staged_header.height != 7 || staged_header.bits != 0x207fffffu
            || staged_txs == nullptr
            || kth::cpp_ref<std::vector<kth::domain::chain::transaction>>(staged_txs).size() != 3;
        kth::del_list<kth::domain::chain::transaction>(staged_txs);
    });
    REQUIRE_FALSE(list_escaped_a_throw);
    REQUIRE_FALSE(wrong_result);
}

TEST_CASE("C-API exception barrier - domain errors of other classes keep their code and leave outputs untouched",
          "[C-API exception barrier]") {
    auto const is_domain_error = [](kth_error_code_t ec) {
        return ec != kth_ec_success && ec != kth_ec_out_of_memory && ec != kth_ec_unhandled_exception;
    };

    kth_payment_address_mut_t address = nullptr;
    auto const bad_address = invoke_once([&] {
        return kth_wallet_payment_address_parse_from_simple("not an address", &address);
    });
    REQUIRE_FALSE(bad_address.escaped);
    REQUIRE(is_domain_error(bad_address.ec));
    REQUIRE(address == nullptr);

    uint8_t const truncated[3] = { 1, 2, 3 };
    kth_header_mut_t header = nullptr;
    auto const bad_header = invoke_once([&] {
        return kth_domain_chain_header_construct_from_data(truncated, sizeof(truncated), 1, &header);
    });
    REQUIRE_FALSE(bad_header.escaped);
    REQUIRE(is_domain_error(bad_header.ec));
    REQUIRE(header == nullptr);

    kth_compact_block_mut_t compact = nullptr;
    auto const bad_compact = invoke_once([&] {
        return kth_domain_message_compact_block_construct_from_data(truncated, sizeof(truncated), 70015u, &compact);
    });
    REQUIRE_FALSE(bad_compact.escaped);
    REQUIRE(is_domain_error(bad_compact.ec));
    REQUIRE(compact == nullptr);

    kth_binary_mut_t binary = nullptr;
    auto const bad_binary = invoke_once([&] {
        return kth_core_binary_parse_from("10x1", &binary);
    });
    REQUIRE_FALSE(bad_binary.escaped);
    REQUIRE(is_domain_error(bad_binary.ec));
    REQUIRE(binary == nullptr);

    // An empty stack: both outputs stay null.
    kth_script_mut_t script = make_script();
    kth_program_mut_t program = kth_vm_program_construct_from_script(script);
    kth_number_mut_t a = nullptr;
    kth_number_mut_t b = nullptr;
    auto const empty_stack = invoke_once([&] { return kth_vm_program_pop_binary(program, &a, &b); });
    REQUIRE_FALSE(empty_stack.escaped);
    REQUIRE(is_domain_error(empty_stack.ec));
    REQUIRE(a == nullptr);
    REQUIRE(b == nullptr);
    kth_vm_program_destruct(program);
    kth_domain_chain_script_destruct(script);
}

TEST_CASE("C-API exception barrier - destructors of newly covered classes accept null",
          "[C-API exception barrier]") {
    kth_vm_number_destruct(nullptr);
    kth_vm_big_number_destruct(nullptr);
    kth_vm_program_destruct(nullptr);
    kth_core_binary_destruct(nullptr);
    kth_domain_chain_header_destruct(nullptr);
    kth_wallet_payment_address_destruct(nullptr);
    kth_wallet_bitcoin_uri_destruct(nullptr);
    kth_wallet_coin_selection_result_destruct(nullptr);
    kth_domain_chain_utxo_list_destruct(nullptr);
    kth_core_u32_list_destruct(nullptr);
    kth_core_u64_list_destruct(nullptr);
    kth_wallet_payment_address_list_destruct(nullptr);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Real malloc exhaustion in the buffer publication (Linux, link-time wrap)
// ---------------------------------------------------------------------------

#if defined(KTH_CAPI_TEST_MALLOC_WRAP)

TEST_CASE("C-API exception barrier - create_endorsement reports out_of_memory when the buffer malloc fails",
          "[C-API exception barrier][malloc]") {
    kth_input_list_mut_t ins = make_inputs();
    kth_output_list_mut_t outs = make_outputs();
    kth_transaction_mut_t tx = nullptr;
    REQUIRE(kth_domain_chain_transaction_construct_move(7u, 99u, &ins, &outs, &tx) == kth_ec_success);
    kth_script_mut_t prevout = make_script();
    kth_hash_t secret = {{ 0 }};
    secret.hash[31] = 1;

    auto const endorse = [&](uint8_t** out, kth_size_t* out_size) {
        return kth_domain_chain_script_create_endorsement(
            &secret, prevout, tx, 0, 0x41, kForkIdFlags, 1000,
            kth_endorsement_type_ecdsa, out, out_size);
    };

    // Learn the (deterministic) signature size from a successful call.
    uint8_t* good = nullptr;
    kth_size_t good_size = 0;
    REQUIRE(endorse(&good, &good_size) == kth_ec_success);
    REQUIRE(good != nullptr);
    REQUIRE(good_size > 0);
    kth_core_destruct_array(good);

    // Fail the one malloc of exactly that size: the buffer allocation.
    uint8_t* out = nullptr;
    kth_size_t out_size = 0xdeadu;
    long const fired_before = g_malloc_faults_fired.load();
    invocation r;
    {
        malloc_fault_scope scope(good_size);
        r = invoke_once([&] { return endorse(&out, &out_size); });
    }
    REQUIRE(g_malloc_faults_fired.load() - fired_before == 1);
    REQUIRE(r.calls == 1);
    REQUIRE_FALSE(r.escaped);
    REQUIRE(r.ec == kth_ec_out_of_memory);
    REQUIRE(out == nullptr);
    REQUIRE(out_size == 0xdeadu);

    // One-shot: the retry succeeds and publishes both outputs.
    REQUIRE(endorse(&out, &out_size) == kth_ec_success);
    REQUIRE(out != nullptr);
    REQUIRE(out_size == good_size);
    kth_core_destruct_array(out);

    kth_domain_chain_script_destruct(prevout);
    kth_domain_chain_transaction_destruct(tx);
}

TEST_CASE("C-API exception barrier - cashtoken encode_nft_number reports out_of_memory when the buffer malloc fails",
          "[C-API exception barrier][malloc][cashtoken]") {
    uint8_t* good = nullptr;
    kth_size_t good_size = 0;
    REQUIRE(kth_wallet_cashtoken_encode_nft_number(1000, &good, &good_size) == kth_ec_success);
    REQUIRE(good != nullptr);
    REQUIRE(good_size == 2);
    kth_core_destruct_array(good);

    uint8_t* out = nullptr;
    kth_size_t out_size = 0xdeadu;
    long const fired_before = g_malloc_faults_fired.load();
    invocation r;
    {
        malloc_fault_scope scope(good_size);
        r = invoke_once([&] { return kth_wallet_cashtoken_encode_nft_number(1000, &out, &out_size); });
    }
    REQUIRE(g_malloc_faults_fired.load() - fired_before == 1);
    REQUIRE_FALSE(r.escaped);
    REQUIRE(r.ec == kth_ec_out_of_memory);
    REQUIRE(out == nullptr);
    REQUIRE(out_size == 0xdeadu);

    REQUIRE(kth_wallet_cashtoken_encode_nft_number(1000, &out, &out_size) == kth_ec_success);
    REQUIRE(out != nullptr);
    REQUIRE(out_size == 2);
    kth_core_destruct_array(out);
}

TEST_CASE("kth::try_create_c_array (helper only) leaves outputs untouched when malloc fails",
          "[C-API exception barrier][malloc]") {
    kth::data_chunk const data{1, 2, 3, 4, 5, 6, 7};
    uint8_t* out = nullptr;
    kth_size_t out_size = 0xdeadu;
    long const fired_before = g_malloc_faults_fired.load();
    kth_error_code_t ec = kth_ec_success;
    {
        malloc_fault_scope scope(data.size());
        ec = kth::try_create_c_array(data, &out, &out_size);
    }
    REQUIRE(g_malloc_faults_fired.load() - fired_before == 1);
    REQUIRE(ec == kth_ec_out_of_memory);
    REQUIRE(out == nullptr);
    REQUIRE(out_size == 0xdeadu);

    // An empty array allocates the 1-byte sentinel; failing it behaves alike.
    kth::data_chunk const empty;
    {
        malloc_fault_scope scope(1);
        ec = kth::try_create_c_array(empty, &out, &out_size);
    }
    REQUIRE(ec == kth_ec_out_of_memory);
    REQUIRE(out == nullptr);
    REQUIRE(out_size == 0xdeadu);
}

#endif // KTH_CAPI_TEST_MALLOC_WRAP
