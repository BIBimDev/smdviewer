#pragma once
#include "model.hpp"
#include <Quantity_Color.hxx>

namespace inspector {
// Linear RGB defaults, matching the original filled B/A/R colours. sRGB
// settings convert at the UI boundary, never reinterpret
// these components as sRGB. Wire accents are derived from these same colours.
struct Palette {
    Quantity_Color b{0.57,0.59,0.61,Quantity_TOC_RGB};
    Quantity_Color a{0.32,0.34,0.36,Quantity_TOC_RGB};
    Quantity_Color r{0.72,0.52,0.53,Quantity_TOC_RGB};
    Quantity_Color surface(Representation representation) const {
        switch(representation) {
        case Representation::Candidate: return b;
        case Representation::Extensions: return a;
        case Representation::Reductions: return r;
        default: return {0.60,0.72,0.85,Quantity_TOC_RGB};
        }
    }
    Quantity_Color wire(Representation representation) const {
        const auto color=surface(representation);
        // A small lift keeps dark concrete legible against the viewport without
        // losing the B/A grey ordering or R's muted red hue.
        return {0.8*color.Red()+0.2,0.8*color.Green()+0.2,0.8*color.Blue()+0.2,Quantity_TOC_RGB};
    }
};
inline const Palette factoryPalette{};
inline constexpr double factoryTransparency=0.72;
struct Appearance {
    Palette palette=factoryPalette;
    double transparency=factoryTransparency;
};
}
