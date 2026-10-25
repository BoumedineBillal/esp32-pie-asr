// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The voice-control demos between the transcript and their worlds: while a demo's page is open, the transcript's
 * final words since it opened make utterances; an utterance ends when words are waiting and none is being written
 * (the speaker paused: a word is final after word_timeout without a token), or at 40 words. Each utterance goes
 * through the open demo's rules (components/voice_rules) into its world (the simulation it drives). The model can be
 * quiet longer than the time-out on a long word: the rest of a word cut so is joined to it ("s" "witch"), and an
 * utterance that does nothing on its own is taken again with the words that follow within 2 s ("Stop" ...
 * "everything"). All of it runs on the UI task (tick()); say() from another task is queued for it.
 */
#pragma once

#include <cstdint>
#include "car_sim.hpp"
#include "demos.hpp"
#include "factory_sim.hpp"
#include "kitchen_sim.hpp"
#include "presenter_sim.hpp"
#include "smarthome_sim.hpp"
#include "vacuum_sim.hpp"
#include "voice_rules.hpp"

namespace demo {

constexpr int WORD_BYTES = 32;
constexpr int COUNT = demos::COUNT;   /* vacuum, smarthome, car, factory, kitchen, presenter */

struct Utterance {
    int n = 0;                                   /* words */
    char words[voice::MAX_WORDS][WORD_BYTES];    /* as the transcript wrote them */
    bool cancelled[voice::MAX_WORDS];            /* in a skipped action's words ("not the bedroom") */
    voice::Result r;
    int32_t delay_ms = -1;                       /* the last word's end -> the action (audio time); -1: tapped */
    uint32_t number = 0;                         /* 1, 2, ... (0: none yet) */
};

void select(int i);                              /* the demo shown (its page about to open): nothing heard yet */
int selected();
void enter();                                    /* its page opened: words from now on, a fresh context */
void leave();
/* the UI task, every frame while a demo's page is open: new final words, an utterance at a pause, a queued say();
 * returns true when the utterance or the words still waiting changed (Heard and Actions to draw again) */
bool tick();
void reset();                                    /* the demo's world as at the start, nothing heard */

const Utterance &last();
/* the words heard since the last utterance and the word being written (shown in grey); returns how many */
int waiting(const char **out, int max);
const char *being_written();

const voice::Demo &rules();                      /* the selected demo's */
voice::World &world();                           /* the selected demo's (each keeps its state) */
vacuum::Sim &vacuum();
smarthome::Home &home();
car::Car &car();
factory::Line &line();
kitchen::Kitchen &kitchen();
presenter::Deck &deck();

void say(const char *text);                      /* any task: as if heard (the Say chips, the debug link) */
uint32_t utterances();                           /* how many were evaluated (the debug link waits on it) */

} // namespace demo
