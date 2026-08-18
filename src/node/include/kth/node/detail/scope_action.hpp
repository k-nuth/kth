// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.
//
// `kth/node/detail/` is excluded from the install rules, so nothing here is part
// of the surface a consumer can reach. It exists so a decision worth testing on
// its own does not have to become a public symbol to be tested — the tests in
// this tree include it by path, a downstream consumer cannot.

#ifndef KTH_NODE_DETAIL_SCOPE_ACTION_HPP_
#define KTH_NODE_DETAIL_SCOPE_ACTION_HPP_

#include <exception>
#include <type_traits>
#include <utility>

#include <spdlog/spdlog.h>

namespace kth::node::detail {

/// Run an action on the way out of a scope, unless the scope disarmed it.
///
/// The executor's lifecycle has two places where the property is "whatever
/// happens, this must have been published", and both used to publish only on the
/// path their author had in mind:
///
///   * a start that threw between being admitted and reaching its coroutine left
///     an outcome owed that nothing would ever pay, so a release waiting for it
///     waited forever;
///   * a release that threw left `release_done_` false, so a second caller waited
///     on a condition variable nobody would notify — and left the io thread
///     joinable for ~std::thread to abort on, which is the failure the whole
///     change exists to remove.
///
/// Naming the mechanism is what makes those properties testable at all: the
/// action can be checked on its own, armed and disarmed, rather than only by
/// arranging for production code to throw. See test/scope_action.cpp.
///
/// A TEMPLATE, AND THAT IS THE POINT
///
/// It held a `std::function` once, and a `std::function` large enough to need the
/// heap allocates when it is built — measured, on this library, at three captured
/// pointers. So the guard that was there to cover a debt could itself throw while
/// being constructed, leaving exactly the debt it was for: an outcome never
/// published, a completion never announced. Storing the callable by value cannot
/// allocate, and the assertions below make the constructor's noexcept a fact the
/// compiler checks rather than a claim in a comment.
///
/// Running it is noexcept too, deliberately and completely: it runs from
/// destructors during exception propagation, so an action that throws must not
/// turn a reported failure into a terminate. It is logged and swallowed instead.
template <typename Action>
class scope_action {
public:
    static_assert(std::is_nothrow_move_constructible_v<Action>,
        "a scope action must be storable without a throw: the guard exists to cover a debt, "
        "and one that can throw while being built leaves that debt uncovered");
    /// `armed` is explicit rather than defaulted: a scope that wants the guard in
    /// place BEFORE it owes anything constructs it disarmed and arms it at the
    /// moment the debt is incurred.
    scope_action(Action action, bool armed) noexcept
        : action_(std::move(action))
        , armed_(armed)
    {}

    scope_action(scope_action const&) = delete;
    scope_action& operator=(scope_action const&) = delete;
    scope_action(scope_action&&) = delete;
    scope_action& operator=(scope_action&&) = delete;

    ~scope_action() noexcept {
        run();
    }

    /// The debt now exists.
    void arm() noexcept {
        armed_ = true;
    }

    /// The scope took responsibility itself. Nothing runs on the way out.
    void disarm() noexcept {
        armed_ = false;
    }

    /// Run it now rather than on the way out, and only once.
    void run() noexcept {
        if ( ! armed_) {
            return;
        }
        armed_ = false;
        // Swallowed rather than asserted away: the action takes a lock and
        // notifies, and requiring it to be noexcept would make a failure there
        // terminate the process instead of being reported.
        try {
            action_();
        } catch (std::exception const& e) {
            spdlog::error("[executor] A scope action failed: {}", e.what());
        } catch (...) {
            spdlog::error("[executor] A scope action failed for an unknown reason");
        }
    }

    [[nodiscard]] bool armed() const noexcept {
        return armed_;
    }

private:
    Action action_;
    bool armed_;
};

template <typename Action>
scope_action(Action, bool) -> scope_action<Action>;

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_SCOPE_ACTION_HPP_
