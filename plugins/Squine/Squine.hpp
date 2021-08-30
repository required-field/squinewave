// PluginSquine.hpp
// rasmus (nevermind@dontcare.se)

#pragma once

#include "SC_PlugIn.hpp"

namespace Squine {

class Squine : public SCUnit {
public:
    Squine();

    // Destructor
    // ~Squine();

private:
    // Calc function
    void next(int nSamples);

    // Member variables
};

} // namespace Squine
