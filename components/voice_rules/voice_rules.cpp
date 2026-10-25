// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * voice_rules.hpp: the words into tokens (lowercase, punctuation as part breaks, "don't" -> "do not", "what's" ->
 * "what is"), a number's words merged into one token (when the demo takes numbers), the parts, the demo's phrases
 * matched longest first, then the actions. Phrases are compared with the tokens directly: no index, no heap, nothing
 * static, so it runs anywhere and on any task.
 */
#include "voice_rules.hpp"
#include <cstdio>
#include <cstring>

namespace voice {

namespace {

constexpr int MAX_RULES = 32;      /* a demo's rules (bit masks; the rest are ignored) */
constexpr int MAX_PENDING = 4;     /* places waiting for their target ("the kitchen and the bedroom lights") */

struct Token {
    char t[TOKEN_BYTES];
    uint8_t src, src_end;          /* its words in the utterance (a merged number may span several) */
    bool brk;                      /* a part ends after it (punctuation) */
    int num;                       /* a number's value, -1: not a number */
};

/* single tokens: the connectors, the negations; the words that may come before a command said alone ("please
 * stop"); the words that make a target a place, not a second order ("the crumbs in the living room"), and the words
 * allowed between them and the target */
const char *const CONNECTORS[] = {"and", "then", "but", "afterwards", nullptr};
const char *const NEGATIONS[] = {"not", "no", "except", "without", "never", nullptr};
const char *const FILLERS[] = {"please", "ok", "okay", "now", "just", "hey", "so", "right", "alright", "oh", "can",
                               "could", "would", "will", "you", "robot", "first", "let", "lets", "us", "go", "do",
                               nullptr};
/* what may also come before an order's verb: "I want you to clean the kitchen" (but not "I clean the kitchen") */
const char *const PREAMBLES[] = {"i want you to", "i need you to", "i would like you to", "id like you to",
                                 nullptr};
const char *const LOCATIVES[] = {"in", "inside", "under", "near", "by", "from", "of", "on", nullptr};
const char *const DETERMINERS[] = {"the", "my", "a", "this", "that", "your", nullptr};
/* "what's" -> "what is" (other 's: a possessive, dropped) */
const char *const CONTRACTED[] = {"what", "where", "how", "it", "that", "there", "here", "who", nullptr};
/* a PARTICLE verb's last word; what may stand between its target and it, and after it at the part's end */
const char *const PARTICLES[] = {"on", "off", "up", "down", nullptr};
const char *const BETWEEN[] = {"in", "inside", "at", "of", nullptr};
const char *const TRAILERS[] = {"please", "now", nullptr};

/* numbers: words up to "nineteen", the tens, the ordinals (index = value) */
const char *const UNITS[] = {"zero",    "one",     "two",       "three",    "four",    "five",    "six",
                             "seven",   "eight",   "nine",      "ten",      "eleven",  "twelve",  "thirteen",
                             "fourteen", "fifteen", "sixteen",  "seventeen", "eighteen", "nineteen", nullptr};
const char *const TENS[] = {"twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety", nullptr};
const char *const ORDINALS[] = {"zeroth",      "first",       "second",     "third",       "fourth",
                                "fifth",       "sixth",       "seventh",    "eighth",      "ninth",
                                "tenth",       "eleventh",    "twelfth",    "thirteenth",  "fourteenth",
                                "fifteenth",   "sixteenth",   "seventeenth", "eighteenth", "nineteenth", nullptr};
const char *const TENS_ORD[] = {"twentieth", "thirtieth", "fortieth", "fiftieth", "sixtieth", "seventieth",
                                "eightieth", "ninetieth", nullptr};

bool in(const char *const *list, const char *s)
{
    for (; list && *list; list++)
        if (!strcmp(*list, s))
            return true;
    return false;
}

int index_of(const char *const *list, const char *s)
{
    for (int i = 0; list && list[i]; i++)
        if (!strcmp(list[i], s))
            return i;
    return -1;
}

void push(Token *toks, int &n, const char *s, int len, int src)
{
    if (len <= 0 || n >= MAX_TOKENS)
        return;
    Token &k = toks[n++];
    if (len >= TOKEN_BYTES)
        len = TOKEN_BYTES - 1;
    memcpy(k.t, s, len);
    k.t[len] = 0;
    k.src = k.src_end = static_cast<uint8_t>(src);
    k.brk = false;
    k.num = -1;
}

/* one cleaned piece of a word (letters, digits, apostrophes) into tokens: "don't" -> "do" "not", "what's" ->
 * "what" "is", "kitchen's" -> "kitchen", apostrophes dropped */
void piece(Token *toks, int &n, char *s, int len, int src)
{
    while (len > 0 && s[0] == '\'')
        s++, len--;
    while (len > 0 && s[len - 1] == '\'')
        len--;
    if (len >= 3 && !strncmp(s + len - 3, "n't", 3)) {
        const int b = len - 3;
        if (b == 2 && !strncmp(s, "wo", 2))
            push(toks, n, "will", 4, src);
        else if (b == 2 && !strncmp(s, "ca", 2))
            push(toks, n, "can", 3, src);
        else
            push(toks, n, s, b, src);
        push(toks, n, "not", 3, src);
        return;
    }
    if (len == 4 && !strncmp(s, "dont", 4)) {
        push(toks, n, "do", 2, src);
        push(toks, n, "not", 3, src);
        return;
    }
    if (len > 2 && !strncmp(s + len - 2, "'s", 2)) {
        char base[TOKEN_BYTES];
        const int b = len - 2 < TOKEN_BYTES - 1 ? len - 2 : TOKEN_BYTES - 1;
        memcpy(base, s, b);
        base[b] = 0;
        if (in(CONTRACTED, base)) {
            push(toks, n, base, b, src);
            push(toks, n, "is", 2, src);
            return;
        }
        len -= 2;
    }
    char clean[TOKEN_BYTES];
    int m = 0;
    for (int i = 0; i < len && m < TOKEN_BYTES - 1; i++)
        if (s[i] != '\'')
            clean[m++] = s[i];
    push(toks, n, clean, m, src);
}

/* the words into tokens */
int tokenize(const char *const *words, int nwords, Token *toks)
{
    int n = 0;
    for (int w = 0; w < nwords && w < MAX_WORDS; w++) {
        char buf[64];
        int len = 0;
        const unsigned char *p = reinterpret_cast<const unsigned char *>(words[w] ? words[w] : "");
        for (; *p; p++) {
            unsigned char c = *p;
            if (c == 0xE2 && p[1] == 0x80 && (p[2] == 0x98 || p[2] == 0x99)) { /* curly apostrophes */
                c = '\'';
                p += 2;
            }
            const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'';
            if (c >= 'A' && c <= 'Z') {
                if (len < 63)
                    buf[len++] = static_cast<char>(c - 'A' + 'a');
                continue;
            }
            if (keep) {
                if (len < 63)
                    buf[len++] = static_cast<char>(c);
                continue;
            }
            /* a separator: the piece so far, and a part break after the last token for punctuation */
            piece(toks, n, buf, len, w);
            len = 0;
            const bool brk = c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?';
            if (brk && n > 0)
                toks[n - 1].brk = true;
        }
        piece(toks, n, buf, len, w);
    }
    return n;
}

/* the number starting at token i ("twenty two", "one hundred and eighty", "21", "5th", "the third"): its tokens
 * (0: none) and value */
int number_at(const Token *toks, int i, int n, int &value)
{
    const char *t = toks[i].t;
    if (t[0] >= '0' && t[0] <= '9') {
        int v = 0, k = 0;
        for (; t[k] >= '0' && t[k] <= '9' && k < 5; k++)
            v = v * 10 + (t[k] - '0');
        if (t[k] && strcmp(t + k, "st") && strcmp(t + k, "nd") && strcmp(t + k, "rd") && strcmp(t + k, "th"))
            return 0;
        value = v;
        return 1;
    }
    if (!strcmp(t, "first")) { /* "the first slide" (not "first clean the kitchen") */
        if (i > 0 && in(DETERMINERS, toks[i - 1].t)) {
            value = 1;
            return 1;
        }
        return 0;
    }
    enum { START, UNIT, TEN, HUNDRED, AND } prev = START;
    int total = 0, cur = 0, len = 0;
    for (int j = i; j < n; j++) {
        const char *w = toks[j].t;
        const int u = index_of(UNITS, w), te = index_of(TENS, w), o = index_of(ORDINALS, w);
        const int to = index_of(TENS_ORD, w);
        const bool open = prev == START || prev == HUNDRED || prev == AND;
        bool last = false;
        if (u >= 0) {
            if (prev == TEN && u >= 1 && u <= 9)
                cur += u;
            else if (open)
                cur += u;
            else
                break;
            prev = UNIT;
        } else if (te >= 0) {
            if (!open)
                break;
            cur += 20 + 10 * te;
            prev = TEN;
        } else if (o >= 1) { /* an ordinal ends the number: "twenty first", "second" */
            if (prev == TEN && o <= 9)
                cur += o;
            else if (open && o >= 2)
                cur += o;
            else
                break;
            last = true;
        } else if (to >= 0) {
            if (!open)
                break;
            cur += 20 + 10 * to;
            last = true;
        } else if (!strcmp(w, "hundred")) {
            if ((prev != UNIT && prev != TEN) || cur % 100 == 0 || cur >= 100)
                break;
            cur *= 100;
            prev = HUNDRED;
        } else if (!strcmp(w, "thousand")) {
            if (prev == START || prev == AND)
                break;
            total += cur * 1000;
            cur = 0;
            prev = HUNDRED;
        } else if (!strcmp(w, "and")) { /* only inside a number: "one hundred and eighty" */
            if (prev != HUNDRED || j + 1 >= n || toks[j].brk ||
                (index_of(UNITS, toks[j + 1].t) < 1 && index_of(TENS, toks[j + 1].t) < 0))
                break;
            prev = AND;
            continue;
        } else {
            break;
        }
        len = j - i + 1;
        if (last || toks[j].brk)
            break;
    }
    if (!len)
        return 0;
    value = total + cur;
    return len;
}

/* each number's tokens into one ("180"), spanning their words */
void merge_numbers(Token *toks, int &n)
{
    int w = 0;
    for (int i = 0; i < n;) {
        int v = 0;
        const int k = number_at(toks, i, n, v);
        if (k > 0) {
            Token m = toks[i];
            snprintf(m.t, sizeof m.t, "%d", v);
            m.num = v;
            m.src_end = toks[i + k - 1].src_end;
            m.brk = toks[i + k - 1].brk;
            toks[w++] = m;
            i += k;
        } else {
            toks[w++] = toks[i++];
        }
    }
    n = w;
}

/* a phrase's word that is a number word ("one" of "go back one slide"): its value, -1 */
int word_value(const char *p, int len)
{
    char w[TOKEN_BYTES];
    if (len <= 0 || len >= TOKEN_BYTES)
        return -1;
    memcpy(w, p, len);
    w[len] = 0;
    const int u = index_of(UNITS, w);
    if (u >= 0)
        return u;
    const int t = index_of(TENS, w);
    return t >= 0 ? 20 + 10 * t : -1;
}

/* phrase (lowercase words separated by single spaces) against tokens [pos, end): its token count, or 0. A merged
 * number matches its word ("go back one slide" when numbers are read) */
int match(const char *phrase, const Token *toks, int pos, int end)
{
    int k = 0;
    const char *p = phrase;
    while (*p) {
        if (pos + k >= end)
            return 0;
        const char *e = strchr(p, ' ');
        const int len = e ? static_cast<int>(e - p) : static_cast<int>(strlen(p));
        const Token &tk = toks[pos + k];
        const bool same = static_cast<int>(strlen(tk.t)) == len && !strncmp(tk.t, p, len);
        if (!same && (tk.num < 0 || tk.src != tk.src_end || word_value(p, len) != tk.num))
            return 0;
        k++;
        p += len;
        if (*p == ' ')
            p++;
        /* a phrase may not run over a part break */
        if (*p && toks[pos + k - 1].brk)
            return 0;
    }
    return k;
}

/* what the longest phrase at a position is, for every rule */
struct Hit {
    int pos, len;
    uint32_t verb, target, alone;  /* the rules it is a verb / target / alone phrase of */
    int8_t tidx[MAX_RULES];        /* the target's index in rule r's targets */
    int8_t place;                  /* a place of the demo's (index), -1 */
    bool neg;
};

bool longest(const Demo &d, const Token *toks, int pos, int end, Hit &h)
{
    h = Hit();
    h.pos = pos;
    h.place = -1;
    for (int r = 0; r < MAX_RULES; r++)
        h.tidx[r] = -1;
    auto consider = [&](const char *const *list, int r, int kind) {
        for (int i = 0; list && list[i]; i++) {
            const int k = match(list[i], toks, pos, end);
            if (!k || k < h.len)
                continue;
            if (k > h.len) { /* a longer phrase: what was found so far was a part of it */
                h.len = k;
                h.verb = h.target = h.alone = 0;
                h.place = -1;
                for (int q = 0; q < MAX_RULES; q++)
                    h.tidx[q] = -1;
            }
            if (kind == 0)
                h.verb |= 1u << r;
            else if (kind == 1) {
                if (!(h.target & (1u << r)))
                    h.tidx[r] = static_cast<int8_t>(i);
                h.target |= 1u << r;
            } else if (kind == 2) {
                h.alone |= 1u << r;
            } else if (h.place < 0) {
                h.place = static_cast<int8_t>(i);
            }
        }
    };
    for (int r = 0; r < d.n && r < MAX_RULES; r++) {
        consider(d.rules[r].verbs, r, 0);
        consider(d.rules[r].targets, r, 1);
        consider(d.rules[r].alone, r, 2);
    }
    consider(d.places, 0, 3);
    if (h.place >= 0 && (h.verb || h.target || h.alone)) /* (a phrase of a rule is that, not a place) */
        h.place = -1;
    if (h.len && (h.verb || h.target || h.alone || h.place >= 0))
        return true;
    h.len = 0;
    if (in(NEGATIONS, toks[pos].t)) {
        h.len = 1;
        h.neg = true;
        return true;
    }
    return false;
}

/* whether tokens [from, to) may stand before an order: fillers, negations ("don't"), a preamble ("I want you to") */
bool order_start(const Token *toks, int from, int to)
{
    for (int q = from; q < to;) {
        if (in(FILLERS, toks[q].t) || in(NEGATIONS, toks[q].t) || toks[q].t[0] == 0) {
            q++;
            continue;
        }
        int k = 0;
        for (int i = 0; PREAMBLES[i] && !k; i++)
            k = match(PREAMBLES[i], toks, q, to);
        if (!k)
            return false;
        q += k;
    }
    return true;
}

int lowest(uint32_t m)
{
    for (int r = 0; r < 32; r++)
        if (m & (1u << r))
            return r;
    return -1;
}

/* the hits of a part, and which hit each token belongs to */
struct Part {
    Hit hits[MAX_TOKENS];
    int nh = 0;
    int8_t hit_of[MAX_TOKENS];
    bool place_tok(int q) const { return hit_of[q] >= 0 && hits[hit_of[q]].place >= 0; }
};

/* a PARTICLE rule's verb "X P" split or after its target: "turn the lights off", "lights off everywhere". X: one word
 * at the order's start, or none; then determiners and places, the target, then determiners, places and "in"; P; then
 * only those and "please"/"now" to the part's end. Returns the rules (and the tokens of X, -1 if none, and P). */
uint32_t particle_verb(const Demo &d, const Token *toks, const Part &pt, int pos, int end, uint32_t prules, int &xtok,
                       int &ptok)
{
    for (int i = 0; i < pt.nh; i++) {
        const Hit &h = pt.hits[i];
        if (h.neg || !(h.target & prules))
            continue;
        int k = h.pos;
        while (k > pos && (in(DETERMINERS, toks[k - 1].t) || pt.place_tok(k - 1)))
            k--;
        int x = -1;
        if (!order_start(toks, pos, k)) {
            if (k - 1 < pos || !order_start(toks, pos, k - 1))
                continue;
            /* X: a plain word, or one that is only a verb itself ("put" of "put the burner to 5") */
            const int xh = pt.hit_of[k - 1];
            if (xh >= 0 && (pt.hits[xh].len != 1 || pt.hits[xh].target || pt.hits[xh].place >= 0 ||
                            pt.hits[xh].alone))
                continue;
            x = k - 1;
        }
        int j = h.pos + h.len;
        while (j < end && (in(DETERMINERS, toks[j].t) || in(BETWEEN, toks[j].t) || pt.place_tok(j)))
            j++;
        if (j >= end || !in(PARTICLES, toks[j].t))
            continue;
        int q = j + 1;
        while (q < end && (in(DETERMINERS, toks[q].t) || in(BETWEEN, toks[q].t) || pt.place_tok(q) ||
                           in(TRAILERS, toks[q].t)))
            q++;
        if (q < end)
            continue;
        uint32_t m = 0;
        for (int r = 0; r < d.n && r < MAX_RULES; r++) {
            if (!(h.target & prules & (1u << r)))
                continue;
            for (const char *const *v = d.rules[r].verbs; v && *v; v++) {
                const char *sp = strrchr(*v, ' ');
                if (!sp || strcmp(sp + 1, toks[j].t))
                    continue;
                const size_t xl = static_cast<size_t>(sp - *v);
                if (x < 0 || (strlen(toks[x].t) == xl && !strncmp(*v, toks[x].t, xl))) {
                    m |= 1u << r;
                    break;
                }
            }
        }
        if (m) {
            xtok = x;
            ptok = j;
            return m;
        }
    }
    return 0;
}

struct Builder {
    const Token *toks;
    Result &out;
    Role trole[MAX_TOKENS];

    Span span(int pos, int len, Role role) const
    {
        Span s;
        const int a = toks[pos].src, b = toks[pos + len - 1].src_end;
        s.first = static_cast<uint8_t>(a);
        s.count = static_cast<uint8_t>(b - a + 1);
        s.role = role;
        return s;
    }

    void mark(int pos, int len, Role role)
    {
        for (int i = pos; i < pos + len; i++)
            if (trole[i] == NONE || role == VERB || (role == TARGET && trole[i] != VERB) ||
                (role == NUMBER && trole[i] != VERB && trole[i] != TARGET))
                trole[i] = role;
    }

    void add(int rule, int target, int place, int value, bool skipped, const Span *trig, int ntrig)
    {
        if (out.n > 0) { /* the same action twice in a row: once */
            const Action &l = out.actions[out.n - 1];
            if (l.rule == rule && l.target == target && l.place == place && l.value == value &&
                l.skipped == skipped)
                return;
        }
        if (out.n >= MAX_ACTIONS)
            return;
        Action &a = out.actions[out.n++];
        a.rule = rule;
        a.target = target;
        a.place = place;
        a.value = value;
        a.skipped = skipped;
        a.ntrig = 0;
        for (int i = 0; i < ntrig && i < MAX_TRIGGERS; i++)
            a.trig[a.ntrig++] = trig[i];
    }
};

} // namespace

void parse(const Demo &d, const char *const *words, int nwords, Result &out, Context *ctx)
{
    Token toks[MAX_TOKENS];
    int nt = tokenize(words, nwords, toks);
    uint32_t prules = 0;
    bool numbers = false;
    for (int r = 0; r < d.n && r < MAX_RULES; r++) {
        numbers = numbers || (d.rules[r].flags & (NUM_REQUIRED | NUM_OPTIONAL));
        if (d.rules[r].flags & PARTICLE)
            prules |= 1u << r;
    }
    if (numbers)
        merge_numbers(toks, nt);
    out.nwords = nwords < MAX_WORDS ? nwords : MAX_WORDS;
    out.n = 0;
    out.reason = NO_WORDS;
    out.continued = false;
    Builder b{toks, out, {}};
    for (int i = 0; i < MAX_TOKENS; i++)
        b.trole[i] = NONE;

    /* the verb carried into parts without one, and the target carried to a part with only a place: from the
     * previous utterance if this one continues it */
    uint32_t carry = 0, tcarry = 0;
    int8_t tcarry_idx[MAX_RULES];
    for (int r = 0; r < MAX_RULES; r++)
        tcarry_idx[r] = -1;
    Span carry_span = {0, 0, VERB};
    bool carry_here = false;       /* the carried verb was said in this utterance (its words can be shown) */
    if (nt > 0 && in(CONNECTORS, toks[0].t) && ctx && ctx->verb_rules) {
        out.continued = true;
        carry = static_cast<uint32_t>(ctx->verb_rules);
        tcarry = static_cast<uint32_t>(ctx->target_rules);
        for (int r = 0; r < MAX_RULES; r++)
            tcarry_idx[r] = ctx->target_index[r];
    }
    bool no_target = false, no_verb = false, mismatch = false, no_number = false;
    /* places said with a verb before their target ("turn on the kitchen and the bedroom lights") */
    int pend_place[MAX_PENDING];
    Span pend_span[MAX_PENDING];
    bool pend_neg[MAX_PENDING];
    int npend = 0;

    Part pt;
    int pos = 0;
    while (pos < nt) {
        /* one part: up to a connector or a break */
        int end = pos;
        while (end < nt && !in(CONNECTORS, toks[end].t)) {
            end++;
            if (toks[end - 1].brk)
                break;
        }
        pt.nh = 0;
        for (int q = pos; q < end; q++)
            pt.hit_of[q] = -1;
        for (int p = pos; p < end;) {
            Hit h;
            if (longest(d, toks, p, end, h)) {
                for (int q = p; q < p + h.len; q++)
                    pt.hit_of[q] = static_cast<int8_t>(pt.nh);
                pt.hits[pt.nh++] = h;
                p += h.len;
            } else {
                p++;
            }
        }
        const Hit *hits = pt.hits;
        const int nh = pt.nh;
        /* the part's number */
        int numtok = -1;
        for (int q = pos; q < end && numtok < 0; q++)
            if (toks[q].num >= 0)
                numtok = q;
        /* the part's verb, its negations, its targets and places */
        int verb = -1;                 /* hit index */
        bool part_neg = false;
        int part_neg_hit = -1, pending_neg = -1;
        int tneg[MAX_TOKENS];          /* per hit: the negation hit that cancels it, -1 */
        for (int i = 0; i < nh; i++) {
            tneg[i] = -1;
            const Hit &h = hits[i];
            if (h.neg) {
                if (verb < 0) {
                    part_neg = true;
                    part_neg_hit = i;
                } else {
                    pending_neg = i;
                }
                continue;
            }
            /* the part's verb: an order's, so at its start ("I clean the kitchen" is not one) */
            if (h.verb && verb < 0 && !h.target && order_start(toks, pos, h.pos))
                verb = i;
            if ((h.target || h.place >= 0) && pending_neg >= 0) {
                tneg[i] = pending_neg;
                pending_neg = -1;
            }
        }
        /* a verb split from its particle, or after the target ("turn the lights off", "lights off"): it wins over a
         * one-word verb at the start ("put the kettle on" is not "put" of "put the burner to 5") */
        int xtok = -1, ptok = -1;
        const uint32_t pfound = prules ? particle_verb(d, toks, pt, pos, end, prules, xtok, ptok) : 0;
        if (pfound && verb >= 0) {
            if (xtok == hits[verb].pos && hits[verb].len == 1)
                verb = -1;
            else
                xtok = ptok = -1;
        }
        const uint32_t pverb = verb < 0 ? pfound : 0;
        uint32_t vr = 0;
        Span vspan[2] = {{0, 0, VERB}, {0, 0, VERB}};
        int nvspan = 0;
        bool vshow = false;
        if (verb >= 0) {
            vr = hits[verb].verb;
            vspan[nvspan++] = b.span(hits[verb].pos, hits[verb].len, VERB);
            vshow = true;
        } else if (pverb) {
            vr = pverb;
            if (xtok >= 0)
                vspan[nvspan++] = b.span(xtok, 1, VERB);
            vspan[nvspan++] = b.span(ptok, 1, VERB);
            vshow = true;
        } else if (carry) {
            vr = carry;
            vspan[nvspan++] = carry_span;
            vshow = carry_here;
        }
        auto mark_verb = [&]() {
            if (verb >= 0)
                b.mark(hits[verb].pos, hits[verb].len, VERB);
            if (pverb) {
                if (xtok >= 0)
                    b.mark(xtok, 1, VERB);
                b.mark(ptok, 1, VERB);
            }
        };
        /* the number an action of rule r takes (-1: none); false: it needs one and the part has none */
        auto value_for = [&](int r, int &value) {
            value = -1;
            if (!(d.rules[r].flags & (NUM_REQUIRED | NUM_OPTIONAL)))
                return true;
            if (numtok >= 0) {
                value = toks[numtok].num;
                return true;
            }
            return !(d.rules[r].flags & NUM_REQUIRED);
        };
        /* the first of the candidate rules whose number is there ("dim the lights" is not "dim the lights to 30
         * percent"); -1: none */
        auto pick = [&](uint32_t cand, int &value) {
            for (uint32_t m = cand; m; m &= m - 1) {
                const int r = lowest(m);
                if (value_for(r, value))
                    return r;
            }
            return -1;
        };
        /* the part's first place (not cancelled on its own) */
        int place_hit = -1;
        for (int i = 0; i < nh && place_hit < 0; i++)
            if (!hits[i].neg && hits[i].place >= 0 && !hits[i].target)
                place_hit = i;
        /* one action, with its triggers */
        auto fire = [&](int r, int tidx, const Span *tspan, int place, const Span *pspan, int value, int ng) {
            Span trig[MAX_TRIGGERS];
            int ntr = 0;
            if (vshow)
                for (int k = 0; k < nvspan && k < 2 && ntr < MAX_TRIGGERS; k++)
                    trig[ntr++] = vspan[k];
            if (tspan && ntr < MAX_TRIGGERS)
                trig[ntr++] = *tspan;
            if (pspan && ntr < MAX_TRIGGERS)
                trig[ntr++] = *pspan;
            if (value >= 0 && numtok >= 0 && ntr < MAX_TRIGGERS) {
                trig[ntr++] = b.span(numtok, 1, NUMBER);
                b.mark(numtok, 1, NUMBER);
            }
            if (ng >= 0 && ntr < MAX_TRIGGERS) {
                trig[ntr++] = b.span(hits[ng].pos, hits[ng].len, NEGATION);
                b.mark(hits[ng].pos, hits[ng].len, NEGATION);
            }
            b.add(r, tidx, place, value, ng >= 0, trig, ntr);
        };
        bool used_verb_as_verb = false;
        bool any_target = false;
        const int actions_before = out.n;
        for (int i = 0; i < nh; i++) {
            const Hit &h = hits[i];
            if (!h.target || h.neg)
                continue;
            any_target = true;
            b.mark(h.pos, h.len, TARGET);
            /* a place after a first target: "the crumbs in the living room" */
            if (out.n > actions_before) {
                int q = h.pos - 1;
                while (q > pos && in(DETERMINERS, toks[q].t))
                    q--;
                if (q >= pos && in(LOCATIVES, toks[q].t))
                    continue;
            }
            const uint32_t cand = h.target & vr;
            if (!cand) {
                (vr ? mismatch : no_verb) = true;
                continue;
            }
            int value;
            const int r = pick(cand, value);
            if (r < 0) {
                no_number = true;
                continue;
            }
            const int ng = tneg[i] >= 0 ? tneg[i] : (part_neg ? part_neg_hit : -1);
            /* places said before this target, waiting for it */
            for (int k = 0; k < npend; k++) {
                const int png = pend_neg[k] ? (part_neg ? part_neg_hit : -1) : ng;
                fire(r, h.tidx[r], nullptr, pend_place[k], &pend_span[k], value, png);
            }
            npend = 0;
            const Span ts = b.span(h.pos, h.len, TARGET);
            Span ps;
            int place = -1;
            if (place_hit >= 0) {
                place = hits[place_hit].place;
                ps = b.span(hits[place_hit].pos, hits[place_hit].len, TARGET);
                b.mark(hits[place_hit].pos, hits[place_hit].len, TARGET);
            }
            if (verb >= 0 || pverb)
                used_verb_as_verb = true;
            mark_verb();
            fire(r, h.tidx[r], &ts, place, place >= 0 ? &ps : nullptr, value, ng);
            tcarry = h.target;
            for (int q = 0; q < MAX_RULES; q++)
                tcarry_idx[q] = h.tidx[q];
        }
        /* places without a target: the previous target's ("... and the bedroom"), or waiting for the next one */
        if (!any_target && place_hit >= 0 && vr) {
            for (int i = 0; i < nh; i++) {
                const Hit &h = hits[i];
                if (h.neg || h.place < 0 || h.target)
                    continue;
                const int ng = tneg[i] >= 0 ? tneg[i] : (part_neg ? part_neg_hit : -1);
                const Span ps = b.span(h.pos, h.len, TARGET);
                if (tcarry & vr) {
                    int value;
                    const int r = pick(tcarry & vr, value);
                    if (r < 0) {
                        no_number = true;
                        continue;
                    }
                    b.mark(h.pos, h.len, TARGET);
                    mark_verb();
                    if (verb >= 0 || pverb)
                        used_verb_as_verb = true;
                    fire(r, tcarry_idx[r], nullptr, h.place, &ps, value, ng);
                    any_target = true;
                } else if (npend < MAX_PENDING) {
                    b.mark(h.pos, h.len, TARGET);
                    pend_place[npend] = h.place;
                    pend_span[npend] = ps;
                    pend_neg[npend] = ng >= 0;
                    npend++;
                }
            }
        }
        /* phrases that act alone, only at the part's start (after fillers: "please stop", not "I will continue
         * tomorrow"); a verb phrase used with a target above does not also act alone */
        bool alone_fired = false;
        for (int i = 0; i < nh; i++) {
            const Hit &h = hits[i];
            if (!h.alone || (i == verb && used_verb_as_verb))
                continue;
            if (!order_start(toks, pos, h.pos))
                continue;
            if (d.rules[lowest(h.alone)].flags & WHOLE) {
                int q = h.pos + h.len;
                while (q < end && (in(TRAILERS, toks[q].t) || in(DETERMINERS, toks[q].t) || in(BETWEEN, toks[q].t) ||
                                   pt.place_tok(q)))
                    q++;
                if (q < end)
                    continue;
            }
            b.mark(h.pos, h.len, VERB);
            Span trig[MAX_TRIGGERS];
            int ntr = 0;
            trig[ntr++] = b.span(h.pos, h.len, VERB);
            if (part_neg) {
                trig[ntr++] = b.span(hits[part_neg_hit].pos, hits[part_neg_hit].len, NEGATION);
                b.mark(hits[part_neg_hit].pos, hits[part_neg_hit].len, NEGATION);
            }
            b.add(lowest(h.alone), -1, -1, -1, part_neg, trig, ntr);
            alone_fired = true;
        }
        /* a verb with no target: an action that takes none, else "what?" (unless its places wait for one) */
        if (verb >= 0 && !any_target && !alone_fired && !npend) {
            uint32_t untargeted = 0;
            for (int r = 0; r < d.n && r < MAX_RULES; r++)
                if ((vr & (1u << r)) && !(d.rules[r].targets && d.rules[r].targets[0]))
                    untargeted |= 1u << r;
            b.mark(hits[verb].pos, hits[verb].len, VERB);
            if (untargeted) {
                int value;
                const int r = pick(untargeted, value);
                if (r >= 0) {
                    Span trig[MAX_TRIGGERS] = {vspan[0]};
                    int ntr = 1;
                    if (value >= 0) {
                        trig[ntr++] = b.span(numtok, 1, NUMBER);
                        b.mark(numtok, 1, NUMBER);
                    }
                    b.add(r, -1, -1, value, part_neg, trig, ntr);
                } else {
                    no_number = true;
                }
            } else {
                no_target = true;
            }
        }
        if (verb >= 0) {
            carry = hits[verb].verb;
            carry_span = vspan[0];
            carry_here = true;
        } else if (pverb) {
            carry = pverb;
            carry_span = vspan[nvspan - 1];
            carry_here = true;
        }
        /* the connector after the part */
        pos = end;
        if (pos < nt && in(CONNECTORS, toks[pos].t)) {
            b.mark(pos, 1, CONNECTOR);
            pos++;
        }
    }
    if (npend)
        no_target = true;
    /* the words' roles: the strongest of their tokens' */
    for (int w = 0; w < out.nwords; w++)
        out.role[w] = NONE;
    for (int i = 0; i < nt; i++) {
        const Role t = b.trole[i];
        for (int w = toks[i].src; w <= toks[i].src_end && w < out.nwords; w++) {
            Role &r = out.role[w];
            if (t == VERB || (t == TARGET && r != VERB) || (t == NUMBER && r != VERB && r != TARGET) ||
                (t == NEGATION && r == NONE) || (t == CONNECTOR && r == NONE))
                r = t;
        }
    }
    bool any = false, skipped = false;
    for (int i = 0; i < out.n; i++)
        (out.actions[i].skipped ? skipped : any) = true;
    out.reason = any        ? OK
                 : skipped   ? ALL_SKIPPED
                 : mismatch  ? MISMATCH
                 : no_number ? NO_NUMBER
                 : no_target ? NO_TARGET
                 : no_verb   ? NO_VERB
                             : NO_WORDS;
    if (ctx) {
        ctx->verb_rules = static_cast<int>(carry);
        ctx->verb_first = carry_here ? carry_span.first : -1;
        ctx->verb_count = carry_here ? carry_span.count : 0;
        ctx->target_rules = static_cast<int>(tcarry);
        for (int r = 0; r < MAX_RULES; r++)
            ctx->target_index[r] = tcarry_idx[r];
    }
}

void parse_text(const Demo &d, const char *text, Result &out, Context *ctx)
{
    char buf[512];
    const char *words[MAX_WORDS];
    int n = 0;
    snprintf(buf, sizeof buf, "%s", text ? text : "");
    for (char *p = buf; *p && n < MAX_WORDS;) {
        while (*p == ' ')
            *p++ = 0;
        if (!*p)
            break;
        words[n++] = p;
        while (*p && *p != ' ')
            p++;
    }
    parse(d, words, n, out, ctx);
}

const char *describe(const Demo &d, const Result &r, char *out, int cap)
{
    static const char *const REASONS[] = {"ok",       "no_words",    "no_target", "no_verb",
                                          "mismatch", "all_skipped", "no_number"};
    int len = 0;
    out[0] = 0;
    for (int i = 0; i < r.n && len < cap; i++) {
        const Action &a = r.actions[i];
        const Rule &ru = d.rules[a.rule];
        len += snprintf(out + len, cap - len, "%s%s%s%s", i ? "; " : "", a.skipped ? "-" : "", ru.id,
                        a.target >= 0 ? ":" : "");
        if (a.target >= 0 && len < cap)
            len += snprintf(out + len, cap - len, "%s", ru.targets[a.target]);
        if (a.place >= 0 && d.places && len < cap)
            len += snprintf(out + len, cap - len, "@%s", d.places[a.place]);
        if (a.value >= 0 && len < cap)
            len += snprintf(out + len, cap - len, "=%d", a.value);
    }
    if (r.reason != OK && len < cap)
        len += snprintf(out + len, cap - len, "%s(%s)", r.n ? " " : "", REASONS[r.reason]);
    return out;
}

} // namespace voice
