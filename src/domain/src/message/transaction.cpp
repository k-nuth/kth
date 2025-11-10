// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/transaction.hpp>

#include <istream>
#include <utility>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>

namespace kth::domain::message {

std::string const transaction::command = "tx";
uint32_t const transaction::version_minimum = version::level::minimum;
uint32_t const transaction::version_maximum = version::level::maximum;

transaction::transaction(chain::transaction&& x)
    : chain::transaction(std::move(x)) {
}

transaction::transaction(chain::transaction const& x)
    : chain::transaction(x) {
}

transaction::transaction(uint32_t version, uint32_t locktime, chain::input::list&& inputs, chain::output::list&& outputs)
    : chain::transaction(version, locktime, std::move(inputs), std::move(outputs)) {
}

transaction::transaction(uint32_t version, uint32_t locktime, const chain::input::list& inputs, const chain::output::list& outputs)
    : chain::transaction(version, locktime, inputs, outputs) {
}

transaction& transaction::operator=(chain::transaction&& x) {
    reset();
    chain::transaction::operator=(std::move(x));
    return *this;
}

bool transaction::operator==(chain::transaction const& x) const {
    return chain::transaction::operator==(x);
}

bool transaction::operator!=(chain::transaction const& x) const {
    return chain::transaction::operator!=(x);
}

bool transaction::operator==(transaction const& x) const {
    return chain::transaction::operator==(x);
}

bool transaction::operator!=(transaction const& x) const {
    return !(*this == x);
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<transaction> transaction::from_data(byte_reader& reader, uint32_t version) {
    auto chain = chain::transaction::from_data(reader, true);
    if ( ! chain) {
        return std::unexpected(chain.error());
    }
    return transaction(std::move(*chain));
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk transaction::to_data(uint32_t /*version*/) const {
    return chain::transaction::to_data(true);
}

void transaction::to_data(uint32_t /*version*/, data_sink& stream) const {
    chain::transaction::to_data(stream, true);
}

size_t transaction::serialized_size(uint32_t /*unused*/) const {
    return chain::transaction::serialized_size(true);
}

} // namespace kth::domain::message
