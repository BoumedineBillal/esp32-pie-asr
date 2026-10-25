// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * kitchen_sim.hpp. Said without a place: "the burner" is the first one off (on) or the first one on (off, up, down),
 * "the burners", "the hob", "the stove" all four; a burner turned on without a level is at 5; the oven without a
 * temperature at 180; a fourth timer replaces the one with the least time left.
 */
#include "kitchen_sim.hpp"
#include <cmath>
#include <cstring>

namespace kitchen {

namespace {

constexpr float OVEN_SPEED = 12.0f;     /* degrees a second (a demo's oven) */
constexpr float OVEN_COOL = 6.0f;
constexpr float KETTLE_SPEED = 80.0f / 15.0f;
constexpr float RING = 10.0f;           /* seconds a timer rings */

const char *target_text(const voice::Action &a)
{
    const voice::Rule &r = demo().rules[a.rule];
    return a.target >= 0 ? r.targets[a.target] : "";
}

} // namespace

void Kitchen::reset()
{
    for (int b = 0; b < BURNERS; b++)
        burner_[b] = 0;
    oven_on_ = false;
    oven_target_ = 180;
    oven_ = 20.0f;
    shown_oven_ = 20;
    ntimers_ = 0;
    shown_secs_ = -1;
    kettle_on_ = false;
    kettle_ = 20.0f;
    shown_kettle_ = 20;
    boiled_ = 0;
    hood_ = lights_ = false;
    clear();
}

void Kitchen::order(const voice::Result &r)
{
    begin(r);
    for (int i = 0; i < r.n; i++) {
        const voice::Action &a = r.actions[i];
        wait_[i] = NOTHING;
        if (a.skipped)
            continue;
        const int place = kitchen::burner(a);
        const char *t = target_text(a);
        const bool every = place == ALL || (place < 0 && (!strcmp(t, "burners") || !strcmp(t, "hob") ||
                                                          !strcmp(t, "stove") || !strcmp(t, "cooktop") ||
                                                          !strcmp(t, "rings")));
        /* the burners an action names */
        bool b[BURNERS] = {};
        if (every) {
            for (int k = 0; k < BURNERS; k++)
                b[k] = true;
        } else if (place >= 0) {
            b[place] = true;
        } else {
            const bool want_off = a.rule == BURNER_ON || a.rule == BURNER_SET;
            for (int k = 0; k < BURNERS; k++)
                if ((burner_[k] == 0) == want_off) {
                    b[k] = true;
                    break;
                }
        }
        switch (a.rule) {
        case BURNER_ON:
            for (int k = 0; k < BURNERS; k++)
                if (b[k])
                    burner_[k] = a.value >= 1 && a.value <= 9 ? a.value : (burner_[k] ? burner_[k] : 5);
            break;
        case BURNER_SET:
            for (int k = 0; k < BURNERS; k++)
                if (b[k])
                    burner_[k] = a.value < 0 ? 0 : a.value > 9 ? 9 : a.value;
            if (a.value > 9)
                say("The burners go from 1 to 9.");
            break;
        case BURNER_UP:
        case BURNER_DOWN:
            for (int k = 0; k < BURNERS; k++)
                if (b[k]) {
                    const int v = burner_[k] + (a.rule == BURNER_UP ? 2 : -2);
                    burner_[k] = v < 0 ? 0 : v > 9 ? 9 : v;
                }
            break;
        case BURNER_OFF:
            for (int k = 0; k < BURNERS; k++)
                if (b[k])
                    burner_[k] = 0;
            break;
        case OVEN_ON:
            oven_on_ = true;
            oven_target_ = a.value >= 50 && a.value <= 300 ? a.value : 180;
            if (a.value >= 0 && (a.value < 50 || a.value > 300))
                say("The oven goes from 50 to 300 degrees: 180.");
            act_[i] = RUNNING;
            wait_[i] = OVEN;
            break;
        case OVEN_OFF:
            oven_on_ = false;
            break;
        case TIMER_SET: {
            const int secs = (a.value < 1 ? 1 : a.value > 180 ? 180 : a.value) * 60;
            int k = ntimers_;
            if (k == MAX_TIMERS) { /* the one with the least time left */
                k = 0;
                for (int q = 1; q < MAX_TIMERS; q++)
                    if (left_[q] < left_[k])
                        k = q;
            } else {
                ntimers_++;
            }
            left_[k] = static_cast<float>(secs);
            total_[k] = secs;
            ring_[k] = 0;
            act_[i] = RUNNING;
            wait_[i] = TIMER;
            break;
        }
        case TIMER_CANCEL:
            if (!ntimers_) {
                say("There is no timer.");
                act_[i] = ANSWERED;
            }
            ntimers_ = 0;
            break;
        case KETTLE:
            kettle_on_ = true;
            boiled_ = 0;
            act_[i] = RUNNING;
            wait_[i] = KETTLE_BOIL;
            break;
        case HOOD_ON:
        case HOOD_OFF:
            hood_ = a.rule == HOOD_ON;
            break;
        case LIGHTS_ON:
        case LIGHTS_OFF:
            lights_ = a.rule == LIGHTS_ON;
            break;
        case ALL_OFF:
            for (int k = 0; k < BURNERS; k++)
                burner_[k] = 0;
            oven_on_ = kettle_on_ = hood_ = false;
            break;
        case STATUS: {
            char tm[48] = "no timer";
            if (ntimers_) {
                int k = 0;
                for (int q = 1; q < ntimers_; q++)
                    if (left_[q] < left_[k])
                        k = q;
                const int s = static_cast<int>(std::ceil(left_[k]));
                snprintf(tm, sizeof tm, "the next timer ends in %d:%02d", s / 60, s % 60);
            }
            int on = 0;
            for (int k = 0; k < BURNERS; k++)
                on += burner_[k] > 0;
            char oven[48] = "Oven off";
            if (oven_on_)
                snprintf(oven, sizeof oven, oven_ready() ? "Oven ready at %d degrees" : "Oven heating to %d degrees",
                         oven_target_);
            say("%s, %s, %d burner%s on.", oven, tm, on, on == 1 ? "" : "s");
            act_[i] = ANSWERED;
            break;
        }
        default: break;
        }
    }
}

void Kitchen::update(float dt)
{
    if (dt <= 0)
        return;
    /* the oven */
    const float to = oven_on_ ? static_cast<float>(oven_target_) : 20.0f;
    const float sp = (oven_on_ ? OVEN_SPEED : OVEN_COOL) * dt;
    oven_ = std::fabs(to - oven_) < sp ? to : oven_ + (to > oven_ ? sp : -sp);
    const int so = static_cast<int>(std::round(oven_ / 5)) * 5;
    if (so != shown_oven_) {
        shown_oven_ = so;
        version_++;
    }
    /* the kettle */
    if (kettle_on_) {
        kettle_ += KETTLE_SPEED * dt;
        if (kettle_ >= 100.0f) {
            kettle_ = 100.0f;
            kettle_on_ = false;
            boiled_ = 5.0f;
            version_++;
        }
    } else {
        kettle_ = kettle_ - 0.5f * dt < 20.0f ? 20.0f : kettle_ - 0.5f * dt;
        if (boiled_ > 0 && (boiled_ -= dt) <= 0) {
            boiled_ = 0;
            version_++;
        }
    }
    const int sk = static_cast<int>(std::round(kettle_ / 5)) * 5;
    if (sk != shown_kettle_) {
        shown_kettle_ = sk;
        version_++;
    }
    /* the timers: their seconds, and one that rang its 10 s goes */
    int w = 0;
    bool rang = false;
    for (int k = 0; k < ntimers_; k++) {
        if (left_[k] > 0) {
            left_[k] -= dt;
            if (left_[k] <= 0) {
                left_[k] = 0;
                ring_[k] = RING;
                rang = true;
            }
        } else if ((ring_[k] -= dt) <= 0) {
            version_++;
            continue;
        }
        left_[w] = left_[k];
        ring_[w] = ring_[k];
        total_[w] = total_[k];
        w++;
    }
    ntimers_ = w;
    int secs = 0;
    for (int k = 0; k < ntimers_; k++)
        secs += static_cast<int>(std::ceil(left_[k])) + (left_[k] <= 0 ? 1000 + static_cast<int>(ring_[k] * 2) : 0);
    if (secs != shown_secs_) {
        shown_secs_ = secs;
        version_++;
    }
    for (int i = 0; i < nact_; i++) {
        if (act_[i] != RUNNING)
            continue;
        if ((wait_[i] == OVEN && (oven_ready() || !oven_on_)) || (wait_[i] == KETTLE_BOIL && !kettle_on_) ||
            (wait_[i] == TIMER && (rang || !ntimers_))) {
            act_[i] = DONE;
            version_++;
        }
    }
}

int Kitchen::timers(int *left, int *total, bool *ringing) const
{
    for (int k = 0; k < ntimers_; k++) {
        left[k] = static_cast<int>(std::ceil(left_[k]));
        total[k] = total_[k];
        ringing[k] = left_[k] <= 0;
    }
    return ntimers_;
}

const char *Kitchen::status(char *out, int cap) const
{
    int on = 0;
    for (int k = 0; k < BURNERS; k++)
        on += burner_[k] > 0;
    char oven[40] = "oven off";
    if (oven_on_)
        snprintf(oven, sizeof oven, "oven %d of %d degrees", shown_oven_, oven_target_);
    snprintf(out, cap, "%d burner%s on, %s, %d timer%s%s", on, on == 1 ? "" : "s", oven, ntimers_,
             ntimers_ == 1 ? "" : "s", kettle_on_ ? ", the kettle boiling" : "");
    return out;
}

} // namespace kitchen
