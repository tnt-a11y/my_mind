"""Synthesize a seamless stereo outdoor rain bed with individually scattered impacts."""

from pathlib import Path
import sys
import wave

import numpy as np


def create_rain(path):
    rng = np.random.default_rng(66981)
    rate, seconds = 44100, 28
    count = rate * seconds
    frequency = np.fft.rfftfreq(count, 1.0 / rate)
    time = np.arange(count) / rate

    def bed():
        spectrum = np.fft.rfft(rng.standard_normal(count))
        rumble_cut = 1 / np.sqrt(1 + (155 / np.maximum(frequency, 1))**4)
        falloff = (1 + (frequency / 650)**1.12)**-.60 / np.sqrt(1 + (frequency / 8400)**5)
        return np.fft.irfft(spectrum * rumble_cut * falloff, n=count)

    shared = bed()
    density = 1.0 + .11*np.sin(2*np.pi*2*time/seconds) + .065*np.sin(2*np.pi*5*time/seconds+.7)
    audio = np.stack([(.63*shared + .37*bed())*.36*density for _ in range(2)], axis=1)
    for _ in range(2700):
        length = int(rng.uniform(.004, .030) * rate)
        position = (rng.integers(count) + np.arange(length)) % count
        phase = np.arange(length) / rate
        attack = np.minimum(phase*1800, 1)
        tail = np.exp(-phase / rng.uniform(.0018, .0075))
        grains = rng.standard_normal(length)*.32 + np.sin(2*np.pi*rng.uniform(570, 4500)*phase)*.58
        gain = rng.uniform(.019, .13) * attack * tail * grains
        pan = rng.uniform(.12, .88)
        audio[position, 0] += gain * (1-pan)**.5
        audio[position, 1] += gain * pan**.5
    pcm = np.asarray(np.clip(audio, -1, 1)*32767, dtype='<i2')
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as output:
        output.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        output.writeframes(pcm.tobytes())


if __name__ == "__main__":
    create_rain(Path(sys.argv[1]))
