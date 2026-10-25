// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// test_vacuum_rules: sentences as the recognizer writes them (capitals, punctuation) -> the vacuum's actions, in
// voice::describe's form ("-": skipped; "(reason)": why nothing is done). Every failure is printed; exit 1 if any.
#include <cstdio>
#include <cstring>
#include "vacuum_rules.hpp"

namespace {

struct Case {
    const char *text, *expect;
};

const Case CASES[] = {
    /* one action */
    {"Clean the kitchen.", "clean_room:kitchen"},
    {"Vacuum the living room.", "clean_room:living room"},
    {"Hoover the lounge.", "clean_room:lounge"},
    {"clean the living-room", "clean_room:living room"},
    {"Clean the Kitchen", "clean_room:kitchen"},
    {"Can you clean the kitchen please?", "clean_room:kitchen"},
    {"Clean up the spill.", "clean_spot:spill"},
    {"Clean the crumbs.", "clean_spot:crumbs"},
    {"Pick up the granola.", "clean_spot:granola"},
    {"Vacuum the dust in the bedroom.", "clean_spot:dust"},
    {"Clean up the crumbs in the living room.", "clean_spot:crumbs"},
    {"Clean everything.", "clean_all:everything"},
    {"Clean the house.", "clean_all:house"},
    {"Start cleaning.", "clean_all"},
    {"Go to the kitchen.", "go_to_room:kitchen"},
    {"Go back to the kitchen.", "go_to_room:kitchen"},
    {"Drive to the bedroom.", "go_to_room:bedroom"},
    {"Go back to the dock.", "return_to_dock:dock"},
    {"Go to the dock.", "return_to_dock:dock"},
    {"Return to the base.", "return_to_dock:base"},
    {"Go home.", "return_to_dock"},
    {"Robot, go home.", "return_to_dock"},
    {"Stop.", "stop"},
    {"Stop!", "stop"},
    {"Please stop.", "stop"},
    {"Pause.", "stop"},
    {"Hold on.", "stop"},
    {"Stop the vacuum.", "stop"},
    {"Stop cleaning the kitchen.", "stop"},
    {"Continue.", "resume"},
    {"Carry on.", "resume"},
    {"Keep going.", "resume"},
    {"Set the suction to high.", "suction:high"},
    {"Suction to max.", "suction:max"},
    {"Turn the suction to quiet.", "suction:quiet"},
    {"Where are you?", "status"},
    {"What are you doing?", "status"},
    {"How much battery is left?", "status"},
    /* several actions, in order */
    {"Clean the kitchen, not the bedroom, then go back to the dock.",
     "clean_room:kitchen; -clean_room:bedroom; return_to_dock:dock"},
    {"Clean the kitchen and the bedroom.", "clean_room:kitchen; clean_room:bedroom"},
    {"Clean the kitchen and bedroom", "clean_room:kitchen; clean_room:bedroom"},
    {"Clean the kitchen, then the living room.", "clean_room:kitchen; clean_room:living room"},
    {"Go to the kitchen and clean up the spill.", "go_to_room:kitchen; clean_spot:spill"},
    {"Clean the bedroom then go home.", "clean_room:bedroom; return_to_dock"},
    {"Clean up the kitchen and go home.", "clean_room:kitchen; return_to_dock"},
    {"Clean the kitchen. Then go back to the dock.", "clean_room:kitchen; return_to_dock:dock"},
    {"Clean the whole flat except the bedroom.", "clean_all:whole flat; -clean_room:bedroom"},
    {"Clean everything but not the kitchen.", "clean_all:everything; -clean_room:kitchen"},
    {"Please don't vacuum the living room, just the kitchen.", "-clean_room:living room; clean_room:kitchen"},
    {"Clean the kitchen, clean the kitchen.", "clean_room:kitchen"},
    /* nothing to do, and why */
    {"Don't clean the bedroom.", "-clean_room:bedroom (all_skipped)"},
    {"Do not clean the bedroom.", "-clean_room:bedroom (all_skipped)"},
    {"What is the weather tomorrow?", "(no_words)"},
    {"The kitchen.", "(no_verb)"},
    {"Clean.", "(no_target)"},
    {"Go to the spill.", "(mismatch)"},
    /* an order's verb comes first (after fillers, "don't" or "I want you to") */
    {"I want you to clean the kitchen.", "clean_room:kitchen"},
    {"Let's clean the kitchen.", "clean_room:kitchen"},
    {"Could you please clean the bedroom?", "clean_room:bedroom"},
    {"First clean the kitchen, then go home.", "clean_room:kitchen; return_to_dock"},
    {"I clean the kitchen yesterday.", "(no_verb)"},
    {"We should clean the kitchen.", "(no_verb)"},
    /* near misses: words of the demo in ordinary sentences do nothing */
    {"I want to go back to sleep.", "(no_words)"},
    {"I will continue tomorrow.", "(no_words)"},
    {"The dog is sleeping in the kitchen.", "(no_verb)"},
    {"We cooked dinner in the kitchen.", "(no_verb)"},
    {"My bedroom is upstairs.", "(no_verb)"},
    {"He came home late.", "(no_words)"},
    {"", "(no_words)"},
    {"... ,", "(no_words)"},
};

int g_fail;

void check(const char *text, const char *got, const char *expect)
{
    if (strcmp(got, expect)) {
        printf("FAIL \"%s\"\n     got    %s\n     expect %s\n", text, got, expect);
        g_fail++;
    }
}

} // namespace

int main()
{
    const voice::Demo &d = vacuum::demo();
    char got[512];
    int n = 0;
    for (const Case &c : CASES) {
        voice::Result r;
        voice::parse_text(d, c.text, r);
        check(c.text, voice::describe(d, r, got, sizeof got), c.expect);
        n++;
    }
    /* an utterance after a pause that continues the previous one */
    {
        voice::Context ctx;
        voice::Result r;
        voice::parse_text(d, "Clean the kitchen.", r, &ctx);
        voice::parse_text(d, "and the bedroom.", r, &ctx);
        check("[Clean the kitchen.] and the bedroom.", voice::describe(d, r, got, sizeof got), "clean_room:bedroom");
        if (!r.continued) {
            printf("FAIL continued not set\n");
            g_fail++;
        }
        voice::Context fresh;
        voice::parse_text(d, "and the bedroom.", r, &fresh);
        check("[] and the bedroom.", voice::describe(d, r, got, sizeof got), "(no_verb)");
        voice::Context ctx2;
        voice::parse_text(d, "Clean the kitchen.", r, &ctx2);
        voice::parse_text(d, "The bedroom.", r, &ctx2);
        check("[Clean the kitchen.] The bedroom.", voice::describe(d, r, got, sizeof got), "(no_verb)");
        n += 3;
    }
    /* the words' roles and the triggers: "Clean the kitchen, not the bedroom." */
    {
        voice::Result r;
        voice::parse_text(d, "Clean the kitchen, not the bedroom.", r);
        const voice::Role want[] = {voice::VERB, voice::NONE, voice::TARGET, voice::NEGATION, voice::NONE,
                                    voice::TARGET};
        bool ok = r.nwords == 6;
        for (int i = 0; ok && i < 6; i++)
            ok = r.role[i] == want[i];
        ok = ok && r.n == 2 && r.actions[0].ntrig == 2 && r.actions[0].trig[0].first == 0 &&
             r.actions[0].trig[1].first == 2 && r.actions[1].skipped && r.actions[1].ntrig == 3 &&
             r.actions[1].trig[0].first == 0 && r.actions[1].trig[2].role == voice::NEGATION &&
             r.actions[1].trig[2].first == 3;
        if (!ok) {
            printf("FAIL roles / triggers of \"Clean the kitchen, not the bedroom.\"\n");
            g_fail++;
        }
        voice::parse_text(d, "Go back to the living room.", r);
        ok = r.n == 1 && r.actions[0].trig[0].first == 0 && r.actions[0].trig[0].count == 2 &&
             r.actions[0].trig[1].first == 4 && r.actions[0].trig[1].count == 2 &&
             vacuum::room(r.actions[0]) == vacuum::LIVING_ROOM;
        if (!ok) {
            printf("FAIL spans of \"Go back to the living room.\"\n");
            g_fail++;
        }
        n += 2;
    }
    printf("%d of %d cases pass\n", n - g_fail, n);
    return g_fail ? 1 : 0;
}
