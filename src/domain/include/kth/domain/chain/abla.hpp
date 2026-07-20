// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_ABLA_HPP
#define KTH_DOMAIN_CHAIN_ABLA_HPP

#include <cinttypes>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#define HAVE_INT128 1

#if !HAVE_INT128
#include <gmp.h>
#endif

namespace {

#if HAVE_INT128
using uint128_t = unsigned __int128;

inline constexpr
std::optional<uint64_t> muldiv(uint64_t x, uint64_t y, uint64_t z) {
    if (z == 0) {
        return std::nullopt;
    }
    uint128_t const res = (uint128_t(x) * uint128_t(y)) / uint128_t(z);
    if (res > uint128_t(std::numeric_limits<uint64_t>::max())) {
        return std::nullopt;
    }
    return uint64_t(res);
}

#else

#error "You need to have __int128 support to use this library"

// Not tested yet
// std::optional<uint64_t> muldiv(uint64_t x, uint64_t y, uint64_t z) {
//     if (z == 0) {
//         return std::nullopt;
//     }

//     mpz_t gx;
//     mpz_t gy;
//     mpz_t gz;
//     mpz_t gres;

//     mpz_init(gx);
//     mpz_init(gy);
//     mpz_init(gz);
//     mpz_init(gres);

//     mpz_set_ui(gx, x);
//     mpz_set_ui(gy, y);
//     mpz_set_ui(gz, z);

//     mpz_mul(gres, gx, gy);          // gres = gx * gy
//     mpz_tdiv_q(gres, gres, gz);     // gres = gres / gz

//     if (mpz_cmp_ui(gres, std::numeric_limits<uint64_t>::max()) > 0) {
//         return std::nullopt;
//     }

//     uint64_t result = mpz_get_ui(gres);

//     mpz_clear(gx);
//     mpz_clear(gy);
//     mpz_clear(gz);
//     mpz_clear(gres);

//     return result;
// }

#endif // HAVE_INT128

} // anonymous namespace

namespace kth::domain::chain::abla {

constexpr const uint64_t DEFAULT_CONSENSUS_BLOCK_SIZE = 32'000'000u;

struct config {
    uint64_t epsilon0 {};
    uint64_t beta0 {};
    uint64_t n0 {};
    uint64_t gamma_reciprocal {};
    uint64_t zeta_xB7 {};
    uint64_t theta_reciprocal {};
    uint64_t delta {};
    uint64_t epsilon_max {};
    uint64_t beta_max {};
    static constexpr const uint64_t B7 = 1u << 7u;
    static constexpr const uint64_t MIN_ZETA_XB7 = 129u;
    static constexpr const uint64_t MAX_ZETA_XB7 = 256u;
    static constexpr const uint64_t MIN_GAMMA_RECIPROCAL = 9484u;
    static constexpr const uint64_t MAX_GAMMA_RECIPROCAL = 151744u;
    static constexpr const uint64_t MIN_DELTA = 0u;
    static constexpr const uint64_t MAX_DELTA = 32u;
    static constexpr const uint64_t MIN_THETA_RECIPROCAL = 9484u;
    static constexpr const uint64_t MAX_THETA_RECIPROCAL = 151744u;
};

inline constexpr
void set_max(config& cfg) {
    uint64_t const max_safe_blocksize_limit = std::numeric_limits<uint64_t>::max() / cfg.zeta_xB7 * config::B7;
    uint64_t const max_elastic_buffer_ratio_numerator = cfg.delta * ((cfg.zeta_xB7 - config::B7) * cfg.theta_reciprocal / cfg.gamma_reciprocal);
    uint64_t const max_elastic_buffer_ratio_denominator = (cfg.zeta_xB7 - config::B7) * cfg.theta_reciprocal / cfg.gamma_reciprocal + config::B7;

    cfg.epsilon_max = max_safe_blocksize_limit / (max_elastic_buffer_ratio_numerator + max_elastic_buffer_ratio_denominator) * max_elastic_buffer_ratio_denominator;
    cfg.beta_max = max_safe_blocksize_limit - cfg.epsilon_max;

    // return cfg;
}

inline constexpr
config default_config(uint64_t default_block_size = DEFAULT_CONSENSUS_BLOCK_SIZE, bool fixed_size = false) {
    config ret;
    ret.epsilon0 = default_block_size / 2u;
    ret.beta0 = default_block_size / 2u;
    ret.gamma_reciprocal = 37938;
    ret.zeta_xB7 = 192;
    ret.theta_reciprocal = 37938;
    ret.delta = 10;
    if ( ! fixed_size) {
        // Auto-set epsilon_max and beta_max to huge, 64-bit safe values
        set_max(ret);
    } else {
        // Fixed-size, rendering this EBAA algorithm a no-op that always returns `default_block_size` (testnets 3 & 4)
        ret.epsilon_max = ret.epsilon0;
        ret.beta_max = ret.beta0;
    }
    return ret;
}

enum class config_validity {
    valid,
    error_epsilon_max,
    error_beta_max,
    error_zeta,
    error_gamma_reciprocal,
    error_delta,
    error_theta_reciprocal,
    error_epsilon0
};

inline constexpr
std::string_view to_string(config_validity validity) {
    switch (validity) {
        case config_validity::valid: return "Valid configuration";
        case config_validity::error_epsilon_max: return "Error, initial control block size limit sanity check failed (epsilonMax)";
        case config_validity::error_beta_max: return "Error, initial elastic buffer size sanity check failed (betaMax).";
        case config_validity::error_zeta: return "Error, zeta sanity check failed.";
        case config_validity::error_gamma_reciprocal: return "Error, gammaReciprocal sanity check failed.";
        case config_validity::error_delta: return "Error, delta sanity check failed.";
        case config_validity::error_theta_reciprocal: return "Error, thetaReciprocal sanity check failed.";
        case config_validity::error_epsilon0: return "Error, epsilon0 sanity check failed. Too low relative to gamma and zeta.";
        default: return "Unknown error";
    }
}

inline constexpr
config_validity validate(config const& cfg) {
    if (cfg.epsilon0 > cfg.epsilon_max) {
        return config_validity::error_epsilon_max;
    }

    if (cfg.beta0 > cfg.beta_max) {
        return config_validity::error_beta_max;
    }

    if (cfg.zeta_xB7 < config::MIN_ZETA_XB7 || cfg.zeta_xB7 > config::MAX_ZETA_XB7) {
        return config_validity::error_zeta;
    }

    if (cfg.gamma_reciprocal < config::MIN_GAMMA_RECIPROCAL || cfg.gamma_reciprocal > config::MAX_GAMMA_RECIPROCAL) {
        return config_validity::error_gamma_reciprocal;
    }

    if (cfg.delta + 1 <= config::MIN_DELTA || cfg.delta > config::MAX_DELTA) {
        return config_validity::error_delta;
    }

    if (cfg.theta_reciprocal < config::MIN_THETA_RECIPROCAL || cfg.theta_reciprocal > config::MAX_THETA_RECIPROCAL) {
        return config_validity::error_theta_reciprocal;
    }

    if (cfg.epsilon0 < muldiv(cfg.gamma_reciprocal, config::B7, cfg.zeta_xB7 - config::B7)) {
        // Required due to truncation of integer ops.
        // With this we ensure that the control size can be adjusted for at least 1 byte.
        // Also, with this we ensure that divisior bytesMax in calculateNextABLAState() can't be 0.
        return config_validity::error_epsilon0;
    }

    return config_validity::valid;
}

struct state {
    uint64_t block_size {};
    uint64_t control_block_size {};
    uint64_t elastic_buffer_size {};

    state() = default;

    constexpr
    state(config const& cfg, uint64_t block_size)
        : block_size(block_size), control_block_size(cfg.epsilon0), elastic_buffer_size(cfg.beta0)
    {}
};

// Calculate the limit for the block to which the algorithm's state
// applies, given algorithm state
inline constexpr
uint64_t block_size_limit(state const& st) {
    return st.control_block_size + st.elastic_buffer_size;
}

// Calculate algorithm's state for the next block (n), given
// current blockchain tip (n-1) block size, algorithm state, and
// algorithm configuration.
inline constexpr
std::optional<state> next(state const& st, config const& cfg, uint64_t next_block_size) {
    state ret;
    ret.block_size = next_block_size;  // save the blocksize for the next block to its State

    // For safety: we clamp this current block's blocksize to the maximum value this algorithm expects. Normally this
    // won't happen unless the node is run with some -excessiveblocksize parameter that permits larger blocks than this
    // algo's current state expects.
    // current_block_size = std::min(current_block_size, st.control_block_size + st.elastic_buffer_size);
    uint64_t const clamped_block_size = std::min(st.block_size, st.control_block_size + st.elastic_buffer_size);

    // algorithmic limit
    // control function

    // zeta * x_{n-1}
    auto const amplified_current_block_size_opt = muldiv(cfg.zeta_xB7, clamped_block_size, cfg.B7);
    if ( ! amplified_current_block_size_opt) {
        return std::nullopt;
    }
    uint64_t const amplified_current_block_size = *amplified_current_block_size_opt;

    // if zeta * x_{n-1} > epsilon_{n-1} then increase
    if (amplified_current_block_size > st.control_block_size) {
        // zeta * x_{n-1} - epsilon_{n-1}
        uint64_t const bytes_to_add = amplified_current_block_size - st.control_block_size;

        // zeta * y_{n-1}
        auto const amplified_block_size_limit_opt = muldiv(cfg.zeta_xB7, st.control_block_size + st.elastic_buffer_size, cfg.B7);
        if ( ! amplified_block_size_limit_opt) {
            return std::nullopt;
        }
        uint64_t const amplified_block_size_limit = *amplified_block_size_limit_opt;

        // zeta * y_{n-1} - epsilon_{n-1}
        uint64_t const bytes_max = amplified_block_size_limit - st.control_block_size;

        // zeta * beta_{n-1} * (zeta * x_{n-1} - epsilon_{n-1}) / (zeta * y_{n-1} - epsilon_{n-1})
        auto const tmp = muldiv(cfg.zeta_xB7, st.elastic_buffer_size, cfg.B7);
        if ( ! tmp) {
            return std::nullopt;
        }
        auto const scaling_offset_opt = muldiv(*tmp, bytes_to_add, bytes_max);
        if ( ! scaling_offset_opt) {
            return std::nullopt;
        }
        uint64_t const scaling_offset = *scaling_offset_opt;

        // epsilon_n = epsilon_{n-1} + gamma * (zeta * x_{n-1} - epsilon_{n-1} - zeta * beta_{n-1} * (zeta * x_{n-1} - epsilon_{n-1}) / (zeta * y_{n-1} - epsilon_{n-1}))
        ret.control_block_size = st.control_block_size + (bytes_to_add - scaling_offset) / cfg.gamma_reciprocal;
    } else {
        // if zeta * x_{n-1} <= epsilon_{n-1} then decrease or no change
        // epsilon_{n-1} - zeta * x_{n-1}
        uint64_t const bytes_to_remove = st.control_block_size - amplified_current_block_size;

        // epsilon_{n-1} + gamma * (zeta * x_{n-1} - epsilon_{n-1})
        // rearranged to:
        // epsilon_{n-1} - gamma * (epsilon_{n-1} - zeta * x_{n-1})
        ret.control_block_size = st.control_block_size - bytes_to_remove / cfg.gamma_reciprocal;

        // epsilon_n = max(epsilon_{n-1} + gamma * (zeta * x_{n-1} - epsilon_{n-1}), epsilon_0)
        ret.control_block_size = std::max(ret.control_block_size, cfg.epsilon0);
    }

    // elastic buffer function

    // beta_{n-1} * theta
    uint64_t const buffer_decay = st.elastic_buffer_size / cfg.theta_reciprocal;

    // if zeta * x_{n-1} > epsilon_{n-1} then increase
    if (amplified_current_block_size > st.control_block_size) {
        // (epsilon_{n} - epsilon_{n-1}) * delta
        uint64_t const bytes_to_add = (ret.control_block_size - st.control_block_size) * cfg.delta;

        // beta_{n-1} - beta_{n-1} * theta + (epsilon_{n} - epsilon_{n-1}) * delta
        ret.elastic_buffer_size = st.elastic_buffer_size - buffer_decay + bytes_to_add;
    } else {
        // if zeta * x_{n-1} <= epsilon_{n-1} then decrease or no change
        // beta_{n-1} - beta_{n-1} * theta
        ret.elastic_buffer_size = st.elastic_buffer_size - buffer_decay;
    }

    // max(beta_{n-1} - beta_{n-1} * theta + (epsilon_{n} - epsilon_{n-1}) * delta, beta_0) , if zeta * x_{n-1} > epsilon_{n-1}
    // max(beta_{n-1} - beta_{n-1} * theta, beta_0) , if zeta * x_{n-1} <= epsilon_{n-1}
    ret.elastic_buffer_size = std::max(ret.elastic_buffer_size, cfg.beta0);

    // clip control_block_size to epsilonMax to avoid integer overflow for extreme sizes
    ret.control_block_size = std::min(ret.control_block_size, cfg.epsilon_max);
    // clip elastic_buffer_size to betaMax to avoid integer overflow for extreme sizes
    ret.elastic_buffer_size = std::min(ret.elastic_buffer_size, cfg.beta_max);

    return ret;
}

// Calculate algorithm's look-ahead block size limit, for a block N blocks ahead of current one.
// Returns the limit for block with current+N height, assuming all blocks 100% full.
inline constexpr
std::optional<state> lookahead(state const& st, config const& cfg, size_t count) {
    state ret = st;
    for (size_t i = 0; i < count; ++i) {
        uint64_t const max_size = block_size_limit(ret);
        auto const opt = next(ret, cfg, max_size);
        if ( ! opt) {
            return std::nullopt;
        }
        ret = *opt;
    }
    return ret;
}

enum class state_validity {
    valid,
    error_control_block_size,
    error_elastic_buffer_size
};

inline constexpr
std::string_view to_string(state_validity validity) {
    switch (validity) {
        case state_validity::valid: return "Valid state";
        case state_validity::error_control_block_size: return "Error, invalid control_block_size state. Can't be below initialization value or above epsilonMax.";
        case state_validity::error_elastic_buffer_size: return "Error, invalid elastic_buffer_size state. Can't be below initialization value or above betaMax.";
        default: return "Unknown error";
    }
}

// Returns true if this state is valid relative to `config`. On false return, optional out `err` is set
// to point to a constant string explaining the reason that this state is invalid.
inline constexpr
state_validity validate(state const& st, config const& cfg) {
    if (st.control_block_size < cfg.epsilon0 || st.control_block_size > cfg.epsilon_max) {
        return state_validity::error_control_block_size;
    }
    if (st.elastic_buffer_size < cfg.beta0 || st.elastic_buffer_size > cfg.beta_max) {
        return state_validity::error_elastic_buffer_size;
    }
    return state_validity::valid;
}

} // namespace kth::domain::chain::abla

#endif //KTH_DOMAIN_CHAIN_ABLA_HPP