#!/usr/bin/env python3
"""Record a real NexusShell session and render docs/screenshot.png and docs/demo.gif.

Not a mock-up: the script starts ./nexusshell on a pseudo-terminal, types into it like a
person would, feeds everything the shell prints into a terminal emulator (pyte), and draws
each screen state with PIL.

    pip install pyte pillow
    python3 docs/make_demo.py            # run from the repository root

The prompt in the README example (akshat@nexus:~/projects/linux-shell $) comes from a demo
account: set DEMO_CMD to a command that starts the shell as that user, or leave it unset to
record with your own account. See SHELL_CMD below.
"""

import fcntl
import os
import pty
import select
import shlex
import struct
import sys
import termios
import time

import pyte
from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 92, 31
OUT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(OUT_DIR)
SHELL_CMD = shlex.split(os.environ.get("DEMO_CMD", os.path.join(REPO, "nexusshell")))

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
SIZE = 15
CELL_W, CELL_H = 9, 19
PAD, BAR = 16, 34

BG, FG = (30, 30, 46), (205, 214, 244)
COLORS = {
    "default": FG, "black": (69, 71, 90), "red": (243, 139, 168), "green": (166, 227, 161),
    "brown": (249, 226, 175), "blue": (137, 180, 250), "magenta": (245, 194, 231),
    "cyan": (148, 226, 213), "white": (186, 194, 222),
}

CTRL_C, CTRL_Z, UP = "\x03", "\x1a", "\x1b[A"


class Recorder:
    def __init__(self):
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.ByteStream(self.screen)
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execvp(SHELL_CMD[0], SHELL_CMD)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        self.frames = []   # (snapshot, timestamp)
        self.marks = {}    # name -> frame index

    def drain(self, timeout):
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                return
            ready, _, _ = select.select([self.fd], [], [], left)
            if ready:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    return
                if not data:
                    return
                self.stream.feed(data)

    def snapshot(self):
        cells = []
        for y in range(ROWS):
            row = self.screen.buffer[y]
            cells.append(tuple((row[x].data, row[x].fg, row[x].bold, row[x].reverse)
                               for x in range(COLS)))
        return (tuple(cells), (self.screen.cursor.x, self.screen.cursor.y))

    def capture(self):
        self.frames.append((self.snapshot(), time.time()))

    def wait(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            self.drain(0.1)
            self.capture()

    def send(self, text):
        os.write(self.fd, text.encode())

    def type(self, text, per_char=0.045):
        for i, ch in enumerate(text):
            self.send(ch)
            self.drain(per_char)
            if i % 2 == 1 or i == len(text) - 1:
                self.capture()

    def line(self, text, after=0.7):
        self.type(text)
        self.wait(0.25)
        self.send("\n")
        self.wait(after)

    def mark(self, name):
        self.drain(0.1)
        self.capture()
        self.marks[name] = len(self.frames) - 1


def scenario(r):
    r.wait(1.2)                                         # banner and first prompt
    r.line('echo build me a shell | tr a-z A-Z')        # pipe
    r.line('ls /usr | sort -r | head -n 3 > /tmp/top.txt')   # pipe + redirection
    r.line('cat /tmp/top.txt')
    r.line('false')                                     # status indicator
    r.line('echo last status was $?')
    r.line('sleep 30', after=1.0)                       # Ctrl+C
    r.send(CTRL_C)
    r.wait(0.8)
    r.line('sleep 60', after=1.0)                       # Ctrl+Z, bg, jobs, fg
    r.send(CTRL_Z)
    r.wait(0.8)
    r.line('bg', after=0.6)
    r.line('jobs', after=0.8)
    r.mark("shot")                                      # the README screenshot
    r.line('fg', after=1.0)
    r.send(CTRL_C)
    r.wait(0.8)
    for _ in range(3):                                  # history recall with Up
        r.send(UP)
        r.drain(0.05)
        r.capture()
        r.wait(0.35)
    r.send("\x15")                                      # Ctrl+U clears the recalled line
    r.wait(0.3)
    r.line('history 6', after=1.2)
    r.line('exit', after=0.6)


def render(snapshot, font, font_bold, title):
    cells, (cx, cy) = snapshot
    width = PAD * 2 + COLS * CELL_W
    height = BAR + PAD * 2 + ROWS * CELL_H
    img = Image.new("RGB", (width, height), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, width, BAR], fill=(24, 24, 37))                    # title bar
    for i, color in enumerate([(243, 139, 168), (249, 226, 175), (166, 227, 161)]):
        d.ellipse([14 + i * 22, 11, 14 + i * 22 + 12, 23], fill=color)
    d.text((width // 2, BAR // 2), title, font=font, fill=(147, 153, 178), anchor="mm")
    for y, row in enumerate(cells):
        for x, (ch, fg, bold, reverse) in enumerate(row):
            px, py = PAD + x * CELL_W, BAR + PAD + y * CELL_H
            color = COLORS.get(fg, FG) if not fg.startswith("#") and fg in COLORS else FG
            if fg not in COLORS and len(fg) == 6:                          # 24-bit colour
                color = tuple(int(fg[i:i + 2], 16) for i in (0, 2, 4))
            if x == cx and y == cy:
                d.rectangle([px, py, px + CELL_W - 1, py + CELL_H - 1], fill=FG)
                color = BG
            elif reverse:
                d.rectangle([px, py, px + CELL_W - 1, py + CELL_H - 1], fill=FG)
                color = BG
            if ch != " ":
                d.text((px, py), ch, font=font_bold if bold else font, fill=color)
    return img


def main():
    r = Recorder()
    scenario(r)
    r.drain(0.3)

    font = ImageFont.truetype(FONT, SIZE)
    font_bold = ImageFont.truetype(FONT_BOLD, SIZE)
    title = os.environ.get("DEMO_TITLE", "akshat@nexus: ~/projects/linux-shell")

    # Merge identical consecutive frames; a frame lasts until the next one appears.
    merged = []
    for i, (snap, ts) in enumerate(r.frames):
        nxt = r.frames[i + 1][1] if i + 1 < len(r.frames) else ts + 1.5
        dur = min(max(nxt - ts, 0.04), 1.6)
        if merged and merged[-1][0] == snap:
            merged[-1][1] += dur
        else:
            merged.append([snap, dur])
    print("%d captured frames -> %d distinct" % (len(r.frames), len(merged)))

    shot_snapshot = r.frames[r.marks["shot"]][0]
    shot = render(shot_snapshot, font, font_bold, title)
    last_row = shot_snapshot[1][1]  # the cursor row: nothing below it is worth showing
    shot = shot.crop((0, 0, shot.width, BAR + PAD * 2 + (last_row + 1) * CELL_H))
    shot.save(os.path.join(OUT_DIR, "screenshot.png"), optimize=True)

    images = [render(snap, font, font_bold, title) for snap, _ in merged]
    # One shared palette, so colours do not flicker between frames.
    samples = [images[i] for i in range(0, len(images), max(1, len(images) // 6))]
    sheet = Image.new("RGB", (samples[0].width, samples[0].height * len(samples)))
    for i, im in enumerate(samples):
        sheet.paste(im, (0, i * im.height))
    palette = sheet.quantize(colors=48, method=Image.Quantize.MEDIANCUT)
    frames = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in images]
    durations = [max(20, int(d * 1000)) for _, d in merged]
    durations[-1] = 2500
    frames[0].save(os.path.join(OUT_DIR, "demo.gif"), save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True, disposal=1)
    print("wrote screenshot.png and demo.gif (%.1f s)" % (sum(durations) / 1000))


if __name__ == "__main__":
    sys.exit(main())
