#pragma once
#include "Middle Bridge/timeline/iarrangement_manager_controller.h"
#include "Middle Bridge/project/isession_manager.h"

namespace bridge {

class ArrangementManagerController : public IArrangementManagerController, public ISessionChangeListener {
public:
    explicit ArrangementManagerController(ISessionManager* sessionManager);
    ~ArrangementManagerController() override;

    // IArrangementManagerController overrides
    std::vector<ArrangementInfo> getArrangements() const override;
    ArrangementID getActiveArrangement() const override;
    void setActiveArrangement(ArrangementID id) override;

    ArrangementID createArrangement(const char* name) override;
    void renameArrangement(ArrangementID id, const char* newName) override;
    void deleteArrangement(ArrangementID id) override;
    ArrangementID cloneArrangement(ArrangementID id, const char* cloneName) override;
    void mergeArrangements(
        ArrangementID sourceId,
        ArrangementID destId,
        int mergeMode,
        const MergeFilterOptions& filterOptions
    ) override;

    // ISessionChangeListener overrides
    void onSessionChanging() override;
    void onSessionChanged(composition::IProjectSession* newSession) override;

private:
    ISessionManager* m_sessionManager = nullptr;
    composition::IArrangementManager* getArrangementManager() const;
};

} // namespace bridge
