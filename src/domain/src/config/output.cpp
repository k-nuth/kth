// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/output.hpp>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include <boost/program_options.hpp>

#include <kth/domain/config/point.hpp>
#include <kth/domain/config/script.hpp>
#include <kth/domain/math/stealth.hpp>
#include <kth/domain/wallet/stealth_address.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::domain::config {

using namespace boost::program_options;

output::output()
    : pay_to_hash_(null_short_hash)
    // , script_()
{}

output::output(std::string const& tuple)
    : output()
{
    std::stringstream(tuple) >> *this;
}

bool output::is_stealth() const {
    return is_stealth_;
}

uint64_t output::amount() const {
    return amount_;
}

uint8_t output::version() const {
    return version_;
}

chain::script const& output::script() const {
    return script_;
}

short_hash const& output::pay_to_hash() const {
    return pay_to_hash_;
}

std::istream& operator>>(std::istream& input, output& argument) {
    std::string tuple;
    input >> tuple;

    auto const tokens = split(tuple, point::delimeter);
    if (tokens.size() < 2 || tokens.size() > 3) {
        BOOST_THROW_EXCEPTION(invalid_option_value(tuple));
    }

    uint64_t amount;
    deserialize(amount, tokens[1], true);
    if (amount > max_money()) {
        BOOST_THROW_EXCEPTION(invalid_option_value(tuple));
    }

    argument.amount_ = amount;
    auto const& target = tokens.front();

    // Is the target a payment address?
    wallet::payment_address const payment(target);
    if (payment) {
        argument.version_ = payment.version();
        argument.pay_to_hash_ = payment.hash20();
        return input;
    }

    // Is the target a stealth address?
    wallet::stealth_address const stealth(target);
    if (stealth) {
        // TODO(legacy): finish stealth multisig implemetation (p2sh and !p2sh).

        if (stealth.spend_keys().size() != 1 || tokens.size() != 3) {
            BOOST_THROW_EXCEPTION(invalid_option_value(tuple));
        }

        auto seed = decode_base16(tokens[2]);
        if ( ! seed || seed->size() < minimum_seed_size) {
            BOOST_THROW_EXCEPTION(invalid_option_value(tuple));
        }

        ec_secret ephemeral_secret;
        if ( ! create_stealth_data(argument.script_, ephemeral_secret, stealth.filter(), *seed)) {
            BOOST_THROW_EXCEPTION(invalid_option_value(tuple));
        }

        ec_compressed stealth_key;
        if ( ! uncover_stealth(stealth_key, stealth.scan_key(), ephemeral_secret, stealth.spend_keys().front())) {
            BOOST_THROW_EXCEPTION(invalid_option_value(tuple));
        }

        argument.is_stealth_ = true;
        argument.pay_to_hash_ = bitcoin_short_hash(stealth_key);
        argument.version_ = stealth.version();
        return input;
    }

    // The target must be a serialized script.
    // Note that it is possible for a base16 encoded script to be interpreted
    // as an address above. That is unlikely but considered intended behavior.
    auto decoded = decode_base16(target);
    if ( ! decoded) {
        BOOST_THROW_EXCEPTION(invalid_option_value(target));
    }

    argument.script_ = script(*decoded);
    return input;
}

} // namespace kth::domain::config
