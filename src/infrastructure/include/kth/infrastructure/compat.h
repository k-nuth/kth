// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_COMPAT_H
#define KTH_INFRASTRUCTURE_COMPAT_H

#ifdef _MSC_VER
    /* There is no <endian.h> for MSVC but it is always little endian. */
    #ifndef __LITTLE_ENDIAN__
        # undef __BIG_ENDIAN__
        # define __LITTLE_ENDIAN__
    #endif
#endif

#ifdef _MSC_VER
    #define KI_C_INLINE __inline
#else
    #define KI_C_INLINE inline
#endif

#endif
