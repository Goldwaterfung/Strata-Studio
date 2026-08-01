#pragma once
#include "musical_composition/musical_primitives.h"

namespace composition {

struct QuantizeSettings {
    uint16_t gridResolutionTicks; // e.g., 240 for a 16th note at 960 PPQN
    float strength;               // 0.0 to 1.0 (1.0 = strict snap)
    bool quantizeEnds;            // Snap note-offs as well?
    int swingPercentage;          // E.g., 50 for straight, 60 for swing
};

struct Quantizer {
    /**
     * @brief Applies quantization grid to a note.
     * @param note The input note to quantize
     * @param settings The quantization grid settings
     * @param tempoMap Pointer to the tempo map for sample <-> tick conversions
     * @return The newly aligned MIDINote
     */
    static MIDINote apply(
        const MIDINote& note, 
        const QuantizeSettings& settings, 
        const void* tempoMap // Injected ITempoMap interface
    );
};

} // namespace composition
