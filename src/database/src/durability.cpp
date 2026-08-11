// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/durability.hpp>

#include <algorithm>

#include <utxoz/utxoz.hpp>

#include <kth/database/native_file.hpp>

namespace kth::database {

namespace {

// Ordered weakest-last so the combination is a max over a rank, which is the
// one operation this needs and the only one that stays right when a level is
// added between two existing ones.
constexpr int rank(durability_level level) {
    switch (level) {
        case durability_level::full:          return 0;
        case durability_level::contents_only: return 1;
        case durability_level::none:          return 2;
    }
    return 2;
}

durability_level weakest(durability_level lhs, durability_level rhs) {
    return rank(lhs) >= rank(rhs) ? lhs : rhs;
}

durability_level from_utxoz(utxoz::durability_level level) {
    switch (level) {
        case utxoz::durability_level::full:          return durability_level::full;
        case utxoz::durability_level::contents_only: return durability_level::contents_only;
        case utxoz::durability_level::none:          return durability_level::none;
    }
    // A level this build does not know is not a level this build may claim.
    return durability_level::none;
}

// The two barriers KTH runs itself.
//
// LMDB's forced sync and the rev files' fsync/FlushFileBuffers both exist on
// every platform KTH builds a node for, so contents are always reachable; what
// varies is whether the name reaching a newly created file can be published.
// That is exactly what `directory_barrier` reports.
durability_level kth_own_barriers() {
#ifdef _WIN32
    return durability_level::contents_only;
#else
    return durability_level::full;
#endif
}

} // namespace

durability_level node_durability_level() {
    return weakest(kth_own_barriers(), from_utxoz(utxoz::platform_durability()));
}

char const* to_string(durability_level level) {
    switch (level) {
        case durability_level::full:          return "full";
        case durability_level::contents_only: return "contents_only";
        case durability_level::none:          return "none";
    }
    return "unrecognised";
}

char const* to_string(barrier_outcome outcome) {
    switch (outcome) {
        case barrier_outcome::crossed:     return "crossed";
        case barrier_outcome::unsupported: return "unsupported";
        case barrier_outcome::failed:      return "failed";
    }
    return "unrecognised";
}

} // namespace kth::database
