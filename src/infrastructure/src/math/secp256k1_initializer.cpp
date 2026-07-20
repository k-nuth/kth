// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "secp256k1_initializer.hpp"

#include <mutex>
#include <secp256k1.h>

namespace kth {

// We do not share contexts because they may or may not both be required.
secp256k1_signing signing;
secp256k1_verification verification;

// Static helper for use with std::call_once.
void secp256k1_initializer::set_context(secp256k1_context** context,
    int flags)
{
    *context = secp256k1_context_create(flags);
}

// Protected base class constructor (must be derived).
secp256k1_initializer::secp256k1_initializer(int flags)
  : flags_(flags), context_(nullptr)
{
}

// Clean up the context on destruct.
secp256k1_initializer::~secp256k1_initializer()
{
    if (context_ != nullptr) {
        secp256k1_context_destroy(context_);
}
}

// Get the curve context and initialize on first use.
secp256k1_context* secp256k1_initializer::context()
{
    std::call_once(mutex_, set_context, &context_, flags_);
    return context_;
}

// Concrete type for signing init.
secp256k1_signing::secp256k1_signing()
  : secp256k1_initializer(SECP256K1_CONTEXT_SIGN)
{
}

// Concrete type for verification init.
secp256k1_verification::secp256k1_verification()
  : secp256k1_initializer(SECP256K1_CONTEXT_VERIFY)
{
}

} // namespace kth
