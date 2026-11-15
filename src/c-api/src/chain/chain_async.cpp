// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_async.h>
#include <array>
#include <cstdio>
#include <memory>
#include <system_error>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/capi/detail/mining_template.hpp>

#include <kth/capi/domain/chain/block_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

#include <asio/co_spawn.hpp>
#include <kth/capi/detail/async_exception.hpp>

namespace {

inline
kth::blockchain::block_chain& safe_chain(kth_chain_t chain) {
    return *static_cast<kth::blockchain::block_chain*>(chain);
}

inline
kth::domain::message::transaction::const_ptr tx_shared(kth_transaction_const_t tx) {
    auto const& tx_ref = *static_cast<kth::domain::chain::transaction const*>(tx);
    auto* tx_new = new kth::domain::message::transaction(tx_ref);
    return kth::domain::message::transaction::const_ptr(tx_new);
}

inline
kth::domain::message::block::const_ptr block_shared(kth_block_const_t block) {
    auto const& block_ref = *static_cast<kth::domain::chain::block const*>(block);
    auto* block_new = new kth::domain::message::block(block_ref);
    return kth::domain::message::block::const_ptr(block_new);
}

} /* end of anonymous namespace */


// ---------------------------------------------------------------------------
extern "C" {

void kth_chain_async_last_height(kth_chain_t chain, void* ctx, kth_last_height_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            auto error = result ? kth_ec_success : kth::to_c_err(result.error());
            handler(chain, ctx, error, result ? result->block : 0);
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, 0);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc]() { return bc.fetch_last_height(); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

// kth_chain_async_mining_info is generated (chain_mining.cpp).

void kth_chain_async_mining_template(kth_chain_t chain, uint64_t coinbase_reserve_size, void* ctx, kth_mining_template_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                kth_mining_template_t tmpl;
                kth_transaction_list_mut_t txs = nullptr;
                kth::capi::fill_mining_template(tmpl, txs, *result);
                handler(chain, ctx, kth::to_c_err(std::error_code{}), tmpl, txs);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), kth_mining_template_t{}, nullptr);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, kth_mining_template_t{}, nullptr);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, coinbase_reserve_size]() { return bc.fetch_mining_template(coinbase_reserve_size); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

// kth_chain_async_block_height is generated (chain_query.cpp).

void kth_chain_async_block_header_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_block_header_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [header, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(header, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, height]() { return bc.fetch_block_header(height); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_block_header_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_block_header_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [header, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(header, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto hash_cpp = std::to_array(hash.hash);
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, hash_cpp]() { return bc.fetch_block_header(hash_cpp); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_block_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_block_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [block, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(block, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, height]() { return bc.fetch_block(height); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_block_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_block_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [block, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(block, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto hash_cpp = std::to_array(hash.hash);
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, hash_cpp]() { return bc.fetch_block(hash_cpp); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_block_header_by_hash_txs_size(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_block_header_txs_size_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [header, block_height, tx_hashes, block_serialized_size] = *result;
                auto header_owned = kth::stage_owned(kth::to_owned_if_success(header, std::error_code{}));
                auto hashes_owned = kth::stage_owned(kth::to_owned_if_success(tx_hashes, std::error_code{}));
                handler(chain, ctx, kth::to_c_err(std::error_code{}),
                        header_owned.release(), block_height, hashes_owned.release(), block_serialized_size);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0, nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0, nullptr, 0);
        }
    };
    try {
        auto hash_cpp = std::to_array(hash.hash);
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, hash_cpp]() { return bc.fetch_block_header_txs_size(hash_cpp); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_merkle_block_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_merkle_block_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [block, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(block, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, height]() { return bc.fetch_merkle_block(height); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_merkle_block_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_merkle_block_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [block, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(block, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto hash_cpp = std::to_array(hash.hash);
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, hash_cpp]() { return bc.fetch_merkle_block(hash_cpp); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_compact_block_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_compact_block_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [block, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(block, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, height]() { return bc.fetch_compact_block(height); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_compact_block_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_compact_block_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [block, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(block, std::error_code{}), h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0);
        }
    };
    try {
        auto hash_cpp = std::to_array(hash.hash);
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, hash_cpp]() { return bc.fetch_compact_block(hash_cpp); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_block_by_height_timestamp(kth_chain_t chain, void* ctx, kth_size_t height, kth_blockhash_timestamp_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [hash, timestamp, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_hash_t(hash), timestamp, h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), kth::to_hash_t(kth::null_hash), 0, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, kth_hash_t{}, 0, 0);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, height]() { return bc.fetch_block_hash_timestamp(height); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_transaction(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_bool_t require_confirmed, kth_transaction_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [transaction, i, h] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_owned_if_success(transaction, std::error_code{}), i, h);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr, 0, 0);
        }
    };
    try {
        auto hash_cpp = std::to_array(hash.hash);
        auto const confirmed = kth::int_to_bool(require_confirmed);
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, confirmed]() { return bc.fetch_transaction(hash_cpp, confirmed); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_transaction_position(kth_chain_t chain, void* ctx, kth_hash_t hash, int require_confirmed, kth_transaction_index_fetch_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto result) {
            if (result) {
                auto const& [position, height] = *result;
                handler(chain, ctx, kth::to_c_err(std::error_code{}), position, height);
            } else {
                handler(chain, ctx, kth::to_c_err(result.error()), 0, 0);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, 0, 0);
        }
    };
    try {
        auto hash_cpp = std::to_array(hash.hash);
        auto const confirmed = kth::int_to_bool(require_confirmed);
        auto& bc = safe_chain(chain);
        ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, confirmed]() { return bc.fetch_transaction_position(hash_cpp, confirmed); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

// The confirmed address index (spend / history / confirmed transactions) was
// backed by the LMDB spend and history stores, which the v1 node no longer
// maintains. Those queries were removed from block_chain; a v1 address index
// will be reintroduced here if/when it is needed.

// Organizers.
//-------------------------------------------------------------------------

void kth_chain_async_submit_block(kth_chain_t chain, void* ctx, kth_block_mut_t block, kth_result_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto error) {
            handler(chain, ctx, kth::to_c_err(error));
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        // Capture the owned admission operation before scheduling the C callback;
        // the coroutine never dereferences the chain object after this call.
        auto operation = bc.submit_block(block_shared(block));
        // Only the status crosses this boundary. Its construction/move is
        // nonthrowing, including Asio's exceptional completion path.
        ::asio::co_spawn(bc.executor(),
            [operation = std::move(operation)]() mutable -> ::asio::awaitable<std::error_code> {
                auto result = co_await std::move(operation);
                co_return result.error;
            }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

void kth_chain_async_organize_transaction(kth_chain_t chain, void* ctx, kth_transaction_mut_t transaction, kth_result_handler_t handler) {
    KTH_PRECONDITION(handler != nullptr);
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto ec) {
            handler(chain, ctx, kth::to_c_err(ec));
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        auto tx_cpp = tx_shared(transaction);
        ::asio::co_spawn(bc.executor(), [&bc, tx_cpp]() { return bc.submit_transaction(tx_cpp); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

} // extern "C"
