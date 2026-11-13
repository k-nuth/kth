// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/png.hpp>

#include <algorithm>
#include <cstddef>
#include <csetjmp>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <png.h>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth {
namespace {

struct encoder {
    png_structp png = nullptr;
    png_infop info = nullptr;
    data_chunk output;
    data_chunk row;
    png_errc error = png_errc::encoding_failed;
    ~encoder() {
        if (png) {
            png_destroy_write_struct(&png, &info);
        }
    }
};

// These private callbacks implement libpng's C ABI and calling convention.
// No C++ exception crosses the C library; allocation errors are caught here.
extern "C" {
static void PNGCBAPI write_bytes(png_structp png, png_bytep bytes, png_size_t count) {
    auto& state = *static_cast<encoder*>(png_get_io_ptr(png));
    bool failed = false;
    try {
        state.output.insert(state.output.end(), bytes, bytes + count);
    } catch (std::bad_alloc const&) {
        state.error = png_errc::allocation_failed;
        failed = true;
    } catch (std::length_error const&) {
        state.error = png_errc::dimensions_out_of_range;
        failed = true;
    }
    // Exit the catch handler before jumping back into the encoding helper.
    if (failed) {
        ::png_error(png, "PNG output allocation failed");
    }
}

static void PNGCBAPI fail(png_structp png, png_const_charp) {
    png_longjmp(png, 1);
}
}

// Clear a contiguous black run with at most two partial-byte masks. Scaling
// no longer visits every expanded pixel individually.
void black_run(data_chunk& row, size_t start, size_t count) noexcept {
    auto* out = row.data() + start / 8;
    auto const offset = start % 8;
    if (offset != 0) {
        auto const bits = std::min(count, 8 - offset);
        *out++ &= static_cast<uint8_t>(~(((1u << bits) - 1u) << (8 - offset - bits)));
        count -= bits;
    }
    std::memset(out, 0, count / 8);
    out += count / 8;
    auto const tail = count % 8;
    if (tail != 0) {
        *out &= static_cast<uint8_t>(0xffu >> tail);
    }
}

// The setjmp frame contains only trivial locals. All owning objects live in
// the caller, so libpng's longjmp never skips a C++ object's destruction.
bool encode(encoder& state, byte_span pixels, uint32_t width, uint32_t expanded,
            png::options const& settings, png_uint_32 resolution) noexcept {
    if (setjmp(png_jmpbuf(state.png)) != 0)
        return false;
    state.info = png_create_info_struct(state.png);
    if ( ! state.info) {
        state.error = png_errc::allocation_failed;
        return false;
    }
    png_color palette[2]{{settings.foreground.red, settings.foreground.green, settings.foreground.blue},
                         {settings.background.red, settings.background.green, settings.background.blue}};
    png_byte alpha[2]{settings.foreground.alpha, settings.background.alpha};
    png_set_PLTE(state.png, state.info, palette, 2);
    png_set_tRNS(state.png, state.info, alpha, 2, nullptr);
    png_set_write_fn(state.png, &state, write_bytes, nullptr);
    png_set_IHDR(state.png, state.info, expanded, expanded, 1, PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_pHYs(state.png, state.info, resolution, resolution, PNG_RESOLUTION_METER);
    png_write_info(state.png, state.info);
    size_t const margin = size_t{settings.margin} * settings.scale;
    for (size_t y = 0; y < margin; ++y) {
        png_write_row(state.png, state.row.data());
    }
    for (size_t y = 0; y < width; ++y) {
        std::fill(state.row.begin(), state.row.end(), uint8_t{0xff});
        if (settings.scale == 1) {
            for (size_t x = 0; x < width; ++x) {
                auto const bit = margin + x;
                state.row[bit / 8] &= static_cast<uint8_t>(~((pixels[y * width + x] & 1u) << (7 - bit % 8)));
            }
        } else {
            for (size_t x = 0; x < width; ++x)
                if ((pixels[y * width + x] & 1) != 0)
                    black_run(state.row, margin + x * settings.scale, settings.scale);
        }
        for (uint32_t repeat = 0; repeat < settings.scale; ++repeat)
            png_write_row(state.png, state.row.data());
    }
    std::fill(state.row.begin(), state.row.end(), uint8_t{0xff});
    for (size_t y = 0; y < margin; ++y) {
        png_write_row(state.png, state.row.data());
    }
    png_write_end(state.png, state.info);
    return true;
}

} // namespace

std::expected<data_chunk, png_errc> png::write(byte_span input, uint32_t scale) {
    return write(input, options{.scale = scale});
}

std::expected<data_chunk, png_errc> png::write(byte_span input, options const& settings) {
    if (settings.scale == 0)
        return std::unexpected(png_errc::invalid_scale);
    byte_reader reader(input);
    auto const version = reader.read_little_endian<uint32_t>();
    auto const parsed_width = reader.read_little_endian<uint32_t>();
    if ( ! version || !parsed_width || *parsed_width == 0)
        return std::unexpected(png_errc::invalid_data);
    auto const width = *parsed_width;
    if (width > std::numeric_limits<size_t>::max() / width)
        return std::unexpected(png_errc::dimensions_out_of_range);
    auto const pixels = reader.read_bytes(size_t{width} * width);
    if ( ! pixels)
        return std::unexpected(png_errc::invalid_data);
    auto const unscaled = uint64_t{width} + uint64_t{settings.margin} * 2;
    if (unscaled > PNG_UINT_31_MAX / settings.scale)
        return std::unexpected(png_errc::dimensions_out_of_range);
    auto const expanded = static_cast<uint32_t>(unscaled * settings.scale);
    auto const resolution = uint64_t{settings.dots_per_inch} * settings.inches_per_meter;
    if (resolution > PNG_UINT_31_MAX)
        return std::unexpected(png_errc::invalid_resolution);
    try {
        encoder state;
        state.row.assign((size_t{expanded} + 7) / 8, 0xff);
        state.output.reserve(256);
        state.png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, fail, nullptr);
        if ( ! state.png)
            return std::unexpected(png_errc::allocation_failed);
        if ( ! encode(state, *pixels, width, expanded, settings, static_cast<png_uint_32>(resolution)))
            return std::unexpected(state.error);
        return std::move(state.output);
    } catch (std::bad_alloc const&) {
        return std::unexpected(png_errc::allocation_failed);
    } catch (std::length_error const&) {
        return std::unexpected(png_errc::dimensions_out_of_range);
    }
}

} // namespace kth
