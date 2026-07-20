// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ENABLE_SHARED_FROM_BASE_HPP
#define KTH_INFRASTRUCTURE_ENABLE_SHARED_FROM_BASE_HPP

#include <memory>

namespace kth {

/// Because enable_shared_from_this doesn't support inheritance.
template <typename Base>
class enable_shared_from_base
  : public std::enable_shared_from_this<Base>
{
protected:
    template <typename Derived>
    std::shared_ptr<Derived> shared_from_base() {
        return std::static_pointer_cast<Derived>(this->shared_from_this());
    }
};

} // namespace kth

#endif
