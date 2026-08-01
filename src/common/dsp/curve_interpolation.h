#pragma once
#include "common/system_primitives.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace DSP {

using Point = ::AutomationPoint;

struct CurveInterpolator {
    static float calculateLinear(float v1, float v2, float phase) {
        return v1 + (v2 - v1) * phase;
    }
    
    static float calculatePower(float v1, float v2, float phase, float exponent, bool easeOut) {
        if (easeOut) {
            return v1 + (v2 - v1) * (1.0f - std::pow(1.0f - phase, exponent));
        }
        return v1 + (v2 - v1) * std::pow(phase, exponent);
    }

    static float calculate(const Point& p1, const Point& p2, uint64_t currentPos) {
        if (p1.curveShape == ::AutomationPoint::Shape::STEP) return p1.value;
        if (p1.curveShape == ::AutomationPoint::Shape::SQUARE) {
            float phase = static_cast<float>(currentPos - p1.positionSample) / 
                          static_cast<float>(p2.positionSample - p1.positionSample);
            return (phase < 0.5f) ? p1.value : p2.value;
        }
        
        float phase = static_cast<float>(currentPos - p1.positionSample) / 
                      static_cast<float>(p2.positionSample - p1.positionSample);
        phase = std::clamp(phase, 0.0f, 1.0f);
                       
        switch (p1.curveShape) {
            case ::AutomationPoint::Shape::LINEAR:
                return calculateLinear(p1.value, p2.value, phase);
                
            case ::AutomationPoint::Shape::EXPONENTIAL: {
                // Tension in [-1.0, 1.0] maps to exponent in [0.2, 5.0]
                float exponent = (p1.tension >= 0.0f) ? (1.0f + 4.0f * p1.tension) : (1.0f / (1.0f - 4.0f * p1.tension));
                return calculatePower(p1.value, p2.value, phase, exponent, false);
            }
            
            case ::AutomationPoint::Shape::EASE_IN: {
                float exponent = 2.0f + 3.0f * std::clamp(p1.tension, 0.0f, 1.0f);
                return calculatePower(p1.value, p2.value, phase, exponent, false);
            }
            
            case ::AutomationPoint::Shape::EASE_OUT: {
                float exponent = 2.0f + 3.0f * std::clamp(p1.tension, 0.0f, 1.0f);
                return calculatePower(p1.value, p2.value, phase, exponent, true);
            }
            
            case ::AutomationPoint::Shape::EASE_IN_OUT: {
                float exponent = 2.0f + 3.0f * std::clamp(p1.tension, 0.0f, 1.0f);
                if (phase < 0.5f) {
                    return calculatePower(p1.value, p1.value + (p2.value - p1.value) * 0.5f, phase * 2.0f, exponent, false);
                } else {
                    return calculatePower(p1.value + (p2.value - p1.value) * 0.5f, p2.value, (phase - 0.5f) * 2.0f, exponent, true);
                }
            }
            
            case ::AutomationPoint::Shape::SINE: {
                float sinePhase = std::sin(phase * 1.5707963f); // sin(phase * pi/2)
                return calculateLinear(p1.value, p2.value, sinePhase);
            }
            
            default:
                return p1.value;
        }
    }
};

} // namespace DSP
