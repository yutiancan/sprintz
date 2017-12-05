//
//  format.cpp
//  Compress
//
//  Created by DB on 12/5/17.
//  Copyright © 2017 D Blalock. All rights reserved.
//

#include "format.h"

static const uint16_t kMetaDataLenBytesRle = 8;

uint16_t write_metadata_rle(int8_t* orig_dest, uint16_t ndims,
    uint32_t ngroups, uint16_t remaining_len)
{
    *(uint32_t*)orig_dest = ngroups;
    *(uint16_t*)(orig_dest + 4) = (uint16_t)remaining_len;
    *(uint16_t*)(orig_dest + 6) = ndims;
    return kMetaDataLenBytesRle;
}

uint16_t read_metadata_rle(const int8_t* src, uint16_t* p_ndims,
    uint64_t* p_ngroups, uint16_t* p_remaining_len)
{
    static const uint32_t len_nbytes = 4;
    uint64_t one = 1; // make next line legible
    uint64_t len_mask = (one << (8 * len_nbytes)) - 1;
    *p_ngroups = (*(uint64_t*)src) & len_mask;
    *p_remaining_len = (*(uint16_t*)(src + len_nbytes));
    *p_ndims = (*(uint16_t*)(src + len_nbytes + 2));

    return kMetaDataLenBytesRle; // bytes taken up by metadata
}
