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
#include <assert.h>

#include "av1/encoder/cost.h"
#include "av1/common/entropy.h"

/* round(-log2(i/256.) * (1 << AV1_PROB_COST_SHIFT))
   Begins with a bogus entry for simpler addressing. */
const uint16_t av1_prob_cost[256] = {
  4096, 4096, 3584, 3284, 3072, 2907, 2772, 2659, 2560, 2473, 2395, 2325, 2260,
  2201, 2147, 2096, 2048, 2003, 1961, 1921, 1883, 1847, 1813, 1780, 1748, 1718,
  1689, 1661, 1635, 1609, 1584, 1559, 1536, 1513, 1491, 1470, 1449, 1429, 1409,
  1390, 1371, 1353, 1335, 1318, 1301, 1284, 1268, 1252, 1236, 1221, 1206, 1192,
  1177, 1163, 1149, 1136, 1123, 1110, 1097, 1084, 1072, 1059, 1047, 1036, 1024,
  1013, 1001, 990,  979,  968,  958,  947,  937,  927,  917,  907,  897,  887,
  878,  868,  859,  850,  841,  832,  823,  814,  806,  797,  789,  780,  772,
  764,  756,  748,  740,  732,  724,  717,  709,  702,  694,  687,  680,  673,
  665,  658,  651,  644,  637,  631,  624,  617,  611,  604,  598,  591,  585,
  578,  572,  566,  560,  554,  547,  541,  535,  530,  524,  518,  512,  506,
  501,  495,  489,  484,  478,  473,  467,  462,  456,  451,  446,  441,  435,
  430,  425,  420,  415,  410,  405,  400,  395,  390,  385,  380,  375,  371,
  366,  361,  356,  352,  347,  343,  338,  333,  329,  324,  320,  316,  311,
  307,  302,  298,  294,  289,  285,  281,  277,  273,  268,  264,  260,  256,
  252,  248,  244,  240,  236,  232,  228,  224,  220,  216,  212,  209,  205,
  201,  197,  194,  190,  186,  182,  179,  175,  171,  168,  164,  161,  157,
  153,  150,  146,  143,  139,  136,  132,  129,  125,  122,  119,  115,  112,
  109,  105,  102,  99,   95,   92,   89,   86,   82,   79,   76,   73,   70,
  66,   63,   60,   57,   54,   51,   48,   45,   42,   38,   35,   32,   29,
  26,   23,   20,   18,   15,   12,   9,    6,    3
};

void av1_cost_tokens_from_cdf(int *costs, const aom_cdf_prob *cdf,
                              const int *inv_map) {
  int i;
  aom_cdf_prob prev_cdf = 0;
  for (i = 0;; ++i) {
    aom_cdf_prob p15 = AOM_ICDF(cdf[i]) - prev_cdf;
    p15 = (p15 < EC_MIN_PROB) ? EC_MIN_PROB : p15;
    // p15 = (p15 < CDF_PROB_TOP - EC_MIN_PROB) ? p15 : CDF_PROB_TOP -
    // EC_MIN_PROB;
    // prev_cdf += p15;
    prev_cdf = AOM_ICDF(cdf[i]);

    if (inv_map)
      costs[inv_map[i]] = av1_cost_symbol(p15);
    else
      costs[i] = av1_cost_symbol(p15);

    // Stop once we reach the end of the CDF
    if (cdf[i] == AOM_ICDF(CDF_PROB_TOP)) break;
  }
}
