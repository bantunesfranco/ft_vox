import socket, struct, sys, time

PORT = int(sys.argv[1])
IDS = dict(Hello=0x0001, Login=0x0002, PlayerMove=0x0010, BreakBlock=0x0020, PlaceBlock=0x0021,
           LoginOk=0x0102, EntityMove=0x0110, PlayerJoin=0x0111, PlayerLeave=0x0112,
           BlockUpdate=0x0120, ChunkData=0x0130, UnloadChunk=0x0131, KeepAlive=0x0F00, Disconnect=0x0FFF)
NAMES = {v: k for k, v in IDS.items()}
EYE = 1.62

def ok(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        sys.exit(1)

class Client:
    def __init__(self, name):
        self.s = socket.create_connection(("127.0.0.1", PORT))
        self.s.settimeout(0.2)
        self.buf = b""
        self.chunks = {}
        self.send("Hello", struct.pack("<I", 1))
        nb = name.encode()
        self.send("Login", struct.pack("<H", len(nb)) + nb)

    def send(self, name, payload=b""):
        self.s.sendall(struct.pack("<IH", len(payload) + 2, IDS[name]) + payload)

    def pump(self, seconds, until=None):
        out = []
        end = time.time() + seconds
        while time.time() < end:
            try:
                data = self.s.recv(1 << 20)
                if not data:
                    out.append(("CLOSED", b""))
                    return out
                self.buf += data
            except socket.timeout:
                pass
            while len(self.buf) >= 6:
                ln, pid = struct.unpack_from("<IH", self.buf)
                if len(self.buf) < 4 + ln:
                    break
                payload = self.buf[6:4 + ln]
                self.buf = self.buf[4 + ln:]
                name = NAMES.get(pid, hex(pid))
                if name == "ChunkData":
                    cx, cz = struct.unpack_from("<ii", payload)
                    self.chunks[(cx, cz)] = decode_column(payload[8:])
                out.append((name, payload))
                if until and until(name, payload):
                    return out
        return out

def decode_column(p):
    mask, = struct.unpack_from("<I", p); off = 4
    sections = {}
    for s in range(20):
        if not mask & (1 << s):
            continue
        n, = struct.unpack_from("<H", p, off); off += 2
        blocks = []
        for _ in range(n):
            ln, b = struct.unpack_from("<HH", p, off); off += 4
            blocks += [b] * ln
        assert len(blocks) == 4096, len(blocks)
        sections[s] = blocks
    assert off == len(p), (off, len(p))
    return sections

def block_at(col, lx, wy, lz):
    sy = wy + 64
    sec = col.get(sy // 16)
    return 0 if sec is None else sec[lx + (lz << 4) + ((sy % 16) << 8)]

# ---- 1. login + spawn -------------------------------------------------------
a = Client("alice")
msgs = a.pump(15, lambda n, p: n == "LoginOk")
login = [p for n, p in msgs if n == "LoginOk"]
ok(len(login) == 1, "alice receives LoginOk")
aid, sx, sy, sz = struct.unpack("<Ifff", login[0])
print(f"     alice id={aid} spawn=({sx:.2f}, {sy:.2f}, {sz:.2f})")

# ---- 2. chunk streaming + codec ---------------------------------------------
a.pump(6)
import math
spawn_chunk = (math.floor(sx / 16), math.floor(sz / 16))
ok(spawn_chunk in a.chunks, f"spawn column streamed and decoded ({len(a.chunks)} columns so far)")
ok(len(a.chunks) > 50, "many columns streamed nearest-first")
far = max(max(abs(x - spawn_chunk[0]), abs(z - spawn_chunk[1])) for x, z in a.chunks)
ok(far <= 17, f"nothing beyond view radius (max ring {far})")
feet_y = round(sy - EYE)
lx, lz = math.floor(sx) % 16, math.floor(sz) % 16
below = block_at(a.chunks[spawn_chunk], lx, feet_y - 1, lz)
at_feet = block_at(a.chunks[spawn_chunk], lx, feet_y, lz)
ok(below != 0 and at_feet == 0, f"spawn stands on a solid block (below={below}, feet={at_feet})")

# column_matches_singleplayer: with `<exe> <seed>` (the seed the server was
# started with), the server's spawn column must hash like the game's own
# generation (`ft_vox --selftest-hash`). Before any edit, and from a fresh world.
if len(sys.argv) >= 4:
    import subprocess
    exe, seed = sys.argv[2], sys.argv[3]
    def column_hash(col):
        h = 14695981039346656037
        for s in range(20):
            for b in col.get(s, [0] * 4096):
                for byte in (b & 0xFF, b >> 8):
                    h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return f"{h:016x}"
    local = subprocess.run([exe, "--selftest-hash", seed, str(spawn_chunk[0]), str(spawn_chunk[1])],
                           capture_output=True, text=True, check=True).stdout.strip()
    remote = column_hash(a.chunks[spawn_chunk])
    ok(local == remote, f"column {spawn_chunk} matches singleplayer generation ({remote} vs {local})")

# ---- 3. second player: join + movement relay ---------------------------------
b = Client("bob")
msgs = b.pump(5, lambda n, p: n == "LoginOk")
bid = struct.unpack("<I", [p for n, p in msgs if n == "LoginOk"][0][:4])[0]
msgs = b.pump(1, lambda n, p: n == "PlayerJoin")
ok(any(n == "PlayerJoin" and struct.unpack_from("<I", p)[0] == aid for n, p in msgs), "bob is told about alice")
msgs = a.pump(2, lambda n, p: n == "PlayerJoin")
ok(any(n == "PlayerJoin" and p[6:] == b"bob" for n, p in msgs), "alice is told bob joined")

b.send("PlayerMove", struct.pack("<fffff", sx + 3, sy, sz + 1, 90.0, -10.0))
def bob_moved(n, p):
    if n != "EntityMove":
        return False
    cnt, = struct.unpack_from("<H", p)
    for i in range(cnt):
        pid, x, y, z, yaw, pitch = struct.unpack_from("<Ifffff", p, 2 + i * 24)
        if pid == bid and abs(x - (sx + 3)) < 1e-4:
            return True
    return False
msgs = a.pump(2, bob_moved)
ok(any(bob_moved(n, p) for n, p in msgs), "alice sees bob's new position in EntityMove")

# ---- 4. edits ------------------------------------------------------------------
target = (math.floor(sx), feet_y - 1, math.floor(sz))
a.send("BreakBlock", struct.pack("<iii", *target))
upd = lambda n, p: n == "BlockUpdate" and struct.unpack_from("<iii", p) == target
ra = a.pump(2, upd); rb = b.pump(2, upd)
pa = [p for n, p in ra if upd(n, p)]
ok(pa and struct.unpack_from("<H", pa[0], 12)[0] == 0, "break accepted: alice gets BlockUpdate(air)")
ok(any(upd(n, p) for n, p in rb), "break broadcast to bob")

a.send("PlaceBlock", struct.pack("<iiiH", target[0], target[1], target[2], 3))  # stone back
pa = [p for n, p in a.pump(2, upd) if upd(n, p)]
ok(pa and struct.unpack_from("<H", pa[0], 12)[0] == 3, "place accepted: BlockUpdate(stone)")

far_target = (target[0] + 40, target[1], target[2])
a.send("BreakBlock", struct.pack("<iii", *far_target))
upd_far = lambda n, p: n == "BlockUpdate" and struct.unpack_from("<iii", p) == far_target
pa = [p for n, p in a.pump(2, upd_far) if upd_far(n, p)]
far_chunk = (far_target[0] // 16, far_target[2] // 16)
real = block_at(a.chunks[far_chunk], far_target[0] % 16, far_target[1], far_target[2] % 16) if far_chunk in a.chunks else None
ok(pa and real is not None and struct.unpack_from("<H", pa[0], 12)[0] == real,
   "out-of-reach break rejected (answered with the real block)")

bedrock = (target[0], -64, target[2])
a.send("PlaceBlock", struct.pack("<iiiH", target[0], feet_y, target[2], 3))  # inside alice herself
inside = (target[0], feet_y, target[2])
upd_in = lambda n, p: n == "BlockUpdate" and struct.unpack_from("<iii", p) == inside
pa = [p for n, p in a.pump(2, upd_in) if upd_in(n, p)]
ok(pa and struct.unpack_from("<H", pa[0], 12)[0] == 0, "place inside a player rejected")

# ---- 5. leave ------------------------------------------------------------------
b.send("Disconnect", b"\x00")
b.s.close()
msgs = a.pump(2, lambda n, p: n == "PlayerLeave")
ok(any(n == "PlayerLeave" and struct.unpack("<I", p)[0] == bid for n, p in msgs), "alice is told bob left")

# ---- 6. malformed packet -> kicked --------------------------------------------
a.send("BreakBlock", struct.pack("<ii", 1, 2))          # truncated
msgs = a.pump(2, lambda n, p: n in ("Disconnect", "CLOSED"))
disc = [p for n, p in msgs if n == "Disconnect"]
ok(disc and disc[0][0] == 2, "truncated packet -> Disconnect(ProtocolViolation)")
print("ALL PASSED")
