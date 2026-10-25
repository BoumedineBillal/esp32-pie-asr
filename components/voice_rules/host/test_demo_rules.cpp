// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// test_demo_rules: sentences of the five demos after the vacuum -> their actions (voice::describe), the numbers the
// rules read, and a sentence that continues the previous one. The board's demo pages say these and must give the same.
#include <cstdio>
#include <cstring>
#include "demos.hpp"

namespace {

struct Case {
    const char *demo, *text, *expect;
};

const Case CASES[] = {
    /* smart home */
    {"smarthome", "Turn off the kitchen lights.", "lights_off:lights@kitchen"},
    {"smarthome", "Turn the kitchen lights off.", "lights_off:lights@kitchen"},
    {"smarthome", "Kitchen lights off.", "lights_off:lights@kitchen"},
    {"smarthome", "Turn on the lights in the bedroom.", "lights_on:lights@bedroom"},
    {"smarthome", "Lights on in the living room.", "lights_on:lights@living room"},
    {"smarthome", "Turn off all the lights.", "lights_off:lights@all"},
    {"smarthome", "Lights off everywhere.", "lights_off:lights@everywhere"},
    {"smarthome", "Set the bedroom lights to twenty percent.", "set_lights:lights@bedroom=20"},
    {"smarthome", "Dim the living room lights.", "dim_lights:lights@living room"},
    {"smarthome", "Dim the lights to thirty percent.", "set_lights:lights=30"},
    {"smarthome", "Brighten the kitchen lights.", "brighten_lights:lights@kitchen"},
    {"smarthome", "Set the thermostat to twenty two degrees.", "set_temperature:thermostat=22"},
    {"smarthome", "Set the temperature to 21.", "set_temperature:temperature=21"},
    {"smarthome", "Set the thermostat.", "(no_number)"},
    {"smarthome", "Make it warmer.", "warmer"},
    {"smarthome", "Turn up the heating.", "warmer:heating"},
    {"smarthome", "Turn the heating down.", "cooler:heating"},
    {"smarthome", "Lock the front door.", "lock:door@front"},
    {"smarthome", "Unlock the back door but not the front.", "unlock:door@back; -unlock:door@front"},
    {"smarthome", "Lock the front and back doors.", "lock:doors@front; lock:doors@back"},
    {"smarthome", "Lock all the doors.", "lock:doors@all"},
    {"smarthome", "Open the bedroom blinds.", "blinds_open:blinds@bedroom"},
    {"smarthome", "Close the blinds in the kitchen.", "blinds_close:blinds@kitchen"},
    {"smarthome", "Play some jazz in the living room.", "play:some jazz@living room"},
    {"smarthome", "Stop the music.", "stop_music:music"},
    {"smarthome", "Turn the music up.", "volume_up:music"},
    {"smarthome", "Turn up the volume.", "volume_up:volume"},
    {"smarthome", "Next song.", "next_song"},
    {"smarthome", "Skip this song please.", "next_song"},
    {"smarthome", "Turn off the kitchen lights and lock the front door.", "lights_off:lights@kitchen; lock:door@front"},
    {"smarthome", "I'm heading out: lights off everywhere, lock the front door and stop the music.",
     "lights_off:lights@everywhere; lock:door@front; stop_music:music"},
    {"smarthome", "Don't touch the lights, just close the blinds in the bedroom.", "blinds_close:blinds@bedroom"},
    {"smarthome", "Turn off the lights in the kitchen and the bedroom.",
     "lights_off:lights@kitchen; lights_off:lights@bedroom"},
    {"smarthome", "Turn on the kitchen and the bedroom lights.", "lights_on:lights@kitchen; lights_on:lights@bedroom"},
    {"smarthome", "What's the temperature?", "status"},
    {"smarthome", "Is the front door locked?", "status"},
    {"smarthome", "The kitchen lights are beautiful.", "(no_verb)"},
    {"smarthome", "I turned off the lights yesterday.", "(no_verb)"},
    {"smarthome", "The music was too loud last night.", "(no_verb)"},
    {"smarthome", "We need new blinds for the bedroom.", "(no_verb)"},
    {"smarthome", "Did you lock the door?", "(no_verb)"},
    {"smarthome", "It's warmer today than yesterday.", "(no_words)"},
    {"smarthome", "Turn left at the end of the street.", "(no_words)"},
    {"smarthome", "I love this song.", "(no_words)"},
    /* car */
    {"car", "Set the temperature to twenty one degrees.", "set_temperature:temperature=21"},
    {"car", "Set the driver side to twenty.", "set_temperature:side@driver=20"},
    {"car", "Make it warmer.", "warmer"},
    {"car", "Turn up the heating.", "warmer:heating"},
    {"car", "Turn the fan up.", "fan_up:fan"},
    {"car", "Set the fan to three.", "set_fan:fan=3"},
    {"car", "Open the windows.", "windows_open:windows"},
    {"car", "Roll down the driver's window.", "windows_open:window@driver"},
    {"car", "Windows down.", "windows_open:windows"},
    {"car", "Close the rear windows.", "windows_close:windows@rear"},
    {"car", "Put the windows up.", "windows_close:windows"},
    {"car", "Heat my seat.", "seat_heat_on:seat"},
    {"car", "Turn on the passenger seat heater.", "seat_heat_on:seat heater@passenger"},
    {"car", "Turn off the seat heating.", "seat_heat_off:seat heating"},
    {"car", "Defrost the windscreen.", "defrost:windscreen"},
    {"car", "Navigate home.", "navigate:home"},
    {"car", "Take me to the airport.", "navigate:the airport"},
    {"car", "Navigate to the nearest charger.", "navigate:nearest charger"},
    {"car", "Cancel the route.", "stop_navigation"},
    {"car", "Stop navigation.", "stop_navigation"},
    {"car", "Play some music.", "play:some music"},
    {"car", "Play the radio.", "play:the radio"},
    {"car", "Pause the music.", "pause:music"},
    {"car", "Next song.", "next_track"},
    {"car", "Turn up the volume.", "volume_up:volume"},
    {"car", "Turn the music down.", "volume_down:music"},
    {"car", "Turn on the headlights.", "lights_on:headlights"},
    {"car", "Headlights off.", "lights_off:headlights"},
    {"car", "How much range do I have?", "range"},
    {"car", "Set the temperature to twenty one degrees and open the windows.",
     "set_temperature:temperature=21; windows_open:windows"},
    {"car", "Open the front windows but not the rear.", "windows_open:windows@front; -windows_open:windows@rear"},
    {"car", "Navigate to work and play the news.", "navigate:work; play:the news"},
    {"car", "The windows were open all night.", "(no_verb)"},
    {"car", "I left my seat at the cinema.", "(no_verb)"},
    {"car", "The temperature dropped to five degrees.", "(no_verb)"},
    {"car", "My home is near the airport.", "(no_verb)"},
    {"car", "The radio said it would rain.", "(no_verb)"},
    {"car", "We should open the windows later.", "(no_verb)"},
    {"car", "That song was great.", "(no_words)"},
    /* factory */
    {"factory", "Start the line.", "start:the line"},
    {"factory", "Stop the conveyor.", "stop:conveyor"},
    {"factory", "Emergency stop!", "emergency_stop"},
    {"factory", "Stop everything!", "emergency_stop"},
    {"factory", "Reset the alarm.", "reset:the alarm"},
    {"factory", "Reset the emergency stop.", "reset:emergency stop"},
    {"factory", "Set the speed to fifty percent.", "set_speed:speed=50"},
    {"factory", "Set the conveyor speed to seventy.", "set_speed:conveyor speed=70"},
    {"factory", "Speed up the line.", "faster:the line"},
    {"factory", "Slow down.", "slower"},
    {"factory", "Go faster.", "faster"},
    {"factory", "Start station two.", "start:station=2"},
    {"factory", "Stop the press.", "stop:the press"},
    {"factory", "Start the robot arm.", "start:robot arm"},
    {"factory", "Switch to blue boxes.", "product:blue boxes"},
    {"factory", "How many boxes?", "status"},
    {"factory", "Stop the line and reset the alarm.", "stop:the line; reset:the alarm"},
    {"factory", "Shut down the press.", "stop:the press"},
    {"factory", "Shut the line down.", "stop:the line"},
    {"factory", "The line was stopped yesterday.", "(no_verb)"},
    {"factory", "We need to stop by the store.", "(no_words)"},
    {"factory", "The press conference starts at noon.", "(no_verb)"},
    {"factory", "This is an emergency exit.", "(no_words)"},
    {"factory", "The robot arm is broken.", "(no_verb)"},
    {"factory", "Stop everything you are doing and listen.", "(no_words)"},
    {"factory", "I like the red boxes.", "(no_verb)"},
    /* kitchen */
    {"kitchen", "Turn on the front left burner.", "burner_on:burner@front left"},
    {"kitchen", "Turn the back right burner to seven.", "burner_set:burner@back right=7"},
    {"kitchen", "Set the front right burner to five.", "burner_set:burner@front right=5"},
    {"kitchen", "Turn the front left burner up.", "burner_up:burner@front left"},
    {"kitchen", "Turn off all the burners.", "burner_off:burners@all"},
    {"kitchen", "Turn off the hob.", "burner_off:hob"},
    {"kitchen", "Preheat the oven to one hundred and eighty degrees.", "oven_on:oven=180"},
    {"kitchen", "Preheat the oven.", "oven_on:oven"},
    {"kitchen", "Turn off the oven.", "oven_off:oven"},
    {"kitchen", "Set a timer for ten minutes.", "timer_set:a timer=10"},
    {"kitchen", "Start a five minute timer.", "timer_set:minute timer=5"},
    {"kitchen", "Set a timer.", "(no_number)"},
    {"kitchen", "Cancel the timer.", "timer_cancel:timer"},
    {"kitchen", "Boil the kettle.", "kettle:kettle"},
    {"kitchen", "Put the kettle on.", "kettle:kettle"},
    {"kitchen", "Turn on the extractor fan.", "hood_on:extractor fan"},
    {"kitchen", "Turn the hood off.", "hood_off:hood"},
    {"kitchen", "Turn on the lights.", "lights_on:lights"},
    {"kitchen", "Turn everything off.", "all_off"},
    {"kitchen", "How much time is left?", "status"},
    {"kitchen", "Preheat the oven to two hundred and set a timer for twenty minutes.",
     "oven_on:oven=200; timer_set:a timer=20"},
    {"kitchen", "The oven is still hot.", "(no_verb)"},
    {"kitchen", "I burned my hand on the stove.", "(no_verb)"},
    {"kitchen", "The kettle is new.", "(no_verb)"},
    {"kitchen", "My timer broke yesterday.", "(no_verb)"},
    {"kitchen", "Turn left after the bakery.", "(no_target)"},
    {"kitchen", "Boiling water is dangerous.", "(no_verb)"},
    {"kitchen", "Set the table for dinner.", "(no_target)"},
    /* presenter */
    {"presenter", "Next slide.", "next"},
    {"presenter", "Next slide please.", "next"},
    {"presenter", "The next slide shows our results.", "(no_words)"},
    {"presenter", "Previous slide.", "previous"},
    {"presenter", "Go back one slide.", "previous"},
    {"presenter", "Go to slide five.", "go_to:slide=5"},
    {"presenter", "Go to the third slide.", "go_to:slide=3"},
    {"presenter", "Jump to slide number seven.", "go_to:slide number=7"},
    {"presenter", "Go to the first slide.", "go_to:slide=1"},
    {"presenter", "First slide.", "first"},
    {"presenter", "Go to the last slide.", "last:the last slide"},
    {"presenter", "Last slide.", "last"},
    {"presenter", "Start the presentation.", "start:presentation"},
    {"presenter", "End the presentation.", "end:presentation"},
    {"presenter", "Black screen.", "blank"},
    {"presenter", "Show the slides.", "unblank"},
    {"presenter", "Start the timer.", "timer_start:timer"},
    {"presenter", "Reset the timer.", "timer_reset:timer"},
    {"presenter", "Zoom in.", "zoom_in"},
    {"presenter", "How much time do I have?", "time_left"},
    {"presenter", "Start the presentation and start the timer.", "start:presentation; timer_start:timer"},
    {"presenter", "Let's go back in time to two thousand ten.", "(no_words)"},
    {"presenter", "In the next slide we will see the results.", "(no_words)"},
    {"presenter", "This slide is about our team.", "(no_verb)"},
    {"presenter", "The presentation went well.", "(no_verb)"},
    {"presenter", "Our timer starts now.", "(no_verb)"},
    {"presenter", "Zoom calls are tiring.", "(no_words)"},
    {"presenter", "We need to end this meeting.", "(no_words)"},
    {"presenter", "Show me the money.", "(no_target)"},
};

struct Num {
    const char *text;
    int value;
};

/* numbers as the recognizer writes them, in a sentence that takes one */
const Num NUMS[] = {
    {"Set the thermostat to zero.", 0},          {"Set the thermostat to seven.", 7},
    {"Set the thermostat to nineteen.", 19},     {"Set the thermostat to twenty.", 20},
    {"Set the thermostat to twenty-five.", 25},  {"Set the thermostat to ninety nine.", 99},
    {"Set the thermostat to one hundred.", 100}, {"Set the thermostat to a 180.", 180},
    {"Set the thermostat to two hundred and fifty.", 250},
    {"Set the thermostat to one thousand two hundred.", 1200},
    {"Set the thermostat to 21st.", 21},
};

} // namespace

int main()
{
    int bad = 0, n = 0;
    char d[1024];
    for (const Case &c : CASES) {
        const voice::Demo *demo = demos::find(c.demo);
        voice::Result r;
        voice::parse_text(*demo, c.text, r);
        voice::describe(*demo, r, d, sizeof d);
        n++;
        if (strcmp(d, c.expect)) {
            printf("FAIL %-10s %-70s got %s, expected %s\n", c.demo, c.text, d, c.expect);
            bad++;
        }
    }
    for (const Num &c : NUMS) {
        voice::Result r;
        voice::parse_text(*demos::find("smarthome"), c.text, r);
        n++;
        if (r.n != 1 || r.actions[0].value != c.value) {
            printf("FAIL number %-50s got %d, expected %d\n", c.text, r.n ? r.actions[0].value : -1, c.value);
            bad++;
        }
    }
    /* an utterance that continues the previous one: "... and the bedroom" (the target carried) */
    {
        voice::Context ctx;
        voice::Result r;
        voice::parse_text(*demos::find("smarthome"), "Turn off the kitchen lights.", r, &ctx);
        voice::parse_text(*demos::find("smarthome"), "and the bedroom.", r, &ctx);
        voice::describe(*demos::find("smarthome"), r, d, sizeof d);
        n++;
        if (strcmp(d, "lights_off:lights@bedroom") || !r.continued) {
            printf("FAIL continued: got %s\n", d);
            bad++;
        }
    }
    printf("%d of %d cases pass\n", n - bad, n);
    return bad ? 1 : 0;
}
