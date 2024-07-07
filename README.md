# Squinewave

Author: rasmus

A sine-square-saw-pulse morphing oscillator with hardsync.  
Implemented for [CSound](./csound), [SuperCollider](./supercollider), 
[Voltage Modular](./java), and [Rust](./rust).

Each implementation is basically the same, but in slightly different environments.


### Squinewave algorithm

Just cosine sweeps and flat parts.  
The waveform is shaped by 2 params: *clip* and *skew*:

* **Clip** "squareness" of waveform shape. Range: 0.0 - 1.0.
  * Clip 0 is sinewave (or saw), clip 1 is squarewave (or pulse).
* **Skew** symmetry of waveform shape.  Range: -1 to +1.
  * Skew = 0 is symmetric like sine or square. Skew +1 or -1 is right/left-facing saw or pulse.

#### Hardsync
When a trigger is received, frequency is raised very quickly until next cycle.  
This creates a short sine pulse (0-20 samples), depending on current waveform phase.  

Optionally emit a hardsync trigger each cycle.  
(Tip: Chain sync output to next Squinewave, for hardsync bursts, since sync output is emitted after its own sync sweep).


#### Bandlimited - not by filtering
This is ensured by init config parameter *MinSweep*, a minimal sample count of rise/fall shapes.  
Recommended range 5-15 samps (randomized per unit). Legal range 4 - 100.

Eg, a squarewave or the front of a saw starts with a very short cosine sweep, 5-10 samples.
* On higher frequency the MinSweep is held, so all waveforms "degrade" to sinewave.  
  Hence no aliasing on higher freq square or pulse waveform.

The cost of generating a sample is 1 cos() call and some if-then switching.  
But generally you can skip one low-pass filter in your synth patch, since reducing *clip* dampens the spectrum.


#### Guarantee
If frequency, clip and skew are generated by smooth signals like sinewave or Squinewave, the output is bandlimited in almost all configurations, including high index FM with hardsync.


#### Notes
Unlike BLEP techniques this has no ripples. The waveform is clean and stable, works both as LFO and as carrier/modulator in FM setups. The waveform shaping extends the palette of standard FM nicely.

In all modesty this could replace most sine and square oscillators in every softsynth ever.

There is no license in this repo.  
