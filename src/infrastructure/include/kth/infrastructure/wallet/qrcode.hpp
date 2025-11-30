// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_QRCODE_HPP
#define KTH_INFRASTRUCTURE_QRCODE_HPP

#include <cstdint>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <qrencode.h>

namespace kth::infrastructure::wallet {

struct KI_API qr {
    using encode_mode = QRencodeMode;
    using error_recovery_level = QRecLevel;

    static constexpr uint32_t version = 0;
    static constexpr bool case_sensitive = true;
    static constexpr encode_mode mode = QR_MODE_8;
    static constexpr error_recovery_level level = QR_ECLEVEL_L;

    static
    data_chunk encode(byte_span data);

    static
    data_chunk encode(byte_span data, uint32_t version, error_recovery_level level,
        encode_mode mode, bool case_sensitive);
};

} // namespace kth::infrastructure::wallet

#endif // KTH_INFRASTRUCTURE_QRCODE_HPP
