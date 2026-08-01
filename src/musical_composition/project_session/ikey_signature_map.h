#pragma once
#include <cstdint>
#include <vector>
#include <type_traits>

namespace composition {

enum class KeyType : uint8_t {
    Major = 0,
    Minor = 1
};

enum class PitchClass : uint8_t {
    C = 0, C_sharp = 1, D = 2, D_sharp = 3, E = 4, F = 5,
    F_sharp = 6, G = 7, G_sharp = 8, A = 9, A_sharp = 10, B = 11
};

struct KeySignaturePoint {
    uint64_t positionSample;
    PitchClass rootNote;
    KeyType type;
    uint8_t reserved[6]; // Padding for 16-byte alignment
};

static_assert(sizeof(KeySignaturePoint) == 16, "KeySignaturePoint must be exactly 16 bytes");
static_assert(std::is_pod<KeySignaturePoint>::value, "KeySignaturePoint must be Plain Old Data");

class IKeySignatureMap {
public:
    virtual ~IKeySignatureMap() = default;

    virtual void addKeySignature(uint64_t positionSample, PitchClass root, KeyType type, bool pushDelta = true) = 0;
    virtual void removeKeySignature(uint64_t positionSample, bool pushDelta = true) = 0;
    virtual bool getKeySignatureAt(uint64_t positionSample, PitchClass& outRoot, KeyType& outType) const = 0;
    virtual const std::vector<KeySignaturePoint>& getEvents() const = 0;
    virtual void setEvents(const std::vector<KeySignaturePoint>& events) = 0;
    virtual void clear() = 0;
};

} // namespace composition
