"""Server persistence end to end: edits and the seed survive a restart, and
a second server cannot open a world that is in use.

    python tests/e2e_persistence.py <path to ft_vox executable>

Runs servers in a temporary directory (never touches saves/).
"""
import math
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

EXE = os.path.abspath(sys.argv[1])
IDS = dict(Hello=0x0001, Login=0x0002, BreakBlock=0x0020, LoginOk=0x0102,
           BlockUpdate=0x0120, ChunkData=0x0130, Disconnect=0x0FFF)
NAMES = {v: k for k, v in IDS.items()}
EYE = 1.62


PROCS = []


def ok(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        for proc in PROCS:
            if proc.poll() is None:
                proc.kill()
        sys.exit(1)


def start_server(cwd, port, *extra):
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    proc = subprocess.Popen([EXE, "--server", str(port), *extra], cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, creationflags=flags)
    PROCS.append(proc)
    return proc


def wait_line(proc, needle, timeout=15):
    """Reads server output until `needle` appears; returns all lines read."""
    lines, end = [], time.time() + timeout
    while time.time() < end:
        line = proc.stdout.readline()
        if not line:
            if proc.poll() is not None:
                break
            continue
        lines.append(line.rstrip())
        if needle in line:
            return lines
    return lines


def stop_server(proc):
    proc.send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
    try:
        return proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
        return None


class Client:
    def __init__(self, port, name):
        self.s = socket.create_connection(("127.0.0.1", port))
        self.s.settimeout(0.2)
        self.buf = b""
        self.chunks = {}
        self.send("Hello", struct.pack("<I", 1))
        self.send("Login", struct.pack("<H", len(name)) + name.encode())

    def send(self, name, payload=b""):
        self.s.sendall(struct.pack("<IH", len(payload) + 2, IDS[name]) + payload)

    def pump(self, seconds, until=None):
        out, end = [], time.time() + seconds
        while time.time() < end:
            try:
                data = self.s.recv(1 << 20)
                if not data:
                    return out
                self.buf += data
            except socket.timeout:
                pass
            while len(self.buf) >= 6:
                ln, pid = struct.unpack_from("<IH", self.buf)
                if len(self.buf) < 4 + ln:
                    break
                payload, self.buf = self.buf[6:4 + ln], self.buf[4 + ln:]
                name = NAMES.get(pid, hex(pid))
                if name == "ChunkData":
                    cx, cz = struct.unpack_from("<ii", payload)
                    self.chunks[(cx, cz)] = payload[8:]
                out.append((name, payload))
                if until and until(name, payload):
                    return out
        return out

    def close(self):
        self.send("Disconnect", b"\x00")
        self.s.close()


def block_at(column_payload, lx, wy, lz):
    mask, = struct.unpack_from("<I", column_payload)
    off, sy = 4, wy + 64
    for s in range(20):
        if not mask & (1 << s):
            continue
        n, = struct.unpack_from("<H", column_payload, off)
        off += 2
        blocks = []
        for _ in range(n):
            ln, b = struct.unpack_from("<HH", column_payload, off)
            off += 4
            blocks += [b] * ln
        if s == sy // 16:
            return blocks[lx + (lz << 4) + ((sy % 16) << 8)]
    return 0


def login(port):
    client = Client(port, "tester")
    msgs = client.pump(20, lambda n, p: n == "LoginOk")
    login_ok = [p for n, p in msgs if n == "LoginOk"]
    ok(len(login_ok) == 1, "login")
    _, sx, sy, sz = struct.unpack("<Ifff", login_ok[0])
    spawn_chunk = (math.floor(sx / 16), math.floor(sz / 16))
    client.pump(10, lambda n, p: spawn_chunk in client.chunks)
    return client, (sx, sy, sz)


with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    world = os.path.join(tmp, "w")

    # 1. first run: break the block the player stands on
    server = start_server(tmp, 25621, "--world", world, "--seed", "42")
    log = wait_line(server, "listening")
    ok(any("seed 42" in line for line in log), "new world uses --seed 42")
    client, (sx, sy, sz) = login(25621)
    target = (math.floor(sx), round(sy - EYE) - 1, math.floor(sz))
    client.send("BreakBlock", struct.pack("<iii", *target))
    upd = lambda n, p: n == "BlockUpdate" and struct.unpack_from("<iii", p) == target
    got = [p for n, p in client.pump(5, upd) if upd(n, p)]
    ok(got and struct.unpack_from("<H", got[0], 12)[0] == 0, "block broken")
    client.close()

    # 2. a second server cannot open the same world
    second = start_server(tmp, 25622, "--world", world)
    lines = wait_line(second, "locked", timeout=10)
    try:
        code = second.wait(timeout=10)
    except subprocess.TimeoutExpired:
        second.kill()
        code = None
    ok(code not in (0, None) and any("locked" in line or "already open" in line for line in lines),
       "second server on the same world is refused")

    # 3. graceful stop saves everything
    ok(stop_server(server) == 0, "server stops cleanly")
    ok(os.path.isdir(os.path.join(world, "region")), "region files written")

    # 4. restart: same seed (the new --seed is ignored), the edit is still there
    server = start_server(tmp, 25623, "--world", world, "--seed", "99")
    log = wait_line(server, "listening")
    ok(any("seed 42" in line for line in log), "existing world keeps seed 42")
    client, _ = login(25623)
    lx, lz = target[0] % 16, target[2] % 16
    chunk = (target[0] // 16, target[2] // 16)
    ok(chunk in client.chunks and block_at(client.chunks[chunk], lx, target[1], lz) == 0,
       "edit survived the restart")
    client.close()
    ok(stop_server(server) == 0, "server stops cleanly again")

print("ALL PASSED")
