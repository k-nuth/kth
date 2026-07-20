// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PENDING_HPP
#define KTH_INFRASTRUCTURE_PENDING_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/noncopyable.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

/// A managed collection of object pointers.
template <typename Element>
class pending : noncopyable {
public:
    using element_ptr = std::shared_ptr<Element>;
    using elements = std::vector<element_ptr>;
    using finder = std::function<bool(const element_ptr& element)>;

    pending(size_t initial_capacity);
    ~pending();

    /// Safely copy the member collection.
    elements collection() const;

    /// The number of elements in the collection.
    size_t size() const;

    /// Determine if there exists an element that satisfies the finder.
    bool exists(finder match) const;

    /// Store a unique element in the collection (fails if stopped).
    code store(element_ptr element, finder match);

    /// Store an element in the collection (fails if stopped).
    code store(element_ptr element);

    /// Remove an element from the collection.
    void remove(element_ptr element);

    /// Stop all elements of the collection (idempotent).
    void stop(code const& ec);

    /// Close and erase all elements of the collection (blocking).
    void close();

private:

    // This is thread safe.
    std::atomic<bool> stopped_;

    // This is protected by mutex.
    elements elements_;
    mutable upgrade_mutex mutex_;
};

} // namespace kth

#include <kth/infrastructure/impl/utility/pending.ipp>

#endif
