import os, wave, math, random, struct

os.makedirs("assets/audio", exist_ok=True)
SR = 44100

def write_wav(path, samples):
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = b"".join(
            struct.pack("<h", max(-32767, min(32767, int(s * 32767))))
            for s in samples
        )
        w.writeframes(frames)

# 1. briefing_static.wav — CRT/neural boot hiss + 60Hz industrial hum + glitch crackle
rng = random.Random(42)
s_static = []
for i in range(int(SR * 1.4)):
    t = i / SR
    env = min(1.0, t * 15.0) * math.exp(-t * 1.4)
    hum = 0.22 * math.sin(2 * math.pi * 60.0 * t) + 0.12 * math.sin(2 * math.pi * 180.0 * t)
    noise = (rng.random() * 2.0 - 1.0) * (0.45 if rng.random() < 0.08 else 0.14)
    s_static.append((hum + noise) * env * 0.75)
write_wav("assets/audio/briefing_static.wav", s_static)

# 2. briefing_pulse.wav — Deep sub-bass actuator thud + metallic cyber-ring
s_pulse = []
for i in range(int(SR * 1.2)):
    t = i / SR
    env = math.exp(-t * 3.8)
    freq = 95.0 * math.exp(-t * 6.0) + 38.0
    sub = math.sin(2 * math.pi * freq * t)
    ring = 0.25 * math.sin(2 * math.pi * 412.0 * t) * math.exp(-t * 9.0)
    s_pulse.append(math.tanh((sub * 1.6 + ring) * env) * 0.8)
write_wav("assets/audio/briefing_pulse.wav", s_pulse)

# 3. briefing_wind.wav — Kodiak freezing salt-fog gale (band-passed noise + low howl)
s_wind = []
lp1 = lp2 = 0.0
for i in range(int(SR * 2.2)):
    t = i / SR
    env = math.sin(math.pi * min(1.0, t / 2.2)) ** 0.7
    white = rng.random() * 2.0 - 1.0
    cutoff = 0.035 + 0.02 * math.sin(2 * math.pi * 0.6 * t)
    lp1 += cutoff * (white - lp1)
    lp2 += cutoff * (lp1 - lp2)
    howl = 0.08 * math.sin(2 * math.pi * (145.0 + 18.0 * math.sin(2 * math.pi * 0.45 * t)) * t)
    s_wind.append((lp2 * 4.5 + howl) * env * 0.7)
write_wav("assets/audio/briefing_wind.wav", s_wind)

# 4. briefing_lock.wav — Heavy double-bolt breech slam + target-lock chirp
s_lock = []
for i in range(int(SR * 0.9)):
    t = i / SR
    # Bolt 1 at t=0.0, Bolt 2 at t=0.14
    b1 = math.exp(-t * 28.0) * math.sin(2 * math.pi * 130.0 * t)
    t2 = max(0.0, t - 0.14)
    b2 = (1.0 if t >= 0.14 else 0.0) * math.exp(-t2 * 22.0) * (
        math.sin(2 * math.pi * 92.0 * t2) + 0.4 * (rng.random() * 2.0 - 1.0)
    )
    # High HUD lock tone at t=0.28
    t3 = max(0.0, t - 0.28)
    chirp = (1.0 if t >= 0.28 else 0.0) * math.exp(-t3 * 14.0) * 0.35 * math.sin(2 * math.pi * 1320.0 * t3)
    s_lock.append(math.tanh((b1 + b2 * 1.3 + chirp) * 1.4) * 0.8)
write_wav("assets/audio/briefing_lock.wav", s_lock)

print("Generated 4 briefing WAV files in assets/audio/")
