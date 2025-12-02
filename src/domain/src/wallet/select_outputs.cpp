// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/select_outputs.hpp>

#include <algorithm>
#include <cstdint>

#include <kth/domain/chain/points_value.hpp>
#include <kth/domain/constants.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth::domain::wallet {

using namespace kth::domain::chain;

void select_outputs::greedy(points_value& out, points_value const& unspent, uint64_t minimum_value) {
    out.points.clear();

    // The minimum required value does not exist.
    if (unspent.value() < minimum_value) {
        return;
    }

    // Optimization for simple case not requiring search.
    if (unspent.points.size() == 1) {
        out.points.push_back(unspent.points.front());
        return;
    }

    // Copy the points list for safe manipulation.
    auto copy = unspent.points;

    auto const below = [minimum_value](point_value const& point) {
        return point.value() < minimum_value;
    };

    auto const lesser = [](point_value const& x, point_value const& y) {
        return x.value() < y.value();
    };

    auto const greater = [](point_value const& x, point_value const& y) {
        return x.value() > y.value();
    };

    // Reorder list beteen values that exceed minimum and those that do not.
    auto const sufficient = std::partition(copy.begin(), copy.end(), below);

    // If there are values large enough, return the smallest (of the largest).
    auto const minimum = std::min_element(sufficient, copy.end(), lesser);

    if (minimum != copy.end()) {
        out.points.push_back(*minimum);
        return;
    }

    // Sort all by descending value in order to use the fewest inputs possible.
    std::sort(copy.begin(), copy.end(), greater);

    // This is naive, will not necessarily find the smallest combination.
    // for (auto point = copy.begin(); point != copy.end(); ++point) {
    for (auto const& point : copy) {
        out.points.push_back(point);

        if (out.value() >= minimum_value) {
            return;
        }
    }

    KTH_ASSERT_MSG(false, "unreachable code reached");
}

void select_outputs::individual(points_value& out, points_value const& unspent, uint64_t minimum_value) {
    out.points.clear();
    out.points.reserve(unspent.points.size());

    // Select all individual points that satisfy the minimum.
    for (auto const& point : unspent.points) {
        if (point.value() >= minimum_value) {
            out.points.push_back(point);
        }
    }

    auto const lesser = [](point_value const& x, point_value const& y) {
        return x.value() < y.value();
    };

    // Return in ascending order by value.
    out.points.shrink_to_fit();
    std::sort(out.points.begin(), out.points.end(), lesser);
}

void select_outputs::select(points_value& out, points_value const& unspent, uint64_t minimum_value, algorithm option) {
    switch (option) {
        case algorithm::individual: {
            individual(out, unspent, minimum_value);
            break;
        }
        case algorithm::greedy:
        default: {
            greedy(out, unspent, minimum_value);
            break;
        }
    }
}

} // namespace kth::domain::wallet
