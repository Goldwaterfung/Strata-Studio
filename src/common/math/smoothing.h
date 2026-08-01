#pragma once

#include <cmath>
#include "primitives.h"

namespace Math {

/**
 * @brief 1-pole IIR Smoothing filter for parameter automation.
 */
class ParameterSmoother {
public:
    ParameterSmoother() = default;

    /**
     * @brief Initializes the smoother.
     */
    void init(float initialValue, float timeMs, float sampleRate) {
        _currentValue = initialValue;
        _targetValue = initialValue;
        updateAlpha(timeMs, sampleRate);
    }

    /**
     * @brief Resets the smoother to an initial value.
     */
    void reset(float initialValue, float sampleRate, float timeMs) {
        init(initialValue, timeMs, sampleRate);
    }

    /**
     * @brief Updates the sample rate while keeping the time constant.
     */
    void setSampleRate(float sampleRate) {
        // Re-calculate alpha with the current time constant
        // We need to store timeMs to do this perfectly, 
        // but for now we'll assume a standard 10ms if not stored.
        // Actually, let's just use the current alpha if we don't have timeMs.
        // Or we can store timeMs.
        updateAlpha(_timeMs, sampleRate);
    }

    /**
     * @brief Updates the smoothing coefficient based on the time constant.
     * 
     * @param timeMs The time in milliseconds it takes to reach ~63% of the target.
     * @param sampleRate The system sample rate.
     */
    void updateAlpha(float timeMs, float sampleRate) {
        _timeMs = timeMs;
        if (timeMs <= 0.0f) {
            _alpha = 0.0f; // No smoothing
            return;
        }
        
        // alpha = e^(-1 / (T * fs))
        float timeSeconds = timeMs * 0.001f;
        _alpha = std::exp(-1.0f / (timeSeconds * sampleRate));
    }

    /**
     * @brief Sets a new target value.
     */
    void setTarget(float targetValue) {
        _targetValue = targetValue;
        _rampSamplesRemaining = 0; // Cancel any active ramp
    }

    /**
     * @brief Sets a new target value with a specific linear ramp duration.
     * 
     * @param targetValue The goal value.
     * @param rampSamples Duration in samples.
     */
    void setTarget(float targetValue, uint32_t rampSamples) {
        if (rampSamples == 0) {
            setTarget(targetValue);
            return;
        }
        
        _targetValue = targetValue;
        _rampSamplesRemaining = rampSamples;
        _rampStep = (_targetValue - _currentValue) / static_cast<float>(rampSamples);
    }

    /**
     * @brief Processes one sample and returns the smoothed value.
     */
    inline float process(float targetValue) {
        setTarget(targetValue);
        return next();
    }

    /**
     * @brief Processes one sample and returns the smoothed value.
     */
    inline float process() {
        return next();
    }

    /**
     * @brief Processes one sample and returns the smoothed value.
     * Handles both IIR smoothing and linear ramps.
     */
    inline float next() {
        if (_rampSamplesRemaining > 0) {
            _currentValue += _rampStep;
            _rampSamplesRemaining--;
            
            if (_rampSamplesRemaining == 0) {
                _currentValue = _targetValue;
            }
        } else {
            // IIR Formula: y[n] = target + alpha * (y[n-1] - target)
            _currentValue = _targetValue + _alpha * (_currentValue - _targetValue);
            if (std::abs(_currentValue - _targetValue) < 1e-6f) {
                _currentValue = _targetValue;
            }
        }
        return _currentValue;
    }

    /**
     * @brief Gets the current smoothed value.
     */
    float getCurrent() const { return _currentValue; }

    /**
     * @brief Checks if the smoothing has effectively reached the target.
     */
    bool isStatic() const {
        if (_rampSamplesRemaining > 0) return false;
        return std::abs(_currentValue - _targetValue) < 1e-6f;
    }

    /**
     * @brief Instantly snaps the current value to the target value.
     */
    void fastForward() {
        _currentValue = _targetValue;
        _rampSamplesRemaining = 0;
    }

private:
    float _currentValue = 0.0f;
    float _targetValue = 0.0f;
    float _alpha = 0.0f;
    float _timeMs = 10.0f;
    
    // Linear Ramp Support
    uint32_t _rampSamplesRemaining = 0;
    float _rampStep = 0.0f;
};

/**
 * @brief Simple linear ramp for fixed-duration transitions (e.g., Mute/Solo).
 */
class LinearRamp {
public:
    LinearRamp() = default;

    void init(float initialValue, uint32_t durationSamples) {
        _currentValue = initialValue;
        _targetValue = initialValue;
        _duration = durationSamples;
        _step = 0.0f;
    }

    void setTarget(float targetValue) {
        if (targetValue == _targetValue) return;
        _targetValue = targetValue;
        _step = (_targetValue - _currentValue) / static_cast<float>(_duration);
    }

    inline float next() {
        if (_currentValue != _targetValue) {
            _currentValue += _step;
            
            // Snap to target to avoid precision drift
            if ((_step > 0.0f && _currentValue > _targetValue) ||
                (_step < 0.0f && _currentValue < _targetValue)) {
                _currentValue = _targetValue;
                _step = 0.0f;
            }
        }
        return _currentValue;
    }

    float getCurrent() const { return _currentValue; }
    bool isStatic() const { return _currentValue == _targetValue; }

    /**
     * @brief Instantly snaps the current value to the target value.
     */
    void fastForward() {
        _currentValue = _targetValue;
        _step = 0.0f;
    }

private:
    float _currentValue = 0.0f;
    float _targetValue = 0.0f;
    float _step = 0.0f;
    uint32_t _duration = 64;
};

} // namespace Math
