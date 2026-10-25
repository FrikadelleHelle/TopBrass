"""Write original, deterministic four-part evaluation MIDI (C4 = 60)."""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
PARTS = ('tenor-sax', 'trumpet', 'trombone', 'tuba')
RANGES = ((44, 88), (53, 84), (34, 65), (29, 62))
PPQ = 960


def variable(value):
    data = [value & 127]
    while value >> 7:
        value >>= 7
        data.insert(0, (value & 127) | 128)
    return bytes(data)


def track(events):
    data, previous = bytearray(), 0
    for tick, priority, message in sorted(events):
        data.extend(variable(tick - previous))
        data.extend(message)
        previous = tick
    data.extend(b'\x00\xff\x2f\x00')
    return b'MTrk' + struct.pack('>I', len(data)) + data


def write(name, bpm, parts, articulation):
    sustained = articulation == 'sustain'
    tempo = round(60000000 / bpm)
    tracks = [track([(0, 0, b'\xff\x51\x03' + tempo.to_bytes(3, 'big')),
                     (0, 1, b'\xff\x58\x04\x04\x02\x18\x08')])]
    for index, notes in enumerate(parts):
        title = PARTS[index].encode()
        events = [(0, -2, b'\xff\x03' + bytes([len(title)]) + title),
                  (0, -1, bytes([0xb0 + index, 11, 100]))]
        previous_end = -1
        for beat, pitch, duration, velocity in sorted(notes):
            assert RANGES[index][0] <= pitch <= RANGES[index][1]
            assert 0 < velocity <= 127 and duration > 0 and beat >= previous_end
            previous_end = beat + duration
            start, end = round(beat * PPQ), round(previous_end * PPQ)
            events.extend([(start, 1, bytes([0x90 + index, pitch, velocity])),
                           (end, 0, bytes([0x80 + index, pitch, 0]))])
            if sustained and duration >= 1.5:
                for fraction, expression in ((0, 88), (.35, 105), (.8, 94)):
                    events.append((round((beat + duration * fraction) * PPQ), -1,
                                   bytes([0xb0 + index, 11, expression])))
            elif sustained:
                events.append((start, -1, bytes([0xb0 + index, 11, 100])))
        tracks.append(track(events))
    destination = ROOT / 'scores' / (name + '.mid')
    destination.write_bytes(b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks), PPQ) + b''.join(tracks))
    return dict(file=destination.name, bpm=bpm, articulation=articulation,
                sha256=hashlib.sha256(destination.read_bytes()).hexdigest(), parts=parts)


funk = [[] for _ in PARTS]
voicings = [(60, 64, 53, 38, 45), (62, 67, 56, 34, 41),
            (62, 64, 58, 36, 43), (61, 64, 55, 33, 40)]
hits = [[(1.5, .25, 108), (2.75, .25, 99), (4, .3, 114)],
        [(1, .3, 110), (2.5, .25, 97), (3.75, .25, 111)],
        [(1.75, .25, 105), (3, .3, 112), (4.5, .2, 95)],
        [(1, .35, 113), (2.75, .25, 101), (4, .3, 116)]]
for bar, (sax, trumpet, bone, root, fifth) in enumerate(voicings):
    for hit, (beat, duration, velocity) in enumerate(hits[bar]):
        for part, pitch, reduction in ((0, sax, 8), (1, trumpet, 0), (2, bone, 12)):
            if part != 2 or hit in (0, 2):
                funk[part].append((bar * 4 + beat - 1, pitch, duration, velocity - reduction))
    for beat, pitch, duration, velocity in ((1, root, .65, 88), (2.5, fifth, .35, 77),
                                           (3, root, .5, 85), (4.25, root + 12, .25, 75)):
        funk[3].append((bar * 4 + beat - 1, pitch, duration, velocity))

soul_bars = [
    [[(1.5,64,2,74)], [(2,67,.75,78),(3,69,.4,72),(3.5,71,.4,76),(4,74,.65,81)], [(1,59,2.5,64)], [(1,36,3,68)]],
    [[(1,60,2,76)], [(1,72,1.5,82),(3,71,.8,74),(4,76,.65,79)], [(1,55,2.5,66)], [(1,33,3,70)]],
    [[(1.5,65,.75,79),(2.5,69,.4,82),(3,67,.65,76),(4,64,.6,70)], [], [(1,60,2.5,65)], [(1,38,3,71)]],
    [[(1,59,2.5,71)], [(1,74,.75,79),(2,76,.75,83),(3,74,.5,75),(3.75,71,.75,69)], [(1,53,2.5,64)], [(1,31,2.75,69)]]]
soul = [[] for _ in PARTS]
for bar, parts in enumerate(soul_bars):
    for part, notes in enumerate(parts):
        soul[part].extend((bar * 4 + beat - 1, pitch, duration, velocity) for beat, pitch, duration, velocity in notes)

if __name__ == '__main__':
    falls = [[] for _ in PARTS]
    for part, root in ((1,60),(2,48)):
        for index, (interval,velocity) in enumerate(zip((0,0,0,0,2,4,7,0),(50,50,50,50,78,98,115,100))):
            falls[part].append((index*1.5,root+interval,.15,velocity))
    manifest = dict(description='Original programmed evaluation performances; no claim of sound acceptance.',
                    channels=dict(zip(PARTS, range(1, 5))), ppq=PPQ,
                    scores=[write('funk-four-parts',104,funk,'short'), write('soul-four-parts',76,soul,'sustain'),
                            write('fall-audition',80,falls,'fall')])
    (ROOT / 'scores' / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
