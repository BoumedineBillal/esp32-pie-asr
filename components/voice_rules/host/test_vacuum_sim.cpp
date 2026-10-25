// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// test_vacuum_sim: orders through the rules into the simulation, run to their end in 40 ms steps (the screen's):
// what is clean, where the robot ends, each action's step, and that it never leaves the flat.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "vacuum_sim.hpp"

namespace {

int g_fail;
float g_t;      /* the simulated time of the last run */

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

void say(vacuum::Sim &s, voice::Context &ctx, const char *text)
{
    voice::Result r;
    voice::parse_text(vacuum::demo(), text, r, &ctx);
    s.order(r);
}

/* steps until the robot rests (docked, idle, stopped) or limit seconds; checks it stays on the map */
void run(vacuum::Sim &s, float limit = 300)
{
    g_t = 0;
    for (; g_t < limit; g_t += 0.04f) {
        s.update(0.04f);
        const vacuum::Point p = s.pos();
        if (p.x < 0 || p.y < 0 || p.x > vacuum::MAP_W || p.y > vacuum::MAP_H) {
            printf("FAIL the robot left the map at (%.0f, %.0f)\n", p.x, p.y);
            g_fail++;
            return;
        }
        const vacuum::Sim::Mode m = s.mode();
        if (m == vacuum::Sim::DOCKED || m == vacuum::Sim::IDLE || m == vacuum::Sim::PAUSED)
            return;
    }
}

} // namespace

int main()
{
    using vacuum::Sim;
    {   /* the prototype's sentence */
        Sim s;
        voice::Context c;
        say(s, c, "Clean the kitchen, not the bedroom, then go back to the dock.");
        CHECK(s.steps() == 3 && s.step(0) == Sim::RUNNING && s.step(1) == Sim::SKIPPED && s.step(2) == Sim::QUEUED);
        run(s);
        printf("clean the kitchen, not the bedroom, then dock: %.1f s\n", g_t);
        CHECK(s.mode() == Sim::DOCKED);
        CHECK(s.room_clean(vacuum::KITCHEN) && !s.room_clean(vacuum::BEDROOM) && !s.room_clean(vacuum::LIVING_ROOM));
        CHECK(s.room_skipped(vacuum::BEDROOM));
        CHECK(s.spot_clean(vacuum::SPILL) && s.spot_clean(vacuum::GRANOLA) && !s.spot_clean(vacuum::CRUMBS));
        CHECK(s.step(0) == Sim::DONE && s.step(2) == Sim::DONE);
        CHECK(s.lanes_done(vacuum::KITCHEN) == vacuum::lanes(vacuum::KITCHEN));
        CHECK(std::fabs(s.pos().x - vacuum::DOCK.x) < 0.5f && std::fabs(s.pos().y - vacuum::DOCK.y) < 0.5f);
        CHECK(s.battery() < 100);
    }
    {   /* everything, every room */
        Sim s;
        voice::Context c;
        say(s, c, "Clean everything.");
        run(s);
        printf("clean everything, then home by itself: %.1f s\n", g_t);
        CHECK(s.mode() == Sim::DOCKED);
        CHECK(std::fabs(s.pos().x - vacuum::DOCK.x) < 0.5f && std::fabs(s.pos().y - vacuum::DOCK.y) < 0.5f);
        for (int r = 0; r < vacuum::ROOMS; r++)
            CHECK(s.room_clean(r));
        for (int k = 0; k < vacuum::SPOTS; k++)
            CHECK(s.spot_clean(k));
        CHECK(s.step(0) == Sim::DONE);
    }
    {   /* everything but a room */
        Sim s;
        voice::Context c;
        say(s, c, "Clean the whole flat except the bedroom.");
        run(s);
        CHECK(s.room_clean(vacuum::KITCHEN) && s.room_clean(vacuum::LIVING_ROOM) && !s.room_clean(vacuum::BEDROOM));
        CHECK(s.step(0) == Sim::DONE && s.step(1) == Sim::SKIPPED);
    }
    {   /* stop half way, then on */
        Sim s;
        voice::Context c;
        say(s, c, "Go to the bedroom.");
        for (int i = 0; i < 10; i++)
            s.update(0.04f);
        say(s, c, "Stop.");
        CHECK(s.mode() == Sim::PAUSED);
        const vacuum::Point p = s.pos();
        for (int i = 0; i < 50; i++)
            s.update(0.04f);
        CHECK(s.pos().x == p.x && s.pos().y == p.y);
        say(s, c, "Continue.");
        CHECK(s.mode() == Sim::MOVING);
        run(s);
        CHECK(s.mode() == Sim::IDLE && s.room_now() == vacuum::BEDROOM);
    }
    {   /* a mess in another room, through a doorway */
        Sim s;
        voice::Context c;
        say(s, c, "Clean up the crumbs.");
        for (int i = 0; i < 2000 && !s.spot_clean(vacuum::CRUMBS); i++)
            s.update(0.04f);
        CHECK(s.spot_clean(vacuum::CRUMBS) && !s.spot_clean(vacuum::SPILL));
        CHECK(s.room_now() == vacuum::LIVING_ROOM);
        char st[96];
        CHECK(s.mode() == Sim::MOVING && strstr(s.status(st, sizeof st), "going back to the dock"));
        run(s);
        CHECK(s.mode() == Sim::DOCKED);
    }
    {   /* on the way home by itself, "and ..." goes on to it, not home first */
        Sim s;
        voice::Context c;
        say(s, c, "Clean up the spill.");
        for (int i = 0; i < 2000 && !s.spot_clean(vacuum::SPILL); i++)
            s.update(0.04f);
        say(s, c, "and the crumbs.");
        run(s);   /* (it stops at the first dock: the crumbs must be clean by then) */
        CHECK(s.spot_clean(vacuum::CRUMBS) && s.mode() == Sim::DOCKED);
    }
    {   /* a move is not a cleaning: it stays */
        Sim s;
        voice::Context c;
        say(s, c, "Clean the kitchen, then go to the bedroom.");
        run(s);
        CHECK(s.room_clean(vacuum::KITCHEN) && s.mode() == Sim::IDLE && s.room_now() == vacuum::BEDROOM);
    }
    {   /* answers: the queue goes on */
        Sim s;
        voice::Context c;
        say(s, c, "Clean the bedroom.");
        for (int i = 0; i < 20; i++)
            s.update(0.04f);
        say(s, c, "Where are you?");
        CHECK(s.answer()[0] != 0 && s.mode() != Sim::IDLE && s.step(0) == Sim::ANSWERED);
        say(s, c, "Set the suction to high.");
        CHECK(s.suction() == vacuum::HIGH && !strcmp(s.answer(), "Suction set to high."));
        run(s);
        CHECK(s.room_clean(vacuum::BEDROOM));
    }
    {   /* an utterance that continues the previous one adds to it */
        Sim s;
        voice::Context c;
        say(s, c, "Clean the kitchen.");
        for (int i = 0; i < 5; i++)
            s.update(0.04f);
        say(s, c, "and the bedroom.");
        run(s);
        CHECK(s.room_clean(vacuum::KITCHEN) && s.room_clean(vacuum::BEDROOM));
    }
    {   /* a new order replaces what was left */
        Sim s;
        voice::Context c;
        say(s, c, "Clean everything.");
        for (int i = 0; i < 25; i++)
            s.update(0.04f);
        say(s, c, "Go back to the dock.");
        run(s);
        CHECK(s.mode() == Sim::DOCKED && !s.room_clean(vacuum::LIVING_ROOM));
    }
    {   /* nothing to do: nothing moves */
        Sim s;
        voice::Context c;
        say(s, c, "What is the weather tomorrow?");
        CHECK(s.mode() == Sim::DOCKED && s.steps() == 0);
        say(s, c, "Don't clean the bedroom.");
        CHECK(s.mode() == Sim::DOCKED && s.step(0) == Sim::SKIPPED);
    }
    printf(g_fail ? "%d checks failed\n" : "all checks pass\n", g_fail);
    return g_fail ? 1 : 0;
}
