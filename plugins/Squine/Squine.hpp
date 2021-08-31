// PluginSquine.hpp
// rasmus ekman

#pragma once

#include "SC_PlugIn.hpp"

namespace ostinato {

class Squine : public SCUnit {
public:
    Squine();

    // Destructor
    // ~Squine();

private:
    // Calc function
    void next(int nSamples);

    void init_phase(const double phase_in, const double freq_in, const double clip_in, const double skew_in);
    void hardsync_init(const double freq, const double warped_phase);

    // Member variables
    double clip;
    double skew;
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

} // namespace ostinato
