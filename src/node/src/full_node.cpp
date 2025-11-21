// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/full_node.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <print>
#include <utility>
#include <kth/blockchain.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/node/sessions/session_block_sync.hpp>
#include <kth/node/sessions/session_header_sync.hpp>
#include <kth/node/sessions/session_inbound.hpp>
#include <kth/node/sessions/session_manual.hpp>
#include <kth/node/sessions/session_outbound.hpp>
#endif

#if defined(KTH_STATISTICS_ENABLED)
#include <tabulate/table.hpp>

tabulate::Row& last(tabulate::Table& table) {
    auto const l = table.end();
    auto f = table.begin();
    auto p = f;
    ++f;

    while (f != l) {
        p = f;
        ++f;
    }
    return *p;
}
#endif

namespace kth::node {

using namespace kth::blockchain;
using namespace kth::domain::chain;
using namespace kth::domain::config;

#if ! defined(__EMSCRIPTEN__)
using namespace kth::network;
#endif

using namespace std::placeholders;

full_node::full_node(configuration const& configuration)
#if ! defined(__EMSCRIPTEN__)
    : multi_crypto_setter(configuration.network)
    , p2p(configuration.network)
#else
    : multi_crypto_setter()
#endif

#if ! defined(__EMSCRIPTEN__)
    , chain_(
        thread_pool()
        , configuration.chain
        , configuration.database
        , get_network(configuration.network.identifier, configuration.network.inbound_port == 48333)
        , configuration.network.relay_transactions
    )
#else
    , chain_(
        thread_pool()
        , configuration.chain
        , configuration.database
        , domain::config::network::mainnet
    )
#endif

#if ! defined(__EMSCRIPTEN__)
    , protocol_maximum_(configuration.network.protocol_maximum)
#endif
    , chain_settings_(configuration.chain)
    , node_settings_(configuration.node)

#if defined(__EMSCRIPTEN__)
    , threadpool_("")
#endif
{}

full_node::~full_node() {
    full_node::close();
}

// Start.
// ----------------------------------------------------------------------------

void full_node::start(result_handler handler) {
    if ( ! stopped()) {
        handler(error::operation_failed);
        return;
    }

    if ( ! chain_.start()) {
        LOG_ERROR(LOG_NODE, "Failure starting blockchain [full_node::start()].");
        handler(error::operation_failed);
        return;
    }

    // This is invoked on the same thread.
    // Stopped is true and no network threads until after this call.
    p2p::start(handler);
}

void full_node::start_chain(result_handler handler) {
    if ( ! stopped()) {
        handler(error::operation_failed);
        return;
    }

    if ( ! chain_.start()) {
        LOG_ERROR(LOG_NODE, "Failure starting blockchain [full_node::start_chain()].");
        handler(error::operation_failed);
        return;
    }

    std::thread t1([this, handler] {
        p2p::start_fake(handler);
    });
    t1.detach();
}

// Run sequence.
// ----------------------------------------------------------------------------

void full_node::run(result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    // Skip sync sessions.
    handle_running(error::success, handler);
    return;

    // TODO: make this safe by requiring sync if gaps found.
    ////// By setting no download connections checkpoints can be used without sync.
    ////// This also allows the maximum protocol version to be set below headers.
    ////if (settings_.sync_peers == 0)
    ////{
    ////    // This will spawn a new thread before returning.
    ////    handle_running(error::success, handler);
    ////    return;
    ////}

    ////// The instance is retained by the stop handler (i.e. until shutdown).
    ////auto const header_sync = attach_header_sync_session();

    ////// This is invoked on a new thread.
    ////header_sync->start(
    ////    std::bind(&full_node::handle_headers_synchronized,
    ////        this, _1, handler));
}

void full_node::run_chain(result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    // Skip sync sessions.
    handle_running_chain(error::success, handler);
    return;
}

void full_node::handle_headers_synchronized(code const& ec, result_handler handler) {
    ////if (stopped())
    ////{
    ////    handler(error::service_stopped);
    ////    return;
    ////}

    ////if (ec)
    ////{
    ////    LOG_ERROR(LOG_NODE
    ////       , "Failure synchronizing headers: ", ec.message());
    ////    handler(ec);
    ////    return;
    ////}

    ////// The instance is retained by the stop handler (i.e. until shutdown).
    ////auto const block_sync = attach_block_sync_session();

    ////// This is invoked on a new thread.
    ////block_sync->start(
    ////    std::bind(&full_node::handle_running,
    ////        this, _1, handler));
}

void full_node::handle_running(code const& ec, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        LOG_ERROR(LOG_NODE, "Failure synchronizing blocks: ", ec.message());
        handler(ec);
        return;
    }

    size_t top_height;
    hash_digest top_hash;

    if ( ! chain_.get_last_height(top_height) ||
         ! chain_.get_block_hash(top_hash, top_height)) {
             LOG_ERROR(LOG_NODE, "The blockchain is corrupt.");
        handler(error::operation_failed);
        return;
    }

    set_top_block({ std::move(top_hash), top_height });

    LOG_INFO(LOG_NODE, "Node start height is (", top_height, ").");

    subscribe_blockchain(
        std::bind(&full_node::handle_reorganized, this, _1, _2, _3, _4));

    // This is invoked on a new thread.
    // This is the end of the derived run startup sequence.
    p2p::run(handler);
}

void full_node::handle_running_chain(code const& ec, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        LOG_ERROR(LOG_NODE, "Failure synchronizing blocks: ", ec.message());
        handler(ec);
        return;
    }

    size_t top_height;
    hash_digest top_hash;

    if ( ! chain_.get_last_height(top_height) ||
         ! chain_.get_block_hash(top_hash, top_height)) {
             LOG_ERROR(LOG_NODE, "The blockchain is corrupt.");
        handler(error::operation_failed);
        return;
    }

    set_top_block({ std::move(top_hash), top_height });

    LOG_INFO(LOG_NODE, "Node start height is (", top_height, ").");

    subscribe_blockchain(
        std::bind(&full_node::handle_reorganized, this, _1, _2, _3, _4));

    // This is invoked on a new thread.
    // This is the end of the derived run startup sequence.
    handler(error::success);
}

// A typical reorganization consists of one incoming and zero outgoing blocks.
bool full_node::handle_reorganized(code ec, size_t fork_height, block_const_ptr_list_const_ptr incoming, block_const_ptr_list_const_ptr outgoing) {
    if (stopped() || ec == error::service_stopped) {
        return false;
    }

    if (ec) {
        LOG_ERROR(LOG_NODE, "Failure handling reorganization: ", ec.message());
        stop();
        return false;
    }

    // Nothing to do here.
    if ( ! incoming || incoming->empty()) {
        return true;
    }

    for (auto const block: *outgoing) {
        LOG_DEBUG(LOG_NODE
           , "Reorganization moved block to orphan pool ["
           , encode_hash(block->header().hash()), "]");
    }

    auto const height = *safe_add(fork_height, incoming->size());

    set_top_block({ incoming->back()->hash(), height });
    return true;
}

// Specializations.
// ----------------------------------------------------------------------------
// Create derived sessions and override these to inject from derived node.

#if ! defined(__EMSCRIPTEN__)
// Must not connect until running, otherwise imports may conflict with sync.
// But we establish the session in network so caller doesn't need to run.
kth::network::session_manual::ptr full_node::attach_manual_session() {
    return attach<node::session_manual>(chain_);
}

kth::network::session_inbound::ptr full_node::attach_inbound_session() {
    return attach<node::session_inbound>(chain_);
}

kth::network::session_outbound::ptr full_node::attach_outbound_session() {
    return attach<node::session_outbound>(chain_);
}

session_header_sync::ptr full_node::attach_header_sync_session() {
    return attach<session_header_sync>(hashes_, chain_, chain_.chain_settings().checkpoints);
}

session_block_sync::ptr full_node::attach_block_sync_session() {
    return attach<session_block_sync>(hashes_, chain_, node_settings_);
}
#endif

// Shutdown
// ----------------------------------------------------------------------------

bool full_node::stop() {
    // Suspend new work last so we can use work to clear subscribers.
    auto const p2p_stop = p2p::stop();
    auto const chain_stop = chain_.stop();

    if ( ! p2p_stop) {
        LOG_ERROR(LOG_NODE, "Failed to stop network.");
    }

    if ( ! chain_stop) {
        LOG_ERROR(LOG_NODE, "Failed to stop blockchain.");
    }

    return p2p_stop && chain_stop;
}

// This must be called from the thread that constructed this class (see join).
bool full_node::close() {
    // Invoke own stop to signal work suspension.
    if ( ! full_node::stop()) {
        return false;
    }

    auto const p2p_close = p2p::close();
    auto const chain_close = chain_.close();

    if ( ! p2p_close) {
        LOG_ERROR(LOG_NODE, "Failed to close network.");
    }

    if ( ! chain_close) {
        LOG_ERROR(LOG_NODE, "Failed to close blockchain.");
    }

    return p2p_close && chain_close;
}

// Properties.
// ----------------------------------------------------------------------------

node::settings const& full_node::node_settings() const {
    return node_settings_;
}

blockchain::settings const& full_node::chain_settings() const {
    return chain_settings_;
}

safe_chain& full_node::chain() {
    return chain_;
}

//TODO: remove this function and use safe_chain in the rpc lib
block_chain& full_node::chain_kth() {
    return chain_;
}

// Subscriptions.
// ----------------------------------------------------------------------------

void full_node::subscribe_blockchain(reorganize_handler&& handler) {
    chain().subscribe_blockchain(std::move(handler));
}

void full_node::subscribe_transaction(transaction_handler&& handler) {
    chain().subscribe_transaction(std::move(handler));
}

void full_node::subscribe_ds_proof(ds_proof_handler&& handler) {
    chain().subscribe_ds_proof(std::move(handler));
}

// Init node utils.
// ------------------------------------------------------------------------

domain::chain::block full_node::get_genesis_block(domain::config::network network) {

    switch (network) {
        case domain::config::network::testnet:
            return domain::chain::block::genesis_testnet();
        case domain::config::network::regtest:
            return domain::chain::block::genesis_regtest();
#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4:
            return domain::chain::block::genesis_testnet4();
        case domain::config::network::scalenet:
            return domain::chain::block::genesis_scalenet();
        case domain::config::network::chipnet:
            return domain::chain::block::genesis_chipnet();
#endif
        default:
        case domain::config::network::mainnet:
            return domain::chain::block::genesis_mainnet();
    }
}

#if defined(KTH_STATISTICS_ENABLED)
#if defined(_WIN32)

void screen_clear() {
    COORD topLeft  = { 0, 0 };
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO screen;
    DWORD written;

    GetConsoleScreenBufferInfo(console, &screen);
    FillConsoleOutputCharacterA(
        console, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written
    );
    FillConsoleOutputAttribute(
        console, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE,
        screen.dwSize.X * screen.dwSize.Y, topLeft, &written
    );
    SetConsoleCursorPosition(console, topLeft);
}


#else

// #include <iostream>
// void screen_clear() {
//     // CSI[2J clears screen, CSI[H moves the cursor to top-left corner
//     std::cout << "\x1B[2J\x1B[H";
// }

#include <cstdio>
void screen_clear() {
    printf("\033c");
}
#endif


//TODO(fernando): could be outside the class
void full_node::print_stat_item_sum(tabulate::Table& stats, size_t from, size_t to,
                        double accum_transactions, double accum_inputs, double accum_outputs, double accum_wait_total,
                        double accum_validation_total, double accum_validation_per_input, double accum_deserialization_per_input,
                        double accum_check_per_input, double accum_population_per_input, double accum_accept_per_input,
                        double accum_connect_per_input, double accum_deposit_per_input) const {
    stats.add_row({
        fmt::format("{}-{}", from, to)
        , "Sum"
        , fmt::format("{:.0f}", accum_transactions)
        , fmt::format("{:.0f}", accum_inputs)
        , fmt::format("{:.0f}", accum_outputs)
        , fmt::format("{:.0f}", accum_wait_total)
        , fmt::format("{:.0f}", accum_validation_total)
        , fmt::format("{:.0f}", accum_validation_per_input)
        , fmt::format("{:.0f}", accum_deserialization_per_input)
        , fmt::format("{:.0f}", accum_check_per_input)
        , fmt::format("{:.0f}", accum_population_per_input)
        , fmt::format("{:.0f}", accum_accept_per_input)
        , fmt::format("{:.0f}", accum_connect_per_input)
        , fmt::format("{:.0f}", accum_deposit_per_input)});

    // last(stats)
    //     .format()
    //     .hide_border_top();
}

// +---------------+--------+----------------+----------------+--------------+--------------+--------------+--------------+--------------+--------------+------------+------------+-------------+----------------+
// |     Blocks    |        |       Txs      |       Ins      |     Outs     |   Wait(ms)   |    Val(ms)   |  Val/In(us)  | Deser/In(us) | Check/In(us) | Pop/In(us) | Acc/In(us) | Conn/In(us) |   Dep/In(us)   |
// | 250000-259999 | Sum    | 2908680.000000 | 6443324.000000 | 10000.000000 | 18894.000000 | 32935.000000 | 57795.000000 | 43758.000000 | 12590.000000 | 315.000000 | 108.000000 |  101.000000 | 1315664.000000 |
// | 250000-259999 | Mean   |     290.868000 |     644.332400 |     1.000000 |     1.889400 |     3.293500 |     5.779500 |     4.375800 |     1.259000 |   0.031500 |   0.010800 |    0.010100 |     131.566400 |
// | 250000-259999 | Median |     241.962427 |     544.131233 |     1.000000 |     1.572434 |     2.994203 |     5.000129 |     4.000001 |     0.999999 |   0.000000 |   0.000000 |    0.000000 |     103.086477 |
// | 250000-259999 | StDev  |     226.992902 |     492.214726 |     0.000000 |     1.409953 |     2.527765 |     6.666403 |     5.388514 |     1.746031 |   0.385127 |   0.148612 |    0.140712 |     440.845418 |
// +---------------+--------+----------------+----------------+--------------+--------------+--------------+--------------+--------------+--------------+------------+------------+-------------+----------------+

// Knuth node
// Version
// DB Mode
// Options

// Synchronizing chain: xxxxx of xxxxx (xx%)

// Peers

// Stats


//TODO(fernando): could be outside the class
void full_node::print_stat_item(tabulate::Table& stats, size_t from, size_t to, std::string const& cat,
                        double accum_transactions, double accum_inputs, double accum_outputs, double accum_wait_total,
                        double accum_validation_total, double accum_validation_per_input, double accum_deserialization_per_input,
                        double accum_check_per_input, double accum_population_per_input, double accum_accept_per_input,
                        double accum_connect_per_input, double accum_deposit_per_input) const {
    stats.add_row({
        fmt::format("{}-{}", from, to)
        , cat
        , fmt::format("{:.2f}", accum_transactions)
        , fmt::format("{:.2f}", accum_inputs)
        , fmt::format("{:.2f}", accum_outputs)
        , fmt::format("{:f}", accum_wait_total)
        , fmt::format("{:f}", accum_validation_total)
        , fmt::format("{:f}", accum_validation_per_input)
        , fmt::format("{:f}", accum_deserialization_per_input)
        , fmt::format("{:f}", accum_check_per_input)
        , fmt::format("{:f}", accum_population_per_input)
        , fmt::format("{:f}", accum_accept_per_input)
        , fmt::format("{:f}", accum_connect_per_input)
        , fmt::format("{:f}", accum_deposit_per_input)});

    last(stats)
        .format()
        .hide_border_top();
}

void full_node::print_statistics(size_t height) const {
    using boost::accumulators::mean;
    using boost::accumulators::median;
    using boost::accumulators::sum;

    auto from1 = (height / accum_blocks_1_) * accum_blocks_1_;
    // auto from2 = (height / accum_blocks_2_) * accum_blocks_2_;

    tabulate::Table stats;
    stats.add_row({"Blocks"
        ,""
        , "Txs", "Ins", "Outs"
        , "Wait(ms)"
        , "Val(ms)"
        , "Val/In(us)"
        , "Deser/In(us)"
        , "Check/In(us)"
        , "Pop/In(us)"
        , "Acc/In(us)"
        , "Conn/In(us)"
        , "Dep/In(us)"});


    // auto formatted = fmt::format("Sum [{}-{}] {:f} txs {:f} ins "
    //     "{:f} wms {:f} vms {:f} vus {:f} rus {:f} cus {:f} pus "
    //     "{:f} aus {:f} sus {:f} dus {:f}",
    //     from1, height,
    //     boost::accumulators::sum(stats_current1_accum_transactions_),
    //     boost::accumulators::sum(stats_current1_accum_inputs_),
    //     boost::accumulators::sum(stats_current1_accum_wait_total_ms_),
    //     boost::accumulators::sum(stats_current1_accum_validation_total_ms_),
    //     boost::accumulators::sum(stats_current1_accum_validation_per_input_us_),
    //     boost::accumulators::sum(stats_current1_accum_deserialization_per_input_us_),
    //     boost::accumulators::sum(stats_current1_accum_check_per_input_us_),
    //     boost::accumulators::sum(stats_current1_accum_population_per_input_us_),
    //     boost::accumulators::sum(stats_current1_accum_accept_per_input_us_),
    //     boost::accumulators::sum(stats_current1_accum_connect_per_input_us_),
    //     boost::accumulators::sum(stats_current1_accum_deposit_per_input_us_),
    //     boost::accumulators::sum(stats_current1_accum_cache_efficiency_));

    // LOG_INFO(LOG_BLOCKCHAIN, "************************************************************************************************************************");
    // LOG_INFO(LOG_BLOCKCHAIN, "Stats:");
    // LOG_INFO(LOG_BLOCKCHAIN, formatted);

    print_stat_item_sum(stats, from1, height,
        sum(stats_current1_accum_transactions_),
        sum(stats_current1_accum_inputs_),
        sum(stats_current1_accum_outputs_),
        sum(stats_current1_accum_wait_total_ms_),
        sum(stats_current1_accum_validation_total_ms_),
        sum(stats_current1_accum_validation_per_input_us_),
        sum(stats_current1_accum_deserialization_per_input_us_),
        sum(stats_current1_accum_check_per_input_us_),
        sum(stats_current1_accum_population_per_input_us_),
        sum(stats_current1_accum_accept_per_input_us_),
        sum(stats_current1_accum_connect_per_input_us_),
        sum(stats_current1_accum_deposit_per_input_us_));

    print_stat_item(stats, from1, height, "Mean",
        mean(stats_current1_accum_transactions_),
        mean(stats_current1_accum_inputs_),
        mean(stats_current1_accum_outputs_),
        mean(stats_current1_accum_wait_total_ms_),
        mean(stats_current1_accum_validation_total_ms_),
        mean(stats_current1_accum_validation_per_input_us_),
        mean(stats_current1_accum_deserialization_per_input_us_),
        mean(stats_current1_accum_check_per_input_us_),
        mean(stats_current1_accum_population_per_input_us_),
        mean(stats_current1_accum_accept_per_input_us_),
        mean(stats_current1_accum_connect_per_input_us_),
        mean(stats_current1_accum_deposit_per_input_us_));

    print_stat_item(stats, from1, height, "Median",
        median(stats_current1_accum_transactions_),
        median(stats_current1_accum_inputs_),
        median(stats_current1_accum_outputs_),
        median(stats_current1_accum_wait_total_ms_),
        median(stats_current1_accum_validation_total_ms_),
        median(stats_current1_accum_validation_per_input_us_),
        median(stats_current1_accum_deserialization_per_input_us_),
        median(stats_current1_accum_check_per_input_us_),
        median(stats_current1_accum_population_per_input_us_),
        median(stats_current1_accum_accept_per_input_us_),
        median(stats_current1_accum_connect_per_input_us_),
        median(stats_current1_accum_deposit_per_input_us_));

    stdev_sample stdev;
    print_stat_item(stats, from1, height, "StDev",
        stdev(stats_current1_accum_transactions_),
        stdev(stats_current1_accum_inputs_),
        stdev(stats_current1_accum_outputs_),
        stdev(stats_current1_accum_wait_total_ms_),
        stdev(stats_current1_accum_validation_total_ms_),
        stdev(stats_current1_accum_validation_per_input_us_),
        stdev(stats_current1_accum_deserialization_per_input_us_),
        stdev(stats_current1_accum_check_per_input_us_),
        stdev(stats_current1_accum_population_per_input_us_),
        stdev(stats_current1_accum_accept_per_input_us_),
        stdev(stats_current1_accum_connect_per_input_us_),
        stdev(stats_current1_accum_deposit_per_input_us_));


    for (size_t i = 2; i < 14; ++i) {
        stats.column(i).format().font_align(tabulate::FontAlign::right);
    }

    for (size_t i = 0; i < 14; ++i) {
        stats[0][i].format()
            .font_color(tabulate::Color::yellow)
            .font_align(tabulate::FontAlign::center)
            .font_style({tabulate::FontStyle::bold});
    }

    screen_clear();
    std::print("{}\n\n", stats.str());

    // LOG_INFO(LOG_BLOCKCHAIN, "************************************************************************************************************************");
}

#endif


} // namespace kth::node