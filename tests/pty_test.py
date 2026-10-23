#!/usr/bin/env python3
"""Runs tests/interactive.lua in a pseudo-terminal and types into it.

    python3 tests/pty_test.py <lua-interpreter> "<cpath template>"
"""
import fcntl, os, pty, select, struct, sys, tempfile, termios, time

lua, cpath = sys.argv[1], sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))
script = os.path.join(here, "interactive.lua")


def run(extra, keys, expect_in_output):
    with tempfile.TemporaryDirectory() as tmp:
        result = os.path.join(tmp, "result.txt")
        pid, fd = pty.fork()
        if pid == 0:
            os.environ["TERM"] = "xterm-256color"
            os.execvp(lua, [lua, script, cpath, result] + extra)
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        out = b""

        def pump(seconds, until=None):
            nonlocal out
            end = time.time() + seconds
            while time.time() < end:
                r, _, _ = select.select([fd], [], [], 0.05)
                if r:
                    try:
                        data = os.read(fd, 65536)
                    except OSError:
                        return False
                    if not data:
                        return False
                    out += data
                if until and until in out:
                    return True
            return until is None

        assert pump(10, b"Add"), "UI never rendered:\n" + out.decode(errors="replace")
        for k in keys:
            os.write(fd, k)
            pump(0.4)
        pump(5)
        deadline = time.time() + 10
        status = None
        while time.time() < deadline:
            p, st = os.waitpid(pid, os.WNOHANG)
            if p:
                status = st
                break
            pump(0.1)
        if status is None:
            os.kill(pid, 9)
            raise AssertionError("process did not exit:\n" + out.decode(errors="replace"))
        pump(0.2)
        text = out.decode(errors="replace")
        for needle in expect_in_output:
            assert needle in text, f"{needle!r} missing from terminal output:\n{text}"
        content = open(result).read() if os.path.exists(result) else None
        return status, content, text


def run_example(name, keys):
    """Starts an example, waits for its first frame and quits it."""
    path = os.path.join(here, "..", "examples", name)
    env = dict(os.environ, TERM="xterm-256color", LUA_CPATH=cpath + ";;")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(env)
        os.execvp(lua, [lua, path])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    out = b""
    end = time.time() + 10
    while time.time() < end and len(out) < 200:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            out += os.read(fd, 65536)
    assert len(out) >= 200, f"{name}: nothing rendered"
    for k in keys:
        time.sleep(0.3)
        os.write(fd, k)
    end = time.time() + 10
    status = None
    while time.time() < end:
        try:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                out += os.read(fd, 65536)
        except OSError:
            pass
        p, st = os.waitpid(pid, os.WNOHANG)
        if p:
            status = st
            break
    if status is None:
        os.kill(pid, 9)
        raise AssertionError(f"{name}: did not quit\n" + out.decode(errors="replace"))
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, (
        f"{name}: exit {status}\n" + out.decode(errors="replace"))
    print(f"ok  example {name}")


# 1. type, move to the button, press it twice, quit with '~'
status, content, _ = run([], [b"hello", b"\x1b[B", b"\r", b"\r", b"~"], ["clicks: 0", "clicks: 2"])
assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, f"bad exit status {status}"
assert content == "value=hello clicks=2 ticked=true\n", repr(content)
print("ok  interactive session")

# 2. an error raised in a callback leaves the loop and surfaces as a Lua error
status, content, text = run(["fail"], [b"\x1b[B", b"\r"], ["boom from interactive click"])
assert os.WIFEXITED(status) and os.WEXITSTATUS(status) != 0, f"expected failure, got {status}"
assert content is None, "script must not reach the code after ui.run"
assert "stack traceback" in text
print("ok  callback error propagates out of ui.run")

run_example("dashboard.lua", [b"q"])
run_example("form.lua", [b"hi", b"\x1b"])
