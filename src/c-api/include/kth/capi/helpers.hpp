// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_HELPERS_HPP_
#define KTH_CAPI_HELPERS_HPP_

// Umbrella for the GENERIC C-API helpers, which live in focused modules under
// `kth/capi/detail/`:
//
//   handle_access.hpp      borrowed access to opaque handles (cpp_ref, ...)
//   ownership.hpp          to_owned*, make_owned*, clone, del*
//   buffers.hpp            C-owned buffer / string allocation and serialization
//   errors.hpp             error-code mapping
//   exception_barrier.hpp  exception policy at the C boundary (opt-in)
//   value_conversions.hpp  generic array / POD struct conversion algorithms
//   enum_cast.hpp          generic enum <-> integer cast
//   secure_scrub.hpp       RAII wipe of sensitive stack copies
//
// None of them knows a concrete type or enum of any library layer. The
// conversions that do are generated into `kth/capi/conversions/` and
// aggregated, together with the headers binding TUs need, by
// `kth/capi/conversions.hpp`.

#include <kth/capi/detail/buffers.hpp>
#include <kth/capi/detail/enum_cast.hpp>
#include <kth/capi/detail/errors.hpp>
#include <kth/capi/detail/exception_barrier.hpp>
#include <kth/capi/detail/handle_access.hpp>
#include <kth/capi/detail/ownership.hpp>
#include <kth/capi/detail/secure_scrub.hpp>
#include <kth/capi/detail/value_conversions.hpp>

#endif /* KTH_CAPI_HELPERS_HPP_ */
