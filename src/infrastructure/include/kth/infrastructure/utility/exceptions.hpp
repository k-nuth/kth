// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_EXCEPTIONS_HPP
#define KTH_INFRASTRUCTURE_EXCEPTIONS_HPP

#include <exception>

#include <kth/infrastructure/define.hpp>

namespace kth {

class KI_API end_of_stream
    : std::exception
{};

}

#endif
