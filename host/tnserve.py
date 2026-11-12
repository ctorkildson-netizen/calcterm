#!/usr/bin/env python3
"""tnserve.py PORT -- a telnet server for the tests: inetutils telnetd for
each connection (as inetd would start it), running a shell instead of
login. On 127.0.0.1 only."""
import os, socket, sys
port = int(sys.argv[1]) if len(sys.argv) > 1 else 2323
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", port))
s.listen(4)
print("listening", port, flush=True)
while True:
    c, _ = s.accept()
    if os.fork() == 0:
        s.close()
        for fd in (0, 1, 2):
            os.dup2(c.fileno(), fd)
        os.execv("/usr/sbin/telnetd", ["telnetd", "-h", "-n", "-E", "/bin/bash --norc --noprofile"])
    c.close()
    try:
        while os.waitpid(-1, os.WNOHANG)[0]:
            pass
    except ChildProcessError:
        pass
