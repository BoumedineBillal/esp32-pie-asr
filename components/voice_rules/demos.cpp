// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * demos.hpp.
 */
#include "demos.hpp"
#include <cstring>
#include "car_rules.hpp"
#include "factory_rules.hpp"
#include "kitchen_rules.hpp"
#include "presenter_rules.hpp"
#include "smarthome_rules.hpp"
#include "vacuum_rules.hpp"

namespace demos {

const voice::Demo &get(int i)
{
    switch (i) {
    case 1: return smarthome::demo();
    case 2: return car::demo();
    case 3: return factory::demo();
    case 4: return kitchen::demo();
    case 5: return presenter::demo();
    default: return vacuum::demo();
    }
}

const voice::Demo *find(const char *id)
{
    for (int i = 0; i < COUNT; i++)
        if (id && !strcmp(get(i).id, id))
            return &get(i);
    return nullptr;
}

} // namespace demos
