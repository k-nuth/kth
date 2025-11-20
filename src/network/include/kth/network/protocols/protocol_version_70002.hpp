// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_VERSION_70002_HPP
#define KTH_NETWORK_PROTOCOL_VERSION_70002_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol_version_31402.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

class KN_API protocol_version_70002
    : public protocol_version_31402, track<protocol_version_70002>
{
public:
    using ptr = std::shared_ptr<protocol_version_70002>;

    /**
     * Construct a version protocol instance using configured minimums.
     * @param[in]  network   The network interface.
     * @param[in]  channel   The channel on which to start the protocol.
     */
    protocol_version_70002(p2p& network, channel::ptr channel);

    /**
     * Construct a version protocol instance.
     * @param[in]  network           The network interface.
     * @param[in]  channel           The channel for the protocol.
     * @param[in]  own_version       This node's maximum version.
     * @param[in]  own_services      This node's advertised services.
     * @param[in]  invalid_services  The disallowed peers services.
     * @param[in]  minimum_version   This required minimum version.
     * @param[in]  minimum_services  This required minimum services.
     * @param[in]  relay             The peer should relay transactions.
     */
    protocol_version_70002(p2p& network, channel::ptr channel,
        uint32_t own_version, uint64_t own_services, uint64_t invalid_services,
        uint32_t minimum_version, uint64_t minimum_services, bool relay);

    /**
     * Start the protocol.
     * @param[in]  handler  Invoked upon stop or receipt of version and verack.
     */
    void start(event_handler handler) override;

protected:
    domain::message::version version_factory() const override;
    bool sufficient_peer(version_const_ptr message) override;

    virtual bool handle_receive_reject(code const& ec, reject_const_ptr reject);

    bool const relay_;
};

} // namespace kth::network

#endif
