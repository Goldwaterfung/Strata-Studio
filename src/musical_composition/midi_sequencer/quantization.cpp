#include "quantization.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include <cmath>

namespace composition {

MIDINote Quantizer::apply(
    const MIDINote& note, 
    const QuantizeSettings& settings, 
    const void* tempoMap
) {
    if (!tempoMap) return note;

    const auto* tempo = static_cast<const ITempoService*>(tempoMap);
    MIDINote result = note;

    auto quantizeSample = [&](uint64_t sample, MusicalPosition& outPos) -> uint64_t {
        BBTPosition bbt = tempo->samplesToBBT(sample);
        uint32_t ticksPerBeat = tempo->getTicksPerBeat();
        
        uint32_t originalTick = bbt.tick;
        float grid = static_cast<float>(settings.gridResolutionTicks);
        uint32_t quantizedTick = static_cast<uint32_t>(std::round(static_cast<float>(originalTick) / grid) * grid);
        
        if (settings.strength < 1.0f) {
            float weightedTick = static_cast<float>(originalTick) * (1.0f - settings.strength) + 
                                static_cast<float>(quantizedTick) * settings.strength;
            quantizedTick = static_cast<uint32_t>(std::round(weightedTick));
        }

        if (quantizedTick >= ticksPerBeat) {
            uint8_t num, den;
            if (tempo->getMeterAtPosition(sample, num, den)) {
                bbt.tick = quantizedTick % ticksPerBeat;
                bbt.beat += quantizedTick / ticksPerBeat;
                while (bbt.beat > num) { bbt.beat -= num; bbt.bar++; }
            } else {
                bbt.tick = quantizedTick;
            }
        } else {
            bbt.tick = quantizedTick;
        }

        uint64_t quantizedSample = tempo->bbtToSamples(bbt);
        
        outPos.bar = bbt.bar;
        outPos.beat = static_cast<uint16_t>(bbt.beat);
        outPos.tick = bbt.tick;
        outPos.totalTicks = 0; 
        
        return quantizedSample;
    };

    result.offsetSample = quantizeSample(note.offsetSample, result.startPosition);
    
    if (settings.quantizeEnds) {
        uint64_t endSample = note.offsetSample + note.durationSample;
        uint64_t quantizedEnd = quantizeSample(endSample, result.endPosition);
        result.durationSample = (quantizedEnd > result.offsetSample) ? (quantizedEnd - result.offsetSample) : 0;
    } else {
        uint64_t endSample = note.offsetSample + note.durationSample;
        BBTPosition endBbt = tempo->samplesToBBT(endSample);
        result.endPosition.bar = endBbt.bar;
        result.endPosition.beat = static_cast<uint16_t>(endBbt.beat);
        result.endPosition.tick = endBbt.tick;
        result.durationSample = note.durationSample;
    }

    return result;
}

} // namespace composition
