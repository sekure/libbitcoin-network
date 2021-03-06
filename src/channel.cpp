/**
 * Copyright (c) 2011-2016 libbitcoin developers (see AUTHORS)
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
#include <bitcoin/network/channel.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/network/proxy.hpp>
#include <bitcoin/network/settings.hpp>
#include <bitcoin/network/socket.hpp>

namespace libbitcoin {
namespace network {

using namespace std::placeholders;

// Factory for deadline timer pointer construction.
static deadline::ptr alarm(threadpool& pool, const asio::duration& duration)
{
    return std::make_shared<deadline>(pool, pseudo_randomize(duration));
}

channel::channel(threadpool& pool, socket::ptr socket,
    const settings& settings)
  : proxy(pool, socket, settings.identifier),
    notify_(false),
    nonce_(0),
    version_({ 0 }),
    own_threshold_(null_hash),
    peer_threshold_(null_hash),
    located_start_(null_hash),
    located_stop_(null_hash),
    expiration_(alarm(pool, settings.channel_expiration())),
    inactivity_(alarm(pool, settings.channel_inactivity())),
    CONSTRUCT_TRACK(channel)
{
}

// Talk sequence.
// ----------------------------------------------------------------------------

// public:
void channel::start(result_handler handler)
{
    proxy::start(
        std::bind(&channel::do_start,
            shared_from_base<channel>(), _1, handler));
}

// Don't start the timers until the socket is enabled.
void channel::do_start(const code& ec, result_handler handler)
{
    start_expiration();
    start_inactivity();
    handler(error::success);
}

// Properties (version write is thread unsafe, isolate from read).
// ----------------------------------------------------------------------------

bool channel::notify() const
{
    return notify_;
}

void channel::set_notify(bool value)
{
    notify_ = value;
}

uint64_t channel::nonce() const
{
    return nonce_;
}

void channel::set_nonce(uint64_t value)
{
    nonce_ = value;
}

const message::version& channel::version() const
{
    return version_;
}

void channel::set_version(const message::version& value)
{
    version_ = value;
}

hash_digest channel::own_threshold()
{
    return own_threshold_.load();
}

void channel::set_own_threshold(const hash_digest& threshold)
{
    own_threshold_.store(threshold);
}

hash_digest channel::peer_threshold()
{
    return peer_threshold_.load();
}

void channel::set_peer_threshold(const hash_digest& threshold)
{
    peer_threshold_.store(threshold);
}

// Proxy pure virtual protected and ordered handlers.
// ----------------------------------------------------------------------------

// It is possible that this may be called multipled times.
void channel::handle_stopping()
{
    expiration_->stop();
    inactivity_->stop();
}

void channel::handle_activity()
{
    start_inactivity();
}

// Timers (these are inherent races, requiring stranding by stop only).
// ----------------------------------------------------------------------------

void channel::start_expiration()
{
    if (stopped())
        return;

    expiration_->start(
        std::bind(&channel::handle_expiration,
            shared_from_base<channel>(), _1));
}

void channel::handle_expiration(const code& ec)
{
    if (stopped())
        return;

    log::debug(LOG_NETWORK)
        << "Channel lifetime expired [" << authority() << "]";

    stop(error::channel_timeout);
}

void channel::start_inactivity()
{
    if (stopped())
        return;

    inactivity_->start(
        std::bind(&channel::handle_inactivity,
            shared_from_base<channel>(), _1));
}

void channel::handle_inactivity(const code& ec)
{
    if (stopped())
        return;

    log::debug(LOG_NETWORK)
        << "Channel inactivity timeout [" << authority() << "]";

    stop(error::channel_timeout);
}

////// Location tracking (thread unsafe, deprecated).
////// ----------------------------------------------------------------------------
////
////bool channel::located(const hash_digest& start, const hash_digest& stop) const
////{
////    return located_start_ == start && located_stop_ == stop;
////}
////
////void channel::set_located(const hash_digest& start, const hash_digest& stop)
////{
////    located_start_ = start;
////    located_stop_ = stop;
////}

} // namespace network
} // namespace libbitcoin
