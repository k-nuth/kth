// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_DURABILITY_HPP_
#define KTH_DATABASE_DURABILITY_HPP_

#include <kth/database/define.hpp>

namespace kth::database {

/// What KTH can promise about a transition reaching stable storage.
///
/// KTH's OWN guarantee, not any one store's. A chain transition crosses four
/// barriers — the LMDB environment, the contents of the rev files, the
/// directory entries naming them, and UTXO-Z — and what the node may claim is
/// the weakest of the four. Naming it here, rather than quoting whichever store
/// was asked last, is what keeps a caller from reading one store's `full` as
/// the node's.
///
/// The shape deliberately mirrors `utxoz::durability_level`, because a value
/// that has to be combined with it must be comparable to it.
enum class durability_level {
    full,           ///< File contents and the names reaching them can both be made durable.
    contents_only,  ///< Contents can; directory entries have no exposed barrier.
    none,           ///< A virtual filesystem: there is no stable storage to reach.
};

/// The result of asking a store for a barrier.
///
/// Three answers, because "this platform has none" and "one was attempted and
/// failed" are different facts and only the second is a defect. Collapsing them
/// into a bool is what let `sync_unsupported` and `sync_failed` arrive as the
/// same thing.
enum class barrier_outcome {
    crossed,        ///< The barrier ran and succeeded.
    unsupported,    ///< The platform exposes none. Consistent only with `none`.
    failed,         ///< It was attempted and did not succeed. Fatal, on every platform.
};

/// The weakest of the four barriers a chain transition depends on.
///
/// Computed rather than configured: a platform's answer cannot be improved by
/// declaring it, and a mismatch between what is declared and what the stores do
/// is exactly the kind of thing that reads as a guarantee and is not one.
[[nodiscard]]
KD_API durability_level node_durability_level();

[[nodiscard]]
KD_API char const* to_string(durability_level level);

[[nodiscard]]
KD_API char const* to_string(barrier_outcome outcome);

} // namespace kth::database

#endif // KTH_DATABASE_DURABILITY_HPP_
