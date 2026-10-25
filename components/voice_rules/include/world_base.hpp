// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * What the worlds of the demos after the vacuum share: the last order's steps, the last answer, the version.
 */
#pragma once

#include <cstdarg>
#include <cstdio>
#include "voice_rules.hpp"

namespace voice {

class BasicWorld : public World {
public:
    Step step(int action) const override { return action >= 0 && action < nact_ ? act_[action] : QUEUED; }
    int steps() const override { return nact_; }
    const char *answer() const override { return answer_; }
    uint32_t version() const override { return version_; }

protected:
    /* a new order: its actions listed (the skipped ones SKIPPED, the rest DONE until the world says otherwise),
     * the last answer cleared */
    void begin(const Result &r)
    {
        nact_ = r.n;
        for (int i = 0; i < r.n; i++)
            act_[i] = r.actions[i].skipped ? SKIPPED : DONE;
        answer_[0] = 0;
        version_++;
    }
    void say(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(answer_, sizeof answer_, fmt, ap);
        va_end(ap);
    }
    void clear()
    {
        nact_ = 0;
        answer_[0] = 0;
        version_++;
    }

    Step act_[MAX_ACTIONS] = {};
    int nact_ = 0;
    char answer_[160] = "";
    uint32_t version_ = 0;
};

} // namespace voice
