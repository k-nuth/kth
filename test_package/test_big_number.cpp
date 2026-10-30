#include <string_view>
#include <kth/infrastructure/machine/big_number.hpp>

int main() {
    using kth::infrastructure::machine::big_number;
    // Exceed 64 bits and use out-of-line operations backed by GMP. No direct
    // GMP include or link directive: kth::infrastructure must supply the edge.
    big_number const value(std::string_view("18446744073709551616"));
    big_number const factor(int64_t{3});
    auto const product = value * factor;
    if (product.to_string() != "55340232221128654848") {
        return 1;
    }
    auto const restored = product / factor;
    return restored.to_string() == value.to_string() ? 0 : 2;
}
