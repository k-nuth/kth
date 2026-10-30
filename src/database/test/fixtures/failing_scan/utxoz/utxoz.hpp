// Fault injection at the UTXO-Z boundary for the production fingerprint CLI.
// Deliver one entry, then fail: a partial traversal must never print a fingerprint.
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <expected>
#include <span>
#include <string>

namespace utxoz {
using raw_outpoint = std::array<uint8_t, 36>;
enum class error_code { version_unreadable };
struct full_db {
    static std::expected<full_db, error_code> open(std::string const&) { return full_db{}; }
    std::size_t size() const { return 2; }
    template <typename F>
    [[nodiscard]] std::expected<void, error_code> for_each_entry(F&& f) const {
        raw_outpoint key{};
        std::array<uint8_t, 1> const data{42};
        f(key, uint32_t{1}, std::span<uint8_t const>{data});
        return std::unexpected(error_code::version_unreadable);
    }
};
}
