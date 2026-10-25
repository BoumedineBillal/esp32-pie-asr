// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * car_sim.hpp. Said without a side: "the window" is the driver's, "the windows" all four, "my seat" the driver's,
 * "the seats" both; the climate's side, both zones.
 */
#include "car_sim.hpp"
#include <cmath>
#include <cstring>

namespace car {

namespace {

constexpr float WINDOW_SPEED = 0.5f;   /* of the way a second */
constexpr float TEMP_SPEED = 0.2f;     /* degrees a second */
constexpr float KMH = 60.0f;
constexpr float DRAIN = 0.004f;        /* battery % a second while driving */
const float DIST_KM[DESTINATIONS] = {12.4f, 8.2f, 3.1f, 27.5f, 5.6f, 41.0f};
const char *const TRACKS[] = {"Morning Drive", "City Lights", "Open Road", "Night Shift", "Coastline"};
constexpr int NTRACKS = 5;

const char *target_text(const voice::Action &a)
{
    const voice::Rule &r = demo().rules[a.rule];
    return a.target >= 0 ? r.targets[a.target] : "";
}

bool plural(const voice::Action &a)
{
    const char *t = target_text(a);
    const size_t n = strlen(t);
    return n > 0 && t[n - 1] == 's';
}

/* the climate zones / seats an action names (DRIVER, PASSENGER) */
void zones(const voice::Action &a, bool both_by_default, bool *out)
{
    const int s = side(a);
    out[DRIVER] = s == SIDE_DRIVER || s == SIDE_FRONT || s == SIDE_ALL || s == SIDE_NONE;
    out[PASSENGER] = s == SIDE_PASSENGER || s == SIDE_FRONT || s == SIDE_ALL || (s == SIDE_NONE && both_by_default);
}

} // namespace

void Car::reset()
{
    for (int z = 0; z < 2; z++) {
        temp_[z] = shown_[z] = 16.0f;
        target_[z] = 21;
        seat_[z] = 0;
    }
    fan_ = 2;
    for (int w = 0; w < WINDOWS; w++)
        win_[w] = win_to_[w] = 0;
    defrost_ = 0;
    dest_ = -1;
    km_ = shown_km_ = 0;
    playing_ = false;
    snprintf(media_, sizeof media_, "radio");
    track_ = 0;
    volume_ = 4;
    lights_ = false;
    battery_ = 78.0f;
    clear();
}

void Car::order(const voice::Result &r)
{
    begin(r);
    for (int i = 0; i < r.n; i++) {
        const voice::Action &a = r.actions[i];
        wait_[i] = NOTHING;
        if (a.skipped)
            continue;
        bool z[2];
        switch (a.rule) {
        case SET_TEMP:
            zones(a, true, z);
            for (int k = 0; k < 2; k++)
                if (z[k])
                    target_[k] = a.value < 16 ? 16 : a.value > 28 ? 28 : a.value;
            if (a.value < 16 || a.value > 28)
                say("The climate goes from 16 to 28 degrees.");
            break;
        case WARMER:
        case COOLER:
            zones(a, true, z);
            for (int k = 0; k < 2; k++)
                if (z[k]) {
                    const int t = target_[k] + (a.rule == WARMER ? 1 : -1);
                    target_[k] = t < 16 ? 16 : t > 28 ? 28 : t;
                }
            break;
        case FAN_SET:
            fan_ = a.value < 0 ? fan_ : a.value > 5 ? 5 : a.value;
            break;
        case FAN_UP:
            fan_ = fan_ < 5 ? fan_ + 1 : 5;
            break;
        case FAN_DOWN:
            fan_ = fan_ > 0 ? fan_ - 1 : 0;
            break;
        case WINDOWS_OPEN:
        case WINDOWS_CLOSE: {
            const int s = side(a);
            bool w[WINDOWS];
            const bool all = s == SIDE_ALL || (s == SIDE_NONE && plural(a));
            w[DRIVER] = all || s == SIDE_DRIVER || s == SIDE_FRONT || (s == SIDE_NONE && !plural(a));
            w[PASSENGER] = all || s == SIDE_PASSENGER || s == SIDE_FRONT;
            w[REAR_LEFT] = w[REAR_RIGHT] = all || s == SIDE_REAR;
            for (int k = 0; k < WINDOWS; k++)
                if (w[k])
                    win_to_[k] = a.rule == WINDOWS_OPEN ? 1.0f : 0.0f;
            act_[i] = RUNNING;
            wait_[i] = WINDOWS_MOVE;
            break;
        }
        case SEAT_ON:
        case SEAT_OFF:
            zones(a, plural(a), z);
            for (int k = 0; k < 2; k++)
                if (z[k])
                    seat_[k] = a.rule == SEAT_ON ? 3 : 0;
            break;
        case DEFROST:
            defrost_ = 60.0f;
            act_[i] = RUNNING;
            wait_[i] = DEFROSTING;
            break;
        case NAVIGATE: {
            const int d = car::destination(a);
            if (d >= 0) {
                dest_ = d;
                km_ = shown_km_ = DIST_KM[d];
                act_[i] = RUNNING;
                wait_[i] = ROUTE;
            }
            break;
        }
        case STOP_NAV:
            if (dest_ < 0) {
                say("There is no route.");
                act_[i] = ANSWERED;
            }
            dest_ = -1;
            break;
        case PLAY: {
            const char *m = car::media(a);
            if (!strncmp(m, "some ", 5))
                m += 5;
            if (!strncmp(m, "the ", 4) || !strncmp(m, "a ", 2))
                m += m[0] == 'a' ? 2 : 4;
            if (!strncmp(m, "my ", 3))
                m += 3;
            snprintf(media_, sizeof media_, "%s", m);
            playing_ = true;
            break;
        }
        case PAUSE:
            playing_ = false;
            break;
        case NEXT_TRACK:
            track_ = (track_ + 1) % NTRACKS;
            playing_ = true;
            break;
        case VOLUME_UP:
            volume_ = volume_ + 2 > 10 ? 10 : volume_ + 2;
            break;
        case VOLUME_DOWN:
            volume_ = volume_ - 2 < 0 ? 0 : volume_ - 2;
            break;
        case LIGHTS_ON:
        case LIGHTS_OFF:
            lights_ = a.rule == LIGHTS_ON;
            break;
        case RANGE:
            say("%d %% charge: about %d km of range.%s", battery(), range_km(),
                dest_ >= 0 && range_km() > km_ ? " Enough for the route." : "");
            act_[i] = ANSWERED;
            break;
        default: break;
        }
    }
}

void Car::update(float dt)
{
    if (dt <= 0)
        return;
    for (int z = 0; z < 2; z++) {
        const float d = target_[z] - temp_[z];
        temp_[z] += std::fabs(d) < TEMP_SPEED * dt ? d : (d > 0 ? 1 : -1) * TEMP_SPEED * dt;
        const float s = std::round(temp_[z] * 10) / 10;
        if (s != shown_[z]) {
            shown_[z] = s;
            version_++;
        }
    }
    bool moving = false;
    for (int w = 0; w < WINDOWS; w++) {
        if (win_[w] == win_to_[w])
            continue;
        const float before = std::floor(win_[w] * 4);
        const float step = WINDOW_SPEED * dt;
        win_[w] = win_[w] < win_to_[w] ? std::fmin(win_to_[w], win_[w] + step) : std::fmax(win_to_[w], win_[w] - step);
        if (std::floor(win_[w] * 4) != before || win_[w] == win_to_[w])
            version_++;
        moving = moving || win_[w] != win_to_[w];
    }
    if (defrost_ > 0) {
        defrost_ -= dt;
        if (defrost_ <= 0) {
            defrost_ = 0;
            version_++;
        }
    }
    if (dest_ >= 0) {
        km_ -= KMH / 3600.0f * dt;
        battery_ -= DRAIN * dt;
        if (km_ <= 0) {
            km_ = 0;
            dest_ = -1;
            say("You have arrived.");
            version_++;
        }
        const float s = std::round(km_ * 10) / 10;
        if (s != shown_km_) {
            shown_km_ = s;
            version_++;
        }
    }
    for (int i = 0; i < nact_; i++) {
        if (act_[i] != RUNNING)
            continue;
        if ((wait_[i] == WINDOWS_MOVE && !moving) || (wait_[i] == DEFROSTING && !defrosting()) ||
            (wait_[i] == ROUTE && dest_ < 0)) {
            act_[i] = DONE;
            version_++;
        }
    }
}

int Car::eta_minutes() const { return dest_ < 0 ? 0 : static_cast<int>(std::ceil(km_ / KMH * 60.0f)); }

const char *Car::track() const { return TRACKS[track_]; }

const char *Car::status(char *out, int cap) const
{
    char route[48] = "parked";
    if (dest_ >= 0)
        snprintf(route, sizeof route, "to %s, %.1f km", destination_name(dest_), static_cast<double>(shown_km_));
    snprintf(out, cap, "Cabin %.1f degrees (set %d), %s, battery %d %% (%d km)", static_cast<double>(shown_[DRIVER]),
             target_[DRIVER], route, battery(), range_km());
    return out;
}

} // namespace car
