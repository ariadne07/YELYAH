"""
Generates test WAV files for the Wokwi simulated SD card.

    python tools/make_test_music.py

Creates sdcard/Music/<Artist>/<Album>/<NN Title>.wav next to
wokwi.toml. Each track is a short generated melody (16 kHz,
16-bit mono) so the whole card stays small.
"""

import array
import math
import os
import random
import wave

SAMPLE_RATE = 16000
TRACK_SECONDS = 20

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sdcard", "Music")

# artist -> (waveform, {album: [titles]})
LIBRARY = {
    "Signal Garden": ("sine", {
        "First Light": ["Sunrise", "Morning Walk", "Open Road"],
        "Night Shift": ["Neon Rain", "Last Train"],
    }),
    "The Square Waves": ("square", {
        "Bleeps": ["Arcade", "Pixel Dust", "Boss Fight"],
    }),
    "Low Tide": ("triangle", {
        "Undertow": ["Deep Blue", "Drift"],
    }),
}

# Major pentatonic scale, in semitones from the root.
SCALE = [0, 2, 4, 7, 9, 12, 14, 16]


def oscillator(shape, phase):
    if shape == "square":
        return 0.6 if math.sin(phase) >= 0 else -0.6
    if shape == "triangle":
        return 2 / math.pi * math.asin(math.sin(phase))
    return math.sin(phase)


def make_track(path, shape, seed):
    rng = random.Random(seed)

    root_hz = rng.choice([196.0, 220.0, 246.9, 261.6, 293.7])
    beat = rng.choice([0.25, 0.3, 0.4])

    samples = array.array("h")
    total = SAMPLE_RATE * TRACK_SECONDS
    phase = 0.0

    while len(samples) < total:
        semitones = rng.choice(SCALE)
        freq = root_hz * 2 ** (semitones / 12)
        length = int(SAMPLE_RATE * beat * rng.choice([1, 1, 2]))

        for i in range(min(length, total - len(samples))):
            # Short attack, exponential decay.
            t = i / SAMPLE_RATE
            envelope = min(1.0, t * 80) * math.exp(-t * 4)

            phase += 2 * math.pi * freq / SAMPLE_RATE
            samples.append(int(12000 * envelope * oscillator(shape, phase)))

    os.makedirs(os.path.dirname(path), exist_ok=True)

    with wave.open(path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(samples.tobytes())


def main():
    seed = 1

    for artist, (shape, albums) in LIBRARY.items():
        for album, titles in albums.items():
            for number, title in enumerate(titles, start=1):
                path = os.path.join(ROOT, artist, album, f"{number:02d} {title}.wav")
                make_track(path, shape, seed)
                seed += 1
                print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
