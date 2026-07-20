// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CRYPTO_SECP256K1_INITIALIZER_HPP
#define KTH_CRYPTO_SECP256K1_INITIALIZER_HPP

#include <mutex>

#include <secp256k1.h>

#include <kth/infrastructure/define.hpp>

namespace kth {

/**
 * Virtual base class for secp256k1 context management.
 * This class holds no static state but will only initialize its state once for
 * the given mutex. This can be assigned to a static or otherwise. It lazily
 * inits the context once and destroys the context on destruct as necessary.
 */
class KI_API secp256k1_initializer
{
private:
    static void set_context(secp256k1_context** context, int flags);

protected:
    int flags_;

    /**
     * Construct a signing context initializer of the specified context.
     * @param[in]  flags  { SECP256K1_CONTEXT_SIGN, SECP256K1_CONTEXT_VERIFY }
     */
    secp256k1_initializer(int flags);

public:
    /**
     * Free the context if initialized.
     */
    ~secp256k1_initializer();

    /**
     * Call to obtain the secp256k1 context, initialized on first call.
     */
    secp256k1_context* context();

private:
    std::once_flag mutex_;
    secp256k1_context* context_;
};

/**
 * Create and hold this class to initialize signing context on first use.
 */
class KI_API secp256k1_signing
  : public secp256k1_initializer
{
public:
    /**
     * Construct a signing context initializer.
     */
    secp256k1_signing();
};

/**
 * Create and hold this class to initialize verification context on first use.
 */
class KI_API secp256k1_verification
  : public secp256k1_initializer
{
public:
    /**
     * Construct a verification context initializer.
     */
    secp256k1_verification();
};

/**
 * Use kth::signing.context() to obtain the secp256k1 signing context.
 */
extern secp256k1_signing signing;

/**
 * Use kth::verification.context() to obtain the secp256k1 verification context.
 */
extern secp256k1_verification verification;

} // namespace kth

#endif
