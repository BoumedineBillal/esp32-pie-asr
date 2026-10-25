// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The demos' rules, by index or id: vacuum, smarthome, car, factory, kitchen, presenter.
 */
#pragma once

#include "voice_rules.hpp"

namespace demos {

constexpr int COUNT = 6;
const voice::Demo &get(int i);
const voice::Demo *find(const char *id);   /* nullptr: none of that id */

} // namespace demos
