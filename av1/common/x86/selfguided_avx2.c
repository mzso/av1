/*
 * Copyright (c) 2018, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <immintrin.h>

#include "./aom_config.h"
#include "./av1_rtcd.h"
#include "av1/common/restoration.h"
#include "aom_dsp/x86/synonyms.h"
#include "aom_dsp/x86/synonyms_avx2.h"

// Load 8 bytes from the possibly-misaligned pointer p, extend each byte to
// 32-bit precision and return them in an AVX2 register.
static __m256i yy256_load_extend_8_32(const void *p) {
  return _mm256_cvtepu8_epi32(xx_loadl_64(p));
}

// Load 8 halfwords from the possibly-misaligned pointer p, extend each
// halfword to 32-bit precision and return them in an AVX2 register.
static __m256i yy256_load_extend_16_32(const void *p) {
  return _mm256_cvtepu16_epi32(xx_loadu_128(p));
}

// Compute the scan of an AVX2 register holding 8 32-bit integers. If the
// register holds x0..x7 then the scan will hold x0, x0+x1, x0+x1+x2, ...,
// x0+x1+...+x7
//
// Let [...] represent a 128-bit block, and let a, ..., h be 32-bit integers
// (assumed small enough to be able to add them without overflow).
//
// Use -> as shorthand for summing, i.e. h->a = h + g + f + e + d + c + b + a.
//
// x   = [h g f e][d c b a]
// x01 = [g f e 0][c b a 0]
// x02 = [g+h f+g e+f e][c+d b+c a+b a]
// x03 = [e+f e 0 0][a+b a 0 0]
// x04 = [e->h e->g e->f e][a->d a->c a->b a]
// s   = a->d
// s01 = [a->d a->d a->d a->d]
// s02 = [a->d a->d a->d a->d][0 0 0 0]
// ret = [a->h a->g a->f a->e][a->d a->c a->b a]
static __m256i scan_32(__m256i x) {
  const __m256i x01 = _mm256_slli_si256(x, 4);
  const __m256i x02 = _mm256_add_epi32(x, x01);
  const __m256i x03 = _mm256_slli_si256(x02, 8);
  const __m256i x04 = _mm256_add_epi32(x02, x03);
  const int32_t s = _mm256_extract_epi32(x04, 3);
  const __m128i s01 = _mm_set1_epi32(s);
  const __m256i s02 = _mm256_insertf128_si256(_mm256_setzero_si256(), s01, 1);
  return _mm256_add_epi32(x04, s02);
}

// Compute two integral images from src. B sums elements; A sums their
// squares. The images are offset by one pixel, so will have width and height
// equal to width + 1, height + 1 and the first row and column will be zero.
//
// A+1 and B+1 should be aligned to 32 bytes. buf_stride should be a multiple
// of 8.
static void integral_images(const uint8_t *src, int src_stride, int width,
                            int height, int32_t *A, int32_t *B,
                            int buf_stride) {
  // Write out the zero top row
  memset(A, 0, sizeof(*A) * (width + 1));
  memset(B, 0, sizeof(*B) * (width + 1));

  const __m256i zero = _mm256_setzero_si256();
  for (int i = 0; i < height; ++i) {
    // Zero the left column.
    A[(i + 1) * buf_stride] = B[(i + 1) * buf_stride] = 0;

    // ldiff is the difference H - D where H is the output sample immediately
    // to the left and D is the output sample above it. These are scalars,
    // replicated across the eight lanes.
    __m256i ldiff1 = zero, ldiff2 = zero;
    for (int j = 0; j < width; j += 8) {
      const int ABj = 1 + j;

      const __m256i above1 = yy_load_256(B + ABj + i * buf_stride);
      const __m256i above2 = yy_load_256(A + ABj + i * buf_stride);

      const __m256i x1 = yy256_load_extend_8_32(src + j + i * src_stride);
      const __m256i x2 = _mm256_madd_epi16(x1, x1);

      const __m256i sc1 = scan_32(x1);
      const __m256i sc2 = scan_32(x2);

      const __m256i row1 =
          _mm256_add_epi32(_mm256_add_epi32(sc1, above1), ldiff1);
      const __m256i row2 =
          _mm256_add_epi32(_mm256_add_epi32(sc2, above2), ldiff2);

      yy_store_256(B + ABj + (i + 1) * buf_stride, row1);
      yy_store_256(A + ABj + (i + 1) * buf_stride, row2);

      // Calculate the new H - D.
      ldiff1 = _mm256_set1_epi32(
          _mm256_extract_epi32(_mm256_sub_epi32(row1, above1), 7));
      ldiff2 = _mm256_set1_epi32(
          _mm256_extract_epi32(_mm256_sub_epi32(row2, above2), 7));
    }
  }
}

// Compute two integral images from src. B sums elements; A sums their squares
//
// A and B should be aligned to 32 bytes. buf_stride should be a multiple of 8.
static void integral_images_highbd(const uint16_t *src, int src_stride,
                                   int width, int height, int32_t *A,
                                   int32_t *B, int buf_stride) {
  // Write out the zero top row
  memset(A, 0, sizeof(*A) * (width + 1));
  memset(B, 0, sizeof(*B) * (width + 1));

  const __m256i zero = _mm256_setzero_si256();
  for (int i = 0; i < height; ++i) {
    // Zero the left column.
    A[(i + 1) * buf_stride] = B[(i + 1) * buf_stride] = 0;

    // ldiff is the difference H - D where H is the output sample immediately
    // to the left and D is the output sample above it. These are scalars,
    // replicated across the eight lanes.
    __m256i ldiff1 = zero, ldiff2 = zero;
    for (int j = 0; j < width; j += 8) {
      const int ABj = 1 + j;

      const __m256i above1 = yy_load_256(B + ABj + i * buf_stride);
      const __m256i above2 = yy_load_256(A + ABj + i * buf_stride);

      const __m256i x1 = yy256_load_extend_16_32(src + j + i * src_stride);
      const __m256i x2 = _mm256_madd_epi16(x1, x1);

      const __m256i sc1 = scan_32(x1);
      const __m256i sc2 = scan_32(x2);

      const __m256i row1 =
          _mm256_add_epi32(_mm256_add_epi32(sc1, above1), ldiff1);
      const __m256i row2 =
          _mm256_add_epi32(_mm256_add_epi32(sc2, above2), ldiff2);

      yy_store_256(B + ABj + (i + 1) * buf_stride, row1);
      yy_store_256(A + ABj + (i + 1) * buf_stride, row2);

      // Calculate the new H - D.
      ldiff1 = _mm256_set1_epi32(
          _mm256_extract_epi32(_mm256_sub_epi32(row1, above1), 7));
      ldiff2 = _mm256_set1_epi32(
          _mm256_extract_epi32(_mm256_sub_epi32(row2, above2), 7));
    }
  }
}

// Compute four values of boxsum from the given integral image. ii should point
// at the middle of the box (for the first value). r is the box radius
static __m256i boxsum_from_ii(const int32_t *ii, int stride, int r) {
  const __m256i tl = yy_loadu_256(ii - (r + 1) - (r + 1) * stride);
  const __m256i tr = yy_loadu_256(ii + (r + 0) - (r + 1) * stride);
  const __m256i bl = yy_loadu_256(ii - (r + 1) + r * stride);
  const __m256i br = yy_loadu_256(ii + (r + 0) + r * stride);
  const __m256i u = _mm256_sub_epi32(tr, tl);
  const __m256i v = _mm256_sub_epi32(br, bl);
  return _mm256_sub_epi32(v, u);
}

static __m256i round_for_shift(unsigned shift) {
  return _mm256_set1_epi32((1 << shift) >> 1);
}

static __m256i compute_p(__m256i sum1, __m256i sum2, int bit_depth, int n) {
  __m256i an, bb;
  if (bit_depth > 8) {
    const __m256i rounding_a = round_for_shift(2 * (bit_depth - 8));
    const __m256i rounding_b = round_for_shift(bit_depth - 8);
    const __m128i shift_a = _mm_cvtsi32_si128(2 * (bit_depth - 8));
    const __m128i shift_b = _mm_cvtsi32_si128(bit_depth - 8);
    const __m256i a =
        _mm256_srl_epi32(_mm256_add_epi32(sum2, rounding_a), shift_a);
    const __m256i b =
        _mm256_srl_epi32(_mm256_add_epi32(sum1, rounding_b), shift_b);
    // b < 2^14, so we can use a 16-bit madd rather than a 32-bit
    // mullo to square it
    bb = _mm256_madd_epi16(b, b);
    an = _mm256_max_epi32(_mm256_mullo_epi32(a, _mm256_set1_epi32(n)), bb);
  } else {
    bb = _mm256_madd_epi16(sum1, sum1);
    an = _mm256_mullo_epi32(sum2, _mm256_set1_epi32(n));
  }
  return _mm256_sub_epi32(an, bb);
}

// Assumes that C, D are integral images for the original buffer which has been
// extended to have a padding of SGRPROJ_BORDER_VERT/SGRPROJ_BORDER_HORZ pixels
// on the sides. A, B, C, D point at logical position (0, 0).
static void calc_ab(int32_t *A, int32_t *B, const int32_t *C, const int32_t *D,
                    int width, int height, int buf_stride, int eps,
                    int bit_depth, int r) {
  const int n = (2 * r + 1) * (2 * r + 1);
  const __m256i s = _mm256_set1_epi32(sgrproj_mtable[eps - 1][n - 1]);
  // one_over_n[n-1] is 2^12/n, so easily fits in an int16
  const __m256i one_over_n = _mm256_set1_epi32(one_by_x[n - 1]);

  const __m256i rnd_z = round_for_shift(SGRPROJ_MTABLE_BITS);
  const __m256i rnd_res = round_for_shift(SGRPROJ_RECIP_BITS);

  for (int i = -1; i < height + 1; ++i) {
    for (int j = -1; j < width + 1; j += 8) {
      const int32_t *Cij = C + i * buf_stride + j;
      const int32_t *Dij = D + i * buf_stride + j;

      const __m256i pre_sum1 = boxsum_from_ii(Dij, buf_stride, r);
      const __m256i pre_sum2 = boxsum_from_ii(Cij, buf_stride, r);

#if CONFIG_DEBUG
      // When width + 2 isn't a multiple of eight, z will contain some
      // uninitialised data in its upper words. This isn't really a problem
      // (they will be clamped to safe indices by the min() below, and will be
      // written to memory locations that we don't read again), but Valgrind
      // complains because we're using an uninitialised value as the address
      // for a load operation
      //
      // This mask is reasonably cheap to compute and quiets the warnings. Note
      // that we can't mask p instead of sum1 and sum2 (which would be cheaper)
      // because Valgrind gets the taint propagation in compute_p wrong.

      const __m128i ones32 = _mm_set_epi64x(0, 0xffffffffffffffffULL);
      const __m128i shift =
          _mm_set_epi64x(0, AOMMAX(0, 8 * (8 - (width + 1 - j))));
      const __m256i mask = _mm256_cvtepi8_epi32(_mm_srl_epi64(ones32, shift));
      const __m256i sum1 = _mm256_and_si256(mask, pre_sum1);
      const __m256i sum2 = _mm256_and_si256(mask, pre_sum2);
#else
      const __m256i sum1 = pre_sum1;
      const __m256i sum2 = pre_sum2;
#endif  // CONFIG_DEBUG

      const __m256i p = compute_p(sum1, sum2, bit_depth, n);

      const __m256i z = _mm256_min_epi32(
          _mm256_srli_epi32(_mm256_add_epi32(_mm256_mullo_epi32(p, s), rnd_z),
                            SGRPROJ_MTABLE_BITS),
          _mm256_set1_epi32(255));

      const __m256i a_res = _mm256_i32gather_epi32(x_by_xplus1, z, 4);

      yy_storeu_256(A + i * buf_stride + j, a_res);

      const __m256i a_complement =
          _mm256_sub_epi32(_mm256_set1_epi32(SGRPROJ_SGR), a_res);

      // sum1 might have lanes greater than 2^15, so we can't use madd to do
      // multiplication involving sum1. However, a_complement and one_over_n
      // are both less than 256, so we can multiply them first.
      const __m256i a_comp_over_n = _mm256_madd_epi16(a_complement, one_over_n);
      const __m256i b_int = _mm256_mullo_epi32(a_comp_over_n, sum1);
      const __m256i b_res = _mm256_srli_epi32(_mm256_add_epi32(b_int, rnd_res),
                                              SGRPROJ_RECIP_BITS);

      yy_storeu_256(B + i * buf_stride + j, b_res);
    }
  }
}

// Calculate 4 values of the "cross sum" starting at buf. This is a 3x3 filter
// where the outer four corners have weight 3 and all other pixels have weight
// 4.
//
// Pixels are indexed as follows:
// xtl  xt   xtr
// xl    x   xr
// xbl  xb   xbr
//
// buf points to x
//
// fours = xl + xt + xr + xb + x
// threes = xtl + xtr + xbr + xbl
// cross_sum = 4 * fours + 3 * threes
//           = 4 * (fours + threes) - threes
//           = (fours + threes) << 2 - threes
static __m256i cross_sum(const int32_t *buf, int stride) {
  const __m256i xtl = yy_loadu_256(buf - 1 - stride);
  const __m256i xt = yy_loadu_256(buf - stride);
  const __m256i xtr = yy_loadu_256(buf + 1 - stride);
  const __m256i xl = yy_loadu_256(buf - 1);
  const __m256i x = yy_loadu_256(buf);
  const __m256i xr = yy_loadu_256(buf + 1);
  const __m256i xbl = yy_loadu_256(buf - 1 + stride);
  const __m256i xb = yy_loadu_256(buf + stride);
  const __m256i xbr = yy_loadu_256(buf + 1 + stride);

  const __m256i fours = _mm256_add_epi32(
      xl, _mm256_add_epi32(xt, _mm256_add_epi32(xr, _mm256_add_epi32(xb, x))));
  const __m256i threes =
      _mm256_add_epi32(xtl, _mm256_add_epi32(xtr, _mm256_add_epi32(xbr, xbl)));

  return _mm256_sub_epi32(_mm256_slli_epi32(_mm256_add_epi32(fours, threes), 2),
                          threes);
}

// The final filter for self-guided restoration. Computes a weighted average
// across A, B with "cross sums" (see cross_sum implementation above)
static void final_filter(int32_t *dst, int dst_stride, const int32_t *A,
                         const int32_t *B, int buf_stride, const void *dgd8,
                         int dgd_stride, int width, int height, int highbd) {
  const int nb = 5;
  const __m256i rounding =
      round_for_shift(SGRPROJ_SGR_BITS + nb - SGRPROJ_RST_BITS);
  const uint8_t *dgd_real =
      highbd ? (const uint8_t *)CONVERT_TO_SHORTPTR(dgd8) : dgd8;

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; j += 4) {
      const __m256i a = cross_sum(A + i * buf_stride + j, buf_stride);
      const __m256i b = cross_sum(B + i * buf_stride + j, buf_stride);

      const __m128i raw =
          xx_loadu_128(dgd_real + ((i * dgd_stride + j) << highbd));
      const __m256i src =
          highbd ? _mm256_cvtepu16_epi32(raw) : _mm256_cvtepu8_epi32(raw);

      __m256i v = _mm256_add_epi32(_mm256_madd_epi16(a, src), b);
      __m256i w = _mm256_srai_epi32(_mm256_add_epi32(v, rounding),
                                    SGRPROJ_SGR_BITS + nb - SGRPROJ_RST_BITS);

      yy_storeu_256(dst + i * dst_stride + j, w);
    }
  }
}

void av1_selfguided_restoration_avx2(const uint8_t *dgd8, int width, int height,
                                     int dgd_stride, int32_t *flt1,
                                     int32_t *flt2, int flt_stride,
                                     const sgr_params_type *params,
                                     int bit_depth, int highbd) {
  // The ALIGN_POWER_OF_TWO macro here ensures that column 1 of Atl, Btl,
  // Ctl and Dtl is 32-byte aligned.
  const int buf_elts = ALIGN_POWER_OF_TWO(RESTORATION_PROC_UNIT_PELS, 3);

  DECLARE_ALIGNED(32, int32_t,
                  buf[4 * ALIGN_POWER_OF_TWO(RESTORATION_PROC_UNIT_PELS, 3)]);
  memset(buf, 0, sizeof(buf));

  const int width_ext = width + 2 * SGRPROJ_BORDER_HORZ;
  const int height_ext = height + 2 * SGRPROJ_BORDER_VERT;

  // Adjusting the stride of A and B here appears to avoid bad cache effects,
  // leading to a significant speed improvement.
  // We also align the stride to a multiple of 32 bytes for efficiency.
  int buf_stride = ALIGN_POWER_OF_TWO(width_ext + 16, 3);

  // The "tl" pointers point at the top-left of the initialised data for the
  // array.
  int32_t *Atl = buf + 0 * buf_elts + 7;
  int32_t *Btl = buf + 1 * buf_elts + 7;
  int32_t *Ctl = buf + 2 * buf_elts + 7;
  int32_t *Dtl = buf + 3 * buf_elts + 7;

  // The "0" pointers are (- SGRPROJ_BORDER_VERT, -SGRPROJ_BORDER_HORZ). Note
  // there's a zero row and column in A, B (integral images), so we move down
  // and right one for them.
  const int buf_diag_border =
      SGRPROJ_BORDER_HORZ + buf_stride * SGRPROJ_BORDER_VERT;

  int32_t *A0 = Atl + 1 + buf_stride;
  int32_t *B0 = Btl + 1 + buf_stride;
  int32_t *C0 = Ctl + 1 + buf_stride;
  int32_t *D0 = Dtl + 1 + buf_stride;

  // Finally, A, B, C, D point at position (0, 0).
  int32_t *A = A0 + buf_diag_border;
  int32_t *B = B0 + buf_diag_border;
  int32_t *C = C0 + buf_diag_border;
  int32_t *D = D0 + buf_diag_border;

  const int dgd_diag_border =
      SGRPROJ_BORDER_HORZ + dgd_stride * SGRPROJ_BORDER_VERT;
  const uint8_t *dgd0 = dgd8 - dgd_diag_border;

  // Generate integral images from the input. C will contain sums of squares; D
  // will contain just sums
  if (highbd)
    integral_images_highbd(CONVERT_TO_SHORTPTR(dgd0), dgd_stride, width_ext,
                           height_ext, Ctl, Dtl, buf_stride);
  else
    integral_images(dgd0, dgd_stride, width_ext, height_ext, Ctl, Dtl,
                    buf_stride);

  // Write to flt1 and flt2
  for (int i = 0; i < 2; ++i) {
    int r = i ? params->r2 : params->r1;
    int e = i ? params->e2 : params->e1;
    int32_t *flt = i ? flt2 : flt1;

    assert(r + 1 <= AOMMIN(SGRPROJ_BORDER_VERT, SGRPROJ_BORDER_HORZ));
    calc_ab(A, B, C, D, width, height, buf_stride, e, bit_depth, r);
    final_filter(flt, flt_stride, A, B, buf_stride, dgd8, dgd_stride, width,
                 height, highbd);
  }
}

void apply_selfguided_restoration_avx2(const uint8_t *dat8, int width,
                                       int height, int stride, int eps,
                                       const int *xqd, uint8_t *dst8,
                                       int dst_stride, int32_t *tmpbuf,
                                       int bit_depth, int highbd) {
  int32_t *flt1 = tmpbuf;
  int32_t *flt2 = flt1 + RESTORATION_TILEPELS_MAX;
  assert(width * height <= RESTORATION_TILEPELS_MAX);
  av1_selfguided_restoration_avx2(dat8, width, height, stride, flt1, flt2,
                                  width, &sgr_params[eps], bit_depth, highbd);

  int xq[2];
  decode_xq(xqd, xq);

  __m256i xq0 = _mm256_set1_epi32(xq[0]);
  __m256i xq1 = _mm256_set1_epi32(xq[1]);

  for (int i = 0; i < height; ++i) {
    // Calculate output in batches of 16 pixels
    for (int j = 0; j < width; j += 16) {
      const int k = i * width + j;
      const int m = i * dst_stride + j;

      const uint8_t *dat8ij = dat8 + i * stride + j;
      __m256i ep_0, ep_1;
      __m128i src_0, src_1;
      if (highbd) {
        src_0 = xx_loadu_128(CONVERT_TO_SHORTPTR(dat8ij));
        src_1 = xx_loadu_128(CONVERT_TO_SHORTPTR(dat8ij + 8));
        ep_0 = _mm256_cvtepu16_epi32(src_0);
        ep_1 = _mm256_cvtepu16_epi32(src_1);
      } else {
        src_0 = xx_loadu_128(dat8ij);
        ep_0 = _mm256_cvtepu8_epi32(src_0);
        ep_1 = _mm256_cvtepu8_epi32(_mm_srli_si128(src_0, 8));
      }

      const __m256i u_0 = _mm256_slli_epi32(ep_0, SGRPROJ_RST_BITS);
      const __m256i u_1 = _mm256_slli_epi32(ep_1, SGRPROJ_RST_BITS);

      const __m256i f1_0 = _mm256_sub_epi32(yy_loadu_256(&flt1[k]), u_0);
      const __m256i f1_1 = _mm256_sub_epi32(yy_loadu_256(&flt1[k + 8]), u_1);

      const __m256i f2_0 = _mm256_sub_epi32(yy_loadu_256(&flt2[k]), u_0);
      const __m256i f2_1 = _mm256_sub_epi32(yy_loadu_256(&flt2[k + 8]), u_1);

      const __m256i v_0 =
          _mm256_add_epi32(_mm256_add_epi32(_mm256_mullo_epi32(xq0, f1_0),
                                            _mm256_mullo_epi32(xq1, f2_0)),
                           _mm256_slli_epi32(u_0, SGRPROJ_PRJ_BITS));
      const __m256i v_1 =
          _mm256_add_epi32(_mm256_add_epi32(_mm256_mullo_epi32(xq0, f1_1),
                                            _mm256_mullo_epi32(xq1, f2_1)),
                           _mm256_slli_epi32(u_1, SGRPROJ_PRJ_BITS));

      const __m256i rounding =
          round_for_shift(SGRPROJ_PRJ_BITS + SGRPROJ_RST_BITS);
      const __m256i w_0 = _mm256_srai_epi32(
          _mm256_add_epi32(v_0, rounding), SGRPROJ_PRJ_BITS + SGRPROJ_RST_BITS);
      const __m256i w_1 = _mm256_srai_epi32(
          _mm256_add_epi32(v_1, rounding), SGRPROJ_PRJ_BITS + SGRPROJ_RST_BITS);

      if (highbd) {
        // Pack into 16 bits and clamp to [0, 2^bit_depth)
        // Note that packing into 16 bits messes up the order of the bits,
        // so we use a permute function to correct this
        const __m256i tmp = _mm256_packus_epi32(w_0, w_1);
        const __m256i tmp2 = _mm256_permute4x64_epi64(tmp, 0xd8);
        const __m256i max = _mm256_set1_epi16((1 << bit_depth) - 1);
        const __m256i res = _mm256_min_epi16(tmp2, max);
        yy_store_256(CONVERT_TO_SHORTPTR(dst8 + m), res);
      } else {
        // Pack into 8 bits and clamp to [0, 256)
        // Note that each pack messes up the order of the bits,
        // so we use a permute function to correct this
        const __m256i tmp = _mm256_packs_epi32(w_0, w_1);
        const __m256i tmp2 = _mm256_permute4x64_epi64(tmp, 0xd8);
        const __m256i res =
            _mm256_packus_epi16(tmp2, tmp2 /* "don't care" value */);
        const __m128i res2 =
            _mm256_castsi256_si128(_mm256_permute4x64_epi64(res, 0xd8));
        xx_store_128(dst8 + m, res2);
      }
    }
  }
}
