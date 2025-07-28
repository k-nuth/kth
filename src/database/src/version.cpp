// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/version.hpp>

namespace kth::database {

char const* version() {
    return KTH_DATABASE_VERSION;
}

}  // namespace kth::database
