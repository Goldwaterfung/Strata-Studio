// src/Middle Bridge/isession_manager.h
#pragma once

#include "musical_composition/project_session/iproject_session.h"
#include "common/system_primitives.h"
#include "Core infrastructure/bridges/spsc_queue.h"

namespace Layer2 { class ITempoService; class IStringRegistry; }

namespace MediaManagement { class IDiskWriterService; class ICodecFactory; }

namespace bridge {

/**
 * @brief Observer interface for subsystems that must react to session changes
 */
class ISessionChangeListener {
public:
    virtual ~ISessionChangeListener() = default;
    
    /**
     * @brief Triggered BEFORE the current project session is destroyed.
     * Use this to clear GUI widgets, temporary handles, and cached states.
     */
    virtual void onSessionChanging() = 0;

    /**
     * @brief Triggered AFTER the new project session has been created and wired.
     * @param newSession The new active project session, or nullptr if closed.
     */
    virtual void onSessionChanged(composition::IProjectSession* newSession) = 0;
};

/**
 * @brief Central authority managing the lifetime and retrieval of the active project session.
 */
class ISessionManager {
public:
    virtual ~ISessionManager() = default;

    /**
     * @brief Retrieves the currently active project session.
     * @return Pointer to active session, or nullptr if no session is active.
     */
    virtual composition::IProjectSession* getActiveSession() const = 0;

    /**
     * @brief Atomically replaces the current active session with a new one.
     * Automatically triggers observer notifications for teardown and setup.
     * @param newSession The new project session instance.
     */
    virtual void setActiveSession(std::unique_ptr<composition::IProjectSession> newSession) = 0;

    /**
     * @brief Closes the active session and resets state.
     */
    virtual void closeActiveSession() = 0;

    // --- Observer Registration ---
    virtual void registerChangeListener(ISessionChangeListener* listener) = 0;
    virtual void unregisterChangeListener(ISessionChangeListener* listener) = 0;

    /**
     * @brief Refreshes the active session notifications, forcing observers to reload their caches.
     */
    virtual void triggerSessionRefresh() = 0;

    /**
     * @brief Called after any modification to the TempoMap (setBPM, addTempoPoint, etc.).
     *        Triggers cache recalculation on all playlists and MIDI sequencers in the active
     *        session so that sample positions derived from BBT remain accurate.
     * @param tempoService The updated TempoService to use for recalculation.
     */
    virtual void onTempoMapChanged(Layer2::ITempoService* tempoService) = 0;

    virtual Layer2::IStringRegistry* getStringRegistry() const = 0;
};

} // namespace bridge
