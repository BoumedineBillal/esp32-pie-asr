// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4lcd: the LCD and the touch panel of the ESP32-P4X-Function-EV-Board, light and fully controlled.
 *
 *     p4lcd::Display lcd;
 *     lcd.open();                                  // the defaults: the board's panel, full brightness
 *     p4lcd::Color *px = lcd.pixels();             // the picture being drawn: width() x height() RGB565
 *     ... draw into px (row stride: stride() pixels) ...
 *     lcd.present();                               // on screen at the next refresh, without tearing
 *
 * Two frame buffers in PSRAM: one on screen, one being drawn. present() hands the drawn one to the panel at the next
 * refresh and waits until it is on screen; the other one then becomes the drawing buffer, holding the picture from
 * two presents ago (draw the whole picture each time).
 *
 * On the ESP-IDF drivers only (esp_lcd's MIPI-DSI / DPI driver, the PPA, i2c_master, ledc, gpio, the on-chip LDO).
 * No exceptions: calls return false and lastError() says why.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace p4lcd {

/* a pixel: RGB565 (R5 G6 B5, as the panel takes it) */
using Color = uint16_t;

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<Color>((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

inline constexpr Color BLACK = rgb(0, 0, 0);
inline constexpr Color WHITE = rgb(255, 255, 255);
inline constexpr Color RED = rgb(255, 0, 0);
inline constexpr Color GREEN = rgb(0, 255, 0);
inline constexpr Color BLUE = rgb(0, 0, 255);
inline constexpr Color YELLOW = rgb(255, 255, 0);
inline constexpr Color CYAN = rgb(0, 255, 255);
inline constexpr Color MAGENTA = rgb(255, 0, 255);

/* the pixel formats draw() takes */
enum class PixelFormat {
    RGB565,   /* 2 bytes per pixel, little-endian R5 G6 B5 */
    BGR888,   /* 3 bytes per pixel, B, G, R in memory (OpenCV's order; the P4's ISP and PPA write RGB888 so) */
    RGB888,   /* 3 bytes per pixel, R, G, B in memory */
    YUV420,   /* the P4 ISP's packed YUV 4:2:0, 12 bits per pixel (per line 3 bytes for 2 pixels: chroma, Y, Y; U on
               * even lines, V on odd lines), full range, BT.601; width, height and the drawn part even */
};

/* an image in memory (internal RAM or PSRAM, no alignment needed): a camera frame, a decoded file, ... */
struct Image {
    const void *data = nullptr;
    int width = 0, height = 0;
    int stride = 0;                       /* pixels per row; 0: width */
    PixelFormat format = PixelFormat::RGB565;
};

/* a TrueType font, made into anti-aliased pixels on the chip (stb_truetype). Two are built in: Inter Regular and Bold
 * (Latin-1, French included, and common signs; 51 and 54 KB of flash, linked only when TrueType text is drawn).
 * load() takes any other .ttf / .otf file in memory. */
class Font {
public:
    static const Font &regular();
    static const Font &bold();
    Font() = default;
    ~Font();
    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;
    /* the file stays the caller's and must outlive the Font; index: the font in a collection (.ttc) */
    bool load(const void *data, size_t size, int index = 0);
    bool valid() const { return impl_ != nullptr; }

    struct Impl;                       /* internal */
    const Impl *impl() const { return impl_; }

private:
    Impl *impl_ = nullptr;
};

enum class Align { LEFT, CENTER, RIGHT };
enum class VAlign { TOP, MIDDLE, BASELINE, BOTTOM };

/* how text(x, y, utf8, style) draws */
struct TextStyle {
    const Font *font = nullptr;    /* nullptr: Font::regular() */
    float size = 24;               /* the em in pixels, as CSS's font-size (Inter's capitals: 0.73 em), 4-256 */
    Color color = WHITE;
    Align align = Align::LEFT;     /* x is each line's left end, its centre or its right end */
    VAlign valign = VAlign::TOP;   /* y is the block's top, its middle, the first line's baseline, or its bottom */
    int wrap = 0;                  /* the width in pixels to wrap at, between words (0: only at '\n') */
    float line_spacing = 1.0f;     /* times the font's own line height */
    float letter_spacing = 0;      /* pixels added after each character */
    int outline = 0;               /* an outline this many pixels wide around the glyphs (0-8): readable on pictures */
    Color outline_color = BLACK;
    bool box = false;              /* a filled box behind the text, `padding` pixels beyond it */
    Color box_color = BLACK;
    int padding = 4;
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct Config {
    /* the panel's refresh rate, set by the pixel clock (0: the panel vendor's timing, 60.4 Hz). 30 by default: the
     * scan-out then costs a PSRAM-bound job 9.7 % instead of 19.5 % at 60 Hz */
    int refresh_hz = 30;
    int brightness = 100;   /* the backlight, 0-100 % */
    /* 2: one on screen, one being drawn (present() swaps them: no tearing). 1: half the PSRAM
     * (1.2 MB); the picture being drawn is the one on screen, so drawing shows as it happens: compose parts in a Canvas
     * and draw() them in (the PPA copies a part in about a millisecond); present() writes the CPU's drawing back from
     * the cache and returns at the next refresh */
    int frame_buffers = 2;
    /* how far the picture is turned clockwise from the panel's own scan order: 0, 90, 180 or 270; width() and
     * height() follow. 180 by default: upright for the user, who faces the board from that side */
    int rotation = 180;
    /* the board (ESP32-P4X-Function-EV-Board) */
    int backlight_pin = 26;
    int reset_pin = 27;
    int dsi_ldo_channel = 3; /* the on-chip LDO that supplies the MIPI-DSI PHY */
    int dsi_ldo_mv = 2500;
};

/* a picture to draw into: an RGB565 buffer and the drawing calls. The Display is one (its picture being drawn); a
 * Canvas of your own is a picture off the screen (a layer, a paint program's page) that draw(canvas.image()) shows. */
class Canvas {
public:
    Canvas() = default;
    ~Canvas() { release(); }
    Canvas(const Canvas &) = delete;
    Canvas &operator=(const Canvas &) = delete;

    /* a picture of its own in PSRAM (cache-line aligned, so draw() can write into it), filled with c */
    bool create(int width, int height, Color c = BLACK);
    /* or a buffer of yours (stride: pixels per row, 0: width). draw() into it needs its start and end on a cache line
     * (64 bytes with ESP-IDF's default; 128 is always safe) */
    bool wrap(Color *pixels, int width, int height, int stride = 0);
    void release();                   /* frees a created buffer; nothing for a wrapped one */
    /* the buffer as it is in memory, as an image for draw() (the Display's: in the panel's layout) */
    Image image() const;

    /* the picture's size, in the drawing functions' coordinates (rotation applied) */
    int width() const { return rotation_ % 180 ? ph_ : pw_; }
    int height() const { return rotation_ % 180 ? pw_ : ph_; }

    /* drawing, clipped at the edges (on the Display: into the picture being drawn, on screen at the next present()) */
    void fill(Color c);
    void pixel(int x, int y, Color c);
    void fillRect(int x, int y, int w, int h, Color c);
    void rect(int x, int y, int w, int h, Color c, int thickness = 1);
    void line(int x0, int y0, int x1, int y1, Color c, int thickness = 1);
    void circle(int cx, int cy, int r, Color c, int thickness = 1);
    void fillCircle(int cx, int cy, int r, Color c);
    /* text in the built-in 8 x 16 font (printable ASCII; others as '?'), each pixel scale x scale; '\n' starts a new
     * line. With bg, the glyph cells are filled first. Returns the width drawn (the widest line). */
    int text(int x, int y, const char *s, Color fg, int scale = 1);
    int text(int x, int y, const char *s, Color fg, Color bg, int scale = 1);
    static int textWidth(const char *s, int scale = 1);   /* the widest line */
    static int textHeight(const char *s, int scale = 1);
    /* text (UTF-8) in a TrueType font, anti-aliased, as the style says. Returns the block it covers: the lines'
     * advance box, with the outline, and the padding when there is a box. Glyphs are made once per font, size and
     * outline and kept (a 256 KB cache in PSRAM, emptied when full). */
    Rect text(int x, int y, const char *utf8, const TextStyle &style);
    static Rect textBox(int x, int y, const char *utf8, const TextStyle &style);   /* the same block, not drawn */
    /* an image with its top-left corner at (x, y), scaled and turned clockwise by `rotation` (0, 90, 180, 270), by
     * the PPA (no CPU work on the pixels); clipped at the edges. The scale is in 1/16 steps from 1/16 to 16 (others
     * rounded down); the drawn size is then floor(width x scale) x floor(height x scale). Waits for the PPA. */
    bool draw(const Image &img, int x, int y, float scale = 1.0f, int rotation = 0);

    /* the buffer, in memory order (the Display's: the panel's layout, 1024 x 600 landscape whatever the rotation) */
    size_t stride() const { return stride_; }           /* pixels per row */
    Color *pixels() { return buf_; }                    /* the Display's: the picture being drawn (not on screen) */
    const Color *pixels() const { return buf_; }
    esp_err_t lastError() const { return err_; }

protected:
    Color *buf_ = nullptr;
    int pw_ = 0, ph_ = 0, stride_ = 0, rotation_ = 0;  /* the buffer's size and row stride; the rotation */
    void *ppa_ = nullptr;                               /* draw()'s PPA client, made at the first draw() */
    bool owned_ = false;
    esp_err_t err_ = ESP_OK;
    void detach();                                      /* the PPA client released, the buffer let go */

private:
    void span(int y, int x0, int x1, Color c); /* a horizontal run in picture coordinates, clipped */
    int glyphs(int x, int y, const char *s, Color fg, const Color *bg, int scale);
    /* a coverage mask (0-255, w x h, row by row) blended in colour c at (x, y), picture coordinates, clipped */
    void blend(int x, int y, int w, int h, const uint8_t *mask, Color c);
};

class Display : public Canvas {
public:
    Display() = default;
    ~Display() { close(); }

    bool open(const Config &c = Config());
    void close();
    bool isOpen() const { return panel_ != nullptr; }

    const Color *screen() const { return front_; }      /* the picture on screen (read only, the panel's layout) */

    /* the drawn picture on screen at the next refresh; returns when it is (false after timeout_ms) */
    bool present(uint32_t timeout_ms = 200);
    bool brightness(int percent);

    uint32_t refreshes() const;      /* the panel's refreshes since the open (its VSYNC events) */
    float refreshHz() const;         /* measured: refreshes over the time since the first one */
    float pixelClockMHz() const { return pclk_mhz_; }
    int rotation() const { return rotation_; }

    struct State;                    /* internal: what the panel's interrupt callbacks touch */

private:
    void *bus_ = nullptr, *io_ = nullptr, *panel_ = nullptr, *ldo_ = nullptr;
    Color *front_ = nullptr;         /* on screen; the picture being drawn is Canvas::buf_ */
    float pclk_mhz_ = 0;
    int backlight_pin_ = -1, reset_pin_ = -1;
    State *st_ = nullptr;
};

/* ---- the touch panel (GT911) ---- */

struct TouchConfig {
    /* as the Display's (180 by default, as there): the touches come in its picture coordinates */
    int rotation = 180;
    int width = 1024, height = 600;          /* the panel's size, landscape */
    /* the touch sensor's own axes against the panel's. The board's GT911 reports the panel turned half a turn: both
     * mirrored (9 touches) */
    bool swap_xy = false, mirror_x = true, mirror_y = true;
    int poll_hz = 100;                       /* the GT911 has no interrupt line on this board: it is polled */
    int task_priority = 5;
    int task_core = -1;                      /* -1: either core */
    /* the I2C bus: the camera sensor's, reused when it is already open (open the camera first, close the touch
     * first); else made here on these pins */
    int i2c_port = 0, sda_pin = 7, scl_pin = 8;
    int i2c_hz = 400000;
    int address = 0;                         /* 0: found (0x5D, then 0x14) */
};

struct TouchPoint {
    int id;                                  /* the finger's track, kept while it stays down */
    int x, y;                                /* picture coordinates */
    int size;                                /* the contact's size, as the GT911 reports it */
};

struct TouchEvent {
    enum Type { DOWN, MOVE, UP } type;
    int id, x, y;                            /* UP: where the finger was last */
    int64_t time_us;                         /* esp_timer time of the poll that saw it */
};

class Touch {
public:
    Touch() = default;
    ~Touch() { close(); }
    Touch(const Touch &) = delete;
    Touch &operator=(const Touch &) = delete;

    /* finds the GT911, reads its identity and starts polling it */
    bool open(const TouchConfig &c = TouchConfig());
    void close();
    bool isOpen() const { return st_ != nullptr; }

    /* the fingers down now (at most 5, from the last poll) */
    int points(TouchPoint *out, int max = 5) const;
    /* the next event, oldest first (a queue of 32), waiting up to timeout_ms; false when none came */
    bool event(TouchEvent &e, uint32_t timeout_ms = 0);

    /* what the chip says about itself */
    const char *product() const;             /* "911" */
    int firmware() const;
    int configVersion() const;
    int rawWidth() const;                    /* its configured resolution */
    int rawHeight() const;
    int address() const;
    /* the polling's record */
    uint32_t polls() const;
    uint32_t busErrors() const;
    uint32_t droppedEvents() const;          /* events lost because the queue was full */
    float busMsPerPoll() const;              /* the mean I2C time of a poll */
    esp_err_t lastError() const { return err_; }

    struct State;                            /* internal */

private:
    State *st_ = nullptr;
    esp_err_t err_ = ESP_OK;
};

} // namespace p4lcd
