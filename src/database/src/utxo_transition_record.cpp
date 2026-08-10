// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/utxo_transition_record.hpp>

#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::database {


uint64_t make_operation_id() {
    return pseudo_random::generate<uint64_t>();
}

data_chunk encode(utxo_transition_record const& record) {
    // The two enumerated body fields, before a single byte is written. encode()
    // will happily checksum any value they hold, and the result is a record that
    // is internally consistent and that decode_transition_record refuses — so a
    // caller passing one persists a database that every subsequent start
    // declines to open, and the only way out is a rebuild. There is no input
    // path to either: both call sites write literals, so a value outside these
    // sets is a bug in this repository and not a fact about any database.
    //
    // KTH_CONTRACT and not KTH_ASSERT: KTH_ASSERT is disabled in Release,
    // exactly where the consequence is worst, and aborting before the write is
    // the smaller failure by a wide margin.
    KTH_CONTRACT(record.type == transition_type::connect_batch ||
                 record.type == transition_type::reorg);
    KTH_CONTRACT(record.state == transition_state::in_progress);

    // format_version is NOT constrained with them, and the difference is not an
    // oversight. type and state are read from the body below, so a value outside
    // their sets is a body this function had no layout for. The version is the
    // envelope's label, and the envelope is fixed OUTSIDE the format version by
    // design — first two bytes the version, last four the checksum — precisely so
    // that a record this build cannot decode is still recognizable as a record.
    //
    // Which makes "version 1's body under a foreign label" a well-defined thing
    // to write, and the only way to construct what a newer build's record looks
    // like from here: decodable as far as the envelope and no further. A refusal
    // on exactly that is the forward-compatibility guarantee, so a contract
    // forbidding it would leave the guarantee with no way to be tested.

    data_chunk out(utxo_transition_record::version_1_size);
    byte_writer writer(out);

    // Every write is bounds-checked and every result is checked, into a buffer
    // sized for this exact record — so a failure here is not a short buffer, it
    // is this function and the size constant disagreeing about the format. A
    // lambda rather than a macro, and rather than a discarded result: a field
    // silently not written would still be checksummed, and the record would be
    // internally consistent and wrong.
    bool ok = true;
    auto const write_ok = [&ok](expect<void> const& result) {
        ok = ok && result.has_value();
    };

    write_ok(writer.write_little_endian<uint16_t>(record.format_version));
    write_ok(writer.write_little_endian<uint16_t>(static_cast<uint16_t>(record.type)));
    write_ok(writer.write_little_endian<uint64_t>(record.operation_id));
    write_ok(writer.write_little_endian<uint32_t>(record.first_height));
    write_ok(writer.write_little_endian<uint32_t>(record.intended_last_height));
    write_ok(writer.write_byte(static_cast<uint8_t>(record.state)));

    KTH_CONTRACT(ok);

    // Over everything written so far, which is everything but itself.
    auto const payload = std::span<uint8_t const>{out}.subspan(
        0, utxo_transition_record::version_1_size - checksum_size);
    write_ok(writer.write_little_endian<uint32_t>(bitcoin_checksum(payload)));

    KTH_CONTRACT(ok);
    return out;
}

std::expected<utxo_transition_record, transition_decode_error>
decode_transition_record(std::span<uint8_t const> bytes) {
    // 1. Enough bytes to hold an envelope. Anything shorter cannot be checked,
    //    so nothing about it can be believed.
    if (bytes.size() < utxo_transition_record::minimum_envelope_size) {
        return std::unexpected(transition_decode_error::too_short);
    }

    // 2. The checksum, before any field it covers is read. Its position and its
    //    algorithm are envelope, not version: that is what lets this run first.
    // Its own reader: the checksum sits at the END, and it has to be read
    // before the fields it covers, so this one read is not part of the
    // sequential pass below.
    auto const payload_size = bytes.size() - checksum_size;
    byte_reader tail(bytes.subspan(payload_size));
    auto const stored = tail.read_little_endian<uint32_t>();
    if ( ! stored) {
        return std::unexpected(transition_decode_error::too_short);
    }
    auto const computed = bitcoin_checksum(bytes.subspan(0, payload_size));
    if (*stored != computed) {
        return std::unexpected(transition_decode_error::checksum_mismatch);
    }

    // The fields, in order, over the checksummed payload only — so a read can
    // never wander into the checksum it just verified.
    byte_reader reader(bytes.subspan(0, payload_size));

    // 3. Only now is the version trustworthy enough to select a decoder.
    auto const version_read = reader.read_little_endian<uint16_t>();
    if ( ! version_read) {
        return std::unexpected(transition_decode_error::too_short);
    }
    auto const version = *version_read;
    if (version != utxo_transition_record::current_format_version) {
        // Written by a build that knows something this one does not. Refusing
        // is the only safe answer: a newer record may mean a transition this
        // build cannot even name.
        return std::unexpected(transition_decode_error::unknown_version);
    }

    if (bytes.size() != utxo_transition_record::version_1_size) {
        return std::unexpected(transition_decode_error::wrong_size);
    }

    // 4. The payload, per that version.
    utxo_transition_record record;
    record.format_version = version;

    auto const type = reader.read_little_endian<uint16_t>();
    auto const operation_id = reader.read_little_endian<uint64_t>();
    auto const first_height = reader.read_little_endian<uint32_t>();
    auto const intended_last_height = reader.read_little_endian<uint32_t>();
    auto const state = reader.read_byte();

    // The size was pinned above, so a short read here would mean the size
    // constant and this decoder disagree — checked rather than assumed, because
    // an unchecked expect<> would default-construct the field and the record
    // would decode successfully with a zero in it.
    if ( ! type || ! operation_id || ! first_height || ! intended_last_height || ! state) {
        return std::unexpected(transition_decode_error::too_short);
    }

    if (*type != static_cast<uint16_t>(transition_type::connect_batch) &&
        *type != static_cast<uint16_t>(transition_type::reorg)) {
        return std::unexpected(transition_decode_error::unknown_type);
    }
    record.type = static_cast<transition_type>(*type);

    record.operation_id = *operation_id;
    record.first_height = *first_height;
    record.intended_last_height = *intended_last_height;

    if (*state != static_cast<uint8_t>(transition_state::in_progress)) {
        return std::unexpected(transition_decode_error::unknown_state);
    }
    record.state = static_cast<transition_state>(*state);

    return record;
}

char const* to_string(transition_type type) {
    switch (type) {
        case transition_type::connect_batch: return "connect batch";
        case transition_type::reorg:         return "reorganization";
    }
    return "unknown";
}

char const* to_string(transition_decode_error error) {
    switch (error) {
        case transition_decode_error::too_short:
            return "the record is shorter than an envelope";
        case transition_decode_error::checksum_mismatch:
            return "the record's checksum does not match its contents";
        case transition_decode_error::unknown_version:
            return "the record was written in a format this build does not know";
        case transition_decode_error::unknown_type:
            return "the record names a transition kind this build does not know";
        case transition_decode_error::unknown_state:
            return "the record names a state this build does not know";
        case transition_decode_error::wrong_size:
            return "the record's length does not match its format version";
    }
    return "unknown decode error";
}

} // namespace kth::database
