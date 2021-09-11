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

	// Production values
	private double audio_out = 0;
	private double sync_out = 0;

	// phase and warped_phase range 0-2. This makes skew/clip into normalized proportions
	// and output is cos(PI * phase)
	private double phase;
	private double warped_phase;
	private double hardsync_phase;
	private double hardsync_inc;

	 // Const inited from environment
	private double Min_Sweep;
	private double Maxphase_By_sr;
	private double Max_Warp_Freq;
	private double Max_Sync_Freq;
	private final double Sync_Trig = 0.9997;

	private double Max_Freq = 10000;  // Arbitrary limit
	private double Max_Warp = 1.0 / 5;

	//-------------------------------------------------------------------------------
	//  Construction
	//-------------------------------------------------------------------------------

	public SquinewaveOscillator(double phase_in, double min_sweep_in, double sample_rate)
	{
		// Static values
		Min_Sweep = Clamp(min_sweep_in, 4, 100);
		Max_Warp = 1.0 / Min_Sweep;
		Maxphase_By_sr = 2.0 / sample_rate;
		Max_Warp_Freq = sample_rate / (2.0 * Min_Sweep);    // Range sr/8 - sr/100
        Max_Sync_Freq = sample_rate / (1.6667 * Math.log(Min_Sweep));  // Range sr/2.3 - sr/7.6

		// Defaults
		freq = 220;
		clip = 1.0;  // Inverted in operation, so 1 = zero effect, 0 = full effect
		skew = 0.0;
		phase = 0;
		warped_phase = 0;
		hardsync_phase = 0;

		// Start on "up" zero-crossing, look like a sine
		set_init_phase(phase_in);
	}

	public SquinewaveOscillator()
	{
		// Use default sine-like phase
		this(-1, Math.floor(4 + Math.random() * 10), 48000.0);
	}

	public double getMinSweep() { return Min_Sweep; }

	//-------------------------------------------------------------------------------
	//  Inputs, called before generate()
	//-------------------------------------------------------------------------------

    // EITHER use this, OR the 4 individual setters
	public void update(double frq, double clp, double skw, double syn) {
		setFreq(frq);
		setClip(clp);
		setSkew(skw);
		setSync(syn);
	}
	
	public void setFreq(double x) { freq = Clamp(x, 1.0 / 60.0, Max_Freq); }
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
			hardsync_init(freq, warped_phase);
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

		double phase_inc = Maxphase_By_sr * freq;

		// Pure sine if freq > sr/(2*Min_Sweep)
		if (freq >= Max_Warp_Freq)
		{
			// Continue from warped
			audio_out = Math.cos(Math.PI * warped_phase);
			phase = warped_phase;
			warped_phase += phase_inc;
		}
		else
		{
			double min_sweep = phase_inc * Min_Sweep;
			double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);

			// 1st half: Sweep down to cos(warped_phase <= Pi) then flat -1 until phase >= midpoint
			if (warped_phase < 1.0 || (warped_phase == 1.0 && phase < midpoint))
			{
				if (warped_phase < 1.0) {
					double sweep_length = Math.max(clip * midpoint, min_sweep);
	
					audio_out = Math.cos(Math.PI * warped_phase);
					warped_phase += Math.min(phase_inc / sweep_length, Max_Warp);
	
					// Handle fractional warped_phase overshoot after sweep ends
					if (warped_phase > 1.0) {
						// Tricky here: phase and warped may disagree where we are in waveform (due to FM + skew/clip changes).
						// Warped dominates to keep waveform stable, waveform (flat part) decides where we are. 
						double flat_length = midpoint - sweep_length;
						// warp overshoot normalized to main phase rate
						double phase_overshoot = (warped_phase - 1.0) * sweep_length;
	
						// phase matches shape
						phase = midpoint - flat_length + phase_overshoot - phase_inc;
	
						// Flat if next samp still not at midpoint
						if (flat_length >= phase_overshoot) {
							warped_phase = 1.0;
							// phase may be > midpoint here (which means actually no flat part),
							// if so it will be corrected in 2nd half (since warped == 1.0)
						}
						else {
							double next_sweep_length = Math.max(clip * (2.0 - midpoint), min_sweep);
							warped_phase = 1.0 + (phase_overshoot - flat_length) / next_sweep_length;
						}
					}
				}
				else {
					// flat until midpoint
					audio_out = -1.0;
					warped_phase = 1.0;
				}
			}
			// 2nd half: Sweep up to cos(warped_phase <= 2.Pi) then flat +1 until phase >= 2
			else {
				if (warped_phase < 2.0) {
					double sweep_length = Math.max(clip * (2.0 - midpoint), min_sweep);
					if (warped_phase == 1.0) {
						// warped_phase overshoot after flat part
						warped_phase = 1.0 + Math.min( Math.min(phase - midpoint, phase_inc) / sweep_length, Max_Warp);
					}
					audio_out = Math.cos(Math.PI * warped_phase);
					warped_phase += Math.min(phase_inc / sweep_length, Max_Warp);
	
					if (warped_phase > 2.0) {
						double flat_length = 2.0 - (midpoint + sweep_length);
						double end_length = (2.0 - phase) / phase_inc;
						double phase_overshoot = (warped_phase - 2.0) * sweep_length;
	
						phase = 2.0 - flat_length + phase_overshoot - phase_inc;
	
						if (flat_length >= phase_overshoot) {
							warped_phase = 2.0;
						}
						else {
							double next_sweep_length = Math.max(clip * midpoint, min_sweep);
							warped_phase = 2.0 + (phase_overshoot - flat_length) / next_sweep_length;
						}
					}
				}
				else {
					audio_out = 1.0;
					warped_phase = 2.0;
				}
			}
		}


		phase += phase_inc;

	    // phase wraparound?
		if (warped_phase >= 2.0 && phase >= 2.0)
		{
			if (hardsync_phase != 0) {
				warped_phase = phase = 0.0;
				hardsync_phase = hardsync_inc = 0.0;
			}
			else {
				phase -= 2.0;
				if (phase > phase_inc) {
					// wild aliasing freq - just reset
					phase = phase_inc * 0.5;
				}
				if (freq < Max_Warp_Freq) {
					double min_sweep = phase_inc * Min_Sweep;
					//skew = 1.0 - Clamp(skew_sig, -1.0, 1.0);
					//clip = 1.0 - Clamp(clip_sig, 0.0, 1.0);
					double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);
					double next_sweep_length = Math.max(clip * midpoint, min_sweep);
					warped_phase = Math.min(phase / next_sweep_length, Max_Warp);
				}
				else
					warped_phase = phase;
			}
			sync_out = 1.0;
		}
		else
			sync_out = 0;
	}


	static double Clamp(double x, double minval, double maxval) {
		// Returns maxval on Inf and NaN.
		return (x >= minval && x <= maxval) ? x : (x < minval) ? minval : maxval;
	}


	private void hardsync_init(double freq, double warped_phase)
	{
		if (hardsync_phase != 0)
			return;

		// If we're in last flat part, we're just done now
		if (warped_phase == 2.0) {
			phase = 2.0;
			return;
		}

		if (freq > Max_Sync_Freq)
			return;
	
		hardsync_inc = (Math.PI / Min_Sweep);
		hardsync_phase = hardsync_inc * 0.5; 
	}


	// Set full state so waveform starts at specific phase.
	// Phase range 0-2; the symbolic range covers 
	//   (0.0-0.5) first sweep down (zero-crossing at 0.25)
	//   (0.5-1.1) flat low part -1
	//   (1.0-1.5) 2nd sweep up (zero-crossing at 1.25)
	//   (1.5-2.0) flat high part +1
	// This is useful for LFO:s; eg for a sweep that starts per note (output amp 0 at the time, or you get a harsh click).
	public void set_init_phase(double phase_in)
	{
		// Set main phase so it matches warp
		double phase_inc = Maxphase_By_sr * freq;
		double min_sweep = phase_inc * Min_Sweep;
		double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);

		// Init phase range 0-2, has 4 segment parts (sweep down, flat -1, sweep up, flat +1)
		warped_phase = phase_in;
		if (warped_phase < 0.0) {
			// "up" 0-crossing (makes it look like a sinewave)
			warped_phase = 1.25;
		}
		if (warped_phase > 2.0)
			warped_phase = warped_phase % 2.0;

		// Select segment and scale within 
		if (warped_phase < 1.0) {
			double sweep_length = Math.max(clip * midpoint, min_sweep);
			if (warped_phase < 0.5) {
				phase = sweep_length * (warped_phase * 2.0);
				warped_phase *= 2.0;
			}
			else {
				double flat_length = midpoint - sweep_length;
				phase = sweep_length + flat_length * ((warped_phase - 0.5) * 2.0);
				warped_phase = 1.0;
			}
		}
		else {
			double sweep_length = Math.max(clip * (2.0 - midpoint), min_sweep);
			if (warped_phase < 1.5) {
				phase = midpoint + sweep_length * ((warped_phase - 1.0) * 2.0);
				warped_phase = 1.0 + (warped_phase - 1.0) * 2.0;
			}
			else {
				double flat_length = 2.0 - (midpoint + sweep_length);
				phase = midpoint + sweep_length + flat_length * ((warped_phase - 1.5) * 2.0);
				warped_phase = 2.0;
			}
		}
	}

}
