#pragma once
#include "ikey_signature_map.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <mutex>
#include <vector>

namespace composition {

class KeySignatureMapImpl : public IKeySignatureMap {
public:
    explicit KeySignatureMapImpl(ICommandHistory* history);
    ~KeySignatureMapImpl() override = default;

    void addKeySignature(uint64_t positionSample, PitchClass root, KeyType type, bool pushDelta = true) override;
    void removeKeySignature(uint64_t positionSample, bool pushDelta = true) override;
    bool getKeySignatureAt(uint64_t positionSample, PitchClass& outRoot, KeyType& outType) const override;
    const std::vector<KeySignaturePoint>& getEvents() const override { return events_; }
    void setEvents(const std::vector<KeySignaturePoint>& events) override;
    void clear() override;

    // For Undo/Redo delta application
    void applyDelta(const ProjectDelta& delta, bool isUndo);

private:
    ICommandHistory* history_;
    std::vector<KeySignaturePoint> events_;
    mutable std::mutex mutex_;

    void addKeySignatureInternal(uint64_t positionSample, PitchClass root, KeyType type);
    void removeKeySignatureInternal(uint64_t positionSample);
};

} // namespace composition
