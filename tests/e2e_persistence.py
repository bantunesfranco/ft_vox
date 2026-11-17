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
IDS = dict(Hello=0x0001, Login=0x0002, PlayerMove=0x0010, BreakBlock=0x0020, InventoryAction=0x0030,
           LoginOk=0x0102, BlockUpdate=0x0120, ChunkData=0x0130, InventoryContents=0x0150, Disconnect=0x0FFF)
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
        self.send("Hello", struct.pack("<I", 2))
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


def inventory(payload):
    """InventoryContents -> (ackSeq, selected, [(item, count)] * 36)."""
    ack, selected = struct.unpack_from("<IB", payload)
    return ack, selected, [struct.unpack_from("<HH", payload, 5 + 4 * i) for i in range(36)]


DIRT_BLOCK, DIRT_ITEM, AMETHYST_ITEM, TORCH_ITEM, GLOWSTONE_ITEM = 2, 2, 5, 15, 14
STARTING_ITEMS = [(AMETHYST_ITEM, 64), (TORCH_ITEM, 64), (GLOWSTONE_ITEM, 64)] + [(0, 0)] * 33


def login(port, name="tester"):
    """Returns the client, the LoginOk (eye, yaw, pitch) and the first InventoryContents."""
    client = Client(port, name)
    msgs = client.pump(20, lambda n, p: n == "LoginOk")
    login_ok = [p for n, p in msgs if n == "LoginOk"]
    ok(len(login_ok) == 1 and len(login_ok[0]) == 24, f"{name} logs in")
    _, sx, sy, sz, yaw, pitch = struct.unpack("<Ifffff", login_ok[0])
    msgs = client.pump(5, lambda n, p: n == "InventoryContents")
    contents = [inventory(p) for n, p in msgs if n == "InventoryContents"]
    ok(len(contents) == 1, f"{name} gets InventoryContents after LoginOk")
    spawn_chunk = (math.floor(sx / 16), math.floor(sz / 16))
    client.pump(10, lambda n, p: spawn_chunk in client.chunks)
    return client, (sx, sy, sz), (yaw, pitch), contents[0]


def find_block(client, eye, block, avoid, reach=6):
    """A cell holding `block` within `reach` of `eye`, outside the (x, z) `avoid`, from the columns received."""
    ex, ey, ez = (math.floor(v) for v in eye)
    for dy in range(-2, -reach, -1):
        for dx in range(-3, 4):
            for dz in range(-3, 4):
                x, y, z = ex + dx, ey + dy, ez + dz
                if (x, z) == avoid:
                    continue
                chunk = (x // 16, z // 16)
                if chunk in client.chunks and block_at(client.chunks[chunk], x % 16, y, z % 16) == block:
                    return x, y, z
    return None


with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    world = os.path.join(tmp, "w")

    # 1. first run: break the block the player stands on
    server = start_server(tmp, 25621, "--world", world, "--seed", "42")
    log = wait_line(server, "listening")
    ok(any("seed 42" in line for line in log), "new world uses --seed 42")
    client, (sx, sy, sz), _, _ = login(25621)
    target = (math.floor(sx), round(sy - EYE) - 1, math.floor(sz))
    client.send("BreakBlock", struct.pack("<iii", *target))
    upd = lambda n, p: n == "BlockUpdate" and struct.unpack_from("<iii", p) == target
    got = [p for n, p in client.pump(5, upd) if upd(n, p)]
    ok(got and struct.unpack_from("<H", got[0], 12)[0] == 0, "block broken")
    client.close()

    # 1b. "Saver" moves, looks, selects a slot and breaks a dirt block
    saver, _, _, _ = login(25621, "Saver")
    saved_eye = (sx + 3.25, sy + 0.5, sz + 2.75)
    saver.send("PlayerMove", struct.pack("<fffff", *saved_eye, 45.0, -20.0))
    saver.pump(10, lambda n, p: (math.floor(saved_eye[0]) // 16, math.floor(saved_eye[2]) // 16) in saver.chunks)
    dirt = find_block(saver, saved_eye, DIRT_BLOCK, avoid=(math.floor(sx), math.floor(sz)))  # not under the spawn
    ok(dirt is not None, f"a dirt block near the saved eye ({dirt})")
    saver.send("InventoryAction", struct.pack("<IBBB", 1, 0, 4, 0))   # Select 4
    saver.send("BreakBlock", struct.pack("<iii", *dirt))
    msgs = saver.pump(5, lambda n, p: n == "InventoryContents" and any(i == DIRT_ITEM for i, c in inventory(p)[2]))
    got = [inventory(p) for n, p in msgs if n == "InventoryContents"]
    ok(got and any(i == DIRT_ITEM for i, c in got[-1][2]) and got[-1][1] == 4, "Saver broke dirt and holds it")
    saver.close()

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
    client, _, _, _ = login(25623)
    lx, lz = target[0] % 16, target[2] % 16
    chunk = (target[0] // 16, target[2] // 16)
    ok(chunk in client.chunks and block_at(client.chunks[chunk], lx, target[1], lz) == 0,
       "edit survived the restart")
    client.close()

    # 5. players: Saver comes back where it left, with its items; a new name starts fresh
    saver, eye, look, contents = login(25623, "Saver")
    ok(abs(eye[0] - saved_eye[0]) < 0.01 and abs(eye[2] - saved_eye[2]) < 0.01
       and eye[1] > saved_eye[1] - 0.01 and abs(look[0] - 45.0) < 0.01 and abs(look[1] + 20.0) < 0.01,
       f"player saved across restart: eye {tuple(round(v, 2) for v in eye)} (saved "
       f"{tuple(round(v, 2) for v in saved_eye)}, may be lifted), look {look}")
    ok(any(i == DIRT_ITEM for i, c in contents[2]) and contents[1] == 4 and contents[0] == 0,
       "player's inventory saved across restart (dirt, slot 4 selected)")
    saver.close()

    other, eye, look, contents = login(25623, "Other")
    ok(abs(eye[0] - sx) < 0.01 and abs(eye[2] - sz) < 0.01 and abs(eye[1] - sy) <= 1.01 and look == (0.0, 0.0),
       f"new name gets the world spawn ({tuple(round(v, 2) for v in eye)})")
    ok(contents[2] == STARTING_ITEMS and contents[1] == 0, "new name gets the starting items")
    other.close()
    ok(stop_server(server) == 0, "server stops cleanly again")

print("ALL PASSED")
