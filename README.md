# String Lab

String Lab is an early JUCE VST3 instrument and standalone application. It synthesizes
plucked strings rather than playing samples. This is a prototype, not a finished or
calibrated physical replica; the tone needs listening and refinement in a DAW.

**Known issue:** Acoustic guitar has been reported silent in Waveform 13 when
playing a MIDI file. The exact cause is unresolved: both the older and current
versions produced acoustic audio in the automated VST3 host tests. Further
testing in Waveform is needed.

## Build on Windows

Install Visual Studio with the C++ desktop workload, CMake, Git, and an internet
connection for the first configure. CMake downloads JUCE 8.0.8. JUCE is distributed
under its own license; review its terms before distributing a product.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use the Visual Studio generator installed on your machine if it differs. The VST3
is produced under `build\StringLab_artefacts\Release\VST3`; the standalone app is
under `build\StringLab_artefacts\Release\Standalone`. Add the VST3 folder to
Waveform's plug-in search paths or copy the `.vst3` bundle to a standard VST3
location, then rescan. Place it on an instrument track with MIDI notes and audio
output. Standalone mode requires selecting an audio output and MIDI input.

## Controls

- **Instrument:** electric guitar, acoustic guitar, four-string electric bass,
  Bass VI, or plucked upright bass. Bass VI is a provisional interpretation of
  the requested six-string bass.
- **Playing mode:** guitar-like assigns at most one note per string; it prefers
  an available string and then the lowest fret, replacing the note on that string
  if all playable strings are occupied. Unrestricted allows up to 32 simultaneous
  voices; beyond that it replaces the oldest. Notes outside every string's
  0–24 fret range are not played.
- **Reference A4:** defaults provisionally to 440 Hz, adjustable from 400 to
  480 Hz. At 440 Hz, middle C (C4) is approximately 261.63 Hz.
- **String tuning offsets:** strings are numbered from lowest to highest, and
  offsets are semitones relative to the selected instrument's default tuning.
  For D2 A2 D3 G3 A3 G4 on guitar, use offsets `-2, 0, 0, 0, -2, +3`.
  The last two controls do not apply to four-string presets.
- **Pluck position** and **pickup position:** fractions of the speaking length
  measured from an end of the string. Pickup position applies to electric presets.
- **Thickness scale** and **stiffness scale:** change the string model; **decay**
  controls the approximate fundamental amplitude-decay time. These, tuning and
  pluck/pickup settings affect newly played notes.
- **Acoustic body amount:** mixes a simple body resonance into acoustic guitar
  and upright-bass sounds. **Output level** adjusts the total output.
- **Acoustic strings:** choose Steel or Nylon for the acoustic guitar. This
  changes newly played notes; it has no effect on other instruments. Nylon uses
  approximate material properties and a gentler high-harmonic spectrum, rather
  than a measured classical-guitar model. Existing projects default to Steel.
- **Drive effect:** Off, Overdrive (soft clipping), or Distortion (hard clipping).
  **Drive amount** sets the gain into the selected effect.
- **Amp style:** Off, Fender-style clean, Marshall JCM 800-style, Vox AC30-style,
  or Mesa-style high gain. **Amp gain** sets the amp's saturation and **Amp tone**
  adjusts its low/high balance. These are original, approximate tonal
  interpretations, not component-level models or official products.

The signal path is string/body, drive effect, amp and simple cabinet filtering,
then output level. Effects can be used with any instrument, including bass and
acoustic. Both selectors default to Off so existing sessions retain their dry
sound. Lower the output level before enabling drive or an amp; monitor levels
and adjust to taste. Changes to effect selectors crossfade briefly.

Default tunings, low to high: guitar E2 A2 D3 G3 B3 E4; electric and upright
bass E1 A1 D2 G2; Bass VI E1 A1 D2 G2 B2 E3. Changing instrument or playing
mode stops current notes. MIDI note on/off, sustain pedal, all notes off and
all sound off are supported. Pitch bend, slides, fret noise and bowing are not
yet modeled. There is no built-in reverb or tremolo.

## Model and limitations

The string is a sum of 16 damped partials. Open pitch determines tension from
length, estimated linear density and flexural rigidity; fretting shortens the
speaking length at equal-tempered fret positions while preserving tension.
The reusable `StringModel` can also be constructed directly with length, linear
mass density, tension and bending rigidity in SI units.
Stiffness introduces inharmonicity; effective rigidity is smoothly limited to
keep the calibrated open-string tension positive at extreme settings.
A triangular-pluck-inspired spectrum and
a position-dependent pickup shape the output. The acoustic body is a small
modal filter bank, not a measured guitar or upright-bass impulse response.
Its resonator gains are normalized across sample rates; this fixes the previous
version's disproportionately loud acoustic resonances at higher sample rates.

Wound strings use approximate core-to-outer-diameter ratios for stiffness and
an approximate solid-steel mass density; these are not measured string
construction data. Material, tension and individual string dimensions are not
yet editable independently. Fretting and pickup behavior are approximations,
especially for bass and upright. No perceptual or hardware-reference accuracy
is claimed. Listen at a safe level: polyphonic peaks can exceed unity, and the
output control may need to be reduced.

The amp and cabinet sections use simple nonlinear waveshaping and filters,
not measured amplifier circuits or speaker impulse responses. Distortion uses
first-order antiderivative antialiasing to reduce hard-clipping artifacts, but
the nonlinear processing is not oversampled and may still alias, particularly
with high notes and drive settings. These controls are intended for
experimentation rather than a claim of a precise JCM 800 or other hardware
emulation. Distortion is intentionally changed from the earlier hard clipper;
existing sessions using that effect may sound slightly different.

The automated tests exercise ideal fret ratios, default-preset tuning tolerance,
inharmonicity, basic note allocation, sustain, alternate tuning and finite output
at several sample rates. A small JUCE host test also loads the built VST3 and
checks MIDI timing, audio output and state restoration. Effects tests cover bypass,
all selector combinations, controls, switching and multiple sample rates. These tests do not replace
listening or testing in Waveform.

## Audio probe

The `StringLabAudioProbe` test hosts the actual built VST3, plays an A2 note
for one second through dry electric, overdrive, distortion, JCM 800-style,
steel acoustic (body off and on), and nylon acoustic configurations. It writes
24-bit WAV files, fixed-scale 30 ms SVG oscilloscope views, and
`measurements.csv` with peak, RMS, and the first four harmonic amplitudes from
100–600 ms. The CSV harmonics describe the decaying note, not a calibrated THD
measurement; stiffness may shift partial frequencies. The probe also drives the
same effects DSP directly with a 200 Hz sine wave and writes corresponding WAVs,
plots, and `sine-measurements.csv`, making generated harmonics easier to inspect.
`alias-measurements.csv` measures the 21 kHz folded third harmonic of a 9 kHz
test tone at 48 kHz, with distortion off and on.

Build and run the test via `ctest --test-dir build -C Release --output-on-failure`.
Results are in `build\AudioProbe`. To generate them independently, run:

```powershell
& '.\build\StringLabAudioProbe_artefacts\Release\StringLabAudioProbe.exe' `
  '.\build\StringLab_artefacts\Release\VST3\String Lab.vst3' '.\build\AudioProbe'
```

The probe overwrites its named outputs in that directory. The scope plots use the
same vertical scale for comparison, and the WAVs can be imported into a DAW.
For versions predating the acoustic-string selector, append `--legacy` to
skip the nylon case when comparing an older plug-in. Legacy mode only renders
the hosted plug-in; the direct effects-DSP tests always exercise current code.
Hard clipping in Distortion deliberately produces flat waveform peaks and extra
harmonics; it is not oversampled and can still alias. These measurements confirm signal
behavior, not whether the sound matches real instruments or amplifiers.
