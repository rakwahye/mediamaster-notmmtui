# SPDX-License-Identifier: LGPL-2.1-or-later
"""A stand-in daemon, to see how the strip follows one.

    fakemm.py <socket> <fixtures> watch|watch-drop|nowatch <version reply>

Answers `status` from status-playing-file.txt, logging each verb it is
sent, one a line, to <socket>.log. With `watch`, a watch connection is
kept open; one second after the first watcher comes, status becomes
status-paused.txt and the watchers are told "state". With `nowatch`,
watch is refused as a daemon too old for it would: "err unknown verb".
`version` is answered with <version reply>, e.g. "ok mediamaster 0.1.0
abi 3.0". `watch levels` gets "levels 80 40 <bands>" 20 times a second; with
watch-drop, each levels watcher is hung up on after a second. `search`
answers search-lists.txt ("no results" for the words "nothing"),
`browse` browse-list.txt, `queue add` "ok added at 7". Each command is
logged with its words. Runs until killed."""
import os, socket, sys, threading, time

path, fixtures, mode, version = sys.argv[1:5]
read = lambda name: open(os.path.join(fixtures, name), "rb").read()
status = [read("status-playing-file.txt")]
watchers = []
lock = threading.Lock()
log = open(path + ".log", "a", buffering=1)


def frame(c):
    """The verb and its words: "<verb> argc=N\\n" then N NUL-ended words."""
    data = b""
    while b"\n" not in data:
        d = c.recv(4096)
        if not d:
            return None
        data += d
    hdr, rest = data.split(b"\n", 1)
    n = int(hdr.split(b"argc=")[1])
    while rest.count(b"\0") < n:
        d = c.recv(4096)
        if not d:
            return None
        rest += d
    return [w.decode() for w in rest.split(b"\0")[:n]]


def later():
    time.sleep(1)
    with lock:
        status[0] = read("status-paused.txt")
        for w in watchers:
            try:
                w.sendall(b"state\n")
            except OSError:
                pass


def levels(c):
    line = b"levels 80 40 " + b"".join(b"%02x" % (8 * k) for k in range(32)) + b"\n"
    end = time.time() + (1.0 if mode == "watch-drop" else 1e9)
    try:
        while time.time() < end:
            c.sendall(line)
            time.sleep(0.05)
    except OSError:
        pass
    c.close()


def serve(c):
    argv = frame(c)
    if not argv:
        c.close()
        return
    log.write(" ".join(argv) + "\n")
    if argv[0] == "watch" and argv[1:] == ["levels"] and mode.startswith("watch"):
        c.sendall(b"ok watching levels\n")
        threading.Thread(target=levels, args=(c,), daemon=True).start()
        return
    if argv[0] == "watch":
        if not mode.startswith("watch"):
            c.sendall(b"err unknown verb: watch\n")
            c.close()
            return
        c.sendall(b"ok watching state track volume seek queue meta\n")
        with lock:
            watchers.append(c)
            if len(watchers) == 1:
                threading.Thread(target=later, daemon=True).start()
        return  # kept open
    with lock:
        if argv[0] == "status":
            reply = status[0]
        elif argv[0] == "version":
            reply = (version + "\n").encode()
        elif argv[0] == "search":
            reply = b"no results\n" if argv[1:] == ["nothing"] else read("search-lists.txt")
        elif argv[0] == "browse":
            reply = read("browse-list.txt")
        elif argv[:2] == ["queue", "add"]:
            reply = b"ok added at 7\n"
        else:
            reply = b"ok\n"
    c.sendall(reply)
    c.close()


try:
    os.unlink(path)
except FileNotFoundError:
    pass
s = socket.socket(socket.AF_UNIX)
s.bind(path)
s.listen(16)
while True:
    c, _ = s.accept()
    threading.Thread(target=serve, args=(c,), daemon=True).start()
