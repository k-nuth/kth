// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_FEATURE_HPP
#define KTH_NODE_SV2_FEATURE_HPP

// Every Stratum V2 header includes this one first: without the feature none of
// them can be included (docs/optional-features.md).
#if ! defined(KTH_WITH_SV2)
#error "Stratum V2 is an optional feature: build with the Conan option sv2=True."
#endif

#endif // KTH_NODE_SV2_FEATURE_HPP
