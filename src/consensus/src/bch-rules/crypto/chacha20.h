// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <span.h>

#include <cstdint>
#include <cstdlib>

/** A PRNG class for ChaCha20. */
class ChaCha20 {
private:
    uint32_t input[16];

public:
    ChaCha20();
    ChaCha20(const uint8_t *key, size_t keylen);
    void SetKey(const uint8_t *key, size_t keylen);
    void SetIV(uint64_t iv);
    void Seek(uint64_t pos);
    void Output(uint8_t *output, size_t bytes);
};

/** ChaCha20 cipher that only operates on multiples of 64 bytes. */
class ChaCha20Aligned {
    uint32_t input[12];

public:
    /** Expected key length in constructor and SetKey. */
    static constexpr unsigned KEYLEN{32};

    /** Block size (inputs/outputs to Keystream / Crypt should be multiples of this). */
    static constexpr unsigned BLOCKLEN{64};

    /** For safety, disallow initialization without key. */
    ChaCha20Aligned() noexcept = delete;

    /** Initialize a cipher with specified 32-byte key. */
    ChaCha20Aligned(Span<const uint8_t> key) noexcept;

    /** Destructor to clean up private memory. */
    ~ChaCha20Aligned();

    /** Set 32-byte key, and seek to nonce 0 and block position 0. */
    void SetKey(Span<const uint8_t> key) noexcept;

    /** Type for 96-bit nonces used by the Set function below.
     *
     * The first field corresponds to the LE32-encoded first 4 bytes of the nonce, also referred
     * to as the '32-bit fixed-common part' in Example 2.8.2 of RFC8439.
     *
     * The second field corresponds to the LE64-encoded last 8 bytes of the nonce.
     *
     */
    using Nonce96 = std::pair<uint32_t, uint64_t>;

    /** Set the 96-bit nonce and 32-bit block counter.
     *
     * Block_counter selects a position to seek to (to byte BLOCKLEN*block_counter). After 256 GiB,
     * the block counter overflows, and nonce.first is incremented.
     */
    void Seek(Nonce96 nonce, uint32_t block_counter) noexcept;

    /** outputs the keystream into out, whose length must be a multiple of BLOCKLEN. */
    void Keystream(Span<uint8_t> out) noexcept;

    /** en/deciphers the message <input> and write the result into <output>
     *
     * The size of input and output must be equal, and be a multiple of BLOCKLEN.
     */
    void Crypt(Span<const uint8_t> input, Span<uint8_t> output) noexcept;
};
