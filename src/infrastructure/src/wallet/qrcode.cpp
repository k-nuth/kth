// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/wallet/qrcode.hpp>

#include <string>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::wallet {

data_chunk qr::encode(byte_span data) {
    return qr::encode(data, version, level, mode, case_sensitive);
}

data_chunk qr::encode(byte_span data, uint32_t version,
    error_recovery_level level, encode_mode mode, bool case_sensitive) {

    std::string const qr_string(data.begin(), data.end());

    auto const qrcode = QRcode_encodeString(qr_string.c_str(), version,
        level, mode, case_sensitive);

    if (qrcode == nullptr) {
        return {};
    }

    if (kth::max_size_t / qrcode->width < qrcode->width) {
        QRcode_free(qrcode);
        return {};
    }

    auto const area = qrcode->width * qrcode->width;

    // Write out raw format of QRcode structure (defined in qrencode.h).
    // Format written is:
    // uint32_t version (little-endian)
    // uint32_t width (little-endian)
    // unsigned char* data (of width^2 length)
    data_chunk out;
    out.reserve(sizeof(uint32_t) + sizeof(uint32_t) + area);

    auto const version_ptr = reinterpret_cast<uint8_t const*>(&qrcode->version);
    auto const width_ptr = reinterpret_cast<uint8_t const*>(&qrcode->width);

    out.insert(out.end(), version_ptr, version_ptr + sizeof(uint32_t));
    out.insert(out.end(), width_ptr, width_ptr + sizeof(uint32_t));
    out.insert(out.end(), qrcode->data, qrcode->data + area);

    QRcode_free(qrcode);
    return out;
}

} // namespace kth::infrastructure::wallet
