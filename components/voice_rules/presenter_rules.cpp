// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * presenter_rules.hpp. A talk is full of "next" and "go back": the commands said alone fire as a whole part only
 * ("next slide please", not "the next slide shows our results"), and there is no "go back" alone ("let's go back in
 * time"); "go to the third slide" is GO_TO with 3 ("the first slide": 1).
 */
#include "presenter_rules.hpp"

namespace presenter {

namespace {

const char *const SLIDE_T[] = {"slide", "page", "slide number", nullptr};
/* (no "start" or "end" alone: they are the show's verbs) */
const char *const FIRST_T[] = {"the beginning", "beginning", "the start", "first slide", nullptr};
const char *const LAST_T[] = {"the end", "last slide", "the last slide", "final slide", "the final slide", nullptr};
const char *const SHOW_T[] = {"presentation", "slideshow", "slide show", "the show", "the slides", nullptr};
const char *const TIMER_T[] = {"timer", "stopwatch", "clock", nullptr};
const char *const NONE[] = {nullptr};

const char *const NEXT_ALONE[] = {"next slide", "the next slide", "next page", "go forward", "move forward",
                                  "forward one slide", nullptr};
const char *const PREV_ALONE[] = {"previous slide", "the previous slide", "previous page", "back one slide",
                                  "go back one slide", "go back a slide", "one slide back", nullptr};
const char *const GO_VERBS[] = {"go to", "jump to", "skip to", "go back to", "back to", "move to", "show",
                                "show me", "open", nullptr};
const char *const FIRST_ALONE[] = {"first slide", nullptr};
const char *const LAST_ALONE[] = {"last slide", "final slide", nullptr};
const char *const START_VERBS[] = {"start", "begin", "launch", "open", nullptr};
const char *const END_VERBS[] = {"end", "stop", "close", "finish", "exit", "quit", nullptr};
const char *const BLANK_ALONE[] = {"black screen", "blank screen", "blank the screen", "black out the screen",
                                   "go black", "hide the slides", "hide the screen", nullptr};
const char *const UNBLANK_ALONE[] = {"show the slides", "unblank", "unblank the screen", "show the screen",
                                     "back to the slides", nullptr};
const char *const TIMER_VERBS[] = {"start", "begin", nullptr};
const char *const RESET_VERBS[] = {"reset", "restart", nullptr};
const char *const ZOOM_IN_ALONE[] = {"zoom in", nullptr};
const char *const ZOOM_OUT_ALONE[] = {"zoom out", nullptr};
const char *const TIME_ALONE[] = {"how much time is left", "how much time do i have", "how much time left",
                                  "how long have i been talking", "how long have i been speaking", "time check",
                                  nullptr};

const voice::Rule TABLE[RULES] = {
    {"next", "next slide", NONE, NONE, NEXT_ALONE, voice::WHOLE},
    {"previous", "previous slide", NONE, NONE, PREV_ALONE, voice::WHOLE},
    {"go_to", "go to slide", GO_VERBS, SLIDE_T, NONE, voice::NUM_REQUIRED},
    {"first", "first slide", GO_VERBS, FIRST_T, FIRST_ALONE, voice::WHOLE},
    {"last", "last slide", GO_VERBS, LAST_T, LAST_ALONE, voice::WHOLE},
    {"start", "start the show", START_VERBS, SHOW_T, NONE, 0},
    {"end", "end the show", END_VERBS, SHOW_T, NONE, 0},
    {"blank", "black screen", NONE, NONE, BLANK_ALONE, voice::WHOLE},
    {"unblank", "show the slides", NONE, NONE, UNBLANK_ALONE, voice::WHOLE},
    {"timer_start", "start the timer", TIMER_VERBS, TIMER_T, NONE, 0},
    {"timer_reset", "reset the timer", RESET_VERBS, TIMER_T, NONE, 0},
    {"zoom_in", "zoom in", NONE, NONE, ZOOM_IN_ALONE, voice::WHOLE},
    {"zoom_out", "zoom out", NONE, NONE, ZOOM_OUT_ALONE, voice::WHOLE},
    {"time_left", "time left", NONE, NONE, TIME_ALONE, 0},
};

const voice::Demo DEMO = {"presenter", "Presenter", TABLE, RULES};

} // namespace

const voice::Demo &demo() { return DEMO; }

const char *slide_title(int i)
{
    static const char *const T[SLIDES] = {"On-device speech",  "Agenda",          "The problem",
                                          "Why on the chip",   "How it works",    "The numbers",
                                          "A live demo",       "What it costs",   "What comes next",
                                          "Thank you"};
    return i >= 0 && i < SLIDES ? T[i] : "";
}

} // namespace presenter
