// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// test_demo_sims: the worlds of the five demos after the vacuum, driven by sentences and the clock: what each order
// changes, what takes time, what is refused, what is answered.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "car_sim.hpp"
#include "factory_sim.hpp"
#include "kitchen_sim.hpp"
#include "presenter_sim.hpp"
#include "smarthome_sim.hpp"

namespace {

int g_fail = 0;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

void say(voice::World &w, const voice::Demo &d, const char *text)
{
    voice::Result r;
    voice::parse_text(d, text, r);
    w.order(r);
}

void run(voice::World &w, float seconds)
{
    for (float t = 0; t < seconds; t += 0.04f)
        w.update(0.04f);
}

} // namespace

int main()
{
    using voice::World;
    { /* the smart home */
        smarthome::Home h;
        const voice::Demo &d = smarthome::demo();
        CHECK(h.light(smarthome::LIVING_ROOM) == 60 && !h.light(smarthome::KITCHEN));
        say(h, d, "Turn on the lights in the kitchen and the bedroom.");
        CHECK(h.light(smarthome::KITCHEN) == 100 && h.light(smarthome::BEDROOM) == 100 && !h.light(smarthome::BATHROOM));
        say(h, d, "Set the bedroom lights to twenty percent.");
        CHECK(h.light(smarthome::BEDROOM) == 20);
        say(h, d, "Dim the bedroom lights.");
        CHECK(h.light(smarthome::BEDROOM) == 10);
        say(h, d, "Lights off everywhere.");
        CHECK(!h.light(0) && !h.light(1) && !h.light(2) && !h.light(3));
        say(h, d, "Turn on the kitchen lights.");
        CHECK(h.light(smarthome::KITCHEN) == 100);
        say(h, d, "Lock all the doors.");
        CHECK(h.locked(smarthome::FRONT) && h.locked(smarthome::BACK));
        say(h, d, "Unlock the back door but not the front.");
        CHECK(h.locked(smarthome::FRONT) && !h.locked(smarthome::BACK));
        CHECK(h.step(0) == World::DONE && h.step(1) == World::SKIPPED);
        say(h, d, "Close the blinds in the living room.");
        CHECK(h.step(0) == World::RUNNING);
        run(h, 2.5f);
        CHECK(h.blinds(smarthome::LIVING_ROOM) == 0 && h.blinds(smarthome::KITCHEN) == 1 && h.step(0) == World::DONE);
        say(h, d, "Set the thermostat to twenty two degrees.");
        CHECK(h.target() == 22 && h.heating());
        run(h, 30);
        CHECK(std::fabs(h.inside() - 22.0f) < 0.05f && !h.heating());
        say(h, d, "Play some jazz in the kitchen.");
        CHECK(h.playing() && h.music_room() == smarthome::KITCHEN && !strcmp(h.media(), "jazz"));
        say(h, d, "Next song.");
        CHECK(h.playing() && !strcmp(h.song(), "So What"));
        say(h, d, "What's the temperature?");
        CHECK(h.step(0) == World::ANSWERED && strstr(h.answer(), "22.0 degrees"));
        say(h, d, "Stop the music.");
        CHECK(!h.playing());
    }
    { /* the car */
        car::Car c;
        const voice::Demo &d = car::demo();
        say(c, d, "Set the driver side to twenty.");
        CHECK(c.target(car::DRIVER) == 20 && c.target(car::PASSENGER) == 21);
        say(c, d, "Roll down the driver's window.");
        run(c, 2.5f);
        CHECK(c.window(car::DRIVER) == 1 && c.window(car::PASSENGER) == 0 && c.step(0) == World::DONE);
        say(c, d, "Open the front windows but not the rear.");
        run(c, 2.5f);
        CHECK(c.window(car::PASSENGER) == 1 && c.window(car::REAR_LEFT) == 0);
        say(c, d, "Windows up.");
        run(c, 2.5f);
        CHECK(c.window(car::DRIVER) == 0 && c.window(car::PASSENGER) == 0);
        say(c, d, "Heat my seat.");
        CHECK(c.seat(car::DRIVER) == 3 && c.seat(car::PASSENGER) == 0);
        say(c, d, "Navigate home.");
        CHECK(c.destination() == car::HOME && c.step(0) == World::RUNNING);
        run(c, 60);
        CHECK(std::fabs(c.distance() - 11.4f) < 0.11f && c.eta_minutes() == 12);
        say(c, d, "Cancel the route.");
        CHECK(c.destination() < 0);
        say(c, d, "Turn on the headlights and play the radio.");
        CHECK(c.headlights() && c.playing() && !strcmp(c.media(), "radio"));
        say(c, d, "How much range do I have?");
        CHECK(c.step(0) == World::ANSWERED && strstr(c.answer(), "km of range"));
    }
    { /* the factory line */
        factory::Line f;
        const voice::Demo &d = factory::demo();
        say(f, d, "Start the line.");
        CHECK(f.running());
        run(f, 10);
        CHECK(f.count() > 0 && f.rejects() == 0);
        say(f, d, "Stop the press.");
        CHECK(!f.station_on(0) && f.running());
        run(f, 6);
        CHECK(f.rejects() > 0);
        say(f, d, "Start station one.");
        CHECK(f.station_on(0));
        say(f, d, "Emergency stop!");
        CHECK(!f.running() && f.estop());
        say(f, d, "Start the line.");
        CHECK(!f.running() && f.step(0) == World::SKIPPED && strstr(f.answer(), "emergency stop"));
        say(f, d, "Reset the emergency stop.");
        CHECK(!f.estop());
        say(f, d, "Start the line.");
        run(f, 70);
        CHECK(!f.running() && f.alarm()[0]);
        say(f, d, "Reset the alarm and start the line.");
        CHECK(f.running() && !f.alarm()[0]);
        say(f, d, "Set the speed to eighty percent.");
        CHECK(f.speed() == 80);
        say(f, d, "Switch to blue boxes.");
        CHECK(f.product() == factory::BLUE);
        say(f, d, "How many boxes?");
        CHECK(f.step(0) == World::ANSWERED && strstr(f.answer(), "boxes made"));
    }
    { /* the kitchen */
        kitchen::Kitchen k;
        const voice::Demo &d = kitchen::demo();
        say(k, d, "Turn on the front left burner.");
        CHECK(k.burner(kitchen::FRONT_LEFT) == 5);
        say(k, d, "Set the back right burner to seven.");
        CHECK(k.burner(kitchen::BACK_RIGHT) == 7);
        say(k, d, "Turn the front left burner up.");
        CHECK(k.burner(kitchen::FRONT_LEFT) == 7);
        say(k, d, "Turn off all the burners.");
        CHECK(!k.burner(0) && !k.burner(1) && !k.burner(2) && !k.burner(3));
        say(k, d, "Preheat the oven to one hundred and eighty degrees.");
        CHECK(k.oven_on() && k.oven_target() == 180 && k.step(0) == World::RUNNING);
        run(k, 15);
        CHECK(k.oven_ready() && k.step(0) == World::DONE);
        say(k, d, "Set a timer for ten minutes.");
        int left[3], total[3];
        bool ring[3];
        CHECK(k.timers(left, total, ring) == 1 && left[0] == 600);
        run(k, 600.5f);
        CHECK(k.timers(left, total, ring) == 1 && ring[0] && k.step(0) == World::DONE);
        run(k, 11);
        CHECK(k.timers(left, total, ring) == 0);
        say(k, d, "Put the kettle on.");
        CHECK(k.kettle_on());
        run(k, 16);
        CHECK(!k.kettle_on() && k.kettle_boiled() && k.step(0) == World::DONE);
        say(k, d, "Turn on the extractor fan and turn on the lights.");
        CHECK(k.hood() && k.lights());
        say(k, d, "Turn everything off.");
        CHECK(!k.hood() && !k.oven_on());
    }
    { /* the presenter */
        presenter::Deck p;
        const voice::Demo &d = presenter::demo();
        say(p, d, "Start the presentation.");
        CHECK(p.presenting() && p.slide() == 0);
        say(p, d, "Next slide.");
        say(p, d, "Next slide please.");
        CHECK(p.slide() == 2 && p.change() == 0);
        run(p, 0.5f);
        CHECK(p.change() == 1);
        say(p, d, "Go to slide five.");
        CHECK(p.slide() == 4);
        say(p, d, "Go to slide twelve.");
        CHECK(p.slide() == 4 && p.step(0) == World::SKIPPED && strstr(p.answer(), "10 slides"));
        say(p, d, "Last slide.");
        CHECK(p.slide() == 9);
        say(p, d, "Black screen.");
        CHECK(p.blank());
        say(p, d, "Show the slides.");
        CHECK(!p.blank());
        say(p, d, "Start the timer.");
        run(p, 65);
        CHECK(p.elapsed() == 65);
        say(p, d, "How much time do I have?");
        CHECK(p.step(0) == World::ANSWERED && !strcmp(p.answer(), "18 minutes 55 seconds left of 20."));
        say(p, d, "End the presentation.");
        CHECK(!p.presenting());
    }
    printf(g_fail ? "%d checks failed\n" : "all checks pass\n", g_fail);
    return g_fail ? 1 : 0;
}
