import os, zlib, struct, math, random

os.makedirs("assets/briefings", exist_ok=True)
W, H = 640, 360

def save_png(path, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))

rng = random.Random(1337)
for idx, name in enumerate(["boot_diagnostics.png", "carnage_hud.png", "kodiak_basin.png", "kodiak_targets.png"]):
    rows = []
    for y in range(H):
        row = bytearray(W * 3)
        scan = 0.72 if (y % 4 == 0) else 1.0
        for x in range(W):
            grid = 18 if (x % 40 == 0 or y % 40 == 0) else 0
            vign = max(0.25, 1.0 - (((x - W / 2) / (W * 0.6)) ** 2 + ((y - H / 2) / (H * 0.6)) ** 2))
            noise = rng.randint(0, 8)
            r = int(min(255, ((28 + idx * 14 + grid + noise) * scan * vign)))
            g = int(min(255, ((8 + (14 if idx == 2 else 4) + grid // 2) * scan * vign)))
            b = int(min(255, ((10 + (18 if idx == 2 else 4) + grid // 2) * scan * vign)))
            row[x*3:x*3+3] = bytes((r, g, b))
        rows.append(row)
    save_png(f"assets/briefings/{name}", rows)
print("Generated 4 briefing backdrop PNGs in assets/briefings/")
