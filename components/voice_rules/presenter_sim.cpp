// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * presenter_sim.hpp. A slide outside the deck ("slide twelve") is refused with an answer; "next" on the last slide
 * stays there.
 */
#include "presenter_sim.hpp"

namespace presenter {

namespace {

constexpr float CHANGE = 0.3f;   /* seconds a slide takes to come in */

} // namespace

void Deck::reset()
{
    slide_ = from_ = 0;
    zoom_ = 100;
    change_ = 1.0f;
    secs_ = 0;
    shown_secs_ = 0;
    presenting_ = blank_ = timing_ = false;
    clear();
}

void Deck::go(int s, int i)
{
    if (s < 0 || s >= SLIDES) {
        say("The deck has %d slides.", SLIDES);
        act_[i] = SKIPPED;
        return;
    }
    if (s != slide_) {
        from_ = slide_;
        slide_ = s;
        change_ = 0;
    }
    blank_ = false;
}

void Deck::order(const voice::Result &r)
{
    begin(r);
    for (int i = 0; i < r.n; i++) {
        const voice::Action &a = r.actions[i];
        if (a.skipped)
            continue;
        switch (a.rule) {
        case NEXT: go(slide_ + 1 < SLIDES ? slide_ + 1 : slide_, i); break;
        case PREVIOUS: go(slide_ > 0 ? slide_ - 1 : 0, i); break;
        case GO_TO: go(a.value - 1, i); break;
        case FIRST: go(0, i); break;
        case LAST: go(SLIDES - 1, i); break;
        case START:
            presenting_ = true;
            go(0, i);
            break;
        case END:
            presenting_ = blank_ = false;
            zoom_ = 100;
            break;
        case BLANK: blank_ = true; break;
        case UNBLANK: blank_ = false; break;
        case TIMER_START: timing_ = true; break;
        case TIMER_RESET:
            secs_ = 0;
            shown_secs_ = 0;
            break;
        case ZOOM_IN: zoom_ = zoom_ < 200 ? zoom_ + 50 : 200; break;
        case ZOOM_OUT: zoom_ = zoom_ > 100 ? zoom_ - 50 : 100; break;
        case TIME_LEFT: {
            const int left = TALK_SECONDS - shown_secs_;
            if (!timing_)
                say("The timer is not running: say \"start the timer\".");
            else if (left > 0)
                say("%d minutes %d seconds left of 20.", left / 60, left % 60);
            else
                say("Time is up: %d minutes over.", -left / 60);
            act_[i] = ANSWERED;
            break;
        }
        default: break;
        }
    }
}

void Deck::update(float dt)
{
    if (dt <= 0)
        return;
    if (change_ < 1.0f)
        change_ = change_ + dt / CHANGE > 1.0f ? 1.0f : change_ + dt / CHANGE; /* (drawn as a moving part) */
    if (timing_) {
        secs_ += dt;
        if (static_cast<int>(secs_) != shown_secs_) {
            shown_secs_ = static_cast<int>(secs_);
            version_++;
        }
    }
}

const char *Deck::status(char *out, int cap) const
{
    snprintf(out, cap, "%s, slide %d of %d%s%s, timer %d:%02d", presenting_ ? "Presenting" : "Not presenting",
             slide_ + 1, SLIDES, blank_ ? ", screen blanked" : "", zoom_ > 100 ? ", zoomed in" : "",
             shown_secs_ / 60, shown_secs_ % 60);
    return out;
}

} // namespace presenter
