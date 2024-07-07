/// A sine-square-saw-pulse morphing oscillator with hardsync.
///
/// # Example 
/// `let mut squine = Squinewave::new(min_sweep, sample_rate, -1.0)`
/// 
/// For each sample, call [squine.update()][Squinewave::update] or the various setters (set_freq, set_clip, set_skew, set_sync),
/// then call [squine.generate()][Squinewave::generate] to generate one sample.  
/// After this, the getters [squine.audio()][Squinewave::audio] and [squine.sync()][Squinewave::sync] are available to use in your synth.  
pub struct Squinewave {
	// Signal inputs, set per sample (before generate() call)
	freq: f64,
	clip: f64,
	skew: f64,
	sync_in: bool,
	// Through-Zero detection	
	raw_freq: f64,
	neg_freq: bool, 

	// Outputs
	audio_out: f64,
	sync_out: f64,

	// phase and sweep_phase range 0-2. This makes skew/clip into normalized proportions
	// and output is (PI * phase).cos()
	phase: f64,
	sweep_phase: f64,
	hardsync_phase: f64,
	hardsync_inc: f64,

	// Const inited from environment
	consts: SquineConfig
}

/// Pseudo-const values inited from environment (sample rate)
struct SquineConfig {
	min_sweep: f64,
	maxphase_by_sr: f64,
	max_sweep_freq: f64,
	max_sync_freq: f64,
	sync_phase_inc: f64,
	sync_trig: f64,  // = const 0.9997;
	max_freq: f64,  // = 10000;  // Arbitrary limit
	max_sweep_inc: f64  // = 1.0 / 5;
}

/// Returns maxval on over/underflow or NaN (rather than propagate the NaN)
fn clamp(x: f64, minval: f64, maxval: f64) -> f64 {
	if x >= minval && x <= maxval {
		return x;
	}
	if x < minval {
		return minval;
	}
	return maxval
}

impl SquineConfig {
	fn new(min_sweep: f64, sample_rate: f64) -> Self {
		SquineConfig {
			min_sweep: clamp(min_sweep, 4.0, 100.0),
			max_sweep_inc: 1.0 / min_sweep,
			maxphase_by_sr: 2.0 / sample_rate,
			max_sweep_freq: sample_rate / (2.0 * min_sweep),
			max_sync_freq: sample_rate / (3.0 * min_sweep.ln()),
			sync_phase_inc: 1.0 / min_sweep.ln(),
			sync_trig: 0.9997,    // If listening to a high-freq oscil, it should hit this value. Prefer a proper sync sig.
			max_freq: 10000.0,    // Technical max is sample_rate / 2.0
		}
	} 
}

impl Squinewave {
	/// # Init values
	/// - `min_sweep` - minimum bend for square/saw shapes.  
	/// Recommended slightly different for each instance, range 4.0 to about 20.0 (max 100.0)  
	/// - `sample_rate` of your application
	/// - `phase_in` - see [set_init_phase()][Squinewave::set_init_phase]  
	/// Set to -1.0 for default sine-like start shape.  
	pub fn new(min_sweep: f64, sample_rate: f64, phase_in: f64) -> Self {
		let mut squine = Squinewave {
			consts: SquineConfig::new(min_sweep, sample_rate),
			freq: 220.0,
			clip: 1.0,
			skew: 0.0,
			sync_in: false,
			// phase and sweep_phase range 0-2.
			// This makes skew/clip into normalized proportions and output is cos(PI * phase)
			phase: 0.0,
			sweep_phase: 0.0,
			hardsync_phase: 0.0,
			hardsync_inc: 0.0,
			// Through-zero detection
			raw_freq: 0.0,
			neg_freq: false,
			// Production
			audio_out: 0.0,
			sync_out: 0.0,
		};
		squine.set_init_phase(phase_in);
		return squine;
	}
}


use std::f64::consts::PI;

impl Squinewave {
	/// # Update values
	///
	/// Call at sample_rate, before each call to [generate()][Squinewave::generate]
	pub fn update(&mut self, freq: f64, clip: f64, skew: f64, sync: f64) {
		self.set_freq(freq);
		self.set_clip(clip);
		self.set_skew(skew);
		self.set_sync(sync);
	}

	/// Frequency: Range 0.0 - 10 kHz (can be changed in code). Negative freq also possible (inverts waveform)
	pub fn set_freq(&mut self, freq: f64) {
		self.freq = clamp(freq.abs(), 0.0, self.consts.max_freq);
		self.raw_freq = freq;
	}
	/// Clip: squareness of the waveform. Range 0.0 - 1.0
	pub fn set_clip(&mut self, clip: f64) {
		self.clip = 1.0 - clamp(clip, 0.0, 1.0);
	}
	/// Skew: left-rigfht symmetry of waveform. Range -1.0 - +1.0.
	pub fn set_skew(&mut self, skew: f64) {
		self.skew = 1.0 - clamp(skew, -1.0, 1.0);
	}
	/// Sync input. Set to 1.0 to start a fast sweep to restart waveform (around 0-20 samples), otherwise 0.0.  
	/// Can also listen to another signal and trigger hardsync each time it reaches 1.0 
	pub fn set_sync(&mut self, sync: f64) {
		self.sync_in = sync >= self.consts.sync_trig;
	}

	/// Output value, available after [generate()][Squinewave::generate]
	pub fn audio(&self) -> f64 { return self.audio_out; }
	/// Sync output, updated by [generate()][Squinewave::generate]. Outputs 1.0 once per cycle, else 0.0
	pub fn sync(&self) -> f64 { return self.sync_out; }

	/// # Audio production
	/// Call once per sample. After this, [audio()][Squinewave::audio] and [sync()][Squinewave::sync] are updated.
	pub fn generate(&mut self) {
		if self.sync_in {
			self.hardsync_init();
			self.sync_in = false;  // Reset here in case set_sync() is not called properly every sample 
		}

		// hardsync ongoing? Increase freq until wraparound
		if self.hardsync_phase != 0.0 {
			let syncsweep = 0.5 * (1.0 - self.hardsync_phase.cos());
			self.freq += syncsweep * (self.consts.max_sync_freq - self.freq);
			self.hardsync_phase += self.hardsync_inc;
			if self.hardsync_phase > PI {
				self.hardsync_phase = PI;
				self.hardsync_inc = 0.0;
			}
		}
		// Through-Zero modulation: Detect neg freq and zero-crossings
		{
			let zero_crossing = (self.raw_freq < 0.0) != self.neg_freq;
			if zero_crossing && self.hardsync_phase == 0.0 {
				// Jump to opposite side of waveform
				self.phase = 1.5 - self.phase;
				if self.phase < 0.0 {
					self.phase += 2.0;
				}
				// mirror sweep_phase around 1 (cos rad)
				self.sweep_phase = 2.0 - self.sweep_phase;
			}
			self.neg_freq = self.raw_freq < 0.0;
			if self.neg_freq {
				// Invert symmetry for backward waveform
				self.skew = clamp(2.0 - self.skew, 0.0, 2.0);
			}
		}

		let phase_inc = self.consts.maxphase_by_sr * self.freq;

		// Pure sine if freq > sr/(2*Min_Sweep)
		if self.freq >= self.consts.max_sweep_freq {
			// Continue from sweep_phase
			self.audio_out = (PI * self.sweep_phase).cos();
			self.phase = self.sweep_phase;
			self.sweep_phase += phase_inc;
		}
		else {
			let min_sweep = phase_inc * self.consts.min_sweep;
			let midpoint = clamp(self.skew, min_sweep, 2.0 - min_sweep);

			// 1st half: Sweep down to cos(sweep_phase <= Pi) then flat -1 until phase >= midpoint
			if self.sweep_phase < 1.0 {
				let sweep_length = (self.clip * midpoint).max(min_sweep);

				self.audio_out = (PI * self.sweep_phase).cos();
				self.sweep_phase += (phase_inc / sweep_length).min(self.consts.max_sweep_inc);

				// Handle fractional sweep_phase overshoot after sweep ends
				if self.sweep_phase > 1.0 {
					// Tricky here: phase and sweep_phase may disagree where we are in waveform (due to FM + skew/clip changes).
					// Sweep_phase dominates to keep waveform stable, waveform (flat part) decides where we are.
					let flat_length = midpoint - sweep_length;
					// sweep_phase overshoot normalized to main phase rate
					let phase_overshoot = (self.sweep_phase - 1.0) * sweep_length;

					// phase matches shape
					self.phase = midpoint - flat_length + phase_overshoot - phase_inc;

					// Flat if next samp still not at midpoint
					if flat_length >= phase_overshoot {
						self.sweep_phase = 1.0;
						// phase may be > midpoint here (which means actually no flat part),
						// if so it will be corrected in 2nd half (since sweep_phase == 1.0)
					}
					else {
						let next_sweep_length = (self.clip * (2.0 - midpoint)).max(min_sweep);
						self.sweep_phase = 1.0 + (phase_overshoot - flat_length) / next_sweep_length;
					}
				}
			}
			// flat until midpoint
			else if self.sweep_phase == 1.0 && self.phase < midpoint {
				self.audio_out = -1.0;
			}

			// 2nd half: Sweep up to cos(sweep_phase <= 2.Pi) then flat +1 until phase >= 2
			else if self.sweep_phase < 2.0 {
				let sweep_length = (self.clip * (2.0 - midpoint)).max(min_sweep);
				if self.sweep_phase == 1.0 {
					// sweep_phase overshoot after flat part
					self.sweep_phase = 1.0 + ( (self.phase - midpoint).min(phase_inc) / sweep_length ).min(self.consts.max_sweep_inc);
				}
				self.audio_out = (PI * self.sweep_phase).cos();
				self.sweep_phase += (phase_inc / sweep_length).min(self.consts.max_sweep_inc);

				if self.sweep_phase > 2.0 {
					let flat_length = 2.0 - (midpoint + sweep_length);
					let phase_overshoot = (self.sweep_phase - 2.0) * sweep_length;

					self.phase = 2.0 - flat_length + phase_overshoot - phase_inc;
					if flat_length >= phase_overshoot {
						self.sweep_phase = 2.0;
					}
					else {
						let next_sweep_length = (self.clip * midpoint).max(min_sweep);
						self.sweep_phase = 2.0 + (phase_overshoot - flat_length) / next_sweep_length;
					}
				}
			}
			// flat until endpoint
			else {
				self.audio_out = 1.0;
				self.sweep_phase = 2.0;
			}
		}

		self.phase += phase_inc;

		// phase wraparound?
		if self.sweep_phase >= 2.0 && self.phase >= 2.0 {
			if self.hardsync_phase != 0.0 {
				self.sweep_phase = 0.0;
				self.phase = 0.0;
				self.hardsync_phase = 0.0;
				self.hardsync_inc = 0.0;
			}
			else {
				self.phase -= 2.0;
				if self.phase > phase_inc {
					// wild aliasing freq - just reset
					self.phase = phase_inc * 0.5;
				}
				if self.freq < self.consts.max_sweep_freq {
					let min_sweep = phase_inc * self.consts.min_sweep;
					let midpoint = clamp(self.skew, min_sweep, 2.0 - min_sweep);
					let next_sweep_length = (self.clip * midpoint).max(min_sweep);
					self.sweep_phase = (self.phase / next_sweep_length).min(self.consts.max_sweep_inc);
				}
				else {
					self.sweep_phase = self.phase;
				}
			}

			self.sync_out = 1.0;
		}
		else {
			self.sync_out = 0.0;
		}

	}
}


impl Squinewave {
	fn hardsync_init(&mut self) {
		// Ignore sync request if already in hardsync
		if self.hardsync_phase != 0.0 {
			return;
		}

		// If waveform is on last flat part, we're just done now
		// (could also start a full spike here, it's an option...)
		if self.sweep_phase == 2.0 {
			self.phase = 2.0;
			return;
		}

		if self.freq > self.consts.max_sync_freq {
			return;
		}

		self.hardsync_inc = self.consts.sync_phase_inc;
		self.hardsync_phase = self.hardsync_inc * 0.5;
	}
}

impl Squinewave {
	/// Called from new() to set the initial waveform phase.  
	/// Phase range 0.0 - 2.0; the symbolic range covers  
	///  - (0.0-0.5) first sweep down (zero-crossing at 0.25)
	///  - (0.5-1.0) flat low part -1
	///  - (1.0-1.5) 2nd sweep up (zero-crossing at 1.25)
	///  - (1.5-2.0) flat high part +1
	/// 
	/// Set to -1.0 to get default value 1.25, making it start at 0 on the "up" part, like a sinewave.  
	/// This is useful for LFO:s; eg for a sweep that starts per note (output amp 0 at the time, or you get a harsh click).
	/// 
	/// DO NOT CALL this during sound production!  
	/// Can be called to reset to a specific phase, eg for a new note.  
	/// Before any call, fade out the signal to 0.0 amplitude to avoid massive clicks.
	pub fn set_init_phase(&mut self, phase_in: f64) {
		// Set main phase so it matches sweep_phase
		let phase_inc = self.consts.maxphase_by_sr * self.freq;
		let min_sweep = phase_inc * self.consts.min_sweep;
		let midpoint = clamp(self.skew, self.consts.min_sweep, 2.0 - self.consts.min_sweep);

		// Init phase range 0-2, has 4 segment parts (sweep down, flat -1, sweep up, flat +1)
		if phase_in < 0.0 {
			// "up" 0-crossing (makes it look like a sinewave)
			self.sweep_phase = 1.25;
		}
		else if phase_in > 2.0 {
			self.sweep_phase = phase_in % 2.0;
		}
		else {
			self.sweep_phase = phase_in;
		}

		// Select segment and scale within
		if self.sweep_phase < 1.0 {
			let sweep_length = (self.clip * midpoint).max(min_sweep);
			if self.sweep_phase < 0.5 {
				self.phase = sweep_length * (self.sweep_phase * 2.0);
				self.sweep_phase *= 2.0;
			}
			else {
				let flat_length = midpoint - sweep_length;
				self.phase = sweep_length + flat_length * ((self.sweep_phase - 0.5) * 2.0);
				self.sweep_phase = 1.0;
			}
		}
		else {
			let sweep_length = (self.clip * (2.0 - midpoint)).max(min_sweep);
			if self.sweep_phase < 1.5 {
				self.phase = midpoint + sweep_length * ((self.sweep_phase - 1.0) * 2.0);
				self.sweep_phase = 1.0 + (self.sweep_phase - 1.0) * 2.0;
			}
			else {
				let flat_length = 2.0 - (midpoint + sweep_length);
				self.phase = midpoint + sweep_length + flat_length * ((self.sweep_phase - 1.5) * 2.0);
				self.sweep_phase = 2.0;
			}
		}
	}
}
