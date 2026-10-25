// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Words to actions, by rules: no language model. A demo lists its actions; each action has verb phrases, target
 * phrases, and phrases that fire it alone ("stop", "go home"). An utterance (the words said before a pause) is
 * split into parts at "and", "then", "but" and punctuation; in each part a verb and a target that belong to the same
 * action fire it. A part with targets and no verb takes the previous part's verb ("clean the kitchen and the
 * bedroom"). "not", "except", "without" before a target cancel that target; "do not" / "don't" / "never" before the
 * part's verb cancel the whole part. A cancelled action is kept, marked skipped, so the screen can say so.
 *
 * Every action carries the words that fired it (the utterance's word indexes), and every word its role, for the
 * screen. No heap, no locale: the same results on the PC and on the chip.
 *
 * What a demo may add (each off unless the demo asks for it, so a demo without them parses as before):
 *   - numbers: a rule with NUM_REQUIRED or NUM_OPTIONAL takes the part's number ("set the thermostat to twenty two
 *     degrees" -> 22): number words (and the "and" inside "one hundred and eighty"), digits, ordinals ("the third
 *     slide"); NUM_REQUIRED without one does not fire (reason NO_NUMBER);
 *   - places (Demo::places): a room or a side said anywhere in the part ("the kitchen lights", "the lights in the
 *     kitchen") is the action's place; a part with a place and no target takes the previous part's target ("the
 *     kitchen and the bedroom", "the back door but not the front");
 *   - PARTICLE: a verb "X on/off/up/down" also fires split ("turn the lights off") or after its target at an
 *     order's start ("lights off", "the windows down"), the particle then being the part's last word (places and
 *     "please" may follow).
 */
#pragma once

#include <cstdint>

namespace voice {

constexpr int MAX_WORDS = 48;      /* words of an utterance looked at (the rest is ignored) */
constexpr int MAX_TOKENS = 64;     /* after "don't" -> "do not" */
constexpr int MAX_ACTIONS = 8;
constexpr int MAX_TRIGGERS = 6;    /* word spans that fired one action */
constexpr int TOKEN_BYTES = 24;

/* Rule::flags */
constexpr int NUM_REQUIRED = 1;    /* fires only with a number in its part */
constexpr int NUM_OPTIONAL = 2;    /* takes the part's number if there is one */
constexpr int PARTICLE = 4;        /* its verbs "X on/off/up/down" also split or after the target */
constexpr int WHOLE = 8;           /* its alone phrases fire only as a whole part ("next slide please", not "the next
                                    * slide shows ..."): after them only "please", "now", places and their "the", "in" */

/* one action of a demo; the lists end with nullptr (verbs and targets may be empty: {nullptr}) */
struct Rule {
    const char *id;                 /* "clean_room" */
    const char *label;              /* "clean room", as the screen shows it */
    const char *const *verbs;       /* "clean", "clean up", "go to" */
    const char *const *targets;     /* "kitchen", "living room" */
    const char *const *alone;       /* fire with no target: "stop", "go home" */
    int flags = 0;                  /* NUM_REQUIRED, NUM_OPTIONAL, PARTICLE */
};

struct Demo {
    const char *id, *name;
    const Rule *rules;
    int n;
    const char *const *places = nullptr; /* rooms, sides ("kitchen", "front", "driver"); nullptr: none */
};

/* a word's role in the utterance (the screen's colours) */
enum Role : uint8_t { NONE, VERB, TARGET, NEGATION, CONNECTOR, NUMBER };

struct Span {
    uint8_t first, count;          /* utterance words [first, first + count) */
    Role role;
};

struct Action {
    int rule;                      /* index in the demo's rules */
    int target;                    /* index in the rule's targets, -1: none */
    int place;                     /* index in the demo's places, -1: none */
    int value;                     /* the number, -1: none */
    bool skipped;                  /* cancelled by a negation */
    Span trig[MAX_TRIGGERS];
    int ntrig;
};

/* why an utterance gave no action to do */
enum Reason : uint8_t {
    OK,                            /* at least one action that is not skipped */
    NO_WORDS,                      /* none of the demo's words */
    NO_TARGET,                     /* a verb that needs a target, with none ("clean") */
    NO_VERB,                       /* targets without a verb ("the kitchen") */
    MISMATCH,                      /* a verb and a target of different actions ("go to the spill") */
    ALL_SKIPPED,                   /* every action cancelled ("don't clean the kitchen") */
    NO_NUMBER,                     /* an action that needs a number, with none ("set the thermostat") */
};

/* what an utterance said to the next one ("... and the bedroom" said after a pause continues it) */
struct Context {
    int verb_rules = 0;            /* bit r: rule r's verb was the last one (0: none) */
    int verb_first = -1, verb_count = 0;
    int target_rules = 0;          /* bit r: rule r's target was the last one */
    int8_t target_index[32] = {};  /* that target's index in rule r's targets */
};

struct Result {
    int nwords;                    /* the utterance's words looked at */
    Role role[MAX_WORDS];          /* each word's role (the strongest of its tokens') */
    Action actions[MAX_ACTIONS];
    int n;
    Reason reason;
    bool continued;                /* it began with "and" / "then": it continues the previous utterance */
};

/* the utterance's words, as written (case and punctuation as the recognizer wrote them); ctx: what the previous
 * utterance left (nullptr: none), updated for the next one. About 4 KB of stack. */
void parse(const Demo &d, const char *const *words, int nwords, Result &out, Context *ctx = nullptr);

/* the same from one string of words separated by spaces (tests and the PC) */
void parse_text(const Demo &d, const char *text, Result &out, Context *ctx = nullptr);

/* "clean_room:kitchen; -clean_room:bedroom; return_to_dock" (-: skipped; "lights_off:lights@kitchen",
 * "set_thermostat:thermostat=22" with a place and a number), then "(reason)" when nothing is done; for logs and
 * tests; returns out */
const char *describe(const Demo &d, const Result &r, char *out, int cap);

/* a demo's world, simulated: what its actions do, for the screen. Deterministic: update(dt) only. */
class World {
public:
    /* an action of the last order, as the screen lists it */
    enum Step { QUEUED, RUNNING, DONE, SKIPPED, ANSWERED };
    virtual ~World() = default;
    virtual void reset() = 0;                        /* as at the start */
    virtual void order(const Result &r) = 0;         /* an utterance's actions */
    virtual void update(float dt) = 0;               /* seconds */
    virtual Step step(int action) const = 0;         /* of the last order's action i */
    virtual int steps() const = 0;
    virtual const char *answer() const = 0;          /* the last answer to a question ("" if none) */
    virtual const char *status(char *out, int cap) const = 0;  /* one line: what it is doing */
    /* changes when anything drawn changes, except what moves every frame (the screen redraws that part alone) */
    virtual uint32_t version() const = 0;
};

} // namespace voice
