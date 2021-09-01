// PluginSquine.cpp
// rasmus (nevermind@dontcare.se)

#include "SC_PlugIn.hpp"
#include "Squine.hpp"

static InterfaceTable* ft;

namespace ostinato {

/* ================================================================== */

// Returns maxval on Inf or NaN
static inline double Clamp(const double x, const double minval, const double maxval) {
    return (x >= minval && x <= maxval) ? x : (x < minval) ? minval : maxval;
}

/* ================================================================== */

Squine::Squine() {
    const double sr = sampleRate();

    Min_Sweep = *in(6);
    // Allow range 4-sr/100
    if (Min_Sweep < 4.0 || Min_Sweep > sr * 0.01) {
        Min_Sweep = (int32_t)Clamp(sr / 3000.0, 5.0, sr * 0.01);
    }

    Maxphase_By_sr = 2.0 / sr;
    Max_Warp_Freq = sr / (2.0 * Min_Sweep);
    Max_Warp = 1.0 / Min_Sweep;

    if (in(7)) {
        // init_phase, freq, clip, skew
        init_phase(in0(7), in0(0), in0(1), in0(2));
    }

    mCalcFunc = make_calc_function<Squine, &Squine::next>();
    next(1);
}

/* ================================================================== */

static inline int32_t find_sync(const float* sync_sig, const int32_t first, const int32_t last)
{
    for (int32_t i = first; i < last; ++i) {
        if (sync_sig[i] >= 1.0)
            return i;
    }
    return -1;
}

/* ================================================================== */

// Set main phase so it matches warp
void Squine::init_phase(const double phase_in, const double freq_in, const double clip_in, const double skew_in) {
    const double phase_inc = Maxphase_By_sr * freq_in;
    const double min_sweep = phase_inc * Min_Sweep;
    skew = 1.0 - Clamp(skew_in, -1.0, 1.0);
    clip = 1.0 - Clamp(clip_in, 0.0, 1.0);
    const double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);

    // Init phase range 0-2, has 4 segment parts (sweep down, flat -1, sweep up, flat +1)
    warped_phase = phase_in;
    if (warped_phase < 0.0) {
        // "up" 0-crossing
        warped_phase = 1.25;
    }
    if (warped_phase > 2.0)
        warped_phase = fmod(warped_phase, 2.0);

    // Select segment and scale within
    if (warped_phase < 1.0) {
        const double sweep_length = fmax(clip * midpoint, min_sweep);
        if (warped_phase < 0.5) {
            phase = sweep_length * (warped_phase * 2.0);
            warped_phase *= 2.0;
        }
        else {
            const double flat_length = midpoint - sweep_length;
            phase = sweep_length + flat_length * ((warped_phase - 0.5) * 2.0);
            warped_phase = 1.0;
        }
    }
    else {
        const double sweep_length = fmax(clip * (2.0 - midpoint), min_sweep);
        if (warped_phase < 1.5) {
            phase = midpoint + sweep_length * ((warped_phase - 1.0) * 2.0);
            warped_phase = 1.0 + (warped_phase - 1.0) * 2.0;
        }
        else {
            const double flat_length = 2.0 - (midpoint + sweep_length);
            phase = midpoint + sweep_length + flat_length * ((warped_phase - 1.5) * 2.0);
            warped_phase = 2.0;
        }
    }

}

/* ================================================================== */

void Squine::hardsync_init(const double freq, const double warped_phase)
{
    if (this->hardsync_phase)
        return;

    // If we're in last flat part, we're just done now
    if (warped_phase == 2.0) {
        this->phase = 2.0;
        return;
    }

    if (freq > this->Max_Warp_Freq)
        return;

    this->hardsync_inc = (pi / this->Min_Sweep);
    this->hardsync_phase = this->hardsync_inc * 0.5;
}

/* ================================================================== */

void Squine::next(int nSamples) {
    const float* freq_sig = in(0);
    const float* clip_sig = in(1);
    const float* skew_sig = in(2);

    // Look for sync if a-rate
    int32_t sync = isAudioRateIn(3) ? find_sync(in(3), 0, nSamples) : -1;
    
    float* sound_out = out(0);
    float* sync_out = nullptr;
    if (numOutputs() > 1) {
        sync_out = out(1);
        memset(sync_out, 0, nSamples * sizeof(float));
    }

    for (int32_t i = 0; i < nSamples; ++i) {
        double freq = fmax(freq_sig[i], 0.0);

        // hardsync requested?
        if (i == sync) {
            hardsync_init(freq, warped_phase);
        }

        // hardsync ongoing?
        if (hardsync_phase) {
            const double syncsweep = 0.5 * (1.0 - cos(hardsync_phase));
            freq += syncsweep * ((2.0 * Max_Warp_Freq) - freq);
            hardsync_phase += hardsync_inc;
            if (hardsync_phase > pi) {
                hardsync_phase = pi;
                hardsync_inc = 0;
            }
        }

        const double phase_inc = Maxphase_By_sr * freq;

        // Pure sine if freq > sr / (2 * Min_Sweep)
        if (freq >= Max_Warp_Freq) {
            // Continue from warped
            *sound_out++ = static_cast<float>( cos(pi * warped_phase) );
            phase = warped_phase;
            warped_phase += phase_inc;
        }
        else {
            const double min_sweep = phase_inc * Min_Sweep;
            const double skew = 1.0 - Clamp(skew_sig[i], -1.0, 1.0);
            const double clip = 1.0 - Clamp(clip_sig[i], 0.0, 1.0);
            const double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);

            // 1st half: Sweep down to cos(warped_phase <= pi) then flat -1 until phase >= midpoint
            if (warped_phase < 1.0 || (warped_phase == 1.0 && phase < midpoint))
            {
                if (warped_phase < 1.0) {
                    const double sweep_length = fmax(clip * midpoint, min_sweep);

                    *sound_out++ = static_cast<float>( cos(pi * warped_phase) );
                    warped_phase += fmin(phase_inc / sweep_length, Max_Warp);

                    // Handle fractional warped_phase overshoot after sweep ends
                    if (warped_phase > 1.0) {
                        /* Tricky here: phase and warped may disagree where we are in waveform (due to FM + skew/clip changes).
                         * Warped dominates to keep waveform stable, waveform (flat part) decides where we are.
                         */
                        const double flat_length = midpoint - sweep_length;
                        // warp overshoot scaled to main phase rate
                        const double phase_overshoot = (warped_phase - 1.0) * sweep_length;

                        // phase matches shape
                        phase = midpoint - flat_length + phase_overshoot - phase_inc;

                        // Flat if next samp still not at midpoint
                        if (flat_length >= phase_overshoot) {
                            warped_phase = 1.0;
                            // phase may be > midpoint here (which means actually no flat part),
                            // if so it will be corrected in 2nd half (since warped == 1.0)
                        }
                        else {
                            const double next_sweep_length = fmax(clip * (2.0 - midpoint), min_sweep);
                            warped_phase = 1.0 + (phase_overshoot - flat_length) / next_sweep_length;
                        }
                    }
                }
                else {
                    // flat up to midpoint
                    *sound_out++ = -1.0;
                    warped_phase = 1.0;
                }
            }
            // 2nd half: Sweep up to cos(warped_phase <= 2.pi) then flat +1 until phase >= 2
            else {
                if (warped_phase < 2.0) {
                    const double sweep_length = fmax(clip * (2.0 - midpoint), min_sweep);
                    if (warped_phase == 1.0) {
                        // warped_phase overshoot after flat part
                        warped_phase = 1.0 + fmin( fmin(phase - midpoint, phase_inc) / sweep_length, Max_Warp);
                    }
                    *sound_out++ = static_cast<float>( cos(pi * warped_phase) );
                    warped_phase += fmin(phase_inc / sweep_length, Max_Warp);

                    if (warped_phase > 2.0) {
                        const double flat_length = 2.0 - (midpoint + sweep_length);
                        const double phase_overshoot = (warped_phase - 2.0) * sweep_length;

                        phase = 2.0 - flat_length + phase_overshoot - phase_inc;

                        if (flat_length >= phase_overshoot) {
                            warped_phase = 2.0;
                        }
                        else {
                            const double next_sweep_length = fmax(clip * midpoint, min_sweep);
                            warped_phase = 2.0 + (phase_overshoot - flat_length) / next_sweep_length;
                        }
                    }
                }
                else {
                    *sound_out++ = 1.0;
                    warped_phase = 2.0;
                }
            }
        }

        phase += phase_inc;

        // Phase wraparound
        if (warped_phase >= 2.0 && phase >= 2.0)
        {
            if (hardsync_phase) {
                warped_phase = phase = 0.0;
                hardsync_phase = hardsync_inc = 0.0;

                sync = isAudioRateIn(3) ? find_sync(in(3), i + 1, nSamples) : -1;
            }
            else {
                phase -= 2.0;
                if (phase > phase_inc) {
                    // wild aliasing freq - just reset
                    phase = phase_inc * 0.5;
                }
                if (freq < Max_Warp_Freq) {
                    const double min_sweep = phase_inc * Min_Sweep;
                    const double skew = 1.0 - Clamp(skew_sig[i], -1.0, 1.0);
                    const double clip = 1.0 - Clamp(clip_sig[i], 0.0, 1.0);
                    const double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);
                    const double next_sweep_length = fmax(clip * midpoint, min_sweep);
                    warped_phase = fmin(phase / next_sweep_length, Max_Warp);
                }
                else
                    warped_phase = phase;
            }

            if (sync_out)
                sync_out[i] = 1.0;
        }
    }
}


} // namespace ostinato

PluginLoad(SquineUGens) {
    // Plugin magic
    ft = inTable;
    registerUnit<ostinato::Squine>(ft, "Squine", false);
}
