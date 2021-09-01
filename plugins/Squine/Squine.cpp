// PluginSquine.cpp
// by rasmus ekman

#include "SC_PlugIn.hpp"

static InterfaceTable* ft;

namespace ostinato {

/* ================================================================== */

class Squine : public SCUnit {
public:
    Squine();

private:
    // Calc function
    void next(int nSamples);

    void init_phase(const double phase_in);
    void hardsync_init(const double freq, const double warped_phase);

    // Member variables
    double freq;
    double clip;
    double skew;

    // Supercollider specials
    double non_sync_freq;  // Used iff kr freq and hardsync
    // Store audiorate state locally to save on pointer dereferences
    bool freq_ar, clip_ar, skew_ar, sync_ar;
    bool freq_kr, clip_kr, skew_kr;

    // phase and warped_phase range 0-2. This makes skew/clip into simple proportions
    double phase;
    double warped_phase;
    double hardsync_phase;
    double hardsync_inc;

    // Instance constants inited from environment
    double Min_Sweep;
    double Maxphase_By_sr;
    double Max_Warp_Freq;
    double Max_Warp;
};

/* ================================================================== */

// Returns maxval on Inf or NaN
static inline double Clamp(const double x, const double minval, const double maxval) {
    return (x >= minval && x <= maxval) ? x : (x < minval) ? minval : maxval;
}

/* ================================================================== */

Squine::Squine() {
    const double sr = sampleRate();

    // Read here in case static value
    freq = non_sync_freq = in0(0);
    clip = 1.0 - Clamp(in0(1), -1.0, 1.0);
    skew = 1.0 - Clamp(in0(2), 0.0, 1.0);

    freq_ar = isAudioRateIn(0);
    clip_ar = isAudioRateIn(1);
    skew_ar = isAudioRateIn(2);
    freq_kr = isControlRateIn(0);
    clip_kr = isControlRateIn(1);
    skew_kr = isControlRateIn(2);
    //Print("AR: %i, %i, %i; KR: %i, %i, %i\n", (int)freq_ar, (int)clip_ar, (int)skew_ar, (int)freq_kr, (int)clip_kr, (int)skew_kr);

    sync_ar = isAudioRateIn(3);
    hardsync_phase = hardsync_inc = 0;

    Min_Sweep = in0(4);
    // Allow range 4-sr/100
    if (Min_Sweep < 4.0 || Min_Sweep > sr * 0.01) {
        Min_Sweep = (int32_t)Clamp(10 * mParent->mRGen->drand() + 5, 5.0, 15);
        //Min_Sweep = (int32_t)Clamp(sr / 3000.0, 5.0, sr * 0.01);
        Print("Min_Sweep: %f\n", Min_Sweep);
    }

    Maxphase_By_sr = 2.0 / sr;
    Max_Warp_Freq = sr / (2.0 * Min_Sweep);
    Max_Warp = 1.0 / Min_Sweep;

    //Print("freq, %f, clip: %f, skew: %f, sync? %i, sweep: %f, phase: %f\n", freq, clip, skew, (int)sync_ar, in0(4), in0(5));

    double startphase = in0(5);
    if (startphase) {
        startphase = (startphase < 0) ? 1.25 : startphase;
        init_phase(startphase);
        //Print("initphase: %f => phase: %f, warped: %f\n", startphase, phase, warped_phase);
    }

    mCalcFunc = make_calc_function<Squine, &Squine::next>();
    next(1);
}

/* ================================================================== */

// Set main phase so it matches warp
void Squine::init_phase(const double phase_in) {
    const double phase_inc = Maxphase_By_sr * freq;
    const double min_sweep = phase_inc * Min_Sweep;
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

static inline int32_t find_sync(const float* sync_sig, const int32_t first, const int32_t last)
{
    for (int32_t i = first; i < last; ++i) {
        if (sync_sig[i] >= 1.0)
            return i;
    }
    return -1;
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
    const float* const freq_sig = freq_ar ? in(0) : nullptr;
    const float* const clip_sig = clip_ar ? in(1) : nullptr;
    const float* const skew_sig = skew_ar ? in(2) : nullptr;

    // Look for sync if a-rate
    int32_t sync = sync_ar ? find_sync(in(3), 0, nSamples) : -1;

    float* const sound_out = out(0);
  /* float* const sync_out = (numOutputs() > 1) ? out(1) : nullptr;
    if (sync_out) {
        memset(sync_out, 0, nSamples * sizeof(float));
    } */

    double freq_inc = freq_kr ? (in0(0) - freq) / nSamples : 0;
    double clip_inc = clip_kr ? ( (1.0 - Clamp(in0(1),  0.0, 1.0)) - clip ) / nSamples : 0;
    double skew_inc = skew_kr ? ( (1.0 - Clamp(in0(2), -1.0, 1.0)) - skew ) / nSamples : 0;

    for (int32_t i = 0; i < nSamples; ++i) {
        // KR values pre-inc'd monotonously here; AR values updated as needed below
        if (freq_kr) {
            freq += freq_inc;
            non_sync_freq = freq;
        }
        else if (freq_ar)
            freq = non_sync_freq = fmax(freq_sig[i], 0.0);
        if (clip_kr)
            clip = Clamp(clip + clip_inc, 0.0, 1.0);
        if (skew_kr)
            skew = Clamp(skew + skew_inc, -1.0, 1.0);

        // hardsync requested?
        if (i == sync) {
            //Print("sync: %i ", sync);
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
            sound_out[i] = static_cast<float>( cos(pi * warped_phase) );
            phase = warped_phase;
            warped_phase += phase_inc;
        }
        else {
            const double min_sweep = phase_inc * Min_Sweep;
            //const double clip = 1.0 - Clamp(clip_sig[i], 0.0, 1.0);
            //const double skew = 1.0 - Clamp(skew_sig[i], -1.0, 1.0);
            if (clip_ar)
                clip = 1.0 - Clamp(clip_sig[i], 0.0, 1.0);
            if (skew_ar)
                skew = 1.0 - Clamp(skew_sig[i], -1.0, 1.0);
            const double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);

            // 1st half: Sweep down to cos(warped_phase <= pi) then flat -1 until phase >= midpoint
            if (warped_phase < 1.0 || (warped_phase == 1.0 && phase < midpoint))
            {
                if (warped_phase < 1.0) {
                    const double sweep_length = fmax(clip * midpoint, min_sweep);

                    sound_out[i] = static_cast<float>( cos(pi * warped_phase) );
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
                    sound_out[i] = -1.0;
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
                    sound_out[i] = static_cast<float>( cos(pi * warped_phase) );
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
                    sound_out[i] = 1.0;
                    warped_phase = 2.0;
                }
            }
        }

        phase += phase_inc;

        // Phase wraparound
        if (warped_phase >= 2.0 && phase >= 2.0)
        {
            if (hardsync_phase) {
                //Print("sync end: %i\n", i);
                warped_phase = phase = 0.0;
                hardsync_phase = hardsync_inc = 0.0;
                freq = non_sync_freq;

                sync = find_sync(in(3), i, nSamples);
            }
            else {
                phase -= 2.0;
                if (phase > phase_inc) {
                    // wild aliasing freq - just reset
                    phase = phase_inc * 0.5;
                }
                if (freq < Max_Warp_Freq) {
                    const double min_sweep = phase_inc * Min_Sweep;
                    //const double clip = 1.0 - Clamp(clip_sig[i], 0.0, 1.0);
                    //const double skew = 1.0 - Clamp(skew_sig[i], -1.0, 1.0);
                    if (clip_ar)
                        clip = 1.0 - Clamp(clip_sig[i], 0.0, 1.0);
                    if (skew_ar)
                        skew = 1.0 - Clamp(skew_sig[i], -1.0, 1.0);
                    const double midpoint = Clamp(skew, min_sweep, 2.0 - min_sweep);
                    const double next_sweep_length = fmax(clip * midpoint, min_sweep);
                    warped_phase = fmin(phase / next_sweep_length, Max_Warp);
                }
                else
                    warped_phase = phase;
            }

            //if (sync_out)
            //    sync_out[i] = 1.0;
        }
    }
}


} // namespace ostinato

PluginLoad(SquineUGens) {
    // Plugin magic
    ft = inTable;
    registerUnit<ostinato::Squine>(ft, "Squine", false);
}
