//
//  format.hpp
//  Compress
//
//  Created by DB on 2017-12-5.
//  Copyright © 2017 D Blalock. All rights reserved.
//

#ifndef format_hpp
#define format_hpp

#include <stdint.h>

#include "util.h"  // just for DIV_ROUND_UP

// template<typename int_t>
// uint16_t write_metadata_rle(int_t* dest, uint16_t ndims, uint32_t ngroups,
//     uint16_t remaining_len);

// template<typename int_t>
// uint16_t read_metadata_rle(const int_t* src, uint16_t* p_ndims,
//     uint64_t* p_ngroups, uint16_t* p_remaining_len);

// template<typename int_t>
// uint16_t write_metadata_simple(int_t* dest, uint16_t ndims, uint32_t len);

// template<typename int_t>
// uint16_t read_metadata_simple(const int_t* src, uint16_t* p_ndims,
//     uint32_t* p_len);

#define kMetaDataLenBytesRle 8
#define kMetaDataLenBytesSimple 6

template<typename int_t>
static inline uint16_t write_metadata_rle(int_t* orig_dest, uint16_t ndims,
    uint32_t ngroups, uint16_t remaining_len)
{
    *(uint32_t*)orig_dest = ngroups;
    *(uint16_t*)(orig_dest + 4) = (uint16_t)remaining_len;
    *(uint16_t*)(orig_dest + 6) = ndims;

    return DIV_ROUND_UP(kMetaDataLenBytesRle, sizeof(int_t));
}

template<typename int_t>
static inline uint16_t read_metadata_rle(const int_t* src, uint16_t* p_ndims,
    uint64_t* p_ngroups, uint16_t* p_remaining_len)
{
    static const uint8_t elem_sz = sizeof(int_t);
    static const uint32_t len_nbytes = 4;
    uint64_t one = 1; // make next line legible
    uint64_t len_mask = (one << (8 * len_nbytes)) - 1;
    *p_ngroups = (*(uint64_t*)src) & len_mask;
    *p_remaining_len = (*(uint16_t*)(src + len_nbytes));
    *p_ndims = (*(uint16_t*)(src + len_nbytes + 2));

    return DIV_ROUND_UP(kMetaDataLenBytesRle, sizeof(int_t));
}

template<typename int_t>
static inline uint16_t write_metadata_simple(int_t* dest, uint16_t ndims, uint32_t len) {
    static const uint8_t elem_sz = sizeof(int_t);
    uint8_t* _dest = (uint8_t*)dest;
    *(uint32_t*)_dest = len;
    *(uint16_t*)(_dest + 4) = ndims;

    return DIV_ROUND_UP(kMetaDataLenBytesSimple, sizeof(int_t));
}

template<typename int_t>
static inline uint16_t read_metadata_simple(const int_t* src, uint16_t* p_ndims,
    uint32_t* p_len)
{
    static const uint8_t elem_sz = sizeof(int_t);
    uint8_t* _src = (uint8_t*)src;
    *p_len = *(uint32_t*)_src;
    *p_ndims = *(uint16_t*)(_src + 4);

    return DIV_ROUND_UP(kMetaDataLenBytesSimple, sizeof(int_t));
    // uint8_t len_nbytes = kMetaDataLenBytesSimple;
    // return (len_nbytes / elem_sz) + ((len_nbytes % elem_sz) > 0);
}

// ------------------------------------------------ 8b wrappers

uint16_t write_metadata_rle_8b(int8_t* dest, uint16_t ndims, uint32_t ngroups,
    uint16_t remaining_len);

uint16_t read_metadata_rle_8b(const int8_t* src, uint16_t* p_ndims,
    uint64_t* p_ngroups, uint16_t* p_remaining_len);

uint16_t write_metadata_simple_8b(int8_t* dest, uint16_t ndims, uint32_t len);

uint16_t read_metadata_simple_8b(const int8_t* src, uint16_t* p_ndims,
    uint32_t* p_len);

// ------------------------------------------------ 16b wrappers

uint16_t write_metadata_rle_16b(int8_t* dest, uint16_t ndims, uint32_t ngroups,
    uint16_t remaining_len);

uint16_t read_metadata_rle_16b(const int8_t* src, uint16_t* p_ndims,
    uint64_t* p_ngroups, uint16_t* p_remaining_len);

uint16_t write_metadata_simple_16b(int16_t* dest, uint16_t ndims, uint32_t len);

uint16_t read_metadata_simple_16b(const int16_t* src, uint16_t* p_ndims,
    uint32_t* p_len);

#endif /* format_hpp */