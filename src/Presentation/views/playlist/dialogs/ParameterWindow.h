// src/Presentation/views/playlist/dialogs/ParameterWindow.h
#pragma once

#include <QDialog>
#include <QWidget>
#include <functional>
#include "common/system_primitives.h"

class QListWidget;
class QLineEdit;

namespace bridge {
class ITrackController;
class IAutomationController;
}

namespace presentation::views {

// Custom item widget representing a single parameter entry with an "ADD LANE" button
class ParameterListItemWidget : public QWidget {
    Q_OBJECT
public:
    ParameterListItemWidget(
        const QString& name, 
        bool alreadyAdded, 
        std::function<void()> onAddCallback, 
        std::function<void()> onRemoveCallback,
        QWidget* parent = nullptr
    );
};

class ParameterWindow : public QDialog {
    Q_OBJECT
public:
    ParameterWindow(
        TrackID trackId,
        bridge::ITrackController* trackController,
        bridge::IAutomationController* automationController,
        QWidget* parent = nullptr
    );

    ~ParameterWindow() override;

signals:
    void laneAdded();

private Q_SLOTS:
    void onSearchTextChanged(const QString& text);

private:
    void populateList();

    TrackID m_trackId;
    bridge::ITrackController* m_trackController{nullptr};
    bridge::IAutomationController* m_automationController{nullptr};

    QLineEdit* m_searchBox{nullptr};
    QListWidget* m_listWidget{nullptr};
};

} // namespace presentation::views
