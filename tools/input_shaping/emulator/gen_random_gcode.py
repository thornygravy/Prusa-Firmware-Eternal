#!/usr/bin/env python3
"""Random G-code stress script for fwrun (OKSYNC): mixed XY moves, extrusion, tiny segments, M204 / M900 / M205 J changes.
usage: gen_random_gcode.py <ELF with check_endstops symbol> <boot_clicks.txt> <seed> > script.txt"""
import random, subprocess, sys
elf, clicks, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
random.seed(seed)
nm = subprocess.run(['avr-nm', '-C', elf], capture_output=True, text=True).stdout
ce = [int(l.split()[0], 16) & 0xFFFF for l in nm.splitlines() if l.endswith(' d check_endstops')][0]
L = [open(clicks).read().rstrip('\n'), f"11.5 POKE {ce:#06x} 0", "12.0 M302 S0", "12.1 M83", "12.2 G91"]
for _ in range(230):
    r = random.random(); F = random.choice([6000, 9000, 12000, 15000])
    if r < 0.50:   L.append(f"12.4 G1 X{random.uniform(-12,12):.2f} Y{random.uniform(-12,12):.2f} F{F}")
    elif r < 0.72: L.append(f"12.4 G1 X{random.uniform(-4,4):.3f} Y{random.uniform(-4,4):.3f} E{random.uniform(0.01,0.3):.3f} F{random.choice([3000,6000,9000])}")
    elif r < 0.84: L.append(f"12.4 G1 X{random.uniform(-0.8,0.8):.3f} Y{random.uniform(-0.8,0.8):.3f} F{F}")
    elif r < 0.89: L.append(f"12.4 M204 P{random.choice([500,800,1250,2000,2500])} T{random.choice([800,1250,2500])}")
    elif r < 0.93: L.append(f"12.4 M900 K{random.choice([0,0.02,0.05,0.1])}")
    elif r < 0.96: L.append(f"12.4 M205 J{random.choice([0,0.008,0.013,0.025])}")
    elif r < 0.98: L.append(f"12.4 G1 E{random.choice([-0.8,0.8])} F2100")
    else:          L.append("12.4 M400")
L += ["12.4 M400", "12.4 M114"]
print("\n".join(L))
