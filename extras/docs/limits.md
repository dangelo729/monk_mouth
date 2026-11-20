# Known limits

- A 1D tube has no teeth. S sits above SH, which keeps them apart, but
  neither has a tooth's edge. The slit rule stands in for the whistle: a
  consonant slot at the lips never whistles, a pucker does.
- L and R are approximations: a constriction above the hiss window (R with
  narrowed lips). No lateral channel, no retroflexion, no low F3.
- TH and DH are a weak hiss near the teeth (place .88, degree 0.3), close to F
  and V.
- Voiced stops have almost no voice bar: the tube really closes, and only the
  shut velum's leak lets the voice out.
- The default voice has a steep spectral tilt (the LF pulse at tenseness 0.6);
  a higher `setTenseness` is brighter and more pressed.
- The LF pulse is not band-limited, so it aliases, more as the pitch rises.
  2x oversampling of the source alone would fix it and is not implemented.
- Words are loose. No text to phoneme, General American vowels only. `sing()`
  and `say()` accept the stress digits and ignore them; `speak()` reads them
  for length, accent and loudness and nothing finer: one phrase shape (a
  declarative fall), no question rise. Its consonants keep the sung table's
  holds. Live `syllable()` cannot anticipate: the vowel lands after the onset.
- One score is 256 events: a line at a time.
- The tongue body cannot close or hiss; only the two slots and the lips can.
- The jet's transit does not scale with size. It is physical (a length over a
  speed), while every mode scales with 1/N, so the jet's reach in hertz stays
  put. At breath 0.8 and N = 16 the table's rows land within 83 cents of their
  note rather than 21, and the top two (past 2.5 kHz) do not lock; at N = 26
  they land up to 206 cents sharp, and two of the bottom three (under 670 Hz)
  do not lock. The hand pose in [whistling](whistling.md) locks from breath
  0.3 at 22, 0.5 at 16 and 0.25 at 26.
- The whistle's pitch follows breath, up to 1.4 semitones from 0.5 to 1.0 at
  the bottom of the range.
- The whistle range is 18 semitones, F5 to B6 at N = 22: the fit finds no lock
  on the front cavity above about 2 kHz or below about 700 Hz.
- Not a self-oscillating fold model, not a 2D or 3D tract.
