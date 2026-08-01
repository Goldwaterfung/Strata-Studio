#include "Middle Bridge/timeline/arrangement_manager_controller.h"
#include "musical_composition/interfaces/iarrangement_manager.h"
#include "musical_composition/project_session/iproject_session.h"

namespace bridge {

ArrangementManagerController::ArrangementManagerController(ISessionManager* sessionManager)
    : m_sessionManager(sessionManager)
{
    if (m_sessionManager) {
        m_sessionManager->registerChangeListener(this);
    }
}

ArrangementManagerController::~ArrangementManagerController()
{
    if (m_sessionManager) {
        m_sessionManager->unregisterChangeListener(this);
    }
}

composition::IArrangementManager* ArrangementManagerController::getArrangementManager() const
{
    if (!m_sessionManager) return nullptr;
    auto* session = m_sessionManager->getActiveSession();
    if (!session) return nullptr;
    return session->getArrangementManager();
}

std::vector<ArrangementInfo> ArrangementManagerController::getArrangements() const
{
    auto* mgr = getArrangementManager();
    if (!mgr) return {};
    return mgr->getArrangements();
}

ArrangementID ArrangementManagerController::getActiveArrangement() const
{
    auto* mgr = getArrangementManager();
    if (!mgr) return ArrangementID::invalid();
    return mgr->getActiveArrangement();
}

void ArrangementManagerController::setActiveArrangement(ArrangementID id)
{
    auto* mgr = getArrangementManager();
    if (!mgr) return;
    mgr->setActiveArrangement(id);
    
    // Changing active arrangement requires a session refresh so that
    // track lists, playlists and viewports are reloaded for the active scene.
    if (m_sessionManager) {
        m_sessionManager->triggerSessionRefresh();
    }
}

ArrangementID ArrangementManagerController::createArrangement(const char* name)
{
    auto* mgr = getArrangementManager();
    if (!mgr) return ArrangementID::invalid();
    ArrangementID newId = mgr->createArrangement(name);
    // Auto switch to newly created arrangement
    if (newId.isValid()) {
        setActiveArrangement(newId);
    }
    return newId;
}

void ArrangementManagerController::renameArrangement(ArrangementID id, const char* newName)
{
    auto* mgr = getArrangementManager();
    if (!mgr) return;
    mgr->renameArrangement(id, newName);
    
    // Refresh session to update UI name labels
    if (m_sessionManager) {
        m_sessionManager->triggerSessionRefresh();
    }
}

void ArrangementManagerController::deleteArrangement(ArrangementID id)
{
    auto* mgr = getArrangementManager();
    if (!mgr) return;
    mgr->deleteArrangement(id);
    
    // Refresh session to reload tracks and views for the new active arrangement
    if (m_sessionManager) {
        m_sessionManager->triggerSessionRefresh();
    }
}

ArrangementID ArrangementManagerController::cloneArrangement(ArrangementID id, const char* cloneName)
{
    auto* mgr = getArrangementManager();
    if (!mgr) return ArrangementID::invalid();
    ArrangementID cloneId = mgr->cloneArrangement(id, cloneName);
    if (cloneId.isValid()) {
        setActiveArrangement(cloneId);
    }
    return cloneId;
}

void ArrangementManagerController::mergeArrangements(
    ArrangementID sourceId,
    ArrangementID destId,
    int mergeMode,
    const MergeFilterOptions& filterOptions
) {
    auto* mgr = getArrangementManager();
    if (!mgr) return;
    mgr->mergeArrangements(sourceId, destId, mergeMode, filterOptions);
    
    // Refresh session to reload tracks and views after merging timeline data
    if (m_sessionManager) {
        m_sessionManager->triggerSessionRefresh();
    }
}

void ArrangementManagerController::onSessionChanging()
{
}

void ArrangementManagerController::onSessionChanged(composition::IProjectSession* /*newSession*/)
{
}

} // namespace bridge
