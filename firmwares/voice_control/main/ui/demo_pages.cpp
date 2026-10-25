// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The six demos' pages: what the demos' page says of each, its Say chips, its world's drawing (env_*.cpp).
 */
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

namespace {

const char *const SAY_VACUUM[SAY_CHIPS] = {"clean the kitchen", "clean up the spill", "go back to the dock",
                                           "clean everything",  "stop",               "where are you"};
const char *const SAY_HOME[SAY_CHIPS] = {"lights off everywhere", "lock the front door", "dim the bedroom lights",
                                         "play some jazz",        "close the blinds",    "what's the temperature"};
const char *const SAY_CAR[SAY_CHIPS] = {"navigate home",  "open the windows", "heat my seat",
                                        "make it warmer", "play the radio",   "how much range"};
const char *const SAY_FACTORY[SAY_CHIPS] = {"start the line",    "emergency stop",       "reset the alarm",
                                            "speed up the line", "switch to blue boxes", "how many boxes"};
const char *const SAY_KITCHEN[SAY_CHIPS] = {"preheat the oven",     "set a timer for one minute",
                                            "put the kettle on",    "front left burner on",
                                            "turn everything off", "how much time is left"};
const char *const SAY_PRESENTER[SAY_CHIPS] = {"start the presentation", "next slide",   "previous slide",
                                              "go to slide five",       "black screen", "start the timer"};

const DemoPage DEMO_PAGES[demo::COUNT] = {
    {"vacuum", "Robot vacuum", "rooms, messes, the dock, the suction", "clean the kitchen, not the bedroom",
     SAY_VACUUM, "None of the vacuum's words were heard, so nothing is done.",
     "Clean what? Say a room, a mess or everything.", icon_vacuum, draw_vacuum, vacuum_moving},
    {"smarthome", "Smart home", "lights, heating, doors, blinds, music", "lights off everywhere, lock the front door",
     SAY_HOME, "None of the house's words were heard, so nothing is done.",
     "Which one? Say the lights, the doors, the blinds or the music.", icon_house, draw_smarthome, nullptr},
    {"car", "Car cabin", "climate, windows, seats, route, media", "set the temperature to twenty one degrees",
     SAY_CAR, "None of the car's words were heard, so nothing is done.",
     "Which one? Say the windows, a seat, the fan, a place or the music.", icon_car, draw_car, nullptr},
    {"factory", "Factory line", "a conveyor, three stations, the alarms", "stop the line and reset the alarm",
     SAY_FACTORY, "None of the line's words were heard, so nothing is done.",
     "Which one? Say the line, a station, the speed or the alarm.", icon_factory, draw_factory, factory_moving},
    {"kitchen", "Kitchen", "burners, oven, timers, kettle, hood", "preheat the oven and set a timer for ten minutes",
     SAY_KITCHEN, "None of the kitchen's words were heard, so nothing is done.",
     "Which one? Say a burner, the oven, a timer, the kettle or the hood.", icon_kitchen, draw_kitchen, nullptr},
    {"presenter", "Presenter", "slides, a blank screen, the talk's timer", "go to the third slide", SAY_PRESENTER,
     "None of the presenter's words were heard, so nothing is done.",
     "Which one? Say a slide, the presentation or the timer.", icon_presenter, draw_presenter, presenter_moving},
};

} // namespace

const DemoPage &demo_page(int i) { return DEMO_PAGES[i >= 0 && i < demo::COUNT ? i : 0]; }

const DemoPage &demo_page() { return demo_page(demo::selected()); }

} // namespace ui
