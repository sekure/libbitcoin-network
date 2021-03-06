/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * libbitcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/network/protocols/protocol_version.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/network/channel.hpp>
#include <bitcoin/network/p2p.hpp>
#include <bitcoin/network/protocols/protocol_timer.hpp>
#include <bitcoin/network/settings.hpp>

namespace libbitcoin {
namespace network {

#define NAME "version"
#define CLASS protocol_version
#define RELAY_TRUE true
#define GENESIS_HEIGHT 0
#define UNSPECIFIED_NONCE 0

using namespace bc::message;
using namespace std::placeholders;

const version protocol_version::template_
{
    bc::protocol_version,
    services::node_network,
    no_timestamp,
    bc::unspecified_network_address,
    bc::unspecified_network_address,
    UNSPECIFIED_NONCE,
    BC_USER_AGENT,
    GENESIS_HEIGHT,
    RELAY_TRUE
};

// TODO: move to libbitcoin utlity with similar blockchain function.
static uint64_t time_stamp()
{
    // Use system clock because we require accurate time of day.
    typedef std::chrono::system_clock wall_clock;
    const auto now = wall_clock::now();
    return wall_clock::to_time_t(now);
}

version protocol_version::template_factory(const config::authority& authority,
    const settings& settings, uint64_t nonce, size_t height)
{
    auto version = protocol_version::template_;

    // Give peer our time as a hint for its own clock validation.
    version.timestamp = time_stamp();

    // The timestamp should not used here and there's no need to set services.
    version.address_recevier = authority.to_network_address();

    // The timestamp should not used here and services should be set in config.
    version.address_sender = settings.self.to_network_address();

    // It is expected that the version is constructed shortly before use.
    BITCOIN_ASSERT_MSG(height < max_uint32, "Time to upgrade the protocol.");
    version.start_height = static_cast<uint32_t>(height);

    // Set required transaction relay policy for the connection.
    version.relay = settings.relay_transactions;

    // A non-zero nonce is used to detect connections to self.
    version.nonce = nonce;
    return version;
}

protocol_version::protocol_version(p2p& network, channel::ptr channel)
  : protocol_timer(network, channel, false, NAME),
    network_(network),
    CONSTRUCT_TRACK(protocol_version)
{
}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_version::start(event_handler handler)
{
    const auto height = network_.height();
    const auto& settings = network_.network_settings();

    // The handler is invoked in the context of the last message receipt.
    protocol_timer::start(settings.channel_handshake(),
        synchronize(handler, 2, NAME, false));

    const auto self = template_factory(authority(), settings, nonce(), height);
    SUBSCRIBE2(version, handle_receive_version, _1, _2);
    SUBSCRIBE2(verack, handle_receive_verack, _1, _2);
    SEND1(self, handle_version_sent, _1);
}

// Protocol.
// ----------------------------------------------------------------------------

bool protocol_version::handle_receive_version(const code& ec,
    version::ptr message)
{
    if (stopped())
        return false;

    if (ec)
    {
        log::debug(LOG_NETWORK)
            << "Failure receiving version from [" << authority() << "] "
            << ec.message();
        set_event(ec);
        return false;
    }

    log::debug(LOG_NETWORK)
        << "Peer [" << authority() << "] version (" << message->value
        << ") services (" << message->services_sender << ") time ("
        << message->timestamp << ") " << message->user_agent;

    set_peer_version(*message);
    SEND1(verack(), handle_verack_sent, _1);

    // 1 of 2
    set_event(error::success);
    return false;
}

bool protocol_version::handle_receive_verack(const code& ec,
    message::verack::ptr)
{
    if (stopped())
        return false;

    if (ec)
    {
        log::debug(LOG_NETWORK)
            << "Failure receiving verack from [" << authority() << "] "
            << ec.message();
        set_event(ec);
        return false;
    }

    // 2 of 2
    set_event(error::success);
    return false;
}

void protocol_version::handle_version_sent(const code& ec)
{
    if (stopped())
        return;

    if (ec)
    {
        log::debug(LOG_NETWORK)
            << "Failure sending version to [" << authority() << "] "
            << ec.message();
        set_event(ec);
        return;
    }
}

void protocol_version::handle_verack_sent(const code& ec)
{
    if (stopped())
        return;

    if (ec)
    {
        log::debug(LOG_NETWORK)
            << "Failure sending verack to [" << authority() << "] "
            << ec.message();
        set_event(ec);
        return;
    }
}

} // namespace network
} // namespace libbitcoin
