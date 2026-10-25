// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * TrueType text (p4lcd.hpp): Font, Canvas::text(x, y, utf8, style), Canvas::textBox.
 *
 * stb_truetype (third_party/stb_truetype.h, public domain or MIT, compiled here with its functions static) turns a
 * glyph's outline into a coverage mask: 0-255 per pixel, the exact area covered. The masks are kept in a cache keyed
 * by font, size, outline and glyph. An outline's mask is the glyph's widened by a disc of the outline's radius, its
 * rim anti-aliased. Lines are laid out with the font's advances and kerning; each glyph's origin on a whole pixel.
 * Blending, per channel, rounded: c = background + (colour - background) x coverage / 255.
 *
 * Fonts are trusted data: stb_truetype does not check a damaged file.
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "p4lcd.hpp"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

/* the built-in fonts (fonts/, EMBED_FILES) */
extern const uint8_t inter_regular_start[] asm("_binary_inter_regular_ttf_start");
extern const uint8_t inter_regular_end[] asm("_binary_inter_regular_ttf_end");
extern const uint8_t inter_bold_start[] asm("_binary_inter_bold_ttf_start");
extern const uint8_t inter_bold_end[] asm("_binary_inter_bold_ttf_end");

namespace p4lcd {

struct Font::Impl {
    stbtt_fontinfo info;
    uint32_t id;                    /* the cache's key; never reused */
    int ascent, descent, gap;       /* font units */
};

static std::atomic<uint32_t> s_next_id{1};

Font::~Font() { delete impl_; }

bool Font::load(const void *data, size_t size, int index)
{
    delete impl_;
    impl_ = nullptr;
    auto *p = static_cast<const unsigned char *>(data);
    if (!p || size < 12)
        return false;
    int off = stbtt_GetFontOffsetForIndex(p, index);
    if (off < 0 || static_cast<size_t>(off) >= size)
        return false;
    Impl *f = new Impl();
    if (!stbtt_InitFont(&f->info, p, off)) {
        delete f;
        return false;
    }
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->gap);
    f->id = s_next_id.fetch_add(1) & 0x7fffffff;
    impl_ = f;
    return true;
}

static const Font *builtin(const uint8_t *a, const uint8_t *b)
{
    Font *f = new Font();
    f->load(a, b - a);
    return f;
}

const Font &Font::regular()
{
    static const Font *f = builtin(inter_regular_start, inter_regular_end);
    return *f;
}

const Font &Font::bold()
{
    static const Font *f = builtin(inter_bold_start, inter_bold_end);
    return *f;
}

/* ---- the glyph cache ---- */

struct Glyph {
    uint64_t key;          /* 0: an empty slot */
    int16_t x0, y0;        /* the mask's top-left corner from the glyph's origin (on the baseline) */
    uint16_t w, h;
    uint8_t *mask;         /* w x h, nullptr when empty (a space) */
};

static constexpr int SLOTS = 2048;              /* a power of two; filled to 3/4 at most */
static constexpr size_t BUDGET = 256 * 1024;    /* the masks' bytes */
static Glyph *s_slots;
static int s_used;
static size_t s_bytes;

static SemaphoreHandle_t cache_lock()
{
    static StaticSemaphore_t mem;
    static SemaphoreHandle_t h = xSemaphoreCreateMutexStatic(&mem);
    return h;
}

static void cache_clear()
{
    for (int i = 0; i < SLOTS; i++)
        heap_caps_free(s_slots[i].mask);
    memset(s_slots, 0, SLOTS * sizeof(Glyph));
    s_used = 0;
    s_bytes = 0;
}

static uint32_t slot_of(uint64_t key)
{
    key ^= key >> 31;
    key *= 0x9e3779b97f4a7c15ull;
    return static_cast<uint32_t>(key >> 40) & (SLOTS - 1);
}

/* the glyph's mask at this scale, widened by `outline` pixels when > 0; nullptr when out of memory */
static const Glyph *glyph(const Font::Impl &f, int size16, float scale, int gi, int outline)
{
    if (!s_slots) {
        s_slots = static_cast<Glyph *>(heap_caps_calloc(SLOTS, sizeof(Glyph), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_slots)
            return nullptr;
    }
    const uint64_t key = (uint64_t)f.id << 33 | (uint64_t)size16 << 20 | (uint64_t)outline << 16 | (uint32_t)gi;
    uint32_t i = slot_of(key);
    for (; s_slots[i].key; i = (i + 1) & (SLOTS - 1))
        if (s_slots[i].key == key)
            return &s_slots[i];

    int ix0, iy0, ix1, iy1;
    stbtt_GetGlyphBitmapBox(&f.info, gi, scale, scale, &ix0, &iy0, &ix1, &iy1);
    int w = ix1 - ix0, h = iy1 - iy0;
    Glyph g = {key, 0, 0, 0, 0, nullptr};
    if (w > 0 && h > 0) {
        const int o = outline, W = w + 2 * o, H = h + 2 * o;
        if (s_used + 1 > SLOTS * 3 / 4 || s_bytes + (size_t)W * H > BUDGET) {
            cache_clear();
            i = slot_of(key);
        }
        uint8_t *m = static_cast<uint8_t *>(heap_caps_calloc(1, (size_t)W * H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!m)
            return nullptr;
        if (!o) {
            stbtt_MakeGlyphBitmap(&f.info, m, w, h, w, scale, scale, gi);
        } else {
            uint8_t *src = static_cast<uint8_t *>(heap_caps_malloc((size_t)w * h, MALLOC_CAP_8BIT));
            if (!src) {
                heap_caps_free(m);
                return nullptr;
            }
            stbtt_MakeGlyphBitmap(&f.info, src, w, h, w, scale, scale, gi);
            /* the disc: each offset weighted by how far its pixel lies inside radius o + 1/2 */
            for (int dy = -o; dy <= o; dy++)
                for (int dx = -o; dx <= o; dx++) {
                    float k = o + 0.5f - sqrtf((float)(dx * dx + dy * dy));
                    int wt = k >= 1 ? 256 : k <= 0 ? 0 : static_cast<int>(k * 256);
                    if (!wt)
                        continue;
                    for (int y = 0; y < h; y++) {
                        const uint8_t *s = src + y * w;
                        uint8_t *d = m + (size_t)(y + o + dy) * W + o + dx;
                        for (int x = 0; x < w; x++) {
                            int v = s[x] * wt >> 8;
                            if (v > d[x])
                                d[x] = static_cast<uint8_t>(v);
                        }
                    }
                }
            heap_caps_free(src);
        }
        g.x0 = static_cast<int16_t>(ix0 - o);
        g.y0 = static_cast<int16_t>(iy0 - o);
        g.w = static_cast<uint16_t>(W);
        g.h = static_cast<uint16_t>(H);
        g.mask = m;
        s_bytes += (size_t)W * H;
    } else if (s_used + 1 > SLOTS * 3 / 4) {
        cache_clear();
        i = slot_of(key);
    }
    s_slots[i] = g;
    s_used++;
    return &s_slots[i];
}

/* ---- layout ---- */

/* the next code point of UTF-8 text, p moved past it; a broken sequence gives U+FFFD */
static uint32_t next_cp(const char *&p)
{
    const uint8_t *s = reinterpret_cast<const uint8_t *>(p);
    uint32_t c = s[0];
    int n = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
    if (n < 0) {
        p++;
        return 0xfffd;
    }
    c &= n == 0 ? 0x7f : n == 1 ? 0x1f : n == 2 ? 0x0f : 0x07;
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            p += i;
            return 0xfffd;
        }
        c = c << 6 | (s[i] & 0x3f);
    }
    p += n + 1;
    return c;
}

struct Layout {
    const Font::Impl *f = nullptr;
    int size16 = 0;
    float scale = 0, ls = 0, line_h = 0, ascent = 0, descent = 0;
    struct Line {
        const char *s, *e;
        float w;
    };
    std::vector<Line> lines;
    int baseline0 = 0;      /* the first line's baseline */
    Rect block;             /* the lines' advance box, before outline and padding */
};

/* the glyph a code point draws with; -1: nothing (a control character) */
static int glyph_index(const Font::Impl &f, uint32_t cp)
{
    if (cp == '\t')
        cp = ' ';
    else if (cp < 0x20)
        return -1;
    return stbtt_FindGlyphIndex(&f.info, static_cast<int>(cp));
}

/* the width of the text [a, b): the pen's travel, without the last character's letter spacing */
static float measure(const Layout &L, const char *a, const char *b)
{
    float pen = 0;
    int prev = -1;
    bool any = false;
    for (const char *p = a; p < b;) {
        int gi = glyph_index(*L.f, next_cp(p));
        if (gi < 0)
            continue;
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&L.f->info, gi, &adv, &lsb);
        if (prev >= 0)
            pen += stbtt_GetGlyphKernAdvance(&L.f->info, prev, gi) * L.scale;
        pen += adv * L.scale + L.ls;
        prev = gi;
        any = true;
    }
    return any ? pen - L.ls : 0;
}

static const char *after_cp(const char *p)
{
    next_cp(p);
    return p;
}

/* one paragraph (no '\n') into lines at most `wrap` wide, broken between words (inside a word only when it alone is
 * wider) */
static void wrap_paragraph(Layout &L, const char *a, const char *end, int wrap)
{
    if (wrap <= 0) {
        L.lines.push_back({a, end, measure(L, a, end)});
        return;
    }
    for (;;) {
        const char *best = nullptr;
        float best_w = 0;
        for (const char *q = a; q < end;) {
            const char *we = q;
            while (we < end && *we == ' ')
                we++;
            while (we < end && *we != ' ')
                we++;
            float w = measure(L, a, we);
            if (w > wrap && best)
                break;
            best = we;
            best_w = w;
            if (w > wrap)
                break;
            q = we;
        }
        if (!best) { /* an empty paragraph */
            L.lines.push_back({a, a, 0});
            return;
        }
        if (best_w > wrap) { /* one word too wide: cut it after the last character that fits, at least one */
            const char *cut = after_cp(a);
            while (cut < best) {
                const char *n = after_cp(cut);
                if (measure(L, a, n) > wrap)
                    break;
                cut = n;
            }
            best = cut;
            best_w = measure(L, a, best);
        }
        L.lines.push_back({a, best, best_w});
        a = best;
        while (a < end && *a == ' ')
            a++;
        if (a >= end)
            return;
    }
}

/* ---- the line-break cache ---- */

/* a text's lines at one font, size, letter spacing and wrap width, kept: a text drawn again (in each band of
 * p4lcd_ui's renderer, in the next frame) is not measured again. Measuring was most of a page's drawing: 391 of
 * 457 ms for a page of wrapped rows, drawn in 11 bands. Keyed by the text's bytes: a changed text is a new entry. */
struct Breaks {
    uint64_t key; /* 0: an empty slot */
    uint32_t len;
    uint32_t n;
    struct {
        uint16_t s, e; /* the line's bytes in the text */
        float w;
    } line[12];
};

static constexpr int BREAK_SLOTS = 256; /* a power of two; filled to 3/4 at most (28 KB) */
static Breaks *s_breaks;
static int s_breaks_used;

static uint64_t breaks_key(const Layout &L, const char *s, size_t len, int wrap)
{
    uint64_t h = 14695981039346656037ull; /* FNV-1a over the bytes, then the style */
    for (size_t i = 0; i < len; i++)
        h = (h ^ static_cast<uint8_t>(s[i])) * 1099511628211ull;
    uint32_t ls;
    memcpy(&ls, &L.ls, sizeof ls);
    const uint64_t style[3] = {(uint64_t)L.f->id << 32 | (uint32_t)L.size16, (uint32_t)wrap, ls};
    for (uint64_t v : style)
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h ? h : 1;
}

static bool breaks_get(Layout &L, const char *s, size_t len, uint64_t key)
{
    bool hit = false;
    xSemaphoreTake(cache_lock(), portMAX_DELAY);
    if (s_breaks)
        for (uint32_t i = slot_of(key) & (BREAK_SLOTS - 1); s_breaks[i].key; i = (i + 1) & (BREAK_SLOTS - 1)) {
            const Breaks &b = s_breaks[i];
            if (b.key == key && b.len == len) {
                for (uint32_t k = 0; k < b.n; k++)
                    L.lines.push_back({s + b.line[k].s, s + b.line[k].e, b.line[k].w});
                hit = true;
                break;
            }
        }
    xSemaphoreGive(cache_lock());
    return hit;
}

static void breaks_put(const Layout &L, const char *s, size_t len, uint64_t key)
{
    if (L.lines.size() > 12 || len > 65535) /* (not kept: measured each time) */
        return;
    xSemaphoreTake(cache_lock(), portMAX_DELAY);
    if (!s_breaks)
        s_breaks = static_cast<Breaks *>(
            heap_caps_calloc(BREAK_SLOTS, sizeof(Breaks), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_breaks) {
        if (s_breaks_used + 1 > BREAK_SLOTS * 3 / 4) {
            memset(s_breaks, 0, BREAK_SLOTS * sizeof(Breaks));
            s_breaks_used = 0;
        }
        uint32_t i = slot_of(key) & (BREAK_SLOTS - 1);
        while (s_breaks[i].key)
            i = (i + 1) & (BREAK_SLOTS - 1);
        s_breaks_used++;
        Breaks &b = s_breaks[i];
        b.key = key;
        b.len = static_cast<uint32_t>(len);
        b.n = static_cast<uint32_t>(L.lines.size());
        for (uint32_t k = 0; k < b.n; k++)
            b.line[k] = {static_cast<uint16_t>(L.lines[k].s - s), static_cast<uint16_t>(L.lines[k].e - s),
                         L.lines[k].w};
    }
    xSemaphoreGive(cache_lock());
}

static bool lay_out(Layout &L, int x, int y, const char *s, const TextStyle &st)
{
    const Font &font = st.font ? *st.font : Font::regular();
    if (!s || !font.valid() || !(st.size >= 4 && st.size <= 256))
        return false;
    L.f = font.impl();
    L.size16 = static_cast<int>(lroundf(st.size * 16));
    L.scale = stbtt_ScaleForMappingEmToPixels(&L.f->info, L.size16 / 16.0f);
    L.ls = st.letter_spacing;
    L.ascent = L.f->ascent * L.scale;
    L.descent = L.f->descent * L.scale;
    L.line_h = (L.f->ascent - L.f->descent + L.f->gap) * L.scale * st.line_spacing;
    const size_t len = strlen(s);
    const uint64_t key = breaks_key(L, s, len, st.wrap);
    if (!breaks_get(L, s, len, key)) {
        for (const char *p = s;;) {
            const char *nl = strchr(p, '\n');
            const char *end = nl ? nl : p + strlen(p);
            wrap_paragraph(L, p, end, st.wrap);
            if (!nl)
                break;
            p = nl + 1;
        }
        breaks_put(L, s, len, key);
    }
    const int n = static_cast<int>(L.lines.size());
    const float height = L.ascent - L.descent + (n - 1) * L.line_h;
    float top = st.valign == VAlign::TOP        ? y
                : st.valign == VAlign::MIDDLE   ? y - height / 2
                : st.valign == VAlign::BASELINE ? y - L.ascent
                                                : y - height;
    /* rounded half up, not lroundf's half away from zero: the same under any whole-pixel shift, so a text drawn in
     * pieces (p4lcd_ui's bands, its top above one piece: negative y) lands on the same rows (Inter's ascent at 16 px
     * is 15.5) */
    L.baseline0 = static_cast<int>(floorf(top + L.ascent + 0.5f));
    float x0 = 1e9f, x1 = -1e9f;
    for (auto &ln : L.lines) {
        float lx = st.align == Align::LEFT ? x : st.align == Align::CENTER ? x - ln.w / 2 : x - ln.w;
        x0 = std::min(x0, lx);
        x1 = std::max(x1, lx + ln.w);
    }
    const float t = L.baseline0 - L.ascent;
    L.block.x = static_cast<int>(floorf(x0));
    L.block.y = static_cast<int>(floorf(t));
    L.block.w = static_cast<int>(ceilf(x1)) - L.block.x;
    L.block.h = static_cast<int>(ceilf(t + height)) - L.block.y;
    return true;
}

static Rect outer(const Layout &L, const TextStyle &st)
{
    int m = std::max(0, std::min(st.outline, 8)) + (st.box ? std::max(st.padding, 0) : 0);
    return {L.block.x - m, L.block.y - m, L.block.w + 2 * m, L.block.h + 2 * m};
}

Rect Canvas::textBox(int x, int y, const char *utf8, const TextStyle &st)
{
    Layout L;
    if (!lay_out(L, x, y, utf8, st))
        return {};
    return outer(L, st);
}

Rect Canvas::text(int x, int y, const char *utf8, const TextStyle &st)
{
    Layout L;
    if (!lay_out(L, x, y, utf8, st))
        return {};
    const Rect r = outer(L, st);
    if (!buf_)
        return r;
    /* nothing of it on this canvas (with a font size of margin for the glyphs' ink beyond their advance box): the
     * glyphs not looked at (in p4lcd_ui's bands, most texts lie in other bands) */
    const int far = static_cast<int>(st.size) + 1;
    if (r.x - far >= width() || r.y - far >= height() || r.x + r.w + far <= 0 || r.y + r.h + far <= 0)
        return r;
    if (st.box)
        fillRect(r.x, r.y, r.w, r.h, st.box_color);
    const int o = std::max(0, std::min(st.outline, 8));
    xSemaphoreTake(cache_lock(), portMAX_DELAY);
    /* the outlines of all lines first, so that no outline covers a neighbouring glyph; then the glyphs */
    for (int pass = o ? 0 : 1; pass < 2; pass++) {
        const Color c = pass ? st.color : st.outline_color;
        for (size_t i = 0; i < L.lines.size(); i++) {
            const auto &ln = L.lines[i];
            const int base = L.baseline0 + static_cast<int>(lroundf(i * L.line_h));
            float pen = st.align == Align::LEFT ? x : st.align == Align::CENTER ? x - ln.w / 2 : x - ln.w;
            int prev = -1;
            for (const char *p = ln.s; p < ln.e;) {
                int gi = glyph_index(*L.f, next_cp(p));
                if (gi < 0)
                    continue;
                if (prev >= 0)
                    pen += stbtt_GetGlyphKernAdvance(&L.f->info, prev, gi) * L.scale;
                const Glyph *g = glyph(*L.f, L.size16, L.scale, gi, pass ? 0 : o);
                if (g && g->mask)
                    blend(static_cast<int>(floorf(pen + 0.5f)) + g->x0, base + g->y0, g->w, g->h, g->mask, c);
                int adv, lsb;
                stbtt_GetGlyphHMetrics(&L.f->info, gi, &adv, &lsb);
                pen += adv * L.scale + L.ls;
                prev = gi;
            }
        }
    }
    xSemaphoreGive(cache_lock());
    return r;
}

static inline int div255(int v) { return (v + (v >= 0 ? 127 : -127)) / 255; }

void Canvas::blend(int x, int y, int w, int h, const uint8_t *mask, Color c)
{
    if (!buf_ || !mask)
        return;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0), x1 = std::min(x + w, width()), y1 = std::min(y + h, height());
    if (x0 >= x1 || y0 >= y1)
        return;
    /* picture (X, Y) -> the buffer's index base + X dx + Y dy (fillRect's rotation, one pixel at a time) */
    const ptrdiff_t pw = pw_, ph = ph_, s = stride_;
    ptrdiff_t base, dx, dy;
    switch (rotation_) {
    case 90: base = pw - 1; dx = s; dy = -1; break;
    case 180: base = (ph - 1) * s + pw - 1; dx = -1; dy = -s; break;
    case 270: base = (ph - 1) * s; dx = -s; dy = 1; break;
    default: base = 0; dx = 1; dy = s; break;
    }
    const int fr = c >> 11, fg = c >> 5 & 63, fb = c & 31;
    for (int Y = y0; Y < y1; Y++) {
        const uint8_t *m = mask + (size_t)(Y - y) * w + (x0 - x);
        Color *p = buf_ + base + Y * dy + x0 * dx;
        for (int X = x0; X < x1; X++, p += dx) {
            const int a = *m++;
            if (!a)
                continue;
            if (a == 255) {
                *p = c;
                continue;
            }
            const Color b = *p;
            const int br = b >> 11, bg = b >> 5 & 63, bb = b & 31;
            *p = static_cast<Color>((br + div255((fr - br) * a)) << 11 | (bg + div255((fg - bg) * a)) << 5 |
                                    (bb + div255((fb - bb) * a)));
        }
    }
}

} // namespace p4lcd
