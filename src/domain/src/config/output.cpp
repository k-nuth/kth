// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/output.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <kth/domain/config/point.hpp>
#include <kth/domain/config/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/math/stealth.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::domain::config {

// static
expect<output> output::parse_from(std::string_view tuple) {
    auto const tokens = split(tuple, point::delimeter);
    if (tokens.size() < 2 || tokens.size() > 3) {
        return std::unexpected(kth::error::illegal_value);
    }

    uint64_t amount = 0;
    deserialize(amount, tokens[1], true);
    if (amount > max_money()) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const& target = tokens.front();

    if (auto payment = wallet::payment_address::parse_from(target); payment) {
        return output{
            /* is_stealth */ false,
            amount,
            payment->version(),
            chain::script{},
            payment->hash20()
        };
    }

    if (auto stealth = wallet::stealth_address::parse_from(target); stealth) {
        // TODO(legacy): finish stealth multisig implementation (p2sh and !p2sh).
        if (stealth->spend_keys().size() != 1 || tokens.size() != 3) {
            return std::unexpected(kth::error::illegal_value);
        }

        auto seed = decode_base16(tokens[2]);
        if ( ! seed || seed->size() < minimum_seed_size) {
            return std::unexpected(kth::error::illegal_value);
        }

        chain::script stealth_script;
        ec_secret ephemeral_secret;
        if ( ! create_stealth_data(stealth_script, ephemeral_secret, stealth->filter(), *seed)) {
            return std::unexpected(kth::error::illegal_value);
        }

        ec_compressed stealth_key;
        if ( ! uncover_stealth(stealth_key, stealth->scan_key(), ephemeral_secret, stealth->spend_keys().front())) {
            return std::unexpected(kth::error::illegal_value);
        }

        return output{
            /* is_stealth */ true,
            amount,
            stealth->version(),
            std::move(stealth_script),
            bitcoin_short_hash(stealth_key)
        };
    }

    // The target must be a serialized script.
    // Note that it is possible for a base16-encoded script to be interpreted as
    // an address above. That is unlikely but is considered intended behavior.
    auto decoded = decode_base16(std::string{target});
    if ( ! decoded) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto script_result = script::from_data_chunk(*decoded);
    if ( ! script_result) {
        return std::unexpected(script_result.error());
    }

    return output{
        /* is_stealth */ false,
        amount,
        /* version */ 0,
        script_result->value(),
        null_short_hash
    };
}

} // namespace kth::domain::config
