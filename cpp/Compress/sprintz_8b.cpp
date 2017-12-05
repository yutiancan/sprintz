//
//  sprintz_8b.cpp
//  Compress
//
//  Created by DB on 12/4/17.
//  Copyright © 2017 D Blalock. All rights reserved.
//

#include "sprintz.h"

#include <stdio.h>

// #include "macros.h"
#include "sprintz_delta.h"


#define LOW_DIMS_CASE
#define CASE(X)

#define FOUR_CASES(START) \
    CASE((START)); CASE((START+1)); CASE((START+2)); CASE((START+3));

#define SIXTEEN_CASES(START)                                                \
    FOUR_CASES(START); FOUR_CASES(START + 4);                               \
    FOUR_CASES(START + 8); FOUR_CASES(START + 12);

#define CASES_5_AND_UP(DEFAULT_CALL)                                        \
    FOUR_CASES(5); FOUR_CASES(9); FOUR_CASES(13);                           \
    SIXTEEN_CASES(16 + 1); SIXTEEN_CASES(32 + 1); SIXTEEN_CASES(48 + 1);    \
    default:                                                                \
        return (DEFAULT_CALL);

#define SWITCH_ON_NDIMS(NDIMS, DEFAULT_CALL)                                \
    switch (NDIMS) {                                                        \
        case 0: printf("Received invalid ndims %d\n", NDIMS); break;        \
        LOW_DIMS_CASE(1); LOW_DIMS_CASE(2);                                 \
        LOW_DIMS_CASE(3); LOW_DIMS_CASE(4);                                 \
        CASES_5_AND_UP(DEFAULT_CALL)                                        \
    };                                                                      \
    return -1; /* unreachable */

#undef LOW_DIMS_CASE
#undef CASE

int64_t sprintz_compress_delta_8b(const uint8_t* src, uint32_t len, int8_t* dest,
                                  uint16_t ndims, bool write_size)
{
    // #undef LOW_DIMS_CASE
    #define LOW_DIMS_CASE(N)                                    \
        case N: return compress8b_rowmajor_delta_rle_lowdim(    \
            src, len, dest, N, write_size);

    #define CASE(N)                                             \
        case N: return compress8b_rowmajor_delta_rle(           \
            src, len, dest, N, write_size);

    SWITCH_ON_NDIMS(ndims, compress8b_rowmajor_delta_rle(
        src, len, dest, ndims, write_size));

    #undef LOW_DIMS_CASE
    #undef CASE
}
int64_t sprintz_decompress_delta_8b(const int8_t* src, uint8_t* dest) {
    uint16_t ndims;
    uint64_t ngroups;
    uint16_t remaining_len;
    src += read_metadata_rle(src, &ndims, &ngroups, &remaining_len);

    #define LOW_DIMS_CASE(NDIMS)                                        \
        case NDIMS: return decompress8b_rowmajor_delta_rle_lowdim(      \
            src, dest, NDIMS, ngroups, remaining_len);

    #define CASE(NDIMS)                                                 \
        case NDIMS: return decompress8b_rowmajor_delta_rle(             \
            src, dest, NDIMS, ngroups, remaining_len);

    SWITCH_ON_NDIMS(ndims, decompress8b_rowmajor_delta_rle(
        src, dest, ndims, ngroups, remaining_len));

    #undef LOW_DIMS_CASE
    #undef CASE
}

int64_t sprintz_compress_xff_8b(const uint8_t* src, uint32_t len, int8_t* dest,
                                  uint16_t ndims, bool write_size)
{
    return -1;
}
int64_t sprintz_decompress_xff_8b(const int8_t* src, uint8_t* dest) {
    return -1;
}
