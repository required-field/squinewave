// PluginSquine.cpp
// rasmus (nevermind@dontcare.se)

#include "SC_PlugIn.hpp"
#include "Squine.hpp"

static InterfaceTable* ft;

namespace Squine {

Squine::Squine() {
    mCalcFunc = make_calc_function<Squine, &Squine::next>();
    next(1);
}

void Squine::next(int nSamples) {
    const float* input = in(0);
    const float* gain = in(1);
    float* outbuf = out(0);

    // simple gain function
    for (int i = 0; i < nSamples; ++i) {
        outbuf[i] = input[i] * gain[i];
    }
}

} // namespace Squine

PluginLoad(SquineUGens) {
    // Plugin magic
    ft = inTable;
    registerUnit<Squine::Squine>(ft, "Squine", false);
}
