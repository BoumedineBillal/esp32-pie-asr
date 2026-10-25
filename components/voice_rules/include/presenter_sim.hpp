// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The presenter's deck, simulated: ten slides, the show started or not, a slide change that slides in (0.3 s), a
 * blanked screen, a zoom, a talk of 20 minutes timed. Deterministic: update(dt) only.
 */
#pragma once

#include "presenter_rules.hpp"
#include "world_base.hpp"

namespace presenter {

constexpr int TALK_SECONDS = 20 * 60;

class Deck : public voice::BasicWorld {
public:
    Deck() { reset(); }
    void reset() override;
    void order(const voice::Result &r) override;
    void update(float dt) override;
    const char *status(char *out, int cap) const override;

    int slide() const { return slide_; }                       /* 0-based */
    int previous() const { return from_; }                     /* the slide before the change */
    float change() const { return change_; }                   /* 0..1 while the slide comes in, 1 after */
    bool presenting() const { return presenting_; }
    bool blank() const { return blank_; }
    int zoom() const { return zoom_; }                         /* 100, 150, 200 % */
    bool timing() const { return timing_; }
    int elapsed() const { return shown_secs_; }                /* seconds */

private:
    void go(int s, int i);

    int slide_, from_, zoom_, shown_secs_;
    float change_, secs_;
    bool presenting_, blank_, timing_;
};

} // namespace presenter
