"""
Generates test MP3 files for the Wokwi simulated SD card.

    pip install lameenc
    python tools/make_test_music.py

Creates sdcard/Music/<Artist>/<Album>/<NN Title>.mp3 next to
wokwi.toml. Each track is a short generated melody with an
ID3v2 tag, like a real download. Most albums are 44.1 kHz
stereo at 128 kbps; one is 22.05 kHz mono at 64 kbps to
exercise the other MP3 frame layout.
"""

import array
import math
import os
import random

import lameenc

TRACK_SECONDS = 20

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sdcard", "Music")

# artist -> (waveform, (sample rate, channels, kbps), {album: [titles]})
LIBRARY = {
    "Signal Garden": ("sine", (44100, 2, 128), {
        "First Light": ["Sunrise", "Morning Walk", "Open Road"],
        "Night Shift": ["Neon Rain", "Last Train"],
    }),
    "The Square Waves": ("square", (44100, 2, 128), {
        "Bleeps": ["Arcade", "Pixel Dust", "Boss Fight"],
    }),
    "Low Tide": ("triangle", (22050, 1, 64), {
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


def make_melody(shape, seed, sample_rate, channels):
    """Returns interleaved 16-bit PCM bytes."""
    rng = random.Random(seed)

    root_hz = rng.choice([196.0, 220.0, 246.9, 261.6, 293.7])
    beat = rng.choice([0.25, 0.3, 0.4])

    samples = array.array("h")
    total = sample_rate * TRACK_SECONDS
    written = 0
    phase = 0.0

    while written < total:
        semitones = rng.choice(SCALE)
        freq = root_hz * 2 ** (semitones / 12)
        length = int(sample_rate * beat * rng.choice([1, 1, 2]))

        # Pan each note a little left or right in stereo.
        pan = rng.uniform(0.3, 0.7)

        for i in range(min(length, total - written)):
            # Short attack, exponential decay.
            t = i / sample_rate
            envelope = min(1.0, t * 80) * math.exp(-t * 4)

            phase += 2 * math.pi * freq / sample_rate
            value = 12000 * envelope * oscillator(shape, phase)

            if channels == 2:
                samples.append(int(value * (1 - pan) * 2))
                samples.append(int(value * pan * 2))
            else:
                samples.append(int(value))

            written += 1

    return samples.tobytes()


def id3v2_tag(title, artist, album, track):
    """Minimal ID3v2.3 tag with title/artist/album/track frames."""

    def text_frame(frame_id, text):
        data = b"\x00" + text.encode("latin-1")  # encoding byte: ISO-8859-1
        return frame_id.encode() + len(data).to_bytes(4, "big") + b"\x00\x00" + data

    frames = (
        text_frame("TIT2", title)
        + text_frame("TPE1", artist)
        + text_frame("TALB", album)
        + text_frame("TRCK", str(track))
    )

    # Tag size is "syncsafe": 7 bits per byte.
    size = len(frames)
    syncsafe = bytes([(size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F])

    return b"ID3\x03\x00\x00" + syncsafe + frames


def make_track(path, shape, seed, fmt, title, artist, album, number):
    sample_rate, channels, kbps = fmt

    encoder = lameenc.Encoder()
    encoder.set_in_sample_rate(sample_rate)
    encoder.set_channels(channels)
    encoder.set_bit_rate(kbps)
    encoder.set_quality(5)

    pcm = make_melody(shape, seed, sample_rate, channels)
    audio = encoder.encode(pcm) + encoder.flush()

    os.makedirs(os.path.dirname(path), exist_ok=True)

    with open(path, "wb") as f:
        f.write(id3v2_tag(title, artist, album, number))
        f.write(audio)


def main():
    seed = 1

    for artist, (shape, fmt, albums) in LIBRARY.items():
        for album, titles in albums.items():
            for number, title in enumerate(titles, start=1):
                base = os.path.join(ROOT, artist, album, f"{number:02d} {title}")

                # Remove the WAV this script used to generate.
                if os.path.exists(base + ".wav"):
                    os.remove(base + ".wav")

                make_track(base + ".mp3", shape, seed, fmt, title, artist, album, number)
                seed += 1
                print("wrote", os.path.relpath(base + ".mp3"))


if __name__ == "__main__":
    main()
