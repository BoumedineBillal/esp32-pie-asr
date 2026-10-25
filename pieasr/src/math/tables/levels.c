// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Lloyd-Max levels of a standard Gaussian: the 2-bit and 4-bit weight levels.
#include "math/tables/tables.h"

const float pq_levels2[4] = {-1.50667085f, -0.451440609f, 0.452947494f, 1.50993262f};
const float pq_levels4[16] = {-2.70999003f, -2.04445561f, -1.59475702f, -1.23333619f, -0.922213966f, -0.639537374f,
                              -0.372663949f, -0.11445072f, 0.143806287f, 0.403988975f, 0.672693369f, 0.961405301f,
                              1.27552578f, 1.6370588f, 2.08442637f, 2.74101413f};
