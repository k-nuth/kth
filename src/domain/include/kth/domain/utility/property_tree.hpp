// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_PROPERTY_TREE_HPP
#define KTH_PROPERTY_TREE_HPP

#include <map>
#include <string>
#include <vector>

#include <boost/property_tree/ptree.hpp>

#include <kth/domain/chain/points_value.hpp>
#include <kth/domain/config/header.hpp>
#include <kth/domain/config/input.hpp>
#include <kth/domain/config/output.hpp>
#include <kth/domain/config/transaction.hpp>
#include <kth/domain/define.hpp>

namespace pt = boost::property_tree;

namespace kth::domain::config {

class wrapper;

/**
 * A tuple to represent settings and serialized values.
 */
using settings_list = std::map<std::string, std::string>;

/**
 * Generate a property list for a block header.
 * @param[in]  header  The header.
 * @return             A property list.
 */
KD_API pt::ptree property_list(config::header const& header);

/**
 * Generate a property tree for a block header.
 * @param[in]  header  The header.
 * @return             A property tree.
 */
KD_API pt::ptree property_tree(config::header const& header);

/**
 * Generate a property tree for a set of headers.
 * @param[in]  headers  The set of headers.
 * @return              A property tree.
 */
KD_API pt::ptree property_tree(std::vector<config::header> const& headers,
                               bool json);

/**
 * Generate a property list for a transaction input.
 * @param[in]  tx_input  The input.
 * @return               A property list.
 */
KD_API pt::ptree property_list(chain::input const& tx_input);

/**
 * Generate a property tree for a transaction input.
 * @param[in]  tx_input  The input.
 * @return               A property tree.
 */
KD_API pt::ptree property_tree(chain::input const& tx_input);

/**
 * Generate a property tree for a set of transaction inputs.
 * @param[in]  tx_inputs  The set of transaction inputs.
 * @param[in]  json       Use json array formatting.
 * @return                A property tree.
 */
KD_API pt::ptree property_tree(const chain::input::list& tx_inputs, bool json);

/**
 * Generate a property list for an input.
 * @param[in]  input  The input.
 * @return            A property list.
 */
KD_API pt::ptree property_list(const config::input& input);

/**
 * Generate a property tree for an input.
 * @param[in]  input  The input.
 * @return            A property tree.
 */
KD_API pt::ptree property_tree(const config::input& input);

/**
 * Generate a property tree for a set of inputs.
 * @param[in]  inputs  The set of inputs.
 * @param[in]  json    Use json array formatting.
 * @return             A property tree.
 */
KD_API pt::ptree property_tree(std::vector<config::input> const& inputs,
                               bool json);

/**
 * Generate a property list for a transaction output.
 * @param[in]  tx_output  The transaction output.
 * @return                A property list.
 */
KD_API pt::ptree property_list(const chain::output& tx_output);

/**
 * Generate a property tree for a transaction output.
 * @param[in]  tx_output  The transaction output.
 * @return                A property tree.
 */
KD_API pt::ptree property_tree(const chain::output& tx_output);

/**
 * Generate a property tree for a set of transaction outputs.
 * @param[in]  tx_outputs  The set of transaction outputs.
 * @param[in]  json        Use json array formatting.
 * @return                 A property tree.
 */
KD_API pt::ptree property_tree(const chain::output::list& tx_outputs,
                               bool json);

/**
 * Generate a property list for a point value.
 * @param[in]  point  The point value.
 * @return            A property list.
 */
KD_API pt::ptree property_list(const chain::point_value& point);

/**
 * Generate a property tree for points value.
 * @param[in]  info  The points value.
 * @param[in]  json  Use json array formatting.
 * @return           A property tree.
 */
KD_API pt::ptree property_tree(chain::points_value const& values, bool json);

/**
 * Generate a property list for a transaction.
 * @param[in]  transaction  The transaction.
 * @param[in]  json         Use json array formatting.
 * @return                  A property list.
 */
KD_API pt::ptree property_list(config::transaction const& transaction, bool json);

/**
 * Generate a property tree for a transaction.
 * @param[in]  transaction  The transaction.
 * @param[in]  json         Use json array formatting.
 * @return                  A property tree.
 */
KD_API pt::ptree property_tree(config::transaction const& transaction, bool json);

/**
 * Generate a property tree for a set of transactions.
 * @param[in]  transactions  The set of transactions.
 * @param[in]  json          Use json array formatting.
 * @return                   A property tree.
 */
KD_API pt::ptree property_tree(std::vector<config::transaction> const& transactions,
                               bool json);

/**
 * Generate a property list for a wrapper.
 * @param[in]  wrapper  The wrapper instance.
 * @return              A property list.
 */
KD_API pt::ptree property_list(const wallet::wrapped_data& wrapper);

/**
 * Generate a property tree for a wrapper.
 * @param[in]  wrapper  The wrapper instance.
 * @return              A property tree.
 */
KD_API pt::ptree property_tree(const wallet::wrapped_data& wrapper);

/**
 * Create a property list for the fetch-tx-index command.
 * @param[in]  hash    The block hash.
 * @param[in]  height  The block height.
 * @param[in]  index   The tx index.
 * @returns            A new property list containing the list.
 */
KD_API pt::ptree property_list(hash_digest const& hash, size_t height, size_t index);

/**
 * Create a property tree for the fetch-tx-index command.
 * @param[in]  hash    The block hash.
 * @param[in]  height  The block height.
 * @param[in]  index   The tx index.
 * @returns            A new property tree containing the list.
 */
KD_API pt::ptree property_tree(hash_digest const& hash, size_t height, size_t index);

/**
 * Create a property tree for the settings command.
 * @param[in]  settings   The list of settings.
 * @returns               A new property tree containing the settings.
 */
KD_API pt::ptree property_tree(settings_list const& settings);

} // namespace kth::domain

#include <kth/domain/impl/utility/property_tree.ipp>

#endif
