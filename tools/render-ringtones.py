"""Renders every built-in ringtone to audio. The note list comes from the
firmware's own parser, compiled for the desktop and dumped here, so what is
auditioned is what the chip will play rather than a second transcription."""
import subprocess, math, wave, struct, os, re

os.chdir('rt')
open('dump.cpp','w').write('''
#include <cstdio>
#include "Arduino.h"
#include "../../../../../home/claude/work/src/tunes.h"
#include "rtttl_impl.h"
int main(){ static Note n[MAX_TUNE_NOTES];
  for(int i=0;i<RINGTONE_COUNT;i++){
    uint16_t c=rtttlParse(RINGTONES[i].rtttl,n,MAX_TUNE_NOTES,nullptr);
    printf("#%s\\n", RINGTONES[i].label);
    for(int k=0;k<c;k++) printf("%u %u\\n", n[k].hz, n[k].ms);
  } return 0; }
''')
subprocess.run(['g++','-std=c++17','-O1','-I.','-o','dump','dump.cpp'],check=True)
out = subprocess.run(['./dump'],capture_output=True,text=True).stdout
os.chdir('..')

tunes, cur = [], None
for line in out.splitlines():
    if line.startswith('#'): cur=(line[1:],[]); tunes.append(cur)
    elif line.strip(): hz,ms=line.split(); cur[1].append((int(hz),int(ms)))

SR=44100; BITS=10; ENVF=1000; RAMP=10
def duty(v,e):
    if v==0 or e==0: return 0
    w=min((v/100.0)**2*(e/ENVF),1.0)
    return max(1,round(math.asin(w)/math.pi*(1<<BITS)))
def env(i,l,r):
    if i>=l: return 0
    if r*2>l: r=l//2
    if r==0: return ENVF
    if i<r: return ENVF*i//r
    if i>l-r: return ENVF*(l-i)//r
    return ENVF
def render(notes,passes):
    s=[]
    for _ in range(passes):
        for hz,ms in notes:
            n=int(SR*ms/1000); ph=0.0
            for i in range(n):
                if hz==0: s.append(0.0); continue
                d=duty(100,env(int(i*1000/SR),ms,RAMP))/(1<<BITS)
                ph=(ph+hz/SR)%1.0
                s.append(1.0 if ph<d else (-d/(1-d) if d<1 else 1.0))
    return s
def save(p,s,g=0.6):
    w=wave.open(p,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(b''.join(struct.pack('<h',int(max(-1,min(1,x))*32767*g)) for x in s)); w.close()

OUT='/home/claude/out/sounds'; os.makedirs(OUT,exist_ok=True)
for f in os.listdir(OUT): os.remove(os.path.join(OUT,f))
for i,(label,notes) in enumerate(tunes):
    one=sum(m for _,m in notes)/1000.0
    passes = max(1, min(4, int(8/one)+1)) if one < 8 else 1
    slug=re.sub(r'[^a-z0-9]+','-',label.lower()).strip('-')
    s=render(notes,passes)
    save(os.path.join(OUT,'%d-%s.wav'%(i+1,slug)),s)
    print("  %-30s loop %5.2f s  file %5.2f s" % (label, one, len(s)/SR))
