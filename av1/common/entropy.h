/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#ifndef AV1_COMMON_ENTROPY_H_
#define AV1_COMMON_ENTROPY_H_

#include "./aom_config.h"
#include "aom/aom_integer.h"
#include "aom_dsp/prob.h"

#include "av1/common/common.h"
#include "av1/common/common_data.h"
#include "av1/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIFF_UPDATE_PROB 252
#define GROUP_DIFF_UPDATE_PROB 252

#define TOKEN_CDF_Q_CTXS 4

// Coefficient token alphabet
#define ZERO_TOKEN 0        // 0     Extra Bits 0+0
#define ONE_TOKEN 1         // 1     Extra Bits 0+1
#define TWO_TOKEN 2         // 2     Extra Bits 0+1
#define THREE_TOKEN 3       // 3     Extra Bits 0+1
#define FOUR_TOKEN 4        // 4     Extra Bits 0+1
#define CATEGORY1_TOKEN 5   // 5-6   Extra Bits 1+1
#define CATEGORY2_TOKEN 6   // 7-10  Extra Bits 2+1
#define CATEGORY3_TOKEN 7   // 11-18 Extra Bits 3+1
#define CATEGORY4_TOKEN 8   // 19-34 Extra Bits 4+1
#define CATEGORY5_TOKEN 9   // 35-66 Extra Bits 5+1
#define CATEGORY6_TOKEN 10  // 67+   Extra Bits 14+1
#define EOB_TOKEN 11        // EOB   Extra Bits 0+0
#define NO_EOB 0            // Not an end-of-block
#define EARLY_EOB 1         // End of block before the last position
#define LAST_EOB 2          // End of block in the last position (implicit)
#define BLOCK_Z_TOKEN 255   // block zero
#define HEAD_TOKENS 5
#define TAIL_TOKENS 9
#define ONE_TOKEN_EOB 1
#define ONE_TOKEN_NEOB 2
#define TWO_TOKEN_PLUS_EOB 3
#define TWO_TOKEN_PLUS_NEOB 4
#define ENTROPY_TOKENS 12

#define ENTROPY_NODES 11

#if CONFIG_LV_MAP
#define TXB_SKIP_CONTEXTS 13

#define EOB_COEF_CONTEXTS 22

#define SIG_COEF_CONTEXTS_2D 26
#define SIG_COEF_CONTEXTS_1D 16
#define USE_CAUSAL_BR_CTX 1
#define SIG_COEF_CONTEXTS_EOB 4
#define SIG_COEF_CONTEXTS (SIG_COEF_CONTEXTS_2D + SIG_COEF_CONTEXTS_1D)

#define COEFF_BASE_CONTEXTS (SIG_COEF_CONTEXTS)
#define DC_SIGN_CONTEXTS 3

#define BR_TMP_OFFSET 12
#define BR_REF_CAT 4
#if USE_CAUSAL_BR_CTX
#define LEVEL_CONTEXTS 21
#else
#define LEVEL_CONTEXTS (BR_TMP_OFFSET * BR_REF_CAT)
#endif

#define NUM_BASE_LEVELS 2

#define BR_CDF_SIZE (4)
#define COEFF_BASE_RANGE (4 * (BR_CDF_SIZE - 1))

#define COEFF_CONTEXT_BITS 6
#define COEFF_CONTEXT_MASK ((1 << COEFF_CONTEXT_BITS) - 1)

#define BASE_CONTEXT_POSITION_NUM 12

typedef enum TX_CLASS {
  TX_CLASS_2D = 0,
  TX_CLASS_HORIZ = 1,
  TX_CLASS_VERT = 2,
  TX_CLASSES = 3,
} TX_CLASS;

#endif

DECLARE_ALIGNED(16, extern const uint8_t, av1_pt_energy_class[ENTROPY_TOKENS]);

#define CAT1_MIN_VAL 5
#define CAT2_MIN_VAL 7
#define CAT3_MIN_VAL 11
#define CAT4_MIN_VAL 19
#define CAT5_MIN_VAL 35
#define CAT6_MIN_VAL 67

#define CAT6_BIT_SIZE 18
// Extra bit probabilities.
extern const aom_cdf_prob *av1_cat1_cdf[];
extern const aom_cdf_prob *av1_cat2_cdf[];
extern const aom_cdf_prob *av1_cat3_cdf[];
extern const aom_cdf_prob *av1_cat4_cdf[];
extern const aom_cdf_prob *av1_cat5_cdf[];
extern const aom_cdf_prob *av1_cat6_cdf[];

#define EOB_MODEL_TOKEN 3

typedef struct {
  const aom_cdf_prob **cdf;
  int len;
  int base_val;
  const int16_t *cost;
} av1_extra_bit;

// indexed by token value
extern const av1_extra_bit av1_extra_bits[ENTROPY_TOKENS];

static INLINE int av1_get_cat6_extrabits_size(TX_SIZE tx_size,
                                              aom_bit_depth_t bit_depth) {
  tx_size = txsize_sqr_up_map[tx_size];
#if CONFIG_TX64X64
  // TODO(debargha): Does TX_64X64 require an additional extrabit?
  if (tx_size > TX_32X32) tx_size = TX_32X32;
#endif
  int tx_offset = (int)(tx_size - TX_4X4);
  int bits = (int)bit_depth + 3 + tx_offset;
  // Round up
  bits = AOMMIN(CAT6_BIT_SIZE, ((bits + 3) & ~3));
  return bits;
}

#define DCT_MAX_VALUE 16384
#define DCT_MAX_VALUE_HIGH10 65536
#define DCT_MAX_VALUE_HIGH12 262144

/* Coefficients are predicted via a 3-dimensional probability table. */

#define REF_TYPES 2  // intra=0, inter=1

/* Middle dimension reflects the coefficient position within the transform. */
#define COEF_BANDS 6

/* Inside dimension is measure of nearby complexity, that reflects the energy
   of nearby coefficients are nonzero.  For the first coefficient (DC, unless
   block type is 0), we look at the (already encoded) blocks above and to the
   left of the current block.  The context index is then the number (0,1,or 2)
   of these blocks having nonzero coefficients.
   After decoding a coefficient, the measure is determined by the size of the
   most recently decoded coefficient.
   Note that the intuitive meaning of this measure changes as coefficients
   are decoded, e.g., prior to the first token, a zero means that my neighbors
   are empty while, after the first token, because of the use of end-of-block,
   a zero means we just decoded a zero and hence guarantees that a non-zero
   coefficient will appear later in this block.  However, this shift
   in meaning is perfectly OK because our context depends also on the
   coefficient band (and since zigzag positions 0, 1, and 2 are in
   distinct bands). */

#define COEFF_CONTEXTS 6
#define COEFF_CONTEXTS0 3  // for band 0
#define BAND_COEFF_CONTEXTS(band) \
  ((band) == 0 ? COEFF_CONTEXTS0 : COEFF_CONTEXTS)

#define SUBEXP_PARAM 4   /* Subexponential code parameter */
#define MODULUS_PARAM 13 /* Modulus parameter */

struct AV1Common;
struct frame_contexts;
void av1_default_coef_probs(struct AV1Common *cm);
#if CONFIG_LV_MAP
void av1_adapt_coef_probs(struct AV1Common *cm);
#endif  // CONFIG_LV_MAP

// This is the index in the scan order beyond which all coefficients for
// 8x8 transform and above are in the top band.
// This macro is currently unused but may be used by certain implementations
#define MAXBAND_INDEX 21

DECLARE_ALIGNED(16, extern const uint8_t,
                av1_coefband_trans_8x8plus[MAX_TX_SQUARE]);
DECLARE_ALIGNED(16, extern const uint8_t, av1_coefband_trans_4x8_8x4[32]);
DECLARE_ALIGNED(16, extern const uint8_t, av1_coefband_trans_4x4[16]);

DECLARE_ALIGNED(16, extern const uint16_t, band_count_table[TX_SIZES_ALL][8]);

static INLINE const uint8_t *get_band_translate(TX_SIZE tx_size) {
  switch (tx_size) {
    case TX_4X4: return av1_coefband_trans_4x4;
    case TX_8X4:
    case TX_4X8: return av1_coefband_trans_4x8_8x4;
    default: return av1_coefband_trans_8x8plus;
  }
}

// 128 lists of probabilities are stored for the following ONE node probs:
// 1, 3, 5, 7, ..., 253, 255
// In between probabilities are interpolated linearly

#define COEFF_PROB_MODELS 255

#define UNCONSTRAINED_NODES 3

#define MODEL_NODES (ENTROPY_NODES - UNCONSTRAINED_NODES)
#define TAIL_NODES (MODEL_NODES + 1)

typedef aom_cdf_prob coeff_cdf_model[REF_TYPES][COEF_BANDS][COEFF_CONTEXTS]
                                    [CDF_SIZE(ENTROPY_TOKENS)];
extern const aom_cdf_prob av1_pareto8_token_probs[COEFF_PROB_MODELS]
                                                 [ENTROPY_TOKENS - 2];
extern const aom_cdf_prob av1_pareto8_tail_probs[COEFF_PROB_MODELS]
                                                [ENTROPY_TOKENS - 3];
struct frame_contexts;

void av1_coef_head_cdfs(struct frame_contexts *fc);
void av1_coef_pareto_cdfs(struct frame_contexts *fc);

typedef char ENTROPY_CONTEXT;

static INLINE int combine_entropy_contexts(ENTROPY_CONTEXT a,
                                           ENTROPY_CONTEXT b) {
  return (a != 0) + (b != 0);
}

static INLINE int get_entropy_context(TX_SIZE tx_size, const ENTROPY_CONTEXT *a,
                                      const ENTROPY_CONTEXT *l) {
  ENTROPY_CONTEXT above_ec = 0, left_ec = 0;

  switch (tx_size) {
    case TX_4X4:
      above_ec = a[0] != 0;
      left_ec = l[0] != 0;
      break;
    case TX_4X8:
      above_ec = a[0] != 0;
      left_ec = !!*(const uint16_t *)l;
      break;
    case TX_8X4:
      above_ec = !!*(const uint16_t *)a;
      left_ec = l[0] != 0;
      break;
    case TX_8X16:
      above_ec = !!*(const uint16_t *)a;
      left_ec = !!*(const uint32_t *)l;
      break;
    case TX_16X8:
      above_ec = !!*(const uint32_t *)a;
      left_ec = !!*(const uint16_t *)l;
      break;
    case TX_16X32:
      above_ec = !!*(const uint32_t *)a;
      left_ec = !!*(const uint64_t *)l;
      break;
    case TX_32X16:
      above_ec = !!*(const uint64_t *)a;
      left_ec = !!*(const uint32_t *)l;
      break;
    case TX_8X8:
      above_ec = !!*(const uint16_t *)a;
      left_ec = !!*(const uint16_t *)l;
      break;
    case TX_16X16:
      above_ec = !!*(const uint32_t *)a;
      left_ec = !!*(const uint32_t *)l;
      break;
    case TX_32X32:
      above_ec = !!*(const uint64_t *)a;
      left_ec = !!*(const uint64_t *)l;
      break;
#if CONFIG_TX64X64
    case TX_64X64:
      above_ec = !!(*(const uint64_t *)a | *(const uint64_t *)(a + 8));
      left_ec = !!(*(const uint64_t *)l | *(const uint64_t *)(l + 8));
      break;
    case TX_32X64:
      above_ec = !!*(const uint64_t *)a;
      left_ec = !!(*(const uint64_t *)l | *(const uint64_t *)(l + 8));
      break;
    case TX_64X32:
      above_ec = !!(*(const uint64_t *)a | *(const uint64_t *)(a + 8));
      left_ec = !!*(const uint64_t *)l;
      break;
#endif  // CONFIG_TX64X64
    case TX_4X16:
      above_ec = a[0] != 0;
      left_ec = !!*(const uint32_t *)l;
      break;
    case TX_16X4:
      above_ec = !!*(const uint32_t *)a;
      left_ec = l[0] != 0;
      break;
    case TX_8X32:
      above_ec = !!*(const uint16_t *)a;
      left_ec = !!*(const uint64_t *)l;
      break;
    case TX_32X8:
      above_ec = !!*(const uint64_t *)a;
      left_ec = !!*(const uint16_t *)l;
      break;
#if CONFIG_TX64X64
    case TX_16X64:
      above_ec = !!*(const uint32_t *)a;
      left_ec = !!(*(const uint64_t *)l | *(const uint64_t *)(l + 8));
      break;
    case TX_64X16:
      above_ec = !!(*(const uint64_t *)a | *(const uint64_t *)(a + 8));
      left_ec = !!*(const uint32_t *)l;
      break;
#endif  // CONFIG_TX64X64
    default: assert(0 && "Invalid transform size."); break;
  }
  return combine_entropy_contexts(above_ec, left_ec);
}

#define COEF_COUNT_SAT 24
#define COEF_MAX_UPDATE_FACTOR 112
#define COEF_COUNT_SAT_AFTER_KEY 24
#define COEF_MAX_UPDATE_FACTOR_AFTER_KEY 128

static INLINE TX_SIZE get_txsize_entropy_ctx(TX_SIZE txsize) {
  return (TX_SIZE)((txsize_sqr_map[txsize] + txsize_sqr_up_map[txsize] + 1) >>
                   1);
}

void av1_average_tile_coef_cdfs(struct frame_contexts *fc,
                                struct frame_contexts *ec_ctxs[],
                                aom_cdf_prob *cdf_ptrs[], int num_tiles);
void av1_average_tile_mv_cdfs(struct frame_contexts *fc,
                              struct frame_contexts *ec_ctxs[],
                              aom_cdf_prob *cdf_ptrs[], int num_tiles);
void av1_average_tile_intra_cdfs(struct frame_contexts *fc,
                                 struct frame_contexts *ec_ctxs[],
                                 aom_cdf_prob *cdf_ptrs[], int num_tiles);
void av1_average_tile_inter_cdfs(struct AV1Common *cm,
                                 struct frame_contexts *fc,
                                 struct frame_contexts *ec_ctxs[],
                                 aom_cdf_prob *cdf_ptrs[], int num_tiles);
void av1_average_tile_loopfilter_cdfs(struct frame_contexts *fc,
                                      struct frame_contexts *ec_ctxs[],
                                      aom_cdf_prob *cdf_ptrs[], int num_tiles);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV1_COMMON_ENTROPY_H_
