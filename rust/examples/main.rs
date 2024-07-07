
use std::error::Error;
use std::fs::File;
use csv::Writer;

use squinewave::Squinewave;


fn main() -> Result<(), Box<dyn Error>>{
	let file_path = "curves.csv";
	let mut writer = Writer::from_writer(File::create(file_path)?);

	// Low sample rate and frequency to generate the plot. Use proper ranges for real audio.
	let mut squine = Squinewave::new(7.0, 10000.0, -1.0);
	let sample_count = 5000;
	let mut freq = 12.0;
	let freq_inc = (80.0 - freq) / sample_count as f64;

	// Waveform shape
	let mut clip = 0.0;
	let mut clip_inc = 1.0 / sample_count;
	let mut skew = -0.1;
	let mut skew_inc = 0.0003;

	// Generate csv values
	for i in 0..sample_count {
		// i == 303: set a hardsync near start
		squine.update(freq, clip, skew, (i == 303) as u32 as f64);
		squine.generate();
		writer.serialize(squine.audio())?;
		freq += freq_inc;
		clip += clip_inc;
		skew += skew_inc;
		if clip >1.0 || clip < 0.0 { clip_inc = -clip_inc }
		if skew < -1.0 || skew > 1.0 { skew_inc = -skew_inc }
	}

	writer.flush()?;

	Ok(())
}
