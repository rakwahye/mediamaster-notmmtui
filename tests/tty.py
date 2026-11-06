# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run notmmtui in a pty of a given size and drive it.

    tty.py <rows> <cols> <command> [steps...]

<command> is split as a shell would (quotes, no expansion).

Steps, in order:
    wait:<text>   until <text> is on the screen
    gone:<text>   until it no longer is
    key:<keys>    type them; \\e is Escape, \\r Enter, [A..[D arrows
    size:<r>,<c>  resize the terminal (SIGWINCH)
    sleep:<s>     pause
    show:         print the screen to stderr
    color:<text>=<rrggbb|default>
                  until the first cell of <text> on screen has that foreground
    bgat:<x>=<rrggbb|default>
                  until the cell at column x of the top row has that background
    brighter:<x1>,<x2>
                  until the top row's background is brighter at x1 than at x2
    moving:<x>    until the top row's cell at x changes colour
    still:<x>,<s> for s seconds, the top row's cell at x keeps its colours
    legible:<s>   for s seconds, every letter on screen stands out from its
                  own background (contrast at least 1.5:1, where both are known)

Then it waits for the program to exit and exits with its status. On a
step that times out it prints the screen text and exits 1.

The pty answers Notcurses' startup queries as foot would (version,
cursor position, device attributes), so it starts without a real
terminal. What it shows is rebuilt by a small VT interpreter (cursor
moves, erases, repeat, colours), since Notcurses writes only the
cells that changed."""
import codecs, shlex, fcntl, os, pty, re, select, signal, struct, sys, termios, time, unicodedata

rows, cols, prog, steps = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4:]
pid, fd = pty.fork()
if pid == 0:
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, cols * 10, rows * 20))
    env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
    env.setdefault("LANG", "C.UTF-8")
    argv = shlex.split(prog)
    os.execve(argv[0], argv, env)

out = b""
answered = False


def pump(timeout):
    global out, answered
    r, _, _ = select.select([fd], [], [], timeout)
    if not r:
        return True
    try:
        d = os.read(fd, 65536)
    except OSError:
        return False
    if not d:
        return False
    out += d
    global pending
    pending = screen.feed(pending + utf8.decode(d))
    if not answered and b"\x1b[c" in out:
        os.write(fd, b"\x1bP>|foot(1.21.0)\x1b\\" + b"\x1b[1;1R" + b"\x1b[?62;22c")
        answered = True
    return True


class Screen:
    def __init__(self, r, c):
        self.fg = None  # None: the terminal's default; else "rrggbb"
        self.bg = None
        self.resize(r, c)
        self.y = self.x = 0
        self.last = " "

    def resize(self, r, c):
        old, oldfg, oldbg = getattr(self, "g", []), getattr(self, "f", []), getattr(self, "b", [])
        self.r, self.c = r, c
        self.g = [[" "] * c for _ in range(r)]
        self.f = [[None] * c for _ in range(r)]
        self.b = [[None] * c for _ in range(r)]
        for y, row in enumerate(old[:r]):
            for x, ch in enumerate(row[:c]):
                self.g[y][x] = ch
                self.f[y][x] = oldfg[y][x]
                self.b[y][x] = oldbg[y][x]

    def sgr(self, params):
        p = [int(v) if v.isdigit() else 0 for v in re.split("[;:]", params)] if params else [0]
        i = 0
        while i < len(p):
            if p[i] == 0:
                self.fg = self.bg = None
            elif p[i] == 39:
                self.fg = None
            elif p[i] == 49:
                self.bg = None
            elif p[i] == 48 and i + 4 < len(p) and p[i + 1] == 2:
                self.bg = "%02x%02x%02x" % (p[i + 2], p[i + 3], p[i + 4])
                i += 4
            elif p[i] == 38 and i + 4 < len(p) and p[i + 1] == 2:
                self.fg = "%02x%02x%02x" % (p[i + 2], p[i + 3], p[i + 4])
                i += 4
            elif p[i] == 38 and i + 2 < len(p) and p[i + 1] == 5:
                self.fg = "idx%d" % p[i + 2]
                i += 2
            elif p[i] == 48 and i + 2 < len(p) and p[i + 1] == 5:
                self.bg = "idx%d" % p[i + 2]
                i += 2
            i += 1

    def put(self, ch):
        w = 2 if unicodedata.east_asian_width(ch) in "WF" else 0 if unicodedata.combining(ch) else 1
        if w == 0:
            return
        if self.x + w > self.c:
            self.x, self.y = 0, min(self.y + 1, self.r - 1)
        self.g[self.y][self.x] = ch
        self.f[self.y][self.x] = self.fg
        self.b[self.y][self.x] = self.bg
        if w == 2 and self.x + 1 < self.c:
            self.g[self.y][self.x + 1] = ""
        self.x += w
        self.last = ch

    def csi(self, params, final):
        n = [int(p) if p.isdigit() else 0 for p in params.split(";")] if params else []
        a = n[0] if n and n[0] else 1
        clampx = lambda v: max(0, min(self.c - 1, v))
        clampy = lambda v: max(0, min(self.r - 1, v))
        if final == "m":
            self.sgr(params)
        elif final in "Hf":
            self.y = clampy((n[0] if n and n[0] else 1) - 1)
            self.x = clampx((n[1] if len(n) > 1 and n[1] else 1) - 1)
        elif final == "A": self.y = clampy(self.y - a)
        elif final == "B": self.y = clampy(self.y + a)
        elif final == "C": self.x = clampx(self.x + a)
        elif final == "D": self.x = clampx(self.x - a)
        elif final == "G": self.x = clampx(a - 1)
        elif final == "d": self.y = clampy(a - 1)
        elif final == "b":
            for _ in range(a): self.put(self.last)
        elif final == "X":
            for x in range(self.x, min(self.c, self.x + a)):
                self.g[self.y][x], self.b[self.y][x] = " ", self.bg
        elif final == "K":
            m = n[0] if n else 0
            lo, hi = (self.x, self.c) if m == 0 else (0, self.x + 1) if m == 1 else (0, self.c)
            for x in range(lo, hi):
                self.g[self.y][x], self.b[self.y][x] = " ", self.bg
        elif final == "J":
            m = n[0] if n else 0
            rows = range(self.y + 1, self.r) if m == 0 else range(0, self.y) if m == 1 else range(self.r)
            for y in rows:
                self.g[y], self.b[y] = [" "] * self.c, [self.bg] * self.c
            if m in (0, 1): self.csi(str(m), "K")

    def feed(self, text):
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "\x1b":
                m = SEQ.match(text, i)
                if not m:
                    return text[i:]  # incomplete: keep for the next feed
                if m.group("csi") is not None and not m.group("priv"):
                    self.csi(m.group("csi"), m.group("final"))
                i = m.end()
                continue
            if ch == "\r": self.x = 0
            elif ch == "\n": self.y = min(self.y + 1, self.r - 1)
            elif ch == "\b": self.x = max(0, self.x - 1)
            elif ch >= " ": self.put(ch)
            i += 1
        return ""

    def text(self):
        return "\n".join("".join(row) for row in self.g)

    def illegible(self):
        """The first letter whose colour does not stand out from its ground."""
        def lum(h):
            c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
            c = [v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4 for v in c]
            return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
        for y in range(self.r):
            for x in range(self.c):
                ch, f, b = self.g[y][x], self.f[y][x], self.b[y][x]
                if not ch.strip() or not f or not b or f.startswith("idx") or b.startswith("idx"):
                    continue
                if 0x2580 <= ord(ch[0]) <= 0x259f or ord(ch[0]) >= 0x1fb00:  # blocks are ground, not words
                    continue
                hi, lo = sorted((lum(f), lum(b)), reverse=True)
                if (hi + 0.05) / (lo + 0.05) < 1.5:
                    return (ch, f, b)
        return None

    def color_of(self, needle):
        for y, row in enumerate(self.g):
            line = "".join(row)
            x = line.find(needle)
            if x >= 0:
                # line index to cell index: wide glyphs leave "" behind
                cells = [i for i, ch in enumerate(row) if ch != ""]
                return self.f[y][cells[x]] or "default"
        return None


SEQ = re.compile(r"\x1b(?:\[(?P<priv>[?<>=]?)(?P<csi>[0-9;:]*)[ -/]*(?P<final>[@-~])"
                 r"|\][^\x07\x1b]*(?:\x07|\x1b\\)|[P_^][^\x1b]*\x1b\\|[()*+][0-9A-Za-z]|[ -/]*[0-~])")
screen = Screen(rows, cols)
pending = ""
utf8 = codecs.getincrementaldecoder("utf-8")("replace")  # a glyph may straddle two reads


def text():
    return screen.text()


def until(cond, what):
    end = time.time() + 10
    while time.time() < end:
        if cond():
            return
        if not pump(0.05):
            break
    sys.stderr.write("tty.py: timed out on %s; screen:\n%s\n" % (what, text()))
    os.kill(pid, 9)
    sys.exit(1)


for st in steps:
    kind, _, arg = st.partition(":")
    if kind == "wait":
        until(lambda: arg in text(), st)
    elif kind == "gone":
        until(lambda: arg not in text(), st)
    elif kind == "key":
        keys = arg.replace("\\e", "\x1b").replace("\\r", "\r")
        keys = re.sub(r"\[([A-D])", lambda m: "\x1b[" + m.group(1), keys)
        os.write(fd, keys.encode())
    elif kind == "size":
        r, c = (int(v) for v in arg.split(","))
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", r, c, c * 10, r * 20))
        screen.resize(r, c)
        os.kill(pid, signal.SIGWINCH)
    elif kind == "color":
        want_text, _, want = arg.rpartition("=")
        until(lambda: screen.color_of(want_text) == want, st + " (is %s)" % screen.color_of(want_text))
    elif kind == "bgat":
        x, _, want = arg.partition("=")
        x = int(x)
        until(lambda: (screen.b[0][x] or "default") == want, st + " (is %s)" % (screen.b[0][x] or "default"))
    elif kind == "brighter":
        a, b = (int(v) for v in arg.split(","))
        lum = lambda h: 0 if not h or not h[0].isalnum() or h.startswith("idx") else \
            0.2126 * int(h[0:2], 16) + 0.7152 * int(h[2:4], 16) + 0.0722 * int(h[4:6], 16)
        until(lambda: lum(screen.b[0][a]) > lum(screen.b[0][b]),
              st + " (%s vs %s)" % (screen.b[0][a], screen.b[0][b]))
    elif kind == "moving":
        x = int(arg)
        first = (screen.f[0][x], screen.b[0][x])
        until(lambda: (screen.f[0][x], screen.b[0][x]) != first, st + " (stays %s on %s)" % first)
    elif kind == "still":
        x, secs = arg.split(",")
        x = int(x)
        first = (screen.f[0][x], screen.b[0][x])
        end = time.time() + float(secs)
        while time.time() < end:
            pump(0.02)
            if (screen.f[0][x], screen.b[0][x]) != first:
                sys.stderr.write("tty.py: %s: went from %s to %s\n" % (st, first, (screen.f[0][x], screen.b[0][x])))
                os.kill(pid, 9)
                sys.exit(1)
    elif kind == "legible":
        end = time.time() + float(arg)
        while time.time() < end:
            pump(0.02)
            bad = screen.illegible()
            if bad:
                sys.stderr.write("tty.py: %s: %r is %s on %s; screen:\n%s\n" % ((st,) + bad + (text(),)))
                os.kill(pid, 9)
                sys.exit(1)
    elif kind == "show":
        sys.stderr.write(text() + "\n")
    elif kind == "sleep":
        end = time.time() + float(arg)
        while time.time() < end:
            pump(0.05)
    else:
        sys.exit("tty.py: unknown step " + st)

end = time.time() + 10
while time.time() < end:
    p, status = os.waitpid(pid, os.WNOHANG)
    if p:
        sys.exit(os.waitstatus_to_exitcode(status))
    pump(0.05)
os.kill(pid, 9)
sys.exit("tty.py: program did not exit")
