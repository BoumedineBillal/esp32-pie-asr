// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The factory line demo's actions and the words of each: a conveyor through three stations (1 the press, 2 the
 * robot arm, 3 the packer), its speed, the product it makes, an emergency stop and the alarms.
 */
#pragma once

#include "voice_rules.hpp"

namespace factory {

/* the rules' order (voice::Action::rule) */
enum Rule { ESTOP, START, STOP, RESET, SET_SPEED, FASTER, SLOWER, PRODUCT, STATUS, RULES };

/* what START / STOP act on */
enum Machine { LINE, PRESS, ARM, PACKER, MACHINES };
enum Product { RED, BLUE, GREEN, PRODUCTS };

const voice::Demo &demo();

int machine(const voice::Action &a);   /* START / STOP: LINE, or a station (by name or "station <n>"); -1 */
int product(const voice::Action &a);   /* PRODUCT's; -1 */

const char *machine_name(int m);       /* "Press" */
const char *product_name(int p);       /* "red" */

} // namespace factory
