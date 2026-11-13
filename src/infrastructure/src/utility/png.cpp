// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/png.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/color.hpp>

namespace kth {

bool png::write_png(byte_span data, uint32_t size, data_chunk& out) {
    return png::write_png(data, size, dots_per_inch, margin, inches_per_meter,
        get_default_foreground(), get_default_background(), out);
}

extern "C" void sink_write(png_structp png_ptr, png_bytep data, png_size_t length) {
    static_assert(sizeof(length) <= sizeof(size_t), "png_size_t too large");
    auto const size = size_t(length);
    auto& out = *static_cast<data_chunk*>(png_get_io_ptr(png_ptr));
    out.insert(out.end(), data, data + size);
}

extern "C" void error_callback(png_structp png_ptr, png_const_charp error_message) {
    throw std::runtime_error(error_message);
}

bool png::write_png(byte_span input, uint32_t size, uint32_t dots_per_inch,
    uint32_t margin, uint32_t inches_per_meter, color const& foreground,
    color const& background, data_chunk& out)
{
    if (size == 0) {
        return false;
    }

    byte_reader reader(input);

    auto const version = reader.read_little_endian<uint32_t>();
    if ( ! version) {
        return false;
    }

    auto const width_exp = reader.read_little_endian<uint32_t>();
    if ( ! width_exp) {
        return false;
    }
    auto const width = *width_exp;

    if (width == 0 || kth::max_size_t / width < width) {
        return false;
    }

    auto const area = size_t{width} * width;
    auto const data = reader.read_bytes(area);
    if ( ! data) {
        return false;
    }

    // libpng limits dimensions to 31 bits. Compute in 64 bits before narrowing.
    auto const unscaled_width = uint64_t{width} + uint64_t{margin} * 2;
    if (unscaled_width > PNG_UINT_31_MAX / size) {
        return false;
    }

    auto const expanded_width = unscaled_width * size;

    struct resources {
        png_structp png = nullptr;
        png_infop info = nullptr;
        ~resources() { if (png) png_destroy_write_struct(&png, &info); }
    } owned;
    data_chunk encoded;
    try {
        static constexpr int32_t bit_depth = 1;
        static constexpr int32_t bits_per_byte = 8;
        static constexpr uint8_t margin_value = 0xff;

        auto const margin_size = uint64_t{margin} * size;
        auto const realwidth = static_cast<uint32_t>(expanded_width);
        auto const row_size = (realwidth + 7) / bits_per_byte;

        data_chunk row;
        row.reserve(row_size);

        auto png_ptr = owned.png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, error_callback, nullptr);
        if (png_ptr == nullptr) {
            return false;
        }

        auto info_ptr = owned.info = png_create_info_struct(png_ptr);
        if (info_ptr == nullptr) {
            return false;
        }

        png_color palette[2];
        palette[0].red = foreground.red;
        palette[0].green = foreground.green;
        palette[0].blue = foreground.blue;
        palette[1].red = background.red;
        palette[1].green = background.green;
        palette[1].blue = background.blue;

        png_byte alpha_values[2];
        alpha_values[0] = foreground.alpha;
        alpha_values[1] = background.alpha;

        png_set_PLTE(png_ptr, info_ptr, palette, 2);
        png_set_tRNS(png_ptr, info_ptr, alpha_values, 2, nullptr);

        png_set_write_fn(png_ptr, &encoded, sink_write, nullptr);

        png_set_IHDR(png_ptr, info_ptr, realwidth, realwidth, bit_depth,
            PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        png_set_pHYs(png_ptr, info_ptr, dots_per_inch * inches_per_meter,
            dots_per_inch * inches_per_meter, PNG_RESOLUTION_METER);

        png_write_info(png_ptr, info_ptr);

        // write top margin
        row.assign(row_size, margin_value);
        for (auto y = 0u; y < margin_size; ++y) {
            png_write_row(png_ptr, row.data());
        }

        // write data
        uint8_t* row_ptr = nullptr;
        auto data_ptr = data->data();
        for (auto y = 0u; y < width; ++y) {
            // TODO: unguarded overflow conditions.
            auto bit = bits_per_byte - 1;
            row.assign(row_size, margin_value);
            row_ptr = row.data();
            row_ptr += margin_size / bits_per_byte;
            bit = (bits_per_byte - 1) - (margin_size % bits_per_byte);

            for (auto x = 0u; x < width; ++x) {
                for (auto xx = 0u; xx < size; ++xx) {
                    *row_ptr ^= (*data_ptr & 1) << bit;
                    --bit;
                    if (bit < 0) {
                        ++row_ptr;
                        bit = bits_per_byte - 1;
                    }
                }
                ++data_ptr;
            }

            for (auto yy = 0u; yy < size; ++yy) {
                png_write_row(png_ptr, row.data());
            }
        }

        // write bottom margin
        row.assign(row_size, margin_value);
        for (auto y = 0u; y < margin_size; ++y) {
            png_write_row(png_ptr, row.data());
        }

        png_write_end(png_ptr, info_ptr);
        out.insert(out.end(), encoded.begin(), encoded.end());
    } catch (std::runtime_error const&) {
        return false;
    }

    return true;
}

} // namespace kth
