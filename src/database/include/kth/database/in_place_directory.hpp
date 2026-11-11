// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_IN_PLACE_DIRECTORY_HPP_
#define KTH_DATABASE_IN_PLACE_DIRECTORY_HPP_

#include <expected>
#include <filesystem>
#include <system_error>

#include <kth/database/define.hpp>

namespace kth::database {

/// What prepare_in_place_directory() found or did.
enum class in_place_directory {
    /// The directory was empty on btrfs and is now NOCOW.
    nocow_set,
    /// The directory was already NOCOW.
    nocow_already,
    /// Not btrfs: there is nothing to change.
    not_btrfs,
    /// The directory already holds files: the attribute would apply only to
    /// files created from now on, so it is left as it is.
    not_empty,
    /// The platform has no such attribute.
    unsupported,
};

/// Creates `directory` if it is missing and, on btrfs, marks it NOCOW while it
/// is still empty, so the files created in it inherit the attribute.
///
/// For stores whose files are rewritten in place (memory-mapped UTXO-Z
/// containers, LMDB). On btrfs every write to a page that already reached the
/// disk reserves space for a new copy of it, and under a sustained stream of
/// such writes the writers wait on those reservations. NOCOW writes in place.
/// It also disables data checksums and compression for those files.
///
/// Fails only if the directory cannot be created or inspected; a refused
/// attribute is reported as an error too, and leaves the directory usable.
KD_API std::expected<in_place_directory, std::error_code>
prepare_in_place_directory(std::filesystem::path const& directory);

/// A short, stable description for logs.
KD_API char const* in_place_directory_name(in_place_directory value) noexcept;

} // namespace kth::database

#endif // KTH_DATABASE_IN_PLACE_DIRECTORY_HPP_
