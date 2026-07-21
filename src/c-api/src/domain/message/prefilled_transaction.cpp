// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/domain/message/prefilled_transaction.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/prefilled_transaction.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::message::prefilled_transaction;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_domain_message_prefilled_transaction_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_prefilled_transaction_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = cpp_t::from_data(data_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_domain_message_prefilled_transaction_create(uint64_t index, kth_transaction_const_t tx, KTH_OUT_OWNED kth_prefilled_transaction_mut_t* out) {
    KTH_PRECONDITION(tx != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& tx_cpp = kth::cpp_ref<kth::domain::chain::transaction>(tx);
    auto result = cpp_t::create(index, tx_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_domain_message_prefilled_transaction_destruct(kth_prefilled_transaction_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_prefilled_transaction_mut_t kth_domain_message_prefilled_transaction_copy(kth_prefilled_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_domain_message_prefilled_transaction_equals(kth_prefilled_transaction_const_t self, kth_prefilled_transaction_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_domain_message_prefilled_transaction_not_equal(kth_prefilled_transaction_const_t self, kth_prefilled_transaction_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_domain_message_prefilled_transaction_to_data(kth_prefilled_transaction_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    return kth::to_c_array_from(kth::cpp_ref<cpp_t>(self), *out_size, version);
}

kth_size_t kth_domain_message_prefilled_transaction_serialized_size(kth_prefilled_transaction_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).serialized_size(version);
}


// Getters

uint64_t kth_domain_message_prefilled_transaction_index(kth_prefilled_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).index();
}

kth_transaction_const_t kth_domain_message_prefilled_transaction_transaction(kth_prefilled_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).transaction());
}

} // extern "C"
