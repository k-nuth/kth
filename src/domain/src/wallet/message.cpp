// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/message.hpp>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/wallet/ec_private.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::wallet {

static constexpr uint8_t max_recovery_id = 3;
static constexpr uint8_t magic_compressed = 31;
static constexpr uint8_t magic_uncompressed = 27;
static constexpr uint8_t magic_differential = magic_compressed - magic_uncompressed;
static_assert(magic_differential > max_recovery_id, "oops!");
static_assert(max_uint8 - max_recovery_id >= magic_uncompressed, "oops!");

hash_digest hash_message(data_slice message) {
    // This is a specified magic prefix.
    static std::string const prefix("Bitcoin Signed Message:\n");

    data_chunk data;
    data_sink ostream(data);
    ostream_writer sink_w(ostream);
    sink_w.write_string(prefix);
    sink_w.write_variable_little_endian(message.size());
    sink_w.write_bytes(message.data(), message.size());
    ostream.flush();
    return bitcoin_hash(data);
}

static
bool recover(short_hash& out_hash, bool compressed, ec_signature const& compact, uint8_t recovery_id, hash_digest const& message_digest) {
    const recoverable_signature recoverable{
        compact,
        recovery_id};

    if (compressed) {
        ec_compressed point;
        if ( ! recover_public(point, recoverable, message_digest)) {
            return false;
        }

        out_hash = bitcoin_short_hash(point);
        return true;
    }

    ec_uncompressed point;
    if ( ! recover_public(point, recoverable, message_digest)) {
        return false;
    }

    out_hash = bitcoin_short_hash(point);
    return true;
}

bool recovery_id_to_magic(uint8_t& out_magic, uint8_t recovery_id, bool compressed) {
    if (recovery_id > max_recovery_id) {
        return false;
    }

    // Offset the recovery id with sentinels to indication compression state.
    auto const increment = compressed ? magic_compressed : magic_uncompressed;
    out_magic = recovery_id + increment;
    return true;
}

bool magic_to_recovery_id(uint8_t& out_recovery_id, bool& out_compressed, uint8_t magic) {
    // Magic less offsets cannot exceed recovery id range [0, max_recovery_id].
    if (magic < magic_uncompressed ||
        magic > magic_compressed + max_recovery_id) {
        return false;
    }

    // Subtract smaller sentinel (guarded above).
    auto recovery_id = magic - magic_uncompressed;

    // Obtain compression state (differential exceeds the recovery id range).
    out_compressed = recovery_id >= magic_differential;

    // If compression is indicated subtract differential (guarded above).
    if (out_compressed) {
        recovery_id -= magic_differential;
    }

    out_recovery_id = *safe_to_unsigned<uint8_t>(recovery_id);
    return true;
}

bool sign_message(message_signature& out_signature, data_slice message, ec_private const& secret) {
    return sign_message(out_signature, message, secret, secret.compressed());
}

bool sign_message(message_signature& out_signature, data_slice message, std::string const& wif) {
    ec_private secret(wif);
    return (secret && sign_message(out_signature, message, secret, secret.compressed()));
}

bool sign_message(message_signature& out_signature, data_slice message, ec_secret const& secret, bool compressed) {
    recoverable_signature recoverable;
    if ( ! sign_recoverable(recoverable, secret, hash_message(message))) {
        return false;
    }

    uint8_t magic;
    if ( ! recovery_id_to_magic(magic, recoverable.recovery_id, compressed)) {
        return false;
    }

    out_signature = splice(to_array(magic), recoverable.signature);
    return true;
}

bool verify_message(data_slice message, payment_address const& address, message_signature const& signature) {
    auto const magic = signature.front();
    auto const compact = slice<1, message_signature_size>(signature);

    bool compressed;
    uint8_t recovery_id;
    if ( ! magic_to_recovery_id(recovery_id, compressed, magic)) {
        return false;
    }

    short_hash hash;
    auto const message_digest = hash_message(message);
    return recover(hash, compressed, compact, recovery_id, message_digest) &&
           hash == address.hash20();
}

} // namespace kth::domain::wallet
