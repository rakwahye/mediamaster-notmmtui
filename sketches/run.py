# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run a sketch in a pty that answers Notcurses' startup queries as foot 1.21
would (so octants, sextants and RGB are on), and drain its output.
Usage: run.py <rows> <cols> <concept> <frames> <dump>"""
import fcntl, os, pty, select, struct, sys, termios, time

rows, cols, concept, frames, dump = sys.argv[1:6]
here = os.path.dirname(os.path.abspath(__file__))
pid, fd = pty.fork()
if pid == 0:
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", int(rows), int(cols), int(cols) * 10, int(rows) * 20))
    env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor", LANG="C.UTF-8", LC_ALL="C.UTF-8",
               LD_PRELOAD=os.path.join(here, "wcshim.so"))
    os.execve(os.path.join(here, "sketch"), ["sketch", concept, frames, dump], env)

buf, answered, deadline = b"", False, time.time() + 60
while True:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            d = os.read(fd, 65536)
        except OSError:
            break
        if not d:
            break
        if not answered:
            buf += d
            if b"\x1b[c" in buf:
                os.write(fd, b"\x1bP>|foot(1.21.0)\x1b\\" + b"\x1b[1;1R" + b"\x1b[?62;22c")
                answered = True
    if time.time() > deadline:
        os.kill(pid, 9)
        sys.exit("timeout")
    p, st = os.waitpid(pid, os.WNOHANG)
    if p:
        sys.exit(os.waitstatus_to_exitcode(st))
_, st = os.waitpid(pid, 0)
sys.exit(os.waitstatus_to_exitcode(st))
