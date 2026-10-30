// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_TEST_BCHN_VECTORS_HPP
#define KTH_TEST_BCHN_VECTORS_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/infrastructure/error.hpp>

/**
 * Structure representing a single script test case for BCHN.
 * This includes additional fields for flags and expected error codes.
 */
struct bchn_script_test {
    std::string script_sig;         ///< The scriptSig (input script)
    std::string script_pub_key;     ///< The scriptPubKey (output script)
    kth::domain::script_flags_t flags;     ///< Active flag rules (OR of kth::domain::machine::script_flags values)
    kth::error::error_code_t expected_error; ///< Expected error code
    std::string comment;            ///< Test description/comment
    uint64_t amount{0};             ///< Input amount in satoshis (for SIGHASH_FORKID tests)
};

using bchn_script_test_list = std::vector<bchn_script_test>;

void run_bchn_test(bchn_script_test const& test);

namespace kth::test {
// Borrowed string literals; no dynamic initialization or test registration.
struct bchn_vector {
    uint32_t index;
    char const* script_sig;
    char const* script_pub_key;
    domain::script_flags_t flags;
    error::error_code_t expected_error;
    char const* comment;
    uint64_t amount{0};
};
struct bchn_vector_group {
    char const* source;
    std::span<bchn_vector const> rows;
};
std::span<bchn_vector_group const> bchn_vectors();
} // namespace kth::test
#endif
