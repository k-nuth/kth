// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The KJ side of the Cap'n Proto RPC backend. Everything in this file runs on
// the one KJ transport thread, and it never touches chain state: a call becomes
// a request_msg, and the answer comes back through a cross-thread fulfiller.
// See bridge.hpp for the seam and docs/capnproto-rpc.md for the rationale.

#include "capnp_rpc/bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <capnp/rpc-twoparty.h>
#include <capnp/rpc.h>
#include <kj/async-io.h>
#include <kj/debug.h>
#include <kj/exception.h>
#include <spdlog/spdlog.h>

#include <kth/rpc/v1/chain.capnp.h>

namespace kth::node::capnp_rpc::detail {

namespace {

namespace v1 = ::kth::rpc::v1;

constexpr size_t hash_size = 32;
// Ends a connection that just failed its last login after the error reply has
// had time to be written.
constexpr auto close_grace = std::chrono::milliseconds(200);

// Compare without an early exit on the first differing byte.
bool equal_constant_time(std::string const& a, std::string const& b) {
    auto const size = std::max(a.size(), b.size());
    uint8_t diff = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < size; ++i) {
        uint8_t const x = i < a.size() ? uint8_t(a[i]) : 0;
        uint8_t const y = i < b.size() ? uint8_t(b[i]) : 0;
        diff |= uint8_t(x ^ y);
    }
    return diff == 0;
}

v1::ErrorCode to_wire(status code) {
    switch (code) {
        case status::invalid_argument:   return v1::ErrorCode::INVALID_ARGUMENT;
        case status::not_found:          return v1::ErrorCode::NOT_FOUND;
        case status::resource_exhausted: return v1::ErrorCode::RESOURCE_EXHAUSTED;
        case status::unavailable:        return v1::ErrorCode::UNAVAILABLE;
        case status::internal:           return v1::ErrorCode::INTERNAL;
    }
    return v1::ErrorCode::INTERNAL;
}

void set_error(v1::Error::Builder out, failure const& error) {
    out.setCode(to_wire(error.code));
    out.setMessage(error.message.c_str());
}

void set_hash(capnp::Data::Builder out, hash_digest const& hash) {
    std::memcpy(out.begin(), hash.data(), hash_size);
}

failure unexpected_reply() {
    return {status::internal, "internal error"};
}

void fill(v1::ChainInfoResult::Builder out, reply const& answer) {
    if (auto const* info = std::get_if<chain_info>(&answer)) {
        auto ok = out.initOk();
        ok.setBlockHeight(info->block_height);
        ok.setHeaderHeight(info->header_height);
        set_hash(ok.initBestBlockHash(hash_size), info->best_block_hash);
    } else if (auto const* error = std::get_if<failure>(&answer)) {
        set_error(out.initError(), *error);
    } else {
        set_error(out.initError(), unexpected_reply());
    }
}

void fill(v1::BlockResult::Builder out, hash_digest const& hash, reply const& answer) {
    if (auto const* block = std::get_if<block_data>(&answer)) {
        auto ok = out.initOk();
        set_hash(ok.initHash(hash_size), hash);
        ok.setHeight(block->height);
        auto wire = ok.initWire(block->wire.size());
        if ( ! block->wire.empty()) {
            std::memcpy(wire.begin(), block->wire.data(), block->wire.size());
        }
    } else if (auto const* error = std::get_if<failure>(&answer)) {
        set_error(out.initError(), *error);
    } else {
        set_error(out.initError(), unexpected_reply());
    }
}

void fill(v1::TransactionResult::Builder out, hash_digest const& hash, reply const& answer) {
    if (auto const* tx = std::get_if<transaction_data>(&answer)) {
        auto ok = out.initOk();
        set_hash(ok.initHash(hash_size), hash);
        auto wire = ok.initWire(tx->wire.size());
        if ( ! tx->wire.empty()) {
            std::memcpy(wire.begin(), tx->wire.data(), tx->wire.size());
        }
        if (tx->confirmed) {
            auto position = ok.getLocation().initConfirmed();
            position.setBlockHeight(tx->confirmed->block_height);
            position.setIndex(tx->confirmed->index);
        } else {
            ok.getLocation().setMempool();
        }
    } else if (auto const* error = std::get_if<failure>(&answer)) {
        set_error(out.initError(), *error);
    } else {
        set_error(out.initError(), unexpected_reply());
    }
}

struct connection;

// What the transport knows of all its connections. KJ thread only.
struct transport_state {
    size_t authenticated = 0;
    size_t unauthenticated = 0;
    std::vector<connection*> live;

    // Answers written to the connections and not yet taken by their clients.
    size_t queued_bytes() const;
};

// Per-connection state, shared by the connection's servers and call guards.
struct conn_state : kj::Refcounted {
    conn_state(bridge& shared, transport_state& transport, credentials const& creds, kj::Timer& timer)
        : shared(shared), transport(transport), creds(creds), timer(timer) {}

    ~conn_state() {
        if (shared.on_connection_released) shared.on_connection_released();
    }

    bridge& shared;
    transport_state& transport;
    credentials const& creds;
    kj::Timer& timer;
    size_t in_flight = 0;
    size_t login_failures = 0;
    bool authenticated = false;
    bool closing = false;
    kj::Own<kj::PromiseFulfiller<void>> close;
    kj::Promise<void> deadline = nullptr;

    void schedule_close(std::chrono::milliseconds after) {
        if (closing) return;
        closing = true;
        deadline = timer.afterDelay(after.count() * kj::MILLISECONDS).then([this]() {
            if (close) close->fulfill();
        }).eagerlyEvaluate(nullptr);
    }
};

// Lives exactly as long as one admitted call's server-side promise. Destroying
// it before the answer arrived is the cancellation signal: the client dropped
// the call, the connection closed, or the transport is shutting down.
struct call_guard {
    call_guard(bridge& shared, kj::Own<conn_state> conn, uint64_t id)
        : shared(shared), conn(kj::mv(conn)), id(id) {}
    ~call_guard() {
        --conn->in_flight;
        if ( ! answered) {
            // A full mailbox only loses the early cancel; the owner still ends
            // the operation, and cancels everything left on shutdown.
            (void)shared.post(cancel_msg{id});
        }
    }
    bridge& shared;
    kj::Own<conn_state> conn;
    uint64_t id;
    bool answered = false;
};

class chain_server final : public v1::Chain::Server {
public:
    explicit chain_server(kj::Own<conn_state> conn)
        : conn_(kj::mv(conn)) {}

protected:
    kj::Promise<void> getChainInfo(GetChainInfoContext context) override {
        return submit(op_kind::chain_info, hash_digest{},
            [context](reply const& answer) mutable {
                fill(context.getResults().initResult(), answer);
            });
    }

    kj::Promise<void> getBlock(GetBlockContext context) override {
        hash_digest hash;
        if ( ! read_hash(context.getParams().getHash(), hash)) {
            set_error(context.getResults().initResult().initError(), bad_hash(context.getParams().getHash()));
            return kj::READY_NOW;
        }
        return submit(op_kind::block, hash,
            [context, hash](reply const& answer) mutable {
                fill(context.getResults().initResult(), hash, answer);
            });
    }

    kj::Promise<void> getTransaction(GetTransactionContext context) override {
        hash_digest hash;
        if ( ! read_hash(context.getParams().getHash(), hash)) {
            set_error(context.getResults().initResult().initError(), bad_hash(context.getParams().getHash()));
            return kj::READY_NOW;
        }
        return submit(op_kind::transaction, hash,
            [context, hash](reply const& answer) mutable {
                fill(context.getResults().initResult(), hash, answer);
            });
    }

private:
    static bool read_hash(capnp::Data::Reader in, hash_digest& out) {
        if (in.size() != hash_size) return false;
        std::memcpy(out.data(), in.begin(), hash_size);
        return true;
    }

    static failure bad_hash(capnp::Data::Reader in) {
        return {status::invalid_argument,
            fmt::format("hash must be exactly 32 bytes, got {}", in.size())};
    }

    // Admit, hand to the owner strand, and wait for the answer without blocking
    // the KJ loop. `deliver` runs on this thread and writes the results.
    template <typename Deliver>
    kj::Promise<void> submit(op_kind kind, hash_digest const& hash, Deliver deliver) {
        auto const& lim = conn_->shared.lim;
        auto reject = [&](failure const& why) {
            deliver(reply{why});
            return kj::READY_NOW;
        };

        // Answers count against the transport until their clients read them: a
        // client that asks and never reads would otherwise grow them without
        // bound. Past the bound, nothing more is answered until they drain.
        if (conn_->transport.queued_bytes() >= lim.max_queued_bytes) {
            return reject({status::resource_exhausted,
                "answers are waiting for their clients to read them; retry later"});
        }
        if (conn_->in_flight >= lim.max_in_flight_per_connection || ! conn_->shared.try_admit()) {
            return reject({status::resource_exhausted,
                "too many calls in flight; retry later"});
        }

        auto paf = kj::newPromiseAndCrossThreadFulfiller<reply>();
        auto const id = conn_->shared.next_id++;
        ++conn_->in_flight;
        auto guard = kj::heap<call_guard>(conn_->shared, kj::addRef(*conn_), id);

        if ( ! conn_->shared.post(request_msg{id, kind, hash, kj::mv(paf.fulfiller)})) {
            // The mailbox holds a request and a cancel per admitted call, but a
            // cancel for a call answered a moment ago can still be in it: then
            // this call is refused rather than queued.
            conn_->shared.release();
            guard->answered = true;
            return reject({status::unavailable, "node is busy; retry later"});
        }

        auto* raw = guard.get();
        return kj::mv(paf.promise).then([raw, deliver = kj::mv(deliver)](reply answer) mutable {
            raw->answered = true;
            deliver(answer);
        }).attach(kj::mv(guard));
    }

    kj::Own<conn_state> conn_;
};

class gateway_server final : public v1::Gateway::Server {
public:
    explicit gateway_server(kj::Own<conn_state> conn)
        : conn_(kj::mv(conn)) {}

protected:
    kj::Promise<void> login(LoginContext context) override {
        auto out = context.getResults().initResult();
        if (conn_->closing) {
            set_error(out.initError(), {status::unavailable, "connection is closing"});
            return kj::READY_NOW;
        }

        auto params = context.getParams();
        std::string supplied = std::string(params.getUser().cStr(), params.getUser().size());
        supplied += ':';
        supplied.append(params.getPassword().cStr(), params.getPassword().size());

        if (equal_constant_time(supplied, conn_->creds.expected)) {
            KJ_IF_MAYBE(chain, chain_) {
                // Logged in already: the same capability, not one more.
                out.setChain(*chain);
                return kj::READY_NOW;
            }
            auto& transport = conn_->transport;
            if (transport.authenticated >= conn_->shared.lim.max_connections) {
                set_error(out.initError(), {status::resource_exhausted,
                    "too many clients are connected; retry later"});
                conn_->schedule_close(close_grace);
                return kj::READY_NOW;
            }
            conn_->authenticated = true;
            --transport.unauthenticated;
            ++transport.authenticated;
            v1::Chain::Client chain = kj::heap<chain_server>(kj::addRef(*conn_));
            chain_ = chain;
            out.setChain(kj::mv(chain));
            return kj::READY_NOW;
        }

        auto error = out.initError();
        error.setCode(v1::ErrorCode::UNAUTHENTICATED);
        error.setMessage("invalid credentials");
        if (++conn_->login_failures >= conn_->shared.lim.max_login_failures) {
            conn_->schedule_close(close_grace);
        }
        return kj::READY_NOW;
    }

private:
    kj::Own<conn_state> conn_;
    // The one Chain capability a login hands out, for every later login too.
    // Here, not in conn_state: the chain server holds conn_state, and a
    // capability kept there would keep both alive after the connection.
    kj::Maybe<v1::Chain::Client> chain_;
};

struct connection {
    connection(kj::Own<kj::AsyncIoStream> accepted, bridge& shared, transport_state& transport,
        credentials const& creds, kj::Timer& timer)
        : state(kj::refcounted<conn_state>(shared, transport, creds, timer))
        , stream(kj::mv(accepted))
        , network(*stream, capnp::rpc::twoparty::Side::SERVER,
              capnp::ReaderOptions{std::max<size_t>(shared.lim.max_request_bytes / 8, 64), 16})
        , rpc(capnp::makeRpcServer(network, v1::Gateway::Client(kj::heap<gateway_server>(kj::addRef(*state))))) {
        rpc.setFlowLimit(std::max<size_t>(shared.lim.max_request_bytes / 8, 64) *
            shared.lim.max_in_flight_per_connection);
    }

    kj::Own<conn_state> state;
    kj::Own<kj::AsyncIoStream> stream;
    capnp::TwoPartyVatNetwork network;
    capnp::RpcSystem<capnp::rpc::twoparty::VatId> rpc;
};

size_t transport_state::queued_bytes() const {
    size_t total = 0;
    for (auto* open : live) {
        total += open->network.getCurrentQueueSize();
    }
    return total;
}

class service final : private kj::TaskSet::ErrorHandler {
public:
    service(bridge& shared, credentials const& creds, kj::Timer& timer)
        : shared_(shared), creds_(creds), timer_(timer), tasks_(*this) {}

    kj::Promise<void> accept_loop(kj::ConnectionReceiver& receiver) {
        return receiver.accept().then([this, &receiver](kj::Own<kj::AsyncIoStream> stream) {
            adopt(kj::mv(stream));
            return accept_loop(receiver);
        }, [this, &receiver](kj::Exception&& error) {
            // Typically a resource shortage (descriptors). Not fatal: back off
            // briefly and keep listening; the owner's stop ends this loop.
            spdlog::warn("[capnp-rpc] accept failed: {}; retrying", error.getDescription().cStr());
            return timer_.afterDelay(100 * kj::MILLISECONDS).then([this, &receiver]() {
                return accept_loop(receiver);
            });
        });
    }

private:
    void adopt(kj::Own<kj::AsyncIoStream> stream) {
        // Connections not logged in yet have a quota of their own, so they can
        // never keep logged-in clients out; logged-in ones are bounded at login.
        if (transport_.unauthenticated >= shared_.lim.max_unauthenticated) {
            return;   // dropped: closing the stream refuses the connection
        }
        ++transport_.unauthenticated;
        auto conn = kj::heap<connection>(kj::mv(stream), shared_, transport_, creds_, timer_);
        transport_.live.push_back(conn.get());
        auto closed = kj::newPromiseAndFulfiller<void>();
        conn->state->close = kj::mv(closed.fulfiller);
        conn->state->deadline = timer_.afterDelay(
                int64_t(shared_.lim.login_timeout.count()) * kj::MILLISECONDS)
            .then([state = &*conn->state]() {
                if ( ! state->authenticated) state->close->fulfill();
            }).eagerlyEvaluate(nullptr);
        auto ended = conn->network.onDisconnect().exclusiveJoin(kj::mv(closed.promise));
        auto* const raw = conn.get();
        tasks_.add(kj::mv(ended).attach(kj::mv(conn), kj::defer([this, raw]() {
            if (raw->state->authenticated) {
                --transport_.authenticated;
            } else {
                --transport_.unauthenticated;
            }
            std::erase(transport_.live, raw);
        })));
    }

    void taskFailed(kj::Exception&& exception) override {
        spdlog::debug("[capnp-rpc] connection ended with error: {}", exception.getDescription().cStr());
    }

    bridge& shared_;
    credentials const& creds_;
    kj::Timer& timer_;
    transport_state transport_;
    kj::TaskSet tasks_;
};

} // namespace

void run_kj_thread(bridge& shared, credentials creds, std::string bind, uint16_t port,
    exited_fn exited) {
    std::string error;
    try {
        auto io = kj::setupAsyncIo();
        auto& scope = io.waitScope;

        auto address = io.provider->getNetwork()
            .parseAddress(kj::heapString(bind.c_str()), port).wait(scope);
        auto receiver = address->listen();

        // The promise lives on this thread; only its fulfiller is used from the
        // thread that asks to stop. A request made earlier fulfills it at once,
        // here. The callback is destroyed before the fulfiller it uses.
        auto stop = kj::newPromiseAndCrossThreadFulfiller<void>();
        std::stop_callback const on_stop(shared.stop.get_token(),
            [fulfiller = stop.fulfiller.get()] { fulfiller->fulfill(); });
        service svc(shared, creds, io.provider->getTimer());
        auto accepting = svc.accept_loop(*receiver);

        if ( ! shared.post(listening_msg{uint16_t(receiver->getPort())})) {
            KJ_FAIL_REQUIRE("owner mailbox unavailable");
        }

        // Serve until stop is requested. The accept loop only ends by failing,
        // which is then reported instead.
        kj::mv(accepting).exclusiveJoin(kj::mv(stop.promise)).wait(scope);
    } catch (kj::Exception const& e) {
        error = e.getDescription().cStr();
    } catch (std::exception const& e) {
        error = e.what();
    } catch (...) {
        error = "unknown failure";
    }

    // All KJ objects are gone. Last, tell the owner: posted to its strand, which
    // cannot fail, so the owner learns of it even with a full mailbox.
    exited(std::move(error));
}

} // namespace kth::node::capnp_rpc::detail
