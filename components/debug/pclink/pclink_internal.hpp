// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The command handlers the parts of pclink give each other.
 */
#pragma once

namespace pclink {
void cmd_load(int argc, char **argv);        /* model_over_usb.cpp */
void cmd_feed_begin(int argc, char **argv);  /* audio_feed.cpp */
void cmd_feed(int argc, char **argv);
void cmd_feed_end(int argc, char **argv);
} // namespace pclink
