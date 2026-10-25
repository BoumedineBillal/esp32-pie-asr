// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The debug link (CONFIG_VOICE_CONTROL_DEBUG_LINK): pclink's service commands, and the screen's and the demo's:
 *
 *   shot                       the screen as an SHT1 message (u16 width, height, rotation, 0; the panel's RGB565)
 *   tap <x> <y>                a touch at (x, y), down then up
 *   drag <x0> <y0> <x1> <y1>   a finger moved from (x0, y0) to (x1, y1)
 *   screen <name>              home, live, stats, history, phrases, settings, about, demos, demo, rules, or a demo's
 *                              id: vacuum, smarthome, car, factory, kitchen, presenter
 *   settings                   the settings as they are now (kept in NVS)
 *   uicheck [reset]            the text-fit check's counts and the UI's frame times (reset: the frames counted anew)
 *   say <words_joined_by_underscores>   an order as if heard, in the selected demo (its Say chips do the same)
 *   demo                       "ok demo <id> <utterances> <delay ms> | <actions> | <steps> | <status> | <words>"
 *   phrases_list               the phrase list in use and how many there are
 */
#include <cstdlib>
#include <cstring>
#include "demo.hpp"
#include "p4lcd_ui.hpp"
#include "pclink.hpp"
#include "phrases.hpp"
#include "settings.hpp"
#include "ui/ui.hpp"

static void send_shot(int w, int h, int rotation, const uint16_t *px)
{
    const uint16_t head[4] = {static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(rotation), 0};
    pclink::send("SHT1", head, sizeof head, px, static_cast<uint32_t>(w * h * 2), true);
}

static void shot(int, char **) { ui::shot(send_shot); } /* the SHT1 message is the reply */

static void tap(int, char **argv)
{
    ui::tap(atoi(argv[1]), atoi(argv[2]));
    pclink::reply("ok tap");
}

static void drag(int, char **argv)
{
    ui::drag(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    pclink::reply("ok drag");
}

static void screen(int, char **argv)
{
    if (ui::show(argv[1]))
        pclink::reply("ok screen %s", argv[1]);
    else
        pclink::reply("err screen %s", argv[1]);
}

static void settings_cmd(int, char **)
{
    const settings::Settings &se = settings::get();
    pclink::reply("ok settings gain %.1f timeout %.2f pause %.2f size %d tentative %d timestamps %d follow %d",
                  se.gain_db, se.word_timeout_s, se.pause_s, se.text_size, se.show_tentative ? 1 : 0,
                  se.timestamps ? 1 : 0, se.follow ? 1 : 0);
}

static void uicheck(int argc, char **)
{
    const ui::Check c = ui::check(argc == 2);
    pclink::reply("ok uicheck overflows %d shrunk %d frames %d mean_ms %.1f max_ms %.1f", c.overflows, c.shrunk,
                  c.frames, c.mean_ms, c.max_ms);
}

static void say(int, char **argv)
{
    for (char *p = argv[1]; *p; p++)
        if (*p == '_')
            *p = ' ';
    demo::say(argv[1]);
    pclink::reply("ok say %s", argv[1]);
}

static void demo_cmd(int, char **)
{
    static const char *const STEP[] = {"next", "now", "done", "skipped", "answered"};
    const demo::Utterance &u = demo::last();
    char acts[512], steps[96] = "", status[128], words[400] = "";
    voice::describe(demo::rules(), u.r, acts, sizeof acts);
    for (int i = 0; i < u.r.n; i++)
        snprintf(steps + strlen(steps), sizeof steps - strlen(steps), "%s%s", i ? "," : "",
                 STEP[demo::world().step(i)]);
    demo::world().status(status, sizeof status);
    for (int i = 0; i < u.n; i++)
        snprintf(words + strlen(words), sizeof words - strlen(words), "%s%s", i ? " " : "", u.words[i]);
    pclink::reply("ok demo %s %u %d | %s | %s | %s | %s", demo::rules().id, (unsigned)demo::utterances(),
                  (int)u.delay_ms, acts, steps, status, words);
}

static void phrases_list(int, char **)
{
    pclink::reply("ok phrases_list %d of %d, %d words %d sentences, %s", phrases::selected(), phrases::lists(),
                  phrases::words(), phrases::sentences(), phrases::on() ? "on" : "off");
}

static const pclink::Command COMMANDS[] = {
    {"shot", 1, shot},         {"tap", 3, tap},           {"drag", 5, drag},     {"screen", 2, screen},
    {"settings", 1, settings_cmd}, {"uicheck", 1, uicheck}, {"uicheck", 2, uicheck}, {"say", 2, say},
    {"demo", 1, demo_cmd},     {"phrases_list", 1, phrases_list},
};

static void fit_log(const char *line) { pclink::log("%s", line); }

void debug_link_start()
{
    pclink::attach("voice_control");
    pclink::add_commands(COMMANDS, sizeof COMMANDS / sizeof COMMANDS[0]);
    p4lcd::ui::setFitLog(fit_log);
}
