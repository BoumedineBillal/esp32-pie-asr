// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// voice_rules_cli [demo] [-w]: lines "file<TAB>seconds<TAB>text" (pieasr/host's transcribe) or plain text on stdin ->
// the same key, a TAB, the demo's actions (voice::describe; the demo: vacuum, smarthome, car, factory, kitchen or
// presenter, default vacuum) and, with -w, each word's role (v verb, t target, n negation, c connector, # number).
#include <cstdio>
#include <cstring>
#include "demos.hpp"

int main(int argc, char **argv)
{
    bool roles = false;
    const voice::Demo *demo = demos::find("vacuum");
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w")) {
            roles = true;
        } else if (!(demo = demos::find(argv[i]))) {
            fprintf(stderr, "no demo %s\n", argv[i]);
            return 1;
        }
    }
    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        const char *key = line, *text = line;
        char *t1 = strchr(line, '\t');
        if (t1) {
            *t1 = 0;
            char *t2 = strchr(t1 + 1, '\t');
            text = t2 ? t2 + 1 : t1 + 1;
        }
        voice::Result r;
        voice::parse_text(*demo, text, r);
        char d[1024];
        printf("%s\t%s", t1 ? key : text, voice::describe(*demo, r, d, sizeof d));
        if (roles) {
            static const char C[] = {'.', 'v', 't', 'n', 'c', '#'};
            printf("\t");
            for (int i = 0; i < r.nwords; i++)
                putchar(C[r.role[i]]);
        }
        printf("\n");
    }
    return 0;
}
