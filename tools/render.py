"""Renders the alarm sounds to WAV by reading the firmware's own tune tables,
reproducing exactly what the chip puts on the pin: a square wave whose pulse
width carries the volume, with the same fade at each end of every note."""
import re, math, wave, struct, os, sys

SRC = open('/home/claude/work/src/tunes.h').read()
SR  = 44100
TONE_BITS = 10
ENV_FULL  = 1000

# note name -> hz, straight out of the header
NOTE = {m[0]: int(m[1]) for m in re.findall(r'#define\s+(N_\w+)\s+(\d+)', SRC)}
NOTE['REST'] = 0

def table(name):
    body = SRC[SRC.index('TUNE_%s[] PROGMEM = {' % name):]
    body = body[body.index('{')+1 : body.index('\n};')]
    out = []
    for hz, ms in re.findall(r'\{\s*(\w+)\s*,\s*(\d+)\s*\}', body):
        out.append((NOTE[hz], int(ms)))
    return out

# the TUNES[] array: tail and ramp per tune
meta = {}
arr = SRC[SRC.index('static const Tune TUNES[]'):]
arr = arr[arr.index('{')+1 : arr.index('\n};')]
for nm, _c, tail, ramp in re.findall(r'\{\s*TUNE_(\w+),[^,]+,\s*(\d+)?\s*(\d+),\s*(\d+)\s*\}', arr):
    pass
for line in arr.split('\n'):
    m = re.search(r'TUNE_(\w+)\s*,.*?,\s*(\d+)\s*,\s*(\d+)\s*\}', line)
    if m: meta[m.group(1)] = (int(m.group(2)), int(m.group(3)))

def duty(volume, env):
    if volume == 0 or env == 0: return 0
    want = (volume/100.0)**2 * (env/ENV_FULL)
    want = min(want, 1.0)
    return max(1, round(math.asin(want)/math.pi * (1 << TONE_BITS)))

def env_at(into, length, ramp):
    if into >= length: return 0
    if ramp*2 > length: ramp = length//2
    if ramp == 0: return ENV_FULL
    if into < ramp: return ENV_FULL*into//ramp
    if into > length-ramp: return ENV_FULL*(length-into)//ramp
    return ENV_FULL

def render(notes, tail, ramp, volume=100, passes=1):
    samples = []
    phase = 0.0
    for _ in range(passes):
        for hz, ms in notes:
            n = int(SR*ms/1000)
            for i in range(n):
                if hz == 0:
                    samples.append(0.0); continue
                into = int(i*1000/SR)
                d = duty(volume, env_at(into, ms, ramp)) / (1 << TONE_BITS)
                phase += hz/SR
                phase %= 1.0
                # the pin: high for `d` of each cycle, low for the rest,
                # centred so silence sits at zero rather than at one rail
                samples.append(1.0 if phase < d else -d/(1-d) if d < 1 else 1.0)
            phase = 0.0
        samples += [0.0]*int(SR*tail/1000)
    return samples

def save(path, samples, gain=0.6):
    w = wave.open(path,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(b''.join(struct.pack('<h', int(max(-1,min(1,s))*32767*gain)) for s in samples))
    w.close()

OUT='/home/claude/out/sounds'
os.makedirs(OUT, exist_ok=True)
NAMES=[('BEEP','1-beep'),('TRILL','2-trill'),('CHIME','3-chime'),
       ('ELISE','4-fur-elise'),('TURKISH','5-turkish-march'),('MINUET','6-minuet-in-g')]
for key, fname in NAMES:
    notes = table(key); tail, ramp = meta[key]
    passes = 2 if key in ('BEEP','TRILL','CHIME') else 1
    s = render(notes, tail, ramp, 100, passes)
    save(os.path.join(OUT, fname+'.wav'), s)
    dur = len(s)/SR
    print("  %-18s %2d notes  one pass %5.2f s  file %5.2f s" %
          (fname, len(notes), (sum(m for _,m in notes)+tail)/1000.0, dur))
