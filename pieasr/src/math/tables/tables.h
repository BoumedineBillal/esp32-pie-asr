// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The model's constant tables: the 2/4-bit levels and the calibrated activation ranges (128 FLEURS clips, float
// model) of the integer path.
#pragma once

// Lloyd-Max levels of a standard Gaussian (levels.c); a weight in the rotated domain = level[code] / sqrt(G) * norm
extern const float pq_levels2[4];
extern const float pq_levels4[16];

// per 2/4-bit matrix: max |input| and max |output| (scales.c)
typedef struct {
    const char *name;
    float ax, ay;
} pq_scale_t;
#define PQ_NSCALES 274
extern const pq_scale_t pq_scales[PQ_NSCALES];

// max |x| of each activation, by name (layer prefix s<stack>.l<layer>.) (act.c)
typedef struct {
    const char *name;
    float amax;
} pq_act_t;
#define PQ_NACT 714
extern const pq_act_t pq_act[PQ_NACT];
float pq_act_max(const char *name);   // 0 if unknown

// per-channel max |x| of the value caches and of the front end's hidden (actch.c)
typedef struct {
    const char *name;
    int n;
    const float *amax;
} pq_actch_t;
#define PQ_NACTCH 88
extern const pq_actch_t pq_actch[PQ_NACTCH];
