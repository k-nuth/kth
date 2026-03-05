// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2017-2025 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <type_traits>

#if defined(__GNUC__) && !defined(__clang__) /* Suppress false positive warnings in this file from GCC 14 & 15 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

/**
 * Implements a drop-in replacement for std::vector<T> which stores up to N
 * elements directly (without heap allocation). The types Size and Diff are used
 * to store element counts, and can be any unsigned + signed type.
 *
 * Storage layout is either:
 * - Direct allocation:
 *   - Size _size: the number of used elements (between 0 and N)
 *   - T direct[N]: an array of N elements of type T
 *     (only the first _size are initialized).
 * - Indirect allocation:
 *   - Size _size: the number of used elements plus N + 1
 *   - Size capacity: the number of allocated elements
 *   - T* indirect: a pointer to an array of capacity elements of type T
 *     (only the first _size are initialized).
 *
 * The data type T must be movable by memmove/realloc(). Once we switch to C++,
 * move constructors can be used instead.
 */
template <unsigned int N, typename T, typename Size = uint32_t,
          typename Diff = int32_t>
class prevector {
public:
    using size_type = Size;
    using difference_type = Diff;
    using value_type = T;
    using reference = value_type &;
    using const_reference = const value_type &;
    using pointer = value_type *;
    using const_pointer = const value_type *;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
#pragma pack(push, 1)
    union direct_or_indirect {
        char direct[sizeof(T) * N];
        struct S {
            char *indirect;
            size_type capacity;
        } s;
    };
#pragma pack(pop)
    alignas(char *) direct_or_indirect _union = {};
    size_type _size = 0;

    static_assert(alignof(char *) % alignof(size_type) == 0 && sizeof(char *) % alignof(size_type) == 0,
                  "size_type cannot have more restrictive alignment requirement than pointer");
    static_assert(alignof(char *) % alignof(T) == 0,
                  "value_type T cannot have more restrictive alignment requirement than pointer");

    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                  "value_type T must be trivially copyable and trivially destructible");

    T *direct_ptr(difference_type pos) {
        return reinterpret_cast<T *>(_union.direct) + pos;
    }
    const T *direct_ptr(difference_type pos) const {
        return reinterpret_cast<const T *>(_union.direct) + pos;
    }
    T *indirect_ptr(difference_type pos) {
        return reinterpret_cast<T *>(_union.s.indirect) + pos;
    }
    const T *indirect_ptr(difference_type pos) const {
        return reinterpret_cast<const T *>(_union.s.indirect) + pos;
    }
    bool is_direct() const { return _size <= N; }

    void change_capacity(size_type new_capacity) {
        if (new_capacity <= N) {
            if (!is_direct()) {
                T *indirect = indirect_ptr(0);
                T *src = indirect;
                T *dst = direct_ptr(0);
                std::memcpy(dst, src, size() * sizeof(T));
                std::free(indirect);
                _size -= N + 1;
            }
        } else {
            if (!is_direct()) {
                // FIXME: Because malloc/realloc here won't call new_handler if
                // allocation fails, assert success. These should instead use an
                // allocator or new/delete so that handlers are called as
                // necessary, but performance would be slightly degraded by
                // doing so.
                _union.s.indirect = static_cast<char *>(std::realloc(_union.s.indirect, sizeof(T) * new_capacity));
                assert(_union.s.indirect);
                _union.s.capacity = new_capacity;
            } else {
                char *new_indirect = static_cast<char *>(std::malloc(sizeof(T) * new_capacity));
                assert(new_indirect);
                T *src = direct_ptr(0);
                T *dst = reinterpret_cast<T *>(new_indirect);
                std::memcpy(dst, src, size() * sizeof(T));
                _union.s.indirect = new_indirect;
                _union.s.capacity = new_capacity;
                _size += N + 1;
            }
        }
    }

    T *item_ptr(difference_type pos) {
        return is_direct() ? direct_ptr(pos) : indirect_ptr(pos);
    }
    const T *item_ptr(difference_type pos) const {
        return is_direct() ? direct_ptr(pos) : indirect_ptr(pos);
    }

    void fill(T *dst, ptrdiff_t count, const T &value = T{}) {
        std::fill_n(dst, count, value);
    }

    template <typename InputIterator>
    void fill(T *dst, InputIterator first, InputIterator last) {
        while (first != last) {
            new (static_cast<void *>(dst)) T(*first);
            ++dst;
            ++first;
        }
    }

public:
    void assign(size_type n, const T &val) {
        clear();
        if (capacity() < n) {
            change_capacity(n);
        }
        _size += n;
        fill(item_ptr(0), n, val);
    }

    template <typename InputIterator>
    void assign(InputIterator first, InputIterator last) {
        size_type n = last - first;
        clear();
        if (capacity() < n) {
            change_capacity(n);
        }
        _size += n;
        fill(item_ptr(0), first, last);
    }

    prevector() noexcept {}

    explicit prevector(size_type n) { resize(n); }

    explicit prevector(size_type n, const T &val) {
        change_capacity(n);
        _size += n;
        fill(item_ptr(0), n, val);
    }

    template <typename InputIterator>
    prevector(InputIterator first, InputIterator last) {
        size_type n = last - first;
        change_capacity(n);
        _size += n;
        fill(item_ptr(0), first, last);
    }

    prevector(const prevector<N, T, Size, Diff> &other) {
        size_type n = other.size();
        change_capacity(n);
        _size += n;
        fill(item_ptr(0), other.begin(), other.end());
    }

    prevector(prevector<N, T, Size, Diff> &&other) noexcept { swap(other); }

    prevector &operator=(const prevector<N, T, Size, Diff> &other) {
        if (&other == this) {
            return *this;
        }
        assign(other.begin(), other.end());
        return *this;
    }

    prevector &operator=(prevector<N, T, Size, Diff> &&other) noexcept {
        swap(other);
        return *this;
    }

    size_type size() const { return is_direct() ? _size : _size - N - 1; }

    bool empty() const { return size() == 0; }

    iterator begin() { return iterator(item_ptr(0)); }
    const_iterator begin() const { return const_iterator(item_ptr(0)); }
    iterator end() { return iterator(item_ptr(size())); }
    const_iterator end() const { return const_iterator(item_ptr(size())); }

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

    size_t capacity() const {
        if (is_direct()) {
            return N;
        } else {
            return _union.s.capacity;
        }
    }

    static constexpr size_t static_capacity() { return N; }

    T &operator[](size_type pos) { return *item_ptr(pos); }

    const T &operator[](size_type pos) const { return *item_ptr(pos); }

    void resize(size_type new_size, const T &value = T{}) {
        size_type cur_size = size();
        if (cur_size == new_size) {
            return;
        }
        if (cur_size > new_size) {
            erase(item_ptr(new_size), end());
            return;
        }
        if (new_size > capacity()) {
            change_capacity(new_size);
        }
        ptrdiff_t increase = new_size - cur_size;
        fill(item_ptr(cur_size), increase, value);
        _size += increase;
    }

    void reserve(size_type new_capacity) {
        if (new_capacity > capacity()) {
            change_capacity(new_capacity);
        }
    }

    void shrink_to_fit() { change_capacity(size()); }

    void clear() { resize(0); }

    iterator insert(iterator pos, const T &value) {
        size_type p = pos - begin();
        size_type new_size = size() + 1;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        T *ptr = item_ptr(p);
        std::memmove(ptr + 1, ptr, (size() - p) * sizeof(T));
        ++_size;
        new (static_cast<void *>(ptr)) T(value);
        return iterator(ptr);
    }

    void insert(iterator pos, size_type count, const T &value) {
        size_type p = pos - begin();
        size_type new_size = size() + count;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        T *ptr = item_ptr(p);
        std::memmove(ptr + count, ptr, (size() - p) * sizeof(T));
        _size += count;
        fill(item_ptr(p), count, value);
    }

    template <typename InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last) {
        size_type p = pos - begin();
        difference_type count = last - first;
        size_type new_size = size() + count;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        T *ptr = item_ptr(p);
        std::memmove(ptr + count, ptr, (size() - p) * sizeof(T));
        _size += count;
        fill(ptr, first, last);
    }

    inline void resize_uninitialized(size_type new_size) {
        // resize_uninitialized changes the size of the prevector but does not
        // initialize it. If size < new_size, the added elements must be
        // initialized explicitly.
        if (capacity() < new_size) {
            change_capacity(new_size);
            _size += new_size - size();
            return;
        }
        if (new_size < size()) {
            erase(item_ptr(new_size), end());
        } else {
            _size += new_size - size();
        }
    }

    iterator erase(iterator pos) { return erase(pos, pos + 1); }

    iterator erase(iterator first, iterator last) {
        // Erase is not allowed to the change the object's capacity. That means
        // that when starting with an indirectly allocated prevector with
        // size and capacity > N, the result may be a still indirectly allocated
        // prevector with size <= N and capacity > N. A shrink_to_fit() call is
        // necessary to switch to the (more efficient) directly allocated
        // representation (with capacity N and size <= N).
        iterator p = first;
        char * const endp = reinterpret_cast<char *>(&*end());
        if constexpr (!std::is_trivially_destructible<T>::value) {
            // NB: this branch is never taken in the current implementation
            while (p != last) {
                p->~T();
                --_size;
                ++p;
            }
        } else {
            _size -= last - p;
        }
        std::memmove(&*first, &*last, endp - reinterpret_cast<char *>(&*last));
        return first;
    }

    template <typename... Args> void emplace_back(Args &&...args) {
        size_type new_size = size() + 1;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        new (item_ptr(size())) T(std::forward<Args>(args)...);
        ++_size;
    }

    void push_back(const T &value) { emplace_back(value); }

    void pop_back() { erase(end() - 1, end()); }

    T &front() { return *item_ptr(0); }

    const T &front() const { return *item_ptr(0); }

    T &back() { return *item_ptr(size() - 1); }

    const T &back() const { return *item_ptr(size() - 1); }

    void swap(prevector<N, T, Size, Diff> &other) noexcept {
        std::swap(_union, other._union);
        std::swap(_size, other._size);
    }

    ~prevector() {
        if constexpr (!std::is_trivially_destructible<T>::value) {
            // NB: this branch is never taken in the current implementation
            clear();
        }
        if (!is_direct()) {
            std::free(_union.s.indirect);
            _union.s.indirect = nullptr;
        }
    }

    bool operator==(const prevector<N, T, Size, Diff> &other) const {
        if (other.size() != size()) {
            return false;
        }
        const_iterator b1 = begin();
        const_iterator b2 = other.begin();
        const_iterator e1 = end();
        while (b1 != e1) {
            if ((*b1) != (*b2)) {
                return false;
            }
            ++b1;
            ++b2;
        }
        return true;
    }

    bool operator!=(const prevector<N, T, Size, Diff> &other) const {
        return !(*this == other);
    }

    bool operator<(const prevector<N, T, Size, Diff> &other) const {
        if (size() < other.size()) {
            return true;
        }
        if (size() > other.size()) {
            return false;
        }
        const_iterator b1 = begin();
        const_iterator b2 = other.begin();
        const_iterator e1 = end();
        while (b1 != e1) {
            if ((*b1) < (*b2)) {
                return true;
            }
            if ((*b2) < (*b1)) {
                return false;
            }
            ++b1;
            ++b2;
        }
        return false;
    }

    size_t allocated_memory() const {
        if (is_direct()) {
            return 0;
        } else {
            return sizeof(T) * _union.s.capacity;
        }
    }

    value_type *data() { return item_ptr(0); }

    const value_type *data() const { return item_ptr(0); }
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
