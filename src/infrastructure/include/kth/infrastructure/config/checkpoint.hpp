// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_CHECKPOINT_HPP
#define KTH_INFRASTUCTURE_CONFIG_CHECKPOINT_HPP

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for a blockchain checkpoint.
 * This is a container for a {block hash, block height} tuple.
 */
struct KI_API checkpoint {
    using list = std::vector<checkpoint>;

    /**
     * Created a sorted copy of the list of checkpoints.
     * @param[in]  checks  The list of checkpoints.
     * @return             The sorted list of checkpoints.
     */
    static
    list sort(list const& checks);

    /**
     * Confirm a checkpoint is in the range of a sorted list of checkpoints.
     * @param[in]  height  The height of checkpoint.
     * @param[in]  checks  The list of checkpoints.
     */
    static
    bool covered(size_t height, list const& checks);

    /**
     * Validate a checkpoint against a set of checkpoints.
     * @param[in]  hash    The hash of the checkpoint.
     * @param[in]  height  The height of checkpoint.
     * @param[in]  checks  The set of checkpoints.
     */
    static
    bool validate(hash_digest const& hash, size_t height, list const& checks);

    checkpoint() = default;
    checkpoint(checkpoint const& x) noexcept = default;

    /**
     * Initialization constructor.
     * The height is optional and will be set to zero if not provided.
     * @param[in]  value  The value of the hash[:height] form.
     */
    explicit
    checkpoint(std::string const& value);

    /**
     * Initialization constructor.
     * @param[in]  hash    The string block hash for the checkpoint.
     * @param[in]  height  The height of the hash.
     */
    checkpoint(std::string const& hash, size_t height);

    /**
     * Initialization constructor.
     * @param[in]  hash    The block hash for the checkpoint.
     * @param[in]  height  The height of the hash.
     */
    checkpoint(hash_digest const& hash, size_t height);

    hash_digest const& hash() const;
    size_t const height() const;

    /**
     * Get the checkpoint as a string.
     * @return The ip address of the authority in the hash:height form.
     */
    std::string to_string() const;

    // bool operator==( const& x) const;
    friend
    auto operator<=>(checkpoint const&, checkpoint const&) = default;

    friend
    std::istream& operator>>(std::istream& input, checkpoint& argument);

    friend
    std::ostream& operator<<(std::ostream& output, checkpoint const& argument);

private:
    hash_digest hash_ = null_hash;
    size_t height_ = 0;
};

} // namespace kth::infrastructure::config

#endif
