#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ioline::branding
{
// Approved ioline mark, matching Assets/icon.svg. Parsed once for all paint calls.
inline void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    static constexpr char svg[] = R"ioline(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30" color="#ffa452" role="img" aria-label="humline"><rect x=".6" y=".6" width="28.8" height="28.8" rx="8" fill="#26262a" stroke="#ffa452" stroke-opacity=".85" stroke-width="1.2"/><path d="M10 7v16M20 7v16" stroke="#e8e8ec" stroke-width="2" stroke-linecap="round"/><path d="M5 15h20" stroke="#ffa452" stroke-width="2" stroke-linecap="round"/></svg>)ioline";
    static const auto drawable = juce::Drawable::createFromImageData (svg, sizeof (svg) - 1);
    if (drawable != nullptr)
        drawable->drawWithin (g, bounds, juce::RectanglePlacement::stretchToFit, 1.0f);
}
} // namespace ioline::branding
