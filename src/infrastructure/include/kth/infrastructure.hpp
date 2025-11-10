// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_INFRASTRUCTURE_HPP_
#define KTH_INFRASTRUCTURE_INFRASTRUCTURE_HPP_

/**
 * API Users: Include only this header. Direct use of other headers is fragile
 * and unsupported as header organization is subject to change.
 *
 * Maintainers: Do not include this header internal to this library.
 */

#include <kth/infrastructure/compat.h>
#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/handlers.hpp>
#include <kth/infrastructure/path.hpp>
#include <kth/infrastructure/version.hpp>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/config/base16.hpp>
#include <kth/infrastructure/config/base2.hpp>
#include <kth/infrastructure/config/base58.hpp>
#include <kth/infrastructure/config/base64.hpp>
#include <kth/infrastructure/config/checkpoint.hpp>
#include <kth/infrastructure/config/directory.hpp>
#include <kth/infrastructure/config/endpoint.hpp>
#include <kth/infrastructure/config/hash160.hpp>
#include <kth/infrastructure/config/hash256.hpp>

// #include <kth/infrastructure/config/parser.hpp>

#include <kth/infrastructure/config/sodium.hpp>

#include <kth/infrastructure/formats/base_10.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/formats/base_85.hpp>

#include <kth/infrastructure/log/sink.hpp>

#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/machine/script_pattern.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/machine/sighash_algorithm.hpp>

#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/crypto.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/math/uint256.hpp>

#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/message/messages.hpp>
#include <kth/infrastructure/message/network_address.hpp>

#include <kth/infrastructure/unicode/console_streambuf.hpp>
#include <kth/infrastructure/unicode/file_lock.hpp>
#include <kth/infrastructure/unicode/ifstream.hpp>
#include <kth/infrastructure/unicode/ofstream.hpp>
#include <kth/infrastructure/unicode/unicode.hpp>
#include <kth/infrastructure/unicode/unicode_istream.hpp>
#include <kth/infrastructure/unicode/unicode_ostream.hpp>
#include <kth/infrastructure/unicode/unicode_streambuf.hpp>

#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/atomic.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/collection.hpp>
#include <kth/infrastructure/utility/color.hpp>
#include <kth/infrastructure/utility/conditional_lock.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/deadline.hpp>
#include <kth/infrastructure/utility/decorator.hpp>

#include <kth/infrastructure/utility/deserializer.hpp>
#include <kth/infrastructure/utility/dispatcher.hpp>
#include <kth/infrastructure/utility/enable_shared_from_base.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/exceptions.hpp>
#include <kth/infrastructure/utility/flush_lock.hpp>
#include <kth/infrastructure/utility/interprocess_lock.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/monitor.hpp>
#include <kth/infrastructure/utility/noncopyable.hpp>
#include <kth/infrastructure/utility/operators.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

#include <kth/infrastructure/utility/png.hpp>
#include <kth/infrastructure/utility/prioritized_mutex.hpp>

#include <kth/infrastructure/utility/pseudo_random.hpp>
#include <kth/infrastructure/utility/pseudo_random_broken_do_not_use.hpp>

#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/resubscriber.hpp>
#include <kth/infrastructure/utility/scope_lock.hpp>
#include <kth/infrastructure/utility/sequencer.hpp>
#include <kth/infrastructure/utility/sequential_lock.hpp>
#include <kth/infrastructure/utility/serializer.hpp>
#include <kth/infrastructure/utility/socket.hpp>
#include <kth/infrastructure/utility/string.hpp>

#include <kth/infrastructure/utility/synchronizer.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>
#include <kth/infrastructure/utility/timer.hpp>
#include <kth/infrastructure/utility/track.hpp>

#include <kth/infrastructure/utility/writer.hpp>

#include <kth/infrastructure/wallet/dictionary.hpp>
#include <kth/infrastructure/wallet/hd_private.hpp>
#include <kth/infrastructure/wallet/hd_public.hpp>
// #include <kth/infrastructure/wallet/message.hpp>
#include <kth/infrastructure/wallet/mini_keys.hpp>
#include <kth/infrastructure/wallet/mnemonic.hpp>
#include <kth/infrastructure/wallet/qrcode.hpp>
#include <kth/infrastructure/wallet/uri.hpp>

#if ! defined(__EMSCRIPTEN__)

#include <kth/infrastructure/config/parameter.hpp>
#include <kth/infrastructure/config/printer.hpp>

#include <kth/infrastructure/utility/delegates.hpp>
#include <kth/infrastructure/utility/pending.hpp>
#include <kth/infrastructure/utility/subscriber.hpp>
#include <kth/infrastructure/utility/work.hpp>
#endif

#endif /*KTH_INFRASTRUCTURE_INFRASTRUCTURE_HPP_*/
