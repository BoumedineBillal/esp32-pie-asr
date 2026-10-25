// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * What every demo's page has: its world (drawn by the demo's env_*.cpp) with a status line, Heard (the last
 * utterance: verbs blue, targets and places amber, numbers green, what was cancelled struck through; the words still
 * coming in grey), Actions (in order, each with its step, its target, place and number, and the words that fired it,
 * or the world's answer, or why nothing is done), Say (examples; a tap runs one), and the Rules page (every phrase of
 * every rule, scrolling).
 */
#include <algorithm>
#include <cstring>
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

pu::Scroll rules_view = {{R_RULES_ROWS.x, R_RULES_ROWS.y + 36, R_RULES_ROWS.w - 16, R_RULES_ROWS.h - 40}, 0};

Color floor_color() { return pu::mix(theme->bg, theme->panel, 0.4f); }

void world_panel(Painter &p)
{
    p.fill(theme->bg);
    p.roundFrame(MAP.x, MAP.y, MAP.w, MAP.h, 12, theme->line, theme->panel, 1);
}

void world_status(char *s, size_t n) { demo::world().status(s, static_cast<int>(n)); }

void draw_world_status(Painter &p)
{
    char s[128];
    world_status(s, sizeof s);
    p.rect(R_MAP_STATUS.x, R_MAP_STATUS.y, R_MAP_STATUS.w, R_MAP_STATUS.h, theme->panel);
    p.fit(R_MAP_STATUS, s, style(14, theme->muted, false, Align::LEFT, VAlign::MIDDLE), "world.status");
}

void draw_world(Painter &p) { demo_page().draw(p); }

/* ---- Heard ---- */

/* a word's text without its trailing punctuation, and that punctuation */
static void split_punct(const char *w, char *core, size_t ncore, char *tail, size_t ntail)
{
    size_t n = strlen(w), k = n;
    while (k > 0 && strchr(".,!?;:", w[k - 1]))
        k--;
    snprintf(core, ncore, "%.*s", static_cast<int>(k), w);
    snprintf(tail, ntail, "%s", w + k);
}

static Color role_color(voice::Role r)
{
    return r == voice::VERB ? theme->accent : r == voice::NUMBER ? theme->ok : theme->warn;
}

void draw_heard(Painter &p)
{
    p.fill(theme->bg);
    pu::card(p, *theme, HEARD, "Heard", "demo.heard");
    const demo::Utterance &u = demo::last();
    const int x0 = HEARD.x + 16, right = HEARD.x + HEARD.w - 16;
    int x = x0, y = HEARD.y + 46, line = 0;
    constexpr int SIZE = 19, LH = 30;
    const int space = p4lcd::Canvas::textBox(0, 0, " ", style(SIZE, theme->ink)).w;
    auto put = [&](const char *text, TextStyle st, const Color *box, bool strike) {
        const int w = p4lcd::Canvas::textBox(0, 0, text, st).w;
        const int pad = box ? 4 : 0;
        if (x + w + 2 * pad > right && x > x0) {
            x = x0;
            y += LH;
            line++;
        }
        if (line > 1)
            return;
        x += pad;
        if (box)
            p.round(x - 4, y - 3, w + 8, SIZE + 9, 6, pu::mix(theme->panel, *box, 0.22f));
        p.text(x, y, text, st);
        if (strike)
            p.line(x - 1, y + SIZE * 6 / 10, x + w + 1, y + SIZE * 6 / 10, theme->faint, 2);
        x += w + pad;
    };
    /* the words still coming, in grey */
    const char *wait[voice::MAX_WORDS];
    const int nw = demo::waiting(wait, voice::MAX_WORDS);
    const char *tent = demo::being_written();
    if (nw || tent[0]) {
        for (int i = 0; i < nw; i++) {
            put(wait[i], style(SIZE, theme->muted), nullptr, false);
            x += space;
        }
        if (tent[0])
            put(tent, style(SIZE, theme->faint), nullptr, false);
        return;
    }
    if (!u.number) {
        p.fit({x0, y, right - x0, 28}, "say one of the commands below...", style(18, theme->faint), "demo.heard.none");
        return;
    }
    if (u.delay_ms < 0)
        pu::pill(p, HEARD.x + HEARD.w - 86, HEARD.y + 12, 24, "tapped", theme->soft, theme->muted, 13);
    /* the utterance: neighbouring words of one role in one highlight, punctuation outside it */
    for (int i = 0; i < u.n;) {
        const voice::Role role = u.r.role[i];
        const bool cancel = u.cancelled[i];
        const bool hl = !cancel && (role == voice::VERB || role == voice::TARGET || role == voice::NUMBER);
        char span[160] = "", core[voice::TOKEN_BYTES * 2], tail[8] = "";
        int j = i;
        while (j < u.n && u.r.role[j] == role && u.cancelled[j] == cancel) {
            split_punct(u.words[j], core, sizeof core, tail, sizeof tail);
            snprintf(span + strlen(span), sizeof span - strlen(span), "%s%s", j > i ? " " : "", core);
            j++;
            if (tail[0] || !hl)
                break; /* punctuation ends a highlight; plain words go one by one (they wrap) */
        }
        const Color rc = role_color(role);
        TextStyle st = hl ? style(SIZE, rc, true)
                          : style(SIZE, cancel || role == voice::NEGATION ? theme->faint : theme->ink);
        put(span, st, hl ? &rc : nullptr, cancel || role == voice::NEGATION);
        if (tail[0] && line <= 1) {
            const TextStyle ts = style(SIZE, cancel ? theme->faint : theme->ink);
            p.text(x, y, tail, ts);
            x += p4lcd::Canvas::textBox(0, 0, tail, ts).w;
        }
        x += space;
        i = j;
    }
}

/* ---- Actions ---- */

static const char *reason_text(voice::Reason r)
{
    switch (r) {
    case voice::NO_TARGET: return demo_page().no_target;
    case voice::NO_VERB: return "What should it do? Start the order with what to do (see Rules).";
    case voice::MISMATCH: return "These words do not go together, so nothing is done rather than a guess.";
    case voice::ALL_SKIPPED: return "Everything said was cancelled, so nothing is done.";
    case voice::NO_NUMBER: return "This needs a number: say it with one, as in \"... to twenty two\".";
    default: return demo_page().no_words;
    }
}

/* the words of span sp, without punctuation */
static void span_text(const demo::Utterance &u, const voice::Span &sp, char *out, size_t n)
{
    out[0] = 0;
    for (int j = sp.first; j < sp.first + sp.count && j < u.n; j++) {
        char core[voice::TOKEN_BYTES * 2], tail[8];
        split_punct(u.words[j], core, sizeof core, tail, sizeof tail);
        snprintf(out + strlen(out), n - strlen(out), "%s%s", j > sp.first ? " " : "", core);
    }
}

void draw_actions(Painter &p)
{
    p.fill(theme->bg);
    pu::card(p, *theme, ACTIONS, "Actions", "demo.actions");
    const demo::Utterance &u = demo::last();
    const voice::World &w = demo::world();
    char note[48] = "in order";
    if (u.number && u.delay_ms >= 0)
        snprintf(note, sizeof note, "%.1f s after the last word", u.delay_ms / 1000.0f);
    p.fit({ACTIONS.x + 140, ACTIONS.y + 14, ACTIONS.w - 156, 24}, note,
          style(13, theme->muted, false, Align::RIGHT, VAlign::MIDDLE), "demo.actions.note");
    const int x = ACTIONS.x + 14, wd = ACTIONS.w - 28;
    if (!u.number) {
        p.fit({x + 2, ACTIONS.y + 46, wd, 26}, "none yet", style(17, theme->faint), "demo.actions.none");
        return;
    }
    if (u.r.n == 0) {
        p.roundFrame(x, ACTIONS.y + 44, wd, 112, 10, theme->line, theme->soft, 1);
        p.fit({x + 16, ACTIONS.y + 56, wd - 32, 26}, "No action", style(18, theme->ink, true), "demo.noaction");
        TextStyle st = style(16, theme->muted);
        st.wrap = wd - 32;
        p.fit({x + 16, ACTIONS.y + 86, wd - 32, 64}, reason_text(u.r.reason), st, "demo.reason");
        return;
    }
    const voice::Demo &d = demo::rules();
    const int rows = u.r.n < 3 ? u.r.n : 3;
    bool answer_shown = false;
    for (int i = 0; i < rows; i++) {
        const voice::Action &a = u.r.actions[i];
        const voice::Rule &rule = d.rules[a.rule];
        const int y = ACTIONS.y + 42 + i * 56;
        const voice::World::Step stp = w.step(i);
        const bool refused = stp == voice::World::SKIPPED && !a.skipped; /* the world said no (its answer: why) */
        Color col = theme->muted;
        const char *state = "next";
        switch (stp) {
        case voice::World::RUNNING: col = theme->accent, state = "now"; break;
        case voice::World::DONE: col = theme->ok, state = "done"; break;
        case voice::World::ANSWERED: col = theme->ok, state = "done"; break;
        case voice::World::SKIPPED: col = refused ? theme->warn : theme->faint, state = refused ? "refused" : "skipped";
            break;
        default: break;
        }
        p.disc(x + 13, y + 14, 13, pu::mix(theme->panel, col, 0.25f));
        char num[4];
        snprintf(num, sizeof num, "%d", i + 1);
        p.text(x + 13, y + 14, num, style(14, col, true, Align::CENTER, VAlign::MIDDLE));
        const Rect lr = p.fit({x + 34, y + 2, 160, 26}, rule.label,
                              style(18, a.skipped || refused ? theme->faint : theme->ink, true), "demo.act.label");
        /* its target, place and number */
        const char *pills[3] = {a.target >= 0 ? rule.targets[a.target] : nullptr,
                                a.place >= 0 && d.places ? d.places[a.place] : nullptr, nullptr};
        char value[12];
        if (a.value >= 0) {
            snprintf(value, sizeof value, "%d", a.value);
            pills[2] = value;
        }
        int px = lr.x + lr.w + 8;
        for (int k = 0; k < 3; k++) {
            if (!pills[k])
                continue;
            const Color c = k == 2 ? theme->ok : theme->warn;
            const int pw = p4lcd::Canvas::textBox(0, 0, pills[k], style(14, c, true)).w + 24;
            if (px + pw > x + wd - 74)
                continue; /* (the next may fit: the number is short) */
            pu::pill(p, px, y + 3, 24, pills[k], pu::mix(theme->panel, c, 0.22f), c, 14);
            px += pw + 6;
        }
        p.fit({x + wd - 70, y + 2, 70, 26}, state, style(13, col, true, Align::RIGHT, VAlign::MIDDLE),
              "demo.act.state");
        /* the world's answer (a question, a refusal), else the words that fired it */
        if ((stp == voice::World::ANSWERED || refused) && w.answer()[0] && !answer_shown) {
            TextStyle as = style(14, theme->muted); /* (the last row's: two lines) */
            as.wrap = wd - 34;
            p.fit({x + 34, y + 30, wd - 34, i == rows - 1 ? ACTIONS.y + ACTIONS.h - 8 - (y + 30) : 22}, w.answer(), as,
                  "demo.act.answer");
            answer_shown = true;
            continue;
        }
        int cx = x + 34 + p.fit({x + 34, y + 30, 70, 22}, "fired by", style(14, theme->muted), "demo.act.by").w + 8;
        for (int k = 0; k < a.ntrig; k++) {
            char t[64];
            span_text(u, a.trig[k], t, sizeof t);
            const Color c = a.trig[k].role == voice::NEGATION ? theme->faint : role_color(a.trig[k].role);
            const int pw = p4lcd::Canvas::textBox(0, 0, t, style(13, c, true)).w + 24;
            if (cx + pw > x + wd)
                break;
            pu::pill(p, cx, y + 29, 22, t, pu::mix(theme->panel, c, 0.18f), c, 13);
            cx += pw + 5;
        }
    }
    /* a note of the world's that no row took ("the thermostat goes from 10 to 30"), or how many more */
    const int foot = ACTIONS.y + ACTIONS.h - 26;
    if (!answer_shown && w.answer()[0] && rows < 3)
        p.fit({x + 34, foot, wd - 34, 20}, w.answer(), style(13, theme->muted), "demo.answer");
    else if (u.r.n > rows) {
        char more[24];
        snprintf(more, sizeof more, "+%d more", u.r.n - rows);
        p.fit({x + 34, foot, 200, 20}, more, style(13, theme->muted), "demo.more");
    }
}

/* ---- Say ---- */

void draw_say(Painter &p)
{
    p.fill(theme->bg);
    pu::card(p, *theme, SAY, "Say", "demo.say");
    p.fit({SAY.x + 80, SAY.y + 14, SAY.w - 96, 24}, "or tap one: it runs as if heard",
          style(13, theme->muted, false, Align::RIGHT, VAlign::MIDDLE), "demo.say.note");
    for (int i = 0; i < SAY_CHIPS; i++) {
        const Button &b = buttons[B_SAY0 + i];
        p.roundFrame(b.r.x, b.r.y, b.r.w, b.r.h, 8, b.pressed ? theme->accent : theme->line, theme->soft, 1);
        p.fit({b.r.x + 10, b.r.y, b.r.w - 20, b.r.h}, demo_page().say[i],
              style(15, theme->ink, false, Align::LEFT, VAlign::MIDDLE), "demo.say.chip");
    }
}

/* ---- Rules ---- */

/* a list of phrases joined by commas */
static void joined(const char *const *list, char *out, int cap)
{
    int o = 0;
    out[0] = 0;
    for (int i = 0; list && list[i] && o < cap; i++)
        o += snprintf(out + o, cap - o, "%s%s", i ? ", " : "", list[i]);
}

/* every phrase of every rule (all of them: the page must not hide one), wrapped, each row as tall as it needs; the
 * rows scroll in their box */
void draw_rules(Painter &p)
{
    p.fill(theme->bg);
    TextStyle st = style(16, theme->muted);
    st.wrap = W - 48;
    p.fit({24, 78, W - 48, 44},
          "An action fires when a part of the sentence holds one of its verbs, then one of its targets. \"and\", "
          "\"then\" and commas split the parts; \"not\" and \"except\" cancel the target after them.",
          st, "rules.intro");
    const Rect t = R_RULES_ROWS;
    p.roundFrame(t.x, t.y, t.w, t.h, 10, theme->line, theme->panel, 1);
    constexpr int CX[3] = {36, 206, 636}, CW[3] = {160, 414, 340};
    const TextStyle head = style(14, theme->muted, true);
    p.text(CX[0], t.y + 12, "Action", head);
    const Rect vb = p.text(CX[1], t.y + 12, "Verbs,", style(14, theme->accent, true));
    p.text(vb.x + vb.w + 6, t.y + 12, "commands said alone", style(14, theme->ok, true));
    p.text(CX[2], t.y + 12, "Targets", style(14, theme->warn, true));
    p.rect(t.x + 12, t.y + 34, t.w - 24, 1, theme->line);
    static char verbs[480], alone[240], targets[480];
    const voice::Demo &d = demo::rules();
    TextStyle vs = style(14, theme->accent, true), as = style(14, theme->ok, true), ts = style(14, theme->warn, true);
    vs.wrap = as.wrap = CW[1];
    ts.wrap = CW[2];
    /* one row: its height; drawn at y when c is given */
    auto row = [&](Painter *c, int y, const char *label, const char *note) {
        const int hv = verbs[0] ? p4lcd::Canvas::textBox(0, 0, verbs, vs).h : 0;
        const int ha = alone[0] ? p4lcd::Canvas::textBox(0, 0, alone, as).h : 0;
        const int ht = targets[0] ? p4lcd::Canvas::textBox(0, 0, targets, ts).h : 0;
        const int gap = hv && ha ? 3 : 0;
        const int h = std::max(std::max(hv + gap + ha, ht), note ? 40 : 20);
        if (c) {
            c->fit({CX[0], y, CW[0], 20}, label, style(16, theme->ink, true), "rules.label");
            if (note)
                c->fit({CX[0], y + 21, CW[0], 18}, note, style(13, theme->ok), "rules.note");
            if (hv)
                c->fit({CX[1], y, CW[1], hv}, verbs, vs, "rules.verbs");
            if (ha)
                c->fit({CX[1], y + hv + gap, CW[1], ha}, alone, as, "rules.alone");
            if (ht)
                c->fit({CX[2], y, CW[2], ht}, targets, ts, "rules.targets");
            else
                c->fit({CX[2], y, CW[2], 18}, "(none)", style(14, theme->faint), "rules.none");
            c->rect(t.x + 12, y + h + 6, t.w - 24, 1, theme->line);
        }
        return h + 12;
    };
    auto all = [&](Painter *c) {
        int y = rules_view.view.y + 4 - (c ? rules_view.offset : 0);
        for (int i = 0; i < d.n; i++) {
            const voice::Rule &r = d.rules[i];
            joined(r.verbs, verbs, sizeof verbs);
            joined(r.alone, alone, sizeof alone);
            joined(r.targets, targets, sizeof targets);
            const char *note = r.flags & voice::NUM_REQUIRED   ? "+ a number"
                               : r.flags & voice::NUM_OPTIONAL ? "+ a number, or not"
                                                               : nullptr;
            const int h = row(nullptr, y, r.label, note);
            if (c && y + h >= rules_view.view.y && y < rules_view.view.y + rules_view.view.h)
                row(c, y, r.label, note);
            y += h;
        }
        if (d.places) { /* the places: said anywhere in the part */
            verbs[0] = alone[0] = 0;
            joined(d.places, targets, sizeof targets);
            const int h = row(nullptr, y, "places", "anywhere in it");
            if (c && y + h >= rules_view.view.y && y < rules_view.view.y + rules_view.view.h)
                row(c, y, "places", "anywhere in it");
            y += h;
        }
        return y + (c ? rules_view.offset : 0) - rules_view.view.y;
    };
    rules_view.follow = false;
    rules_view.update(all(nullptr));
    Painter c = p.clip(rules_view.view);
    all(&c);
    rules_view.edges(c, theme->panel);
    rules_view.scrollbar(p, t.x + t.w - 14, theme->mark);
}

} // namespace ui
