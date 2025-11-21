// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_FULL_NODE_HPP
#define KTH_NODE_FULL_NODE_HPP

#include <cstdint>
#include <memory>

#if defined(KTH_STATISTICS_ENABLED)
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
// #include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/mean.hpp>
// #include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/sum.hpp>
#include <boost/accumulators/statistics/variance.hpp>

// #include <tabulate/table.hpp>

//TODO(fernando): remove this workaround when Tabulate people fix their library.
namespace tabulate {
class Table;
}

#endif

#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/node/sessions/session_block_sync.hpp>
#include <kth/node/sessions/session_header_sync.hpp>
#endif

#include <kth/node/utility/check_list.hpp>

namespace kth::node {

enum class start_modules {
    all,
    just_chain,
    just_p2p
};

#if defined(KTH_STATISTICS_ENABLED)

//TODO(fernando): move to another place
struct stdev_sample {
    template <typename T>
    auto operator()(T const& x) {
        auto n = boost::accumulators::count(x);
        return std::sqrt(boost::accumulators::variance(x) * (n / (n - 1.0)));
    }
};

//TODO(fernando): move to another place
template <typename T1, typename T2>
struct statistics_entry {
    T1 transactions;
    T1 inputs;
    T1 outputs;
    T1 wait_total_ms;
    T1 validation_total_ms;
    T1 validation_per_input_us;
    T1 deserialization_per_input_us;
    T1 check_per_input_us;
    T1 population_per_input_us;
    T1 accept_per_input_us;
    T1 connect_per_input_us;
    T1 deposit_per_input_us;
    T2 cache_efficiency;                        // (hits/queries)
};

using accum_t = boost::accumulators::accumulator_set<double,
    boost::accumulators::stats<
        boost::accumulators::tag::sum,
        boost::accumulators::tag::count,
        boost::accumulators::tag::mean,
        boost::accumulators::tag::median,
        boost::accumulators::tag::variance>>;

#endif

struct multi_crypto_setter {
    multi_crypto_setter() {
        set_cashaddr_prefix("bitcoincash");
    }

#if ! defined(__EMSCRIPTEN__)
    multi_crypto_setter(network::settings const& net_settings) {
#if defined(KTH_CURRENCY_BCH)
        switch (net_settings.identifier) {
            case netmagic::bch_mainnet:
                set_cashaddr_prefix("bitcoincash");
                break;
            case netmagic::bch_testnet:
            case netmagic::bch_testnet4:
            case netmagic::bch_scalenet:
            // case netmagic::bch_chipnet:  // same net magic as bch_testnet4
                set_cashaddr_prefix("bchtest");
                break;
            case netmagic::bch_regtest:
                set_cashaddr_prefix("bchreg");
                break;
            default:
                set_cashaddr_prefix("");
        }
#endif
    }
#endif
};

#if ! defined(__EMSCRIPTEN__)
#define OVERRIDE_COND override
#else
#define OVERRIDE_COND
#endif


/// A full node on the Bitcoin P2P network.
class KND_API full_node
    : public multi_crypto_setter
#if ! defined(__EMSCRIPTEN__)
    , public network::p2p
#endif
{
public:
#if defined(__EMSCRIPTEN__)
    using result_handler = std::function<void(code const&)>;
#endif

    using ptr = std::shared_ptr<full_node>;
    using reorganize_handler = blockchain::block_chain::reorganize_handler;
    using transaction_handler = blockchain::block_chain::transaction_handler;
    using ds_proof_handler = blockchain::block_chain::ds_proof_handler;

    /// Construct the full node.
    full_node(configuration const& configuration);

    /// Ensure all threads are coalesced.
    virtual ~full_node();

    // Start/Run sequences.
    // ------------------------------------------------------------------------

    /// Invoke startup and seeding sequence, call from constructing thread.
    void start(result_handler handler) OVERRIDE_COND;

    /// Invoke just chain startup, call from constructing thread.
    void start_chain(result_handler handler);

    /// Synchronize the blockchain and then begin long running sessions,
    /// call from start result handler. Call base method to skip sync.
    void run(result_handler handler) OVERRIDE_COND;
    void run_chain(result_handler handler) OVERRIDE_COND;

    // Shutdown.
    // ------------------------------------------------------------------------

    /// Idempotent call to signal work stop, start may be reinvoked after.
    /// Returns the result of file save operation.
    bool stop() OVERRIDE_COND;

    /// Blocking call to coalesce all work and then terminate all threads.
    /// Call from thread that constructed this class, or don't call at all.
    /// This calls stop, and start may be reinvoked after calling this.
    bool close() OVERRIDE_COND;

    // Properties.
    // ------------------------------------------------------------------------

    /// Node configuration settings.
    virtual
    const node::settings& node_settings() const;

    /// Node configuration settings.
    virtual
    blockchain::settings const& chain_settings() const;

    /// Blockchain query interface.
    virtual
    blockchain::safe_chain& chain();

    /// Blockchain.
    //TODO: remove this function and use safe_chain in the rpc lib
    virtual
    blockchain::block_chain& chain_kth();

    // Subscriptions.
    // ------------------------------------------------------------------------

    /// Subscribe to blockchain reorganization and stop events.
    virtual
    void subscribe_blockchain(reorganize_handler&& handler);

    /// Subscribe to transaction pool acceptance and stop events.
    virtual
    void subscribe_transaction(transaction_handler&& handler);

    /// Subscribe to DSProof pool acceptance and stop events.
    virtual
    void subscribe_ds_proof(ds_proof_handler&& handler);

    // Init node utils.
    // ------------------------------------------------------------------------
    static
    domain::chain::block get_genesis_block(domain::config::network network);



#if defined(KTH_STATISTICS_ENABLED)
    void reset_current1_stats() {
        stats_current1_accum_transactions_ = {};
        stats_current1_accum_inputs_ = {};
        stats_current1_accum_outputs_ = {};
        stats_current1_accum_wait_total_ms_ = {};
        stats_current1_accum_validation_total_ms_ = {};
        stats_current1_accum_validation_per_input_us_ = {};
        stats_current1_accum_deserialization_per_input_us_ = {};
        stats_current1_accum_check_per_input_us_ = {};
        stats_current1_accum_population_per_input_us_ = {};
        stats_current1_accum_accept_per_input_us_ = {};
        stats_current1_accum_connect_per_input_us_ = {};
        stats_current1_accum_deposit_per_input_us_ = {};
        stats_current1_accum_cache_efficiency_ = {};
    }

    void reset_current2_stats() {
        stats_current2_accum_transactions_ = {};
        stats_current2_accum_inputs_ = {};
        stats_current2_accum_outputs_ = {};
        stats_current2_accum_wait_total_ms_ = {};
        stats_current2_accum_validation_total_ms_ = {};
        stats_current2_accum_validation_per_input_us_ = {};
        stats_current2_accum_deserialization_per_input_us_ = {};
        stats_current2_accum_check_per_input_us_ = {};
        stats_current2_accum_population_per_input_us_ = {};
        stats_current2_accum_accept_per_input_us_ = {};
        stats_current2_accum_connect_per_input_us_ = {};
        stats_current2_accum_deposit_per_input_us_ = {};
        stats_current2_accum_cache_efficiency_ = {};
    }

    void update_accum(size_t height, statistics_entry<size_t, float> const& entry) {
        if (height % accum_blocks_1_ == 0) {
            reset_current1_stats();
        }
        if (height % accum_blocks_2_ == 0) {
            reset_current2_stats();
        }

        stats_current1_accum_transactions_(entry.transactions);
        stats_current1_accum_inputs_(entry.inputs);
        stats_current1_accum_outputs_(entry.outputs);
        stats_current1_accum_wait_total_ms_(entry.wait_total_ms);
        stats_current1_accum_validation_total_ms_(entry.validation_total_ms);
        stats_current1_accum_validation_per_input_us_(entry.validation_per_input_us);
        stats_current1_accum_deserialization_per_input_us_(entry.deserialization_per_input_us);
        stats_current1_accum_check_per_input_us_(entry.check_per_input_us);
        stats_current1_accum_population_per_input_us_(entry.population_per_input_us);
        stats_current1_accum_accept_per_input_us_(entry.accept_per_input_us);
        stats_current1_accum_connect_per_input_us_(entry.connect_per_input_us);
        stats_current1_accum_deposit_per_input_us_(entry.deposit_per_input_us);
        stats_current1_accum_cache_efficiency_(entry.cache_efficiency);

        stats_current2_accum_transactions_(entry.transactions);
        stats_current2_accum_inputs_(entry.inputs);
        stats_current2_accum_outputs_(entry.outputs);
        stats_current2_accum_wait_total_ms_(entry.wait_total_ms);
        stats_current2_accum_validation_total_ms_(entry.validation_total_ms);
        stats_current2_accum_validation_per_input_us_(entry.validation_per_input_us);
        stats_current2_accum_deserialization_per_input_us_(entry.deserialization_per_input_us);
        stats_current2_accum_check_per_input_us_(entry.check_per_input_us);
        stats_current2_accum_population_per_input_us_(entry.population_per_input_us);
        stats_current2_accum_accept_per_input_us_(entry.accept_per_input_us);
        stats_current2_accum_connect_per_input_us_(entry.connect_per_input_us);
        stats_current2_accum_deposit_per_input_us_(entry.deposit_per_input_us);
        stats_current2_accum_cache_efficiency_(entry.cache_efficiency);

        stats_total_accum_transactions_(entry.transactions);
        stats_total_accum_inputs_(entry.inputs);
        stats_total_accum_outputs_(entry.outputs);
        stats_total_accum_wait_total_ms_(entry.wait_total_ms);
        stats_total_accum_validation_total_ms_(entry.validation_total_ms);
        stats_total_accum_validation_per_input_us_(entry.validation_per_input_us);
        stats_total_accum_deserialization_per_input_us_(entry.deserialization_per_input_us);
        stats_total_accum_check_per_input_us_(entry.check_per_input_us);
        stats_total_accum_population_per_input_us_(entry.population_per_input_us);
        stats_total_accum_accept_per_input_us_(entry.accept_per_input_us);
        stats_total_accum_connect_per_input_us_(entry.connect_per_input_us);
        stats_total_accum_deposit_per_input_us_(entry.deposit_per_input_us);
        stats_total_accum_cache_efficiency_(entry.cache_efficiency);
    }

    template <typename... Args>
    void collect_statistics(size_t height, Args&&... args) {

    // std::chrono::time_point<std::chrono::high_resolution_clock> time_from_block_0_;
    // std::chrono::time_point<std::chrono::high_resolution_clock> time_from_last_10000_block_;
    // std::chrono::time_point<std::chrono::high_resolution_clock> time_from_last_100000_block_;

        if (height == 1) {
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            printf("**********************************************************************\n");
            time_from_block_0_ = std::chrono::high_resolution_clock::now();
            time_from_last_10000_block_ = time_from_block_0_;
            time_from_last_100000_block_ = time_from_block_0_;
        } else {
            if (height % accum_blocks_1_ == 0) {
                print_statistics(height - 1);
            }
        }

        // auto r = statistics_detail_.try_emplace(height, statistics_entry<size_t, float>{std::forward<Args>(args)...});
        // if (std::get<1>(r)) {
        //     update_accum(height, std::get<0>(r)->second);
        // }

        statistics_entry<size_t, float> entry{std::forward<Args>(args)...};
        update_accum(height, entry);
    }

    //TODO(fernando): could be outside the class
    void print_stat_item_sum(tabulate::Table& stats, size_t from, size_t to,
                         double accum_transactions, double accum_inputs, double accum_outputs, double accum_wait_total,
                         double accum_validation_total, double accum_validation_per_input, double accum_deserialization_per_input,
                         double accum_check_per_input, double accum_population_per_input, double accum_accept_per_input,
                         double accum_connect_per_input, double accum_deposit_per_input) const;

    //TODO(fernando): could be outside the class
    void print_stat_item(tabulate::Table& stats, size_t from, size_t to, std::string const& cat,
                         double accum_transactions, double accum_inputs, double accum_outputs, double accum_wait_total,
                         double accum_validation_total, double accum_validation_per_input, double accum_deserialization_per_input,
                         double accum_check_per_input, double accum_population_per_input, double accum_accept_per_input,
                         double accum_connect_per_input, double accum_deposit_per_input) const;

    // //TODO(fernando): could be outside the class
    // template <typename F>
    // void print_stat_item(tabulate::Table& stats, size_t from, size_t to, std::string const& cat, F f,
    //                      accum_t accum_transactions, accum_t accum_inputs, accum_t accum_outputs, accum_t accum_wait_total,
    //                      accum_t accum_validation_total, accum_t accum_validation_per_input, accum_t accum_deserialization_per_input,
    //                      accum_t accum_check_per_input, accum_t accum_population_per_input, accum_t accum_accept_per_input,
    //                      accum_t accum_connect_per_input, accum_t accum_deposit_per_input) const {
    //     stats.add_row({
    //         fmt::format("{}-{}", from, to)
    //         , cat
    //         , fmt::format("{:f}", f(accum_transactions))
    //         , fmt::format("{:f}", f(accum_inputs))
    //         , fmt::format("{:f}", f(accum_outputs))
    //         , fmt::format("{:f}", f(accum_wait_total))
    //         , fmt::format("{:f}", f(accum_validation_total))
    //         , fmt::format("{:f}", f(accum_validation_per_input))
    //         , fmt::format("{:f}", f(accum_deserialization_per_input))
    //         , fmt::format("{:f}", f(accum_check_per_input))
    //         , fmt::format("{:f}", f(accum_population_per_input))
    //         , fmt::format("{:f}", f(accum_accept_per_input))
    //         , fmt::format("{:f}", f(accum_connect_per_input))
    //         , fmt::format("{:f}", f(accum_deposit_per_input))});

    //     last(stats)
    //         .format()
    //         .hide_border_top();


    //     // auto formatted = fmt::format("{} [{}-{}] {:f} txs {:f} ins {:f} outs "
    //     //     "{:f} wms {:f} vms {:f} vus {:f} rus {:f} cus {:f} pus "
    //     //     "{:f} aus {:f} sus {:f} dus {:f}",
    //     //     cat, from, too,
    //     //     f(stats_current1_accum_transactions_),
    //     //     f(stats_current1_accum_inputs_),
    //     //     f(stats_current1_accum_outputs_),
    //     //     f(stats_current1_accum_wait_total_ms_),
    //     //     f(stats_current1_accum_validation_total_ms_),
    //     //     f(stats_current1_accum_validation_per_input_us_),
    //     //     f(stats_current1_accum_deserialization_per_input_us_),
    //     //     f(stats_current1_accum_check_per_input_us_),
    //     //     f(stats_current1_accum_population_per_input_us_),
    //     //     f(stats_current1_accum_accept_per_input_us_),
    //     //     f(stats_current1_accum_connect_per_input_us_),
    //     //     f(stats_current1_accum_deposit_per_input_us_),
    //     //     f(stats_current1_accum_cache_efficiency_));

    //     // spdlog::info("[blockchain] {}", formatted);
    // }

    void print_statistics(size_t height) const;


#endif


protected:
    /// Attach a node::session to the network, caller must start the session.
    template <typename Session, typename... Args>
    typename Session::ptr attach(Args&&... args) {
        return std::make_shared<Session>(*this, std::forward<Args>(args)...);
    }

#if ! defined(__EMSCRIPTEN__)
    /// Override to attach specialized p2p sessions.
    ////network::session_seed::ptr attach_seed_session() OVERRIDE_COND;
    network::session_manual::ptr attach_manual_session() OVERRIDE_COND;
    network::session_inbound::ptr attach_inbound_session() OVERRIDE_COND;
    network::session_outbound::ptr attach_outbound_session() OVERRIDE_COND;

    /// Override to attach specialized node sessions.
    virtual
    session_header_sync::ptr attach_header_sync_session();

    virtual
    session_block_sync::ptr attach_block_sync_session();
#endif

    ///For mining
    blockchain::block_chain chain_;

private:
    using block_ptr_list = domain::message::block::ptr_list;

#if defined(KTH_STATISTICS_ENABLED)
    static constexpr size_t screen_refresh = 100;
    static constexpr size_t accum_blocks_1_ = 10'000;
    static constexpr size_t accum_blocks_2_ = 100'000;

    // using statistics_detail_t = std::unordered_map<size_t, statistics_entry<size_t, float>>;
    // using statistics_accum_t = std::vector<statistics_entry>;
#endif


    bool handle_reorganized(code ec, size_t fork_height, block_const_ptr_list_const_ptr incoming, block_const_ptr_list_const_ptr outgoing);
    void handle_headers_synchronized(code const& ec, result_handler handler);
    void handle_network_stopped(code const& ec, result_handler handler);

    void handle_started(code const& ec, result_handler handler);
    void handle_running(code const& ec, result_handler handler);
    void handle_running_chain(code const& ec, result_handler handler);


#if defined(KTH_STATISTICS_ENABLED)
    // statistics_detail_t statistics_detail_;
    // statistics_accum_t statistics_accum_;

    std::chrono::time_point<std::chrono::high_resolution_clock> time_from_block_0_;
    std::chrono::time_point<std::chrono::high_resolution_clock> time_from_last_10000_block_;
    std::chrono::time_point<std::chrono::high_resolution_clock> time_from_last_100000_block_;

    accum_t stats_current1_accum_transactions_;
    accum_t stats_current1_accum_inputs_;
    accum_t stats_current1_accum_outputs_;
    accum_t stats_current1_accum_wait_total_ms_;
    accum_t stats_current1_accum_validation_total_ms_;
    accum_t stats_current1_accum_validation_per_input_us_;
    accum_t stats_current1_accum_deserialization_per_input_us_;
    accum_t stats_current1_accum_check_per_input_us_;
    accum_t stats_current1_accum_population_per_input_us_;
    accum_t stats_current1_accum_accept_per_input_us_;
    accum_t stats_current1_accum_connect_per_input_us_;
    accum_t stats_current1_accum_deposit_per_input_us_;
    accum_t stats_current1_accum_cache_efficiency_;

    accum_t stats_current2_accum_transactions_;
    accum_t stats_current2_accum_inputs_;
    accum_t stats_current2_accum_outputs_;
    accum_t stats_current2_accum_wait_total_ms_;
    accum_t stats_current2_accum_validation_total_ms_;
    accum_t stats_current2_accum_validation_per_input_us_;
    accum_t stats_current2_accum_deserialization_per_input_us_;
    accum_t stats_current2_accum_check_per_input_us_;
    accum_t stats_current2_accum_population_per_input_us_;
    accum_t stats_current2_accum_accept_per_input_us_;
    accum_t stats_current2_accum_connect_per_input_us_;
    accum_t stats_current2_accum_deposit_per_input_us_;
    accum_t stats_current2_accum_cache_efficiency_;

    accum_t stats_total_accum_transactions_;
    accum_t stats_total_accum_inputs_;
    accum_t stats_total_accum_outputs_;
    accum_t stats_total_accum_wait_total_ms_;
    accum_t stats_total_accum_validation_total_ms_;
    accum_t stats_total_accum_validation_per_input_us_;
    accum_t stats_total_accum_deserialization_per_input_us_;
    accum_t stats_total_accum_check_per_input_us_;
    accum_t stats_total_accum_population_per_input_us_;
    accum_t stats_total_accum_accept_per_input_us_;
    accum_t stats_total_accum_connect_per_input_us_;
    accum_t stats_total_accum_deposit_per_input_us_;
    accum_t stats_total_accum_cache_efficiency_;
#endif

    // These are thread safe.
    check_list hashes_;
    //blockchain::block_chain chain_;

#if ! defined(__EMSCRIPTEN__)
    const uint32_t protocol_maximum_;
#endif

    const node::settings& node_settings_;
    blockchain::settings const& chain_settings_;

#if defined(__EMSCRIPTEN__)
    threadpool threadpool_;

    threadpool& thread_pool() {
        return threadpool_;
    }
#endif
};

} // namespace kth::node

#endif
