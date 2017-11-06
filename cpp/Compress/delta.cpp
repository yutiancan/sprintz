//
//  delta.cpp
//  Compress
//
//  Created by DB on 11/1/17.
//  Copyright © 2017 D Blalock. All rights reserved.
//

#include "delta.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "immintrin.h"

#include "debug_utils.hpp" // TODO rm
#include "util.h"

uint32_t encode_delta_rowmajor(const uint8_t* src, uint32_t len, int8_t* dest,
                               uint16_t ndims, bool write_size)
{
    static const uint8_t vector_sz = 32;
    static const uint8_t block_sz = 8;

    int8_t* orig_dest = dest;

    uint32_t block_sz_elems = block_sz * ndims;
    uint32_t nrows = len / ndims;
    uint32_t nblocks = nrows / block_sz;
    uint32_t padded_ndims = round_up_to_multiple(ndims, vector_sz);
    uint16_t nvectors = padded_ndims / vector_sz + ((padded_ndims % vector_sz) > 0);

    uint8_t* prev_vals_ar = (uint8_t*)calloc(vector_sz, nvectors);

    if (write_size) {
        *(uint32_t*)dest = len;
        dest += 4;
        *(uint16_t*)dest = ndims;
        dest += 2;
        orig_dest = dest; // NOTE: we don't do this in any other function
    }

    // printf("-------- compression (len = %lld, ndims = %d)\n", (int64_t)len, ndims);
    // printf("saw original data:\n"); dump_bytes(src, len, ndims);
    // printf("saw original data:\n"); dump_bytes(src, ndims * 8, ndims);

    // nblocks = 0; // TODO rm

    // uint16_t overrun_ndims = ndims % vector_sz;
    // uint32_t npad_bytes = overrun_ndims * block_sz;

    // uint32_t npad_blocks = (npad_bytes / block_sz_elems) + ((npad_bytes % block_sz_elems) > 0);
    // nblocks -= MIN(nblocks, npad_blocks);
    // // if (len < vector_sz * block_sz) { nblocks = 0; }

    // printf("overrun ndims: %d\n", overrun_ndims);

    // ensure we don't write past the end of the output
    uint16_t overrun_ndims = vector_sz - (ndims % vector_sz);
    uint32_t trailing_nelements = len % block_sz_elems;
    if (nblocks > 1 && overrun_ndims > trailing_nelements) { nblocks -= 1; }
    // if (nblocks > 1) { nblocks -= 1; }

    // printf("using nblocks, nvectors: %d, %d\n", nblocks, nvectors);

    // nblocks = 0;

    for (int32_t b = 0; b < nblocks; b++) { // for each block
        for (int32_t v = nvectors - 1; v >= 0; v--) { // for each stripe
            __m256i* prev_vals_ptr = (__m256i*)(prev_vals_ar + v * vector_sz);
            __m256i prev_vals = _mm256_loadu_si256(prev_vals_ptr);
            for (uint8_t i = 0; i < block_sz; i++) {
                const uint8_t* in_ptr = src + i * ndims + v * vector_sz;
                int8_t* out_ptr = dest + i * ndims + v * vector_sz;
                __m256i vals = _mm256_loadu_si256((__m256i*)in_ptr);
                __m256i vdeltas = _mm256_sub_epi8(vals, prev_vals);
                _mm256_storeu_si256((__m256i*)out_ptr, vdeltas);
                // hacky move instruction
                // prev_vals = _mm256_xor_si256(vals, _mm256_setzero_si256());
                prev_vals = vals;
            }
            _mm256_storeu_si256((__m256i*)(prev_vals_ptr), prev_vals);
        } // for each vector
        src += block_sz_elems;
        dest += block_sz_elems;
    } // for each block

    // delta code trailing elements serially; note that if we jump straight
    // to this section, we need to not read past the beginning of the input
    if (nblocks == 0) {
        uint32_t cpy_len = MIN(ndims, len);
        memcpy(dest, src, cpy_len);
        dest += ndims;
        src += ndims;
    }
    // printf("copying trailing %d bytes\n", (int)((orig_dest + len) - dest));
    for (; dest < (orig_dest + len); ++dest) {
        *dest = *(src) - *(src - ndims);
        src++;
    }

    free(prev_vals_ar);
    if (write_size) { return len + 6; }
    return len;
}

template<int ndims>
uint32_t decode_delta_rowmajor_small_ndims(const int8_t* src, uint32_t len,
    uint8_t* dest)
{
    uint8_t* orig_dest = dest;

    uint32_t cpy_len = MIN(ndims, len);
    memcpy(dest, src, cpy_len);
    dest += ndims;
    src += ndims;
    for (; dest < (orig_dest + len); ++dest) {
        *dest = *src + *(dest - ndims);
        src++;
    }
    return len;
}

inline int64_t decode_delta_serial(const int8_t* src, uint8_t* dest,
    const uint8_t* dest_end, uint16_t lag, bool needs_initial_cpy)
{
    int64_t len = (int64_t)(dest_end - dest);
    if (len < 1) { return - 1; }

    if (needs_initial_cpy) {
        int64_t cpy_len = MIN(lag, len);
        memcpy(dest, src, cpy_len);
        dest += lag;
        src += lag;
    }
    for (; dest < dest_end; ++dest) {
        *dest = *src + *(dest - lag);
        src++;
    }
    return len;
}

uint32_t decode_delta_rowmajor_large_ndims(const int8_t* src, uint32_t len,
    uint8_t* dest, uint16_t ndims)
{
    static const uint8_t vector_sz = 32;
    static const uint8_t block_sz = 8;
    uint8_t* orig_dest = dest;
    // const int8_t* orig_src = src;

    if (ndims == 0) { return 0; }

    uint32_t block_sz_elems = block_sz * ndims;
    uint32_t nrows = len / ndims;
    uint32_t nblocks = nrows / block_sz;
    uint32_t padded_ndims = round_up_to_multiple(ndims, vector_sz);
    uint16_t nvectors = padded_ndims / vector_sz + ((padded_ndims % vector_sz) > 0);

    uint8_t* prev_vals_ar = (uint8_t*)calloc(vector_sz, nvectors);

    // ensure we don't write past the end of the output
    uint16_t overrun_ndims = vector_sz - (ndims % vector_sz);
    uint32_t trailing_nelements = len % block_sz_elems;
    if (nblocks > 1 && overrun_ndims > trailing_nelements) { nblocks -= 1; }

    for (uint32_t b = 0; b < nblocks; b++) { // for each block
        const int8_t* block_in_ptr = src + (nvectors - 1) * vector_sz;
        uint8_t* block_out_ptr = dest + (nvectors - 1) * vector_sz;
        for (int32_t v = nvectors - 1; v >= 0; v--) { // for each stripe
            __m256i* prev_vals_ptr = (__m256i*)(prev_vals_ar + v * vector_sz);
            __m256i prev_vals = _mm256_loadu_si256(prev_vals_ptr);
            const int8_t* in_ptr = block_in_ptr;
            uint8_t* out_ptr = block_out_ptr;
            for (uint8_t i = 0; i < block_sz; i++) {
                __m256i errs = _mm256_loadu_si256((__m256i*)in_ptr);
                __m256i vals = _mm256_add_epi8(errs, prev_vals);
                _mm256_storeu_si256((__m256i*)out_ptr, vals);
                prev_vals = vals;
                in_ptr += ndims;
                out_ptr += ndims;
            }
            _mm256_storeu_si256((__m256i*)(prev_vals_ptr), prev_vals);
            block_in_ptr -= vector_sz;
            block_out_ptr -= vector_sz;
        } // for each vector
        src += block_sz_elems;
        dest += block_sz_elems;
    } // for each block

    // undo delta coding for trailing elements serially
    decode_delta_serial(src, dest, orig_dest + len, ndims, nblocks == 0);
    // if (nblocks == 0) {
    //     uint32_t cpy_len = MIN(ndims, len);
    //     memcpy(dest, src, cpy_len);
    //     dest += ndims;
    //     src += ndims;
    // }
    // for (; dest < (orig_dest + len); ++dest) {
    //     *dest = *src + *(dest - ndims);
    //     src++;
    // }
    free(prev_vals_ar);
    return len;
}

template<int ndims> // TODO same body as prev func; maybe put in macro?
uint32_t decode_delta_rowmajor(const int8_t* src, uint32_t len, uint8_t* dest) {
    static const uint8_t vector_sz = 32;
    static const uint8_t block_sz = 8;
    uint8_t* orig_dest = dest;
    // const int8_t* orig_src = src;

    uint32_t block_sz_elems = block_sz * ndims;
    uint32_t nrows = len / ndims;
    uint32_t nblocks = nrows / block_sz;
    uint32_t padded_ndims = round_up_to_multiple(ndims, vector_sz);
    uint16_t nvectors = padded_ndims / vector_sz + ((padded_ndims % vector_sz) > 0);

    uint8_t* prev_vals_ar = (uint8_t*)calloc(vector_sz, nvectors);

    // printf("-------- decompression (len = %lld, ndims = %d)\n", (int64_t)len, ndims);
    // printf("saw compressed data:\n");
    // dump_bytes(src, len, ndims);
    // dump_bytes(src, ndims * 8, ndims);

    // ensure we don't write past the end of the output
    uint16_t overrun_ndims = vector_sz - (ndims % vector_sz);
    uint32_t trailing_nelements = len % block_sz_elems;
    if (nblocks > 1 && overrun_ndims > trailing_nelements) { nblocks -= 1; }

    // nblocks = 0;
    // printf("using nblocks: %d\n", nblocks);

    for (uint32_t b = 0; b < nblocks; b++) { // for each block
        const int8_t* block_in_ptr = src + (nvectors - 1) * vector_sz;
        uint8_t* block_out_ptr = dest + (nvectors - 1) * vector_sz;
        for (int32_t v = nvectors - 1; v >= 0; v--) { // for each stripe
            __m256i* prev_vals_ptr = (__m256i*)(prev_vals_ar + v * vector_sz);
            __m256i prev_vals = _mm256_loadu_si256(prev_vals_ptr);
            const int8_t* in_ptr = block_in_ptr;
            uint8_t* out_ptr = block_out_ptr;
            for (uint8_t i = 0; i < block_sz; i++) {
                // const int8_t* in_ptr = src + i * ndims + v * vector_sz;
                // uint8_t* out_ptr = dest + i * ndims + v * vector_sz;
                // const int8_t* in_ptr = block_in_ptr + i * ndims;
                // uint8_t* out_ptr = block_out_ptr + i * ndims;
                // if (b == 0) { printf("---- i = %d (offset %d)\n", i, (int)(in_ptr - orig_src)); }
                __m256i errs = _mm256_loadu_si256((__m256i*)in_ptr);
                __m256i vals = _mm256_add_epi8(errs, prev_vals);
                // if (b == 0) { printf("vals:     "); dump_m256i(vals); }
                _mm256_storeu_si256((__m256i*)out_ptr, vals);
                prev_vals = vals;
                in_ptr += ndims;
                out_ptr += ndims;

                // __builtin_prefetch(in_ptr + block_sz_elems, 1);
                // __builtin_prefetch(out_ptr + block_sz_elems, 1);
            }
            _mm256_storeu_si256((__m256i*)(prev_vals_ptr), prev_vals);
            block_in_ptr -= vector_sz;
            block_out_ptr -= vector_sz;
        } // for each vector
        src += block_sz_elems;
        dest += block_sz_elems;
    } // for each block

    // undo delta coding for trailing elements serially
    decode_delta_serial(src, dest, orig_dest + len, ndims, nblocks == 0);
    // if (nblocks == 0) {
    //     uint32_t cpy_len = MIN(ndims, len);
    //     memcpy(dest, src, cpy_len);
    //     dest += ndims;
    //     src += ndims;
    // }
    // // printf("copying trailing %d bytes\n", (int)((orig_dest + len) - dest));
    // for (; dest < (orig_dest + len); ++dest) {
    //     *dest = *src + *(dest - ndims);
    //     src++;
    // }

    // printf("reconstructed input:\n");
    // dump_bytes(orig_dest, ndims * 8, ndims);

    free(prev_vals_ar);
    return len;
}

uint32_t decode_delta_rowmajor(const int8_t* src, uint32_t len, uint8_t* dest,
    uint16_t ndims)
{
    #define CASE(NDIMS) \
        case NDIMS: return decode_delta_rowmajor<NDIMS>(src, len, dest); break;

    #define FOUR_CASES(START) \
        CASE((START)); CASE((START+1)); CASE((START+2)); CASE((START+3));

    #define SIXTEEN_CASES(START)                        \
        FOUR_CASES(START); FOUR_CASES(START + 4);       \
        FOUR_CASES(START + 8); FOUR_CASES(START + 12);

    switch (ndims) {
        case 0: return decode_delta_rowmajor_small_ndims<0>(src, len, dest); break;
        case 1: return decode_delta_rowmajor_small_ndims<1>(src, len, dest); break;
        case 2: return decode_delta_rowmajor_small_ndims<2>(src, len, dest); break;
        CASE(3); CASE(4);
        FOUR_CASES(5); FOUR_CASES(9); FOUR_CASES(13); // cases 5-16
        SIXTEEN_CASES(16 + 1); SIXTEEN_CASES(32 + 1); SIXTEEN_CASES(48 + 1);
        default:
            return decode_delta_rowmajor_large_ndims(src, len, dest, ndims);
    }
    return 0; // This can never happen

    #undef CASE
    #undef FOUR_CASES
    #undef SIXTEEN_CASES
}

uint32_t decode_delta_rowmajor_inplace(uint8_t* buff, uint32_t len,
                                       uint16_t ndims)
{
    // TODO might go a bit faster if we batch the copies
    //
    // static const uint8_t vector_sz = 32;
    // static const uint8_t block_sz = 8;
    //
    // uint32_t block_sz_elems = block_sz * ndims;
    // uint32_t nrows = len / ndims;
    // uint32_t nblocks = nrows / block_sz;
    //
    // static const uint8_t batch_nblocks = 8;
    // uint32_t batch_size_elems = block_sz_elems * batch_nblocks;
    // uint8_t* tmp = (uint8_t*)malloc(batch_size_elems + vector_sz);
    // decode_delta_rowmajor((int8_t*)buff, len, tmp, ndims);

    uint8_t* tmp = (uint8_t*)malloc(len);
    uint32_t sz = decode_delta_rowmajor((int8_t*)buff, len, tmp, ndims);
    memcpy(buff, tmp, sz);
    free(tmp);
    return sz;
}

uint32_t decode_delta_rowmajor(const int8_t* src, uint8_t* dest) {
    uint32_t len = *(uint32_t*)src;
    src += 4;
    uint16_t ndims = *(uint16_t*)src;
    src += 2;
    return decode_delta_rowmajor(src, len, dest, ndims);
}

// ================================================================ double delta

uint32_t encode_doubledelta_rowmajor(const uint8_t* src, uint32_t len,
    int8_t* dest, uint16_t ndims, bool write_size)
{
    static const uint8_t vector_sz = 32;
    static const uint8_t block_sz = 8;

    int8_t* orig_dest = dest;

    uint32_t block_sz_elems = block_sz * ndims;
    uint32_t nrows = len / ndims;
    uint32_t nblocks = nrows / block_sz;
    uint32_t padded_ndims = round_up_to_multiple(ndims, vector_sz);
    uint16_t nvectors = padded_ndims / vector_sz + ((padded_ndims % vector_sz) > 0);

    uint8_t* prev_vals_ar = (uint8_t*)calloc(vector_sz, nvectors);

    if (write_size) {
        *(uint32_t*)dest = len;
        dest += 4;
        *(uint16_t*)dest = ndims;
        dest += 2;
        orig_dest = dest; // NOTE: we don't do this in any other function
    }

    // printf("-------- compression (len = %lld, ndims = %d)\n", (int64_t)len, ndims);
    // printf("saw original data:\n"); dump_bytes(src, len, ndims);
    // printf("saw original data:\n"); dump_bytes(src, ndims * 8, ndims);

    // ensure we don't write past the end of the output
    uint16_t overrun_ndims = vector_sz - (ndims % vector_sz);
    uint32_t trailing_nelements = len % block_sz_elems;
    if (nblocks > 1 && overrun_ndims > trailing_nelements) { nblocks -= 1; }

    for (int32_t b = 0; b < nblocks; b++) { // for each block
        for (int32_t v = nvectors - 1; v >= 0; v--) { // for each stripe
            __m256i* prev_vals_ptr = (__m256i*)(prev_vals_ar + v * vector_sz);
            __m256i prev_vals = _mm256_loadu_si256(prev_vals_ptr);
            for (uint8_t i = 0; i < block_sz; i++) {
                const uint8_t* in_ptr = src + i * ndims + v * vector_sz;
                int8_t* out_ptr = dest + i * ndims + v * vector_sz;
                __m256i vals = _mm256_loadu_si256((__m256i*)in_ptr);
                __m256i vdeltas = _mm256_sub_epi8(vals, prev_vals);
                _mm256_storeu_si256((__m256i*)out_ptr, vdeltas);
                // hacky move instruction
                // prev_vals = _mm256_xor_si256(vals, _mm256_setzero_si256());
                prev_vals = vals;
            }
            _mm256_storeu_si256((__m256i*)(prev_vals_ptr), prev_vals);
        } // for each vector
        src += block_sz_elems;
        dest += block_sz_elems;
    } // for each block

    // delta code trailing elements serially; note that if we jump straight
    // to this section, we need to not read past the beginning of the input
    if (nblocks == 0) {
        uint32_t cpy_len = MIN(ndims, len);
        memcpy(dest, src, cpy_len);
        dest += ndims;
        src += ndims;
    }
    // printf("copying trailing %d bytes\n", (int)((orig_dest + len) - dest));
    for (; dest < (orig_dest + len); ++dest) {
        *dest = *(src) - *(src - ndims);
        src++;
    }

    free(prev_vals_ar);
    if (write_size) { return len + 6; }
    return len;
}

uint32_t decode_doubledelta_rowmajor(const int8_t* src, uint32_t len,
    uint8_t* dest, uint16_t ndims)
{
    static const uint8_t vector_sz = 32;
    static const uint8_t block_sz = 8;
    uint8_t* orig_dest = dest;
    // const int8_t* orig_src = src;

    if (ndims == 0) { return 0; }

    uint32_t block_sz_elems = block_sz * ndims;
    uint32_t nrows = len / ndims;
    uint32_t nblocks = nrows / block_sz;
    uint32_t padded_ndims = round_up_to_multiple(ndims, vector_sz);
    uint16_t nvectors = padded_ndims / vector_sz + ((padded_ndims % vector_sz) > 0);

    uint8_t* prev_vals_ar = (uint8_t*)calloc(vector_sz, nvectors);

    // ensure we don't write past the end of the output
    uint16_t overrun_ndims = vector_sz - (ndims % vector_sz);
    uint32_t trailing_nelements = len % block_sz_elems;
    if (nblocks > 1 && overrun_ndims > trailing_nelements) { nblocks -= 1; }

    for (uint32_t b = 0; b < nblocks; b++) { // for each block
        for (int32_t v = nvectors - 1; v >= 0; v--) { // for each stripe
            __m256i* prev_vals_ptr = (__m256i*)(prev_vals_ar + v * vector_sz);
            __m256i prev_vals = _mm256_loadu_si256(prev_vals_ptr);
            for (uint8_t i = 0; i < block_sz; i++) {
                const int8_t* in_ptr = src + i * ndims + v * vector_sz;
                uint8_t* out_ptr = dest + i * ndims + v * vector_sz;
                __m256i errs = _mm256_loadu_si256((__m256i*)in_ptr);
                __m256i vals = _mm256_add_epi8(errs, prev_vals);
                _mm256_storeu_si256((__m256i*)out_ptr, vals);
                prev_vals = vals;
            }
            _mm256_storeu_si256((__m256i*)(prev_vals_ptr), prev_vals);
        } // for each vector
        src += block_sz_elems;
        dest += block_sz_elems;
    } // for each block

    // undo delta coding for trailing elements serially
    decode_delta_serial(src, dest, orig_dest + len, ndims, nblocks == 0);
    // if (nblocks == 0) {
    //     uint32_t cpy_len = MIN(ndims, len);
    //     memcpy(dest, src, cpy_len);
    //     dest += ndims;
    //     src += ndims;
    // }
    // for (; dest < (orig_dest + len); ++dest) {
    //     *dest = *src + *(dest - ndims);
    //     src++;
    // }
    free(prev_vals_ar);
    return len;
}

uint32_t decode_doubledelta_rowmajor_inplace(uint8_t* buff, uint32_t len,
                                       uint16_t ndims)
{
    uint8_t* tmp = (uint8_t*)malloc(len);
    uint32_t sz = decode_doubledelta_rowmajor((int8_t*)buff, len, tmp, ndims);
    memcpy(buff, tmp, sz);
    free(tmp);
    return sz;
}
uint32_t decode_doubledelta_rowmajor(const int8_t* src, uint8_t* dest) {
    uint32_t len = *(uint32_t*)src;
    src += 4;
    uint16_t ndims = *(uint16_t*)src;
    src += 2;
    return decode_doubledelta_rowmajor(src, len, dest, ndims);
}

#undef MIN
