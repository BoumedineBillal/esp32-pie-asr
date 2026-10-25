# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""word_rules.py: the transcript's word and line rules (components/asr_service/transcript.cpp) written again in Python,
to check the board's words on its own committed tokens.

A token that starts a word (U+2581) makes the word being written final; so do `timeout` frames without a token. A
new line starts at a token that starts a word, `pause` frames or more after the last piece of a word (a piece alone
after its word ended, such as a late ".", does not count). A word without letters or digits is written without a
space before it.

    words, lines = words_of(pieces, frames, toks, timeout_s, pause_s)
    words: [(text, space before, line, end ms)]; toks: [(frame, token id)] as the board's "toks" gives them
"""
FRAME_S = 0.04


class WordRules:
    def __init__(self, pieces, timeout_s, pause_s):
        self.pieces = pieces
        self.timeout = int(round(timeout_s / FRAME_S))
        self.pause = int(round(pause_s / FRAME_S))
        self.frame = 0
        self.cur, self.cur_end, self.last_token, self.last_line_tok = [], 0, -10 ** 9, -10 ** 9
        self.line, self.lines, self.words = 0, [0.0], []

    def piece(self, k):
        return self.pieces[k] if 0 <= k < len(self.pieces) else ""

    def chunk(self, toks):
        """one token id per frame (0: none)"""
        for k in toks:
            f = self.frame
            if k:
                starts = self.piece(k).startswith("▁")
                if starts and self.cur:
                    self.close_word()
                if not self.cur and starts and f - self.last_line_tok >= self.pause and self.words:
                    self.line += 1
                    self.lines.append(round(f * FRAME_S, 2))
                if starts or self.cur:
                    self.last_line_tok = f
                self.cur.append(k)
                self.cur_end, self.last_token = f, f
            elif self.cur and f - self.last_token >= self.timeout:
                self.close_word()
            self.frame += 1

    def close_word(self):
        if not self.cur:
            return
        text = "".join(self.piece(k) for k in self.cur).replace("▁", " ").strip()
        self.cur = []
        if not text:
            return
        punct = not any(ch.isalnum() for ch in text)
        end = round((self.cur_end + 1) * FRAME_S, 2)
        self.words.append((text, int(not punct or not self.words), self.line, int(round(end * 1000))))


def words_of(pieces, frames, toks, timeout_s, pause_s):
    t = WordRules(pieces, timeout_s, pause_s)
    per = [0] * frames
    for f, k in toks:
        per[f] = k
    for c in range(frames // 32):   # the stream's chunks: 32 frames each
        t.chunk(per[c * 32:(c + 1) * 32])
    t.close_word()
    return t.words, t.lines
