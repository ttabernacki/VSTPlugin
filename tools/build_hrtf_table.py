#!/usr/bin/env python3
"""Builds the embedded HRTF table from a SADIE II SOFA file.

Usage: build_hrtf_table.py D1_48K_24bit_256tap_FIR_SOFA.sofa Resources/hrtf_d1_5deg.bin

Each measured HRIR is split into
  * a minimum-phase filter (spectral/pinna/head-shadow detail, no delay), and
  * an interaural time difference (ITD), estimated from low-passed cross-correlation.
Both are then resampled onto a regular 5-degree azimuth/elevation grid using
inverse-distance weighting of the nearest measured directions, so the plugin can
interpolate cheaply and smoothly at runtime.

Plugin coordinates: azimuth 0 = front, +90 = right, +-180 = back;
elevation +90 = straight up. (SOFA azimuth is counter-clockwise, so it is negated.)

Two filter sets are stored: [0] the measured pinna detail as-is, [1] the same with
the direction-dependent spectrum exaggerated relative to its cone-of-confusion mean
(scaled by ENHANCE, ~1.5-16 kHz; ITD/ILD untouched) to make elevation and front/back cues more pronounced. The
plugin's "Focus" control crossfades between them.

Binary layout (little endian):
  char[4]  magic "HRT2"
  uint32   sampleRate, taps, azSteps, elSteps
  float32  azStepDeg, elStepDeg, elMinDeg, int16Scale
  int16    ir[2 sets][elSteps][azSteps][2 ears (L,R)][taps]   (value / int16Scale = gain)
  float32  itd[elSteps][azSteps]                      (samples at sampleRate; left - right
                                                       arrival time, >0 means source on the right)
"""
import struct
import sys

import h5py
import numpy as np

TAPS = 128
FFT = 1024
AZ_STEP = 5.0
EL_STEP = 5.0
EL_MIN = -90.0
INT16_SCALE = 4096.0
ENHANCE = 2.0  # cue multiplier for the enhanced set
MAX_BOOST_DB = 12.0
MAX_CUT_DB = 24.0


def min_phase(h):
    """Minimum-phase version of h via the real cepstrum."""
    spec = np.fft.rfft(h, FFT)
    mag = np.maximum(np.abs(spec), 1e-6)
    cep = np.fft.irfft(np.log(mag), FFT)
    fold = np.zeros(FFT)
    fold[0] = cep[0]
    fold[1:FFT // 2] = 2.0 * cep[1:FFT // 2]
    fold[FFT // 2] = cep[FFT // 2]
    mp = np.fft.irfft(np.exp(np.fft.rfft(fold)), FFT)[:TAPS]
    # fade the tail so truncation does not click
    fade = 32
    mp[-fade:] *= 0.5 * (1.0 + np.cos(np.pi * np.arange(fade) / fade))
    return mp


def min_phase_from_logmag(logmag):
    cep = np.fft.irfft(logmag, FFT)
    fold = np.zeros(FFT)
    fold[0] = cep[0]
    fold[1:FFT // 2] = 2.0 * cep[1:FFT // 2]
    fold[FFT // 2] = cep[FFT // 2]
    mp = np.fft.irfft(np.exp(np.fft.rfft(fold)), FFT)[:TAPS]
    fade = 32
    mp[-fade:] *= 0.5 * (1.0 + np.cos(np.pi * np.arange(fade) / fade))
    return mp


def enhance_all(mins, vm, fs):
    """Exaggerate what distinguishes directions on the same cone of confusion.

    Directions with the same lateral angle share (roughly) the same ITD/ILD and differ
    only in pinna/torso filtering - exactly the front/back and up/down cues. For every
    filter, the deviation of its log-magnitude from the mean over its cone (per ear) is
    multiplied by ENHANCE between ~1.5 and ~16 kHz. ITD/ILD are left untouched.
    """
    m = mins.shape[0]
    freqs = np.fft.rfftfreq(FFT, 1.0 / fs)
    logm = np.log(np.maximum(np.abs(np.fft.rfft(mins, FFT, axis=-1)), 1e-6))  # (m, 2, bins)
    lat = np.degrees(np.arcsin(np.clip(vm[:, 0], -1, 1)))
    cone = np.round(lat / 5.0).astype(int)
    w = np.clip((freqs - 1500.0) / 1500.0, 0, 1) * np.clip((18000.0 - freqs) / 4000.0, 0, 1)
    out = np.zeros_like(mins)
    for c in np.unique(cone):
        members = np.where(cone == c)[0]
        mean = logm[members].mean(axis=0)  # (2, bins)
        for i in members:
            db = (logm[i] - mean) * 20.0 / np.log(10.0)
            db_new = np.where(db > 0, np.clip(db * ENHANCE, 0, MAX_BOOST_DB), np.clip(db * ENHANCE, -MAX_CUT_DB, 0))
            db_new = (1 - w) * db + w * db_new
            new = mean + db_new * np.log(10.0) / 20.0
            out[i, 0] = min_phase_from_logmag(new[0])
            out[i, 1] = min_phase_from_logmag(new[1])
    return out


def estimate_itd(hl, hr, fs):
    """ITD in samples (left arrival - right arrival) from 1.6 kHz low-passed xcorr."""
    def lp(x):
        spec = np.fft.rfft(x, 1024)
        freqs = np.fft.rfftfreq(1024, 1.0 / fs)
        spec[freqs > 1600.0] = 0.0
        return np.fft.irfft(spec, 1024)

    l, r = lp(hl), lp(hr)
    xc = np.fft.irfft(np.fft.rfft(l) * np.conj(np.fft.rfft(r)), 1024)
    xc = np.concatenate([xc[-64:], xc[:65]])  # lags -64..64
    k = int(np.argmax(xc))
    lag = k - 64
    if 0 < k < len(xc) - 1:  # parabolic refinement
        a, b, c = xc[k - 1], xc[k], xc[k + 1]
        den = a - 2 * b + c
        if den != 0:
            lag += 0.5 * (a - c) / den
    # xc peak at +lag means l is a delayed copy of r: left arrives later.
    return lag


def main(sofa_path, out_path):
    f = h5py.File(sofa_path, "r")
    ir = f["Data.IR"][:]  # (M, 2, N)
    pos = f["SourcePosition"][:]
    fs = int(f["Data.SamplingRate"][0])

    m = ir.shape[0]
    mins = np.zeros((m, 2, TAPS))
    itds = np.zeros(m)
    for i in range(m):
        mins[i, 0] = min_phase(ir[i, 0])
        mins[i, 1] = min_phase(ir[i, 1])
        # the min-phase pair carries a little interaural delay of its own at low
        # frequencies; subtract it so that (min-phase + ITD) reproduces the measurement
        itds[i] = estimate_itd(ir[i, 0], ir[i, 1], fs) - estimate_itd(mins[i, 0], mins[i, 1], fs)

    # measured directions as unit vectors in plugin coordinates
    az = np.radians(-pos[:, 0])
    el = np.radians(pos[:, 1])
    vm = np.stack([np.cos(el) * np.sin(az), np.cos(el) * np.cos(az), np.sin(el)], 1)  # x right, y front, z up

    mins_enh = enhance_all(mins, vm, fs)

    az_steps = int(round(360.0 / AZ_STEP))
    el_steps = int(round((90.0 - EL_MIN) / EL_STEP)) + 1
    mirror = (-np.arange(az_steps)) % az_steps

    # neighbour weights per grid direction (shared by both filter sets)
    nbr = {}
    for ei in range(el_steps):
        e = np.radians(EL_MIN + ei * EL_STEP)
        for ai in range(az_steps):
            a = np.radians(ai * AZ_STEP)
            t = np.array([np.cos(e) * np.sin(a), np.cos(e) * np.cos(a), np.sin(e)])
            ang = np.arccos(np.clip(vm @ t, -1, 1))
            idx = np.argsort(ang)[:4]
            w = 1.0 / (ang[idx] + np.radians(0.5)) ** 2
            nbr[(ei, ai)] = (idx, w / w.sum())

    def build(mins_set):
        grid = np.zeros((el_steps, az_steps, 2, TAPS), dtype=np.float64)
        for (ei, ai), (idx, w) in nbr.items():
            grid[ei, ai] = np.tensordot(w, mins_set[idx], axes=(0, 0))
        # Symmetrise: a generic head should be left/right mirror-symmetric so that centre
        # images stay centred. L(az) = mean(L(az), R(-az)); R(az) = L(-az).
        left = 0.5 * (grid[:, :, 0] + grid[:, mirror, 1])
        grid[:, :, 0] = left
        grid[:, :, 1] = left[:, mirror]
        # normalise so the front-centre (both ears) has unit RMS gain
        front = grid[int(round((0 - EL_MIN) / EL_STEP)), 0]
        grid /= np.sqrt(np.sum(front ** 2) / 2.0)
        return grid

    gitd = np.zeros((el_steps, az_steps))
    for (ei, ai), (idx, w) in nbr.items():
        gitd[ei, ai] = np.dot(w, itds[idx])
    gitd = 0.5 * (gitd - gitd[:, mirror])

    sets = [build(mins), build(mins_enh)]
    peak = max(np.abs(g).max() for g in sets)
    assert peak * INT16_SCALE < 32767, "increase INT16_SCALE headroom (peak=%g)" % peak

    with open(out_path, "wb") as o:
        o.write(b"HRT2")
        o.write(struct.pack("<IIII", fs, TAPS, az_steps, el_steps))
        o.write(struct.pack("<ffff", AZ_STEP, EL_STEP, EL_MIN, INT16_SCALE))
        for g in sets:
            o.write(np.round(g * INT16_SCALE).astype("<i2").tobytes())
        o.write(gitd.astype("<f4").tobytes())

    # sanity report
    r90 = gitd[int(round((0 - EL_MIN) / EL_STEP)), int(90 / AZ_STEP)]
    print("fs=%d taps=%d grid=%dx%d peak=%.2f" % (fs, TAPS, el_steps, az_steps, peak))
    print("ITD right(+90): %.1f samples = %.3f ms; max |ITD| = %.3f ms" % (r90, 1000 * r90 / fs, 1000 * np.abs(gitd).max() / fs))
    # how much stronger is the front/back spectral difference in the enhanced set?
    def hf_db(g, az):
        h = g[int(round((0 - EL_MIN) / EL_STEP)), int(az / AZ_STEP), 0]
        sp = np.abs(np.fft.rfft(h, 1024))
        f = np.fft.rfftfreq(1024, 1.0 / fs)
        band = (f > 4000) & (f < 12000)
        return 20 * np.log10(np.sqrt(np.mean(sp[band] ** 2)))
    for name, g in zip(("raw", "enhanced"), sets):
        print("%-8s 4-12k level front %.1f dB, back %.1f dB" % (name, hf_db(g, 0), hf_db(g, 180)))
    print("wrote", out_path)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
