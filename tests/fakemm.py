# SPDX-License-Identifier: LGPL-2.1-or-later
"""A stand-in daemon, to see how the strip follows one.

    fakemm.py <socket> <fixtures> watch|nowatch

Answers `status` from status-playing-file.txt, logging each verb it is
sent, one a line, to <socket>.log. With `watch`, a watch connection is
kept open; one second after the first watcher comes, status becomes
status-paused.txt and the watchers are told "state". With `nowatch`,
watch is refused as a daemon too old for it would: "err unknown verb".
Runs until killed."""
import os, socket, sys, threading, time

path, fixtures, mode = sys.argv[1:4]
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


def serve(c):
    argv = frame(c)
    if not argv:
        c.close()
        return
    log.write(argv[0] + "\n")
    if argv[0] == "watch":
        if mode != "watch":
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
        reply = status[0] if argv[0] == "status" else b"ok fake\n" if argv[0] == "version" else b"ok\n"
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
