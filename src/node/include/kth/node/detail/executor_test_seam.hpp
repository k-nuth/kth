// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.
//
// `kth/node/detail/` is excluded from the install rules, so nothing here is part
// of the surface a consumer can reach. `executor` names this type as a friend and
// nothing else; without this header that friendship can be used by no one.

#ifndef KTH_NODE_DETAIL_EXECUTOR_TEST_SEAM_HPP_
#define KTH_NODE_DETAIL_EXECUTOR_TEST_SEAM_HPP_

#include <utility>

#include <kth/node/executor/executor.hpp>

namespace kth::node::detail {

/// Replace how an executor builds its node.
///
/// Two of this class's guarantees are about what happens when building the node
/// does not work: that a start throwing before it reaches its coroutine still
/// publishes an answer, and that a factory handing back nothing fails the start
/// normally instead of dereferencing it. Neither can be exercised while the node
/// is built by a `make_shared` that only fails when the machine is out of memory.
///
/// Only before a start. The factory is read once, by the admitted start, and
/// replacing it afterwards changes nothing.
struct executor_test_seam {
    template <typename Factory>
    static void set_node_factory(executor& target, Factory factory) {
        target.make_node_ = std::move(factory);
    }

    /// The lifecycle's four observable points, named for a test to use.
    ///
    /// The type stays private to `executor`; this friend is what publishes it.
    using probe_point = executor::lifecycle_probe_point;

    /// Watch the lifecycle at those points, and fail it there.
    ///
    /// A probe that counts observes; one that throws injects. Nothing here is
    /// reachable from outside: p2p_node::stop() logs only when it fails, a
    /// std::thread that cannot be created is not something a caller can arrange,
    /// and a teardown that fails once it owns the object has no step a caller can
    /// reach. Install before the start, or before the stop that is meant to fail.
    ///
    /// One member covers all four. Four separate callables would cost this class
    /// 128 bytes to answer questions that are asked only by the controls.
    template <typename Probe>
    static void set_lifecycle_probe(executor& target, Probe probe) {
        target.lifecycle_probe_ = std::move(probe);
    }
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_EXECUTOR_TEST_SEAM_HPP_
