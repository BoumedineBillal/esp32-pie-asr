# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""shots.py: every page as a picture on the PC (shots/*.png): Home, the demos' cards (reached by taps as a user
would), then each demo: its page idle, after its orders (each a few actions, a question, a refusal, words that match
nothing), as its world moves on, its rules and its rules scrolled; then the transcriber's six tabs; then the board's
text-fit counters and frame times (the last line: "ok uicheck overflows <n> ..."). A text the board had to cut is
printed as its TEXTFIT line (once per text since the boot).
"""
import os
import time

import board

OUT = os.path.join(board.HERE, "shots")
os.makedirs(OUT, exist_ok=True)

# each demo: (its orders, each with the seconds to let its world move on before the picture)
ORDERS = {
    "vacuum": [("Clean the kitchen, not the bedroom, then go back to the dock.", 0.3), ("", 4.0),
               ("Where are you?", 0.3), ("What is the weather tomorrow?", 0.3)],
    "smarthome": [("Turn on the kitchen lights, close the bedroom blinds and play some jazz.", 0.3), ("", 3.0),
                  ("Set the thermostat to twenty five degrees and lock all the doors.", 4.0),
                  ("What's the temperature?", 0.3), ("Set the thermostat.", 0.3)],
    "car": [("Set the temperature to twenty one degrees and open the windows.", 0.3), ("", 3.0),
            ("Navigate to the airport, heat my seat and turn on the headlights.", 2.0),
            ("Play the radio and defrost the windscreen.", 0.5), ("How much range do I have?", 0.3)],
    "factory": [("Start the line.", 6.0), ("Switch to blue boxes and speed up the line.", 4.0),
                ("Emergency stop!", 0.5), ("Start the line.", 0.3), ("Reset the emergency stop and start the line.", 3.0),
                ("How many boxes?", 0.3)],
    "kitchen": [("Preheat the oven and set a timer for one minute.", 5.0),
                ("Turn on the front left burner and put the kettle on.", 6.0),
                ("Set the back right burner to seven and turn on the extractor fan.", 0.5),
                ("How much time is left?", 0.3)],
    "presenter": [("Start the presentation and start the timer.", 1.0), ("Go to the third slide.", 0.1), ("", 1.0),
                  ("Zoom in.", 0.5), ("Black screen.", 0.3), ("How much time do I have?", 0.3)],
}


def shot(name, wait=0.8):
    time.sleep(wait)   # the screen's next frames
    print("saved", board.shot(b, os.path.join(OUT, name + ".png")), flush=True)


def tap(x, y):
    b.cmd("tap %d %d" % (x, y))
    time.sleep(0.4)


b = board.ready()
b.on_message = lambda m, p: print(p.decode(errors="replace"), flush=True) if m == b"LOG0" and b"TEXTFIT" in p else None
b.cmd("screen home")
b.cmd("uicheck reset")
shot("home")
tap(752, 343)                 # the Voice control tile
shot("demos")
tap(16 + 159, 144 + 107)      # the first card: the robot vacuum
for demo, orders in ORDERS.items():
    b.cmd("screen " + demo)
    time.sleep(0.5)
    tap(1024 - 16 - 150 - 10 - 55, 34)   # Reset
    shot(demo + "_idle")
    for k, (text, wait) in enumerate(orders):
        if text:
            print(board.say(b, text), flush=True)
        shot("%s_%d" % (demo, k + 1), wait)
    tap(1024 - 16 - 150 - 10 - 110 - 10 - 50, 34)   # Rules
    shot(demo + "_rules")
    b.cmd("drag 500 500 500 200")                   # the rows scrolled
    shot(demo + "_rules_scrolled")
    tap(1024 - 16 - 66, 34)                         # Back
tap(38, 34)                   # the home button
tap(272, 343)                 # the Live transcription tile
shot("live")
for s in ("stats", "history", "phrases", "settings", "about"):
    b.cmd("screen " + s)
    shot(s)
b.cmd("screen home")
print(b.cmd("uicheck"))
b.close()
