package com.sonobiosis.dsp;

// By rasmus ekman 2019-11-25
// NOTE: This code uses normal +/-1 range, for Voltage Modular scale inputs by 0.2, outputs by 5

// Converted from C++, sorry for un_java_like snake_casing

public class SquinewaveOscillator
{
	// Signal inputs, set per sample (before generate() call)
	private double freq;
	private double clip;
	private double skew;
	private boolean sync_in = false;
	// Through-Zero detection	
	private double raw_freq;
	private boolean neg_freq = false;

	// Production values
	private double audio_out = 0;
	private double sync_out = 0;

	// phase and sweep_phase range 0-2. This makes skew/clip into normalized proportions
	// and output is cos(PI * phase)
	private double phase;
	private double sweep_phase;
	private double hardsync_phase;
	private double hardsync_inc;

	// Const inited from environment
	private double Min_Sweep;
	private double Maxphase_By_sr;
	private double Max_Sweep_Freq;
	private double Max_Sync_Freq;
	private double Sync_Phase_Inc;
	private final double Sync_Trig = 0.9997;

	private double Max_Freq = 10000;  // Arbitrary limit
	private double Max_Sweep_Inc = 1.0 / 5;

	//-------------------------------------------------------------------------------
	//  Construction
	//-------------------------------------------------------------------------------

	public SquinewaveOscillator(double phase_in, double min_sweep_in, double sample_rate)
	{
		// Static values
		Min_Sweep = Clamp(min_sweep_in, 4, 100);
		Max_Sweep_Inc = 1.0 / Min_Sweep;
		Maxphase_By_sr = 2.0 / sample_rate;
		Max_Sweep_Freq = sample_rate / (2.0 * Min_Sweep);           // Range sr/8 - sr/200
		Max_Sync_Freq = sample_rate / (3.0 * Math.log(Min_Sweep));  // Range sr/4.1 - sr/13.8
		Sync_Phase_Inc = 1.0 / Math.log(Min_Sweep);

		// Defaults
		freq = 220;
		clip = 1.0;  // Inverted in operation, so 1 = zero effect, 0 = full effect
		skew = 0.0;
		phase = 0;
		sweep_phase = 0;
		hardsync_phase = 0;

		// Start on "up" zero-crossing, look like a sine
		set_init_phase(phase_in);
	}

	public SquinewaveOscillator()
	{
		// Use default sine-like phase
		this(-1, 4 + Math.random() * 10, 48000.0);
	}

	public double getMinSweep() { return Min_Sweep; }

	//-------------------------------------------------------------------------------
	//  Inputs, called before generate()
	//-------------------------------------------------------------------------------

	// EITHER use this, OR the 4 individual setters, before every call to generate()
	public void update(double freq, double clip, double skew, double sync) {
		setFreq(freq);
		setClip(cip);
		setSkew(skew);
		setSync(sync);
	}

	public void setFreq(double x) {
		freq = Clamp(Math.abs(x), 0, Max_Freq);
		raw_freq = x;
	}
	public void setSkew(double x) {
		// Map to 0-2, leftfacing when -1
		skew = 1 - Clamp(x, -1, 1);
	}
	public void setClip(double x) {
		// inverted to proportion of segment
		clip = 1 - Clamp(x, 0, 1);
	}
	public void setSync(double x) { sync_in = (x >= Sync_Trig); }


	//-------------------------------------------------------------------------------
	//  Outputs, available after generate()
	//-------------------------------------------------------------------------------

	public double getSample() { return audio_out; }
	public double getSync() { return sync_out; }

	// Range 0-2 (see comment top)
	public double getPhase() { return phase; }

	//-------------------------------------------------------------------------------
	//  generate one sample
	//-------------------------------------------------------------------------------
	public void generate()
	{
		if (sync_in) {
			hardsync_init(freq, sweep_phase);
			sync_in = false;
		}

		// hardsync ongoing? Increase freq until wraparound
		if (hardsync_phase != 0) {
			double syncsweep = 0.5 * (1.0 - Math.cos(hardsync_phase));
			freq += syncsweep * (Max_Sync_Freq - freq);
			hardsync_phase += hardsync_inc;
			if (hardsync_phase > Math.PI) {
				hardsync_phase = Math.PI;
				hardsync_inc = 0.0;
			}
		}
		// Through-Zero modulation: Detect neg freq and zero-crossings
		{
			boolean zero_crossing = (raw_freq < 0.0) != neg_freq;
			if (zero_crossing && hardsync_phase == 0) {
				// Jump to opposite side of waveform
				phase = 1.5 - phase;
				if (phase < 0) phase += 2.0;
				// mirror sweep_phase around 1 (cos rad)
				sweep_phase = 2.0 - sweep_phase;
			}
			neg_freq = raw_freq < 0.0;
			if (neg_freq) {
				// Invert symmetry for backward waveform
				skew = Clamp(2.0 - skew, 0.0, 2.0);
			}
		}

		double phase_inc = Maxphase_By_sr * freq;

		// Pure sine if freq > sr/(2*Min_Sweep)
		if (freq >= Max_Sweep_Freq)
		{
			// Continue from sweep_phase
			audio_out = Math.cos(Math.PI * sweep_phase);
			phase = sweep_phase;
			sweep_phase += phase_inc;
		}
		else
		{
			double min_sweep = phase_inc * Min_Sweep;
			double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);

			// 1st half: Sweep down to cos(sweep_phase <= Pi) then flat -1 until phase >= midpoint
			if (sweep_phase < 1.0)
			{
				double sweep_length = Math.max(clip * midpoint, min_sweep);

				audio_out = Math.cos(Math.PI * sweep_phase);
				sweep_phase += Math.min(phase_inc / sweep_length, Max_Sweep_Inc);

				// Handle fractional sweep_phase overshoot after sweep ends
				if (sweep_phase > 1.0) {
					// Tricky here: phase and sweep_phase may disagree where we are in waveform (due to FM + skew/clip changes).
					// Sweep_phase dominates to keep waveform stable, waveform (flat part) decides where we are.
					double flat_length = midpoint - sweep_length;
					// sweep_phase overshoot normalized to main phase rate
					double phase_overshoot = (sweep_phase - 1.0) * sweep_length;

					// phase matches shape
					phase = midpoint - flat_length + phase_overshoot - phase_inc;

					// Flat if next samp still not at midpoint
					if (flat_length >= phase_overshoot) {
						sweep_phase = 1.0;
						// phase may be > midpoint here (which means actually no flat part),
						// if so it will be corrected in 2nd half (since sweep_phase == 1.0)
					}
					else {
						double next_sweep_length = Math.max(clip * (2.0 - midpoint), min_sweep);
						sweep_phase = 1.0 + (phase_overshoot - flat_length) / next_sweep_length;
					}
				}
			}
			// flat until midpoint
			else if (sweep_phase == 1.0 && phase < midpoint) {
				audio_out = -1.0;
				sweep_phase = 1.0;
			}

			// 2nd half: Sweep up to cos(sweep_phase <= 2.Pi) then flat +1 until phase >= 2
			else if (sweep_phase < 2.0) {
				double sweep_length = Math.max(clip * (2.0 - midpoint), min_sweep);
				if (sweep_phase == 1.0) {
					// sweep_phase overshoot after flat part
					sweep_phase = 1.0 + Math.min( Math.min(phase - midpoint, phase_inc) / sweep_length, Max_Sweep_Inc);
				}
				audio_out = Math.cos(Math.PI * sweep_phase);
				sweep_phase += Math.min(phase_inc / sweep_length, Max_Sweep_Inc);

				if (sweep_phase > 2.0) {
					double flat_length = 2.0 - (midpoint + sweep_length);
					double phase_overshoot = (sweep_phase - 2.0) * sweep_length;

					phase = 2.0 - flat_length + phase_overshoot - phase_inc;

					if (flat_length >= phase_overshoot) {
						sweep_phase = 2.0;
					}
					else {
						double next_sweep_length = Math.max(clip * midpoint, min_sweep);
						sweep_phase = 2.0 + (phase_overshoot - flat_length) / next_sweep_length;
					}
				}
			}
			// flat until endpoint
			else {
				audio_out = 1.0;
				sweep_phase = 2.0;
			}
		}


		phase += phase_inc;

		// phase wraparound?
		if (sweep_phase >= 2.0 && phase >= 2.0)
		{
			if (hardsync_phase != 0) {
				sweep_phase = phase = 0.0;
				hardsync_phase = hardsync_inc = 0.0;
			}
			else {
				phase -= 2.0;
				if (phase > phase_inc) {
					// wild aliasing freq - just reset
					phase = phase_inc * 0.5;
				}
				if (freq < Max_Sweep_Freq) {
					double min_sweep = phase_inc * Min_Sweep;
					//skew = 1.0 - Clamp(skew_sig, -1.0, 1.0);
					//clip = 1.0 - Clamp(clip_sig, 0.0, 1.0);
					double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);
					double next_sweep_length = Math.max(clip * midpoint, min_sweep);
					sweep_phase = Math.min(phase / next_sweep_length, Max_Sweep_Inc);
				}
				else {
					sweep_phase = phase;
				}
			}
			sync_out = 1.0;
		}
		else {
			sync_out = 0;
		}
	}


	static double Clamp(double x, double minval, double maxval) {
		// Returns maxval on Inf and NaN.
		return (x >= minval && x <= maxval) ? x : (x < minval) ? minval : maxval;
	}


	private void hardsync_init(double freq, double sweep_phase)
	{
		// Ignore sync request if already in hardsync
		if (hardsync_phase != 0)
			return;

		// If waveform is on last flat part, we're just done now
		// (could also start a full spike here, it's an option...)
		if (sweep_phase == 2.0) {
			phase = 2.0;
			return;
		}

		if (freq > Max_Sync_Freq)
			return;

		hardsync_inc = Sync_Phase_Inc;
		hardsync_phase = hardsync_inc * 0.5;
	}


	// Set full state so waveform starts at specific phase.
	// Phase range 0-2; the symbolic range covers
	//   (0.0-0.5) first sweep down (zero-crossing at 0.25)
	//   (0.5-1.0) flat low part -1
	//   (1.0-1.5) 2nd sweep up (zero-crossing at 1.25)
	//   (1.5-2.0) flat high part +1
	// This is useful for LFO:s; eg for a sweep that starts per note (output amp 0 at the time, or you get a harsh click).
	public void set_init_phase(double phase_in)
	{
		// Set main phase so it matches sweep_phase
		double phase_inc = Maxphase_By_sr * freq;
		double min_sweep = phase_inc * Min_Sweep;
		double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);

		// Init phase range 0-2, has 4 segment parts (sweep down, flat -1, sweep up, flat +1)
		sweep_phase = phase_in;
		if (sweep_phase < 0.0) {
			// "up" 0-crossing (makes it look like a sinewave)
			sweep_phase = 1.25;
		}
		if (sweep_phase > 2.0)
			sweep_phase = sweep_phase % 2.0;

		// Select segment and scale within
		if (sweep_phase < 1.0) {
			double sweep_length = Math.max(clip * midpoint, min_sweep);
			if (sweep_phase < 0.5) {
				phase = sweep_length * (sweep_phase * 2.0);
				sweep_phase *= 2.0;
			}
			else {
				double flat_length = midpoint - sweep_length;
				phase = sweep_length + flat_length * ((sweep_phase - 0.5) * 2.0);
				sweep_phase = 1.0;
			}
		}
		else {
			double sweep_length = Math.max(clip * (2.0 - midpoint), min_sweep);
			if (sweep_phase < 1.5) {
				phase = midpoint + sweep_length * ((sweep_phase - 1.0) * 2.0);
				sweep_phase = 1.0 + (sweep_phase - 1.0) * 2.0;
			}
			else {
				double flat_length = 2.0 - (midpoint + sweep_length);
				phase = midpoint + sweep_length + flat_length * ((sweep_phase - 1.5) * 2.0);
				sweep_phase = 2.0;
			}
		}
	}

}
