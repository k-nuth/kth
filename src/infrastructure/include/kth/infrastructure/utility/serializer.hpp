// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_SERIALIZER_HPP
#define KTH_INFRASTRUCTURE_SERIALIZER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include <kth/infrastructure/error.hpp>
// #include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
////#include <kth/infrastructure/utility/noncopyable.hpp>
#include <kth/infrastructure/utility/writer.hpp>

namespace kth {

/// Writer to wrap arbitrary iterator.
template <typename Iterator>
class serializer
//   : public writer/*, noncopyable*/
{
public:
    using functor = std::function<void (serializer<Iterator> &)>;

    explicit
    serializer(Iterator begin);

    template <typename Buffer>
    void write_forward(const Buffer& data);

    template <typename Buffer>
    void write_reverse(const Buffer& data);

    template <typename Integer>
    void write_big_endian(Integer value);

    template <typename Integer>
    void write_little_endian(Integer value);

    /// Context.
    // implicit
    operator bool() const;

    bool operator!() const;

    /// Write hashes.
    void write_hash(hash_digest const& hash);
    void write_short_hash(short_hash const& hash);
    void write_mini_hash(mini_hash const& hash);

    /// Write big endian integers.
    void write_2_bytes_big_endian(uint16_t value);
    void write_4_bytes_big_endian(uint32_t value);
    void write_8_bytes_big_endian(uint64_t value);
    void write_variable_big_endian(uint64_t value);
    void write_size_big_endian(size_t value);

    /// Write little endian integers.
    void write_error_code(code const& ec);
    void write_2_bytes_little_endian(uint16_t value);
    void write_4_bytes_little_endian(uint32_t value);
    void write_8_bytes_little_endian(uint64_t value);
    void write_variable_little_endian(uint64_t value);
    void write_size_little_endian(size_t value);

    /// Write one byte.
    void write_byte(uint8_t value);

    /// Write all bytes.
    void write_bytes(data_chunk const& data);

    /// Write required size buffer.
    void write_bytes(uint8_t const* data, size_t size);

    /// Write variable length string.
    void write_string(std::string const& value);

    /// Write required length string, padded with nulls.
    void write_string(std::string const& value, size_t size);

    /// Advance iterator without writing.
    void skip(size_t size);

    // non-interface
    //-------------------------------------------------------------------------

    /// Delegate write to a write function.
    void write_delegated(functor write);

    /// Utility for variable skipping of writer.
    size_t read_size_big_endian();

    /// Utility for variable skipping of writer.
    size_t read_size_little_endian();

private:
    bool valid_;
    Iterator iterator_;
};

// Factories.
//-----------------------------------------------------------------------------

template <typename Iterator>
serializer<Iterator> make_unsafe_serializer(Iterator begin);

} // namespace kth

#include <kth/infrastructure/impl/utility/serializer.ipp>

#endif
