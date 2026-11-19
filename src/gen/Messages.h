#pragma once
#include "../dsp/Common.h"

namespace gx
{
// "AI is creating your sound" console lines: 'count' random lines (never repeating back-to-back),
// followed by a category-specific final line. Bank: 42 generic + 12 per category, ES and EN.
juce::StringArray aiMessages (const juce::String& category, bool english, Rng& rng, int count, juce::String& lastShown);
int numMessagesFor (const juce::String& category, bool english); // >= 54 for every category
}
