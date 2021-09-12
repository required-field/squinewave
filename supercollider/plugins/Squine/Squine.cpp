// Squinewave oscillator for Supercollider
// by rasmus ekman

#include "SC_PlugIn.hpp"

static InterfaceTable* ft;

namespace ostinato {

/* ================================================================== */

class Squine : public SCUnit {
public:
    Squine();

private:
    /* Allow either static value, or buffer-rate or audio-rate signal.
     * Buffer-rate values are ramped. (TODO: also smoothing?)
     */
    class input_param
    {
        const float*  host_sig = nullptr;
        double        target = 0;
        double        value = 0;
        double        change = 0;
    public:
        // Or include whatever needed for std::min
        inline int min_val(int x, int y) { return x < y ? x : y; }

         // Called at startup to declare host signal
        void init(const float* host_sig_in, bool is_audiorate)
        {
            if (is_audiorate) {
                host_sig = host_sig_in;
            }
            else {
                host_sig = nullptr;
                value = host_sig_in[0];
            }
        }

       // Called each process block to refresh host signal
        void reinit(const float* host_sig_in, int sample_count)
        {
            if (host_sig) {
                host_sig = host_sig_in;
            }
            else {
                set_target(host_sig_in[0], 1. / sample_count);
            }
        }

        void check_finished() {
            if (change && fabs(value - target) <= fabs(change)) {
                value = target;
                change = 0;
            }
        }

        void set_target(double val, double changerate) {
            target = val;
            change = (target - value) * changerate;
            check_finished();
        }

        double get_next(int n) {
            if (host_sig) {
                value = host_sig[n];
                return value;
            }
            value += change;
            check_finished();
            return value;
        }

        double get_current() const {
            return value;
        }
    };


private:
    // Calc function
    void next(int nSamples);

    void init_phase(const double phase_in, const double freq, const double clip, const double skew);
    void hardsync_init(const double freq, const double warped_phase);

    // Input variables
    input_param freq_param;
    input_param clip_param;
    input_param skew_param;
    // Refresh input each perform buffer?
    bool freq_update;
    bool clip_update;
    bool skew_update;
    bool sync_ar;

    // phase and warped_phase range 0-2. This makes skew/clip into simple proportions
    double phase;
    double warped_phase;
    double hardsync_phase;
    double hardsync_inc;

    // Instance constants inited from environment
    double Min_Sweep;
    double Maxphase_By_sr;
    double Max_Warp_Freq;
    double Max_Sync_Freq;
    double Max_Warp;
};

/* ================================================================== */

// Returns maxval on Inf or NaN
static inline double Clamp(const double x, const double minval, const double maxval) {
    return (x >= minval && x <= maxval) ? x : (x < minval) ? minval : maxval;
}

// No negative freq (and not mirrored either...)
#define GET_FREQ(x) fmax(x, 0.0)
// Inverted to get proportion flat parts
#define GET_CLIP(x) (1.0 - Clamp((x),  0.0, 1.0))
// Rescaled to 0-2, to match phase
#define GET_SKEW(x) (1.0 - Clamp((x), -1.0, 1.0))

/* ================================================================== */

Squine::Squine() {
    const double sr = sampleRate();

    // Get in param rates
    freq_param.init(in(0), isAudioRateIn(0));
    clip_param.init(in(1), isAudioRateIn(1));
    skew_param.init(in(2), isAudioRateIn(2));

    freq_update = isAudioRateIn(0) || isControlRateIn(0);
    clip_update = isAudioRateIn(1) || isControlRateIn(1);
    skew_update = isAudioRateIn(2) || isControlRateIn(2);
    //Print("Update: %i, %i, %i\n", (int)freq_update, (int)clip_update, (int)skew_update);

    sync_ar = isAudioRateIn(3);
    hardsync_phase = hardsync_inc = 0;

    // Allow range 4-sr/100, randomize if below (eg zero or -1)
    Min_Sweep = in0(4);
    if (Min_Sweep < 4 || Min_Sweep > 100) {
        // Random value range 5-15
        if (Min_Sweep < 4)
            Min_Sweep = (int32_t)Clamp(10 * mParent->mRGen->drand() + 5, 5.0, 15);
        else
            Min_Sweep = 100;
        Print("Min_Sweep: %f\n", Min_Sweep);
    }

    Maxphase_By_sr = 2.0 / sr;
    Max_Warp_Freq = sr / (2.0 * Min_Sweep);          // range sr/8 - sr/200
    Max_Sync_Freq = sr / (1.6667 * log(Min_Sweep));  // range sr/2.3 - sr/7.6
    Max_Warp = 1.0 / Min_Sweep;

    // Init phase range 0-2 (which is wraparaound)
    double startphase = in0(5);
    if (startphase) {
        startphase = (startphase < 0 || startphase > 2.0) ? 1.25 : startphase;
        double freq = GET_FREQ(in0(0));
        double clip = GET_CLIP(in0(1));
        double skew = GET_SKEW(in0(2));
        init_phase(startphase, freq, clip, skew);
    }


    mCalcFunc = make_calc_function<Squine, &Squine::next>();
    next(1);
}

/* ================================================================== */

// Set main phase so it matches warp
void Squine::init_phase(const double phase_in, const double freq, const double clip, const double skew) {
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

    if (freq > this->Max_Sync_Freq)
        return;

    this->hardsync_inc = (pi / this->Min_Sweep);
    this->hardsync_phase = this->hardsync_inc * 0.5;
}

/* ================================================================== */

void Squine::next(int nSamples) {
    // Get next input buffer (or kr value)
    if (freq_update)
        freq_param.reinit(in(0), nSamples);
    if (clip_update)
        clip_param.reinit(in(1), nSamples);
    if (skew_update)
        skew_param.reinit(in(2), nSamples);

    // Look for sync if a-rate
    int32_t sync = sync_ar ? find_sync(in(3), 0, nSamples) : -1;

    float* const sound_out = out(0);
  /* float* const sync_out = (numOutputs() > 1) ? out(1) : nullptr;
    if (sync_out) {
        memset(sync_out, 0, nSamples * sizeof(float));
    } */

    for (int32_t i = 0; i < nSamples; ++i) {
        // Annoying switch on update rate :(
        double freq = GET_FREQ(freq_param.get_next(i));
        double clip = GET_CLIP(clip_param.get_next(i));
        double skew = GET_SKEW(skew_param.get_next(i));

        // hardsync requested?
        if (i == sync) {
            hardsync_init(freq, warped_phase);
        }

        // hardsync ongoing? Increase freq until wraparound
        if (hardsync_phase) {
            const double syncsweep = 0.5 * (1.0 - cos(hardsync_phase));
            freq += syncsweep * (Max_Sync_Freq - freq);
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
                warped_phase = phase = 0.0;
                hardsync_phase = hardsync_inc = 0.0;

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
