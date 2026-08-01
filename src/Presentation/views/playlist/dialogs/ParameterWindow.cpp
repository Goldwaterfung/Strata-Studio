// src/Presentation/views/playlist/dialogs/ParameterWindow.cpp
#include "ParameterWindow.h"
#include "tracks/itrack_controller.h"
#include "automation/iautomation_controller.h"
#include "theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QListWidgetItem>

namespace presentation::views {

ParameterListItemWidget::ParameterListItemWidget(
    const QString& name, 
    bool alreadyAdded, 
    std::function<void()> onAddCallback, 
    std::function<void()> onRemoveCallback,
    QWidget* parent
) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    auto* label = new QLabel(name, this);
    label->setStyleSheet("color: #E1E2E6; font-size: 11px; font-weight: normal;");
    layout->addWidget(label);
    layout->addStretch();

    auto* btn = new QPushButton(this);
    btn->setFixedWidth(80);
    btn->setFixedHeight(22);
    btn->setFont(theme::Font::monospace(6, QFont::Bold));

    auto updateToAddState = [btn]() {
        btn->setText("ADD LANE");
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #2E3440;"
            "  color: #88C0D0;"
            "  border: 1px solid #4C566A;"
            "  border-radius: 4px;"
            "  padding: 0 4px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #88C0D0;"
            "  color: #2E3440;"
            "  border-color: #88C0D0;"
            "}"
        );
    };

    auto updateToRemoveState = [btn]() {
        btn->setText("REMOVE");
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(255, 102, 102, 0.1);"
            "  color: #FF6666;"
            "  border: 1px solid #FF6666;"
            "  border-radius: 4px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #FF6666;"
            "  color: #2E3440;"
            "}"
        );
    };

    auto isAddedState = std::make_shared<bool>(alreadyAdded);

    if (*isAddedState) {
        updateToRemoveState();
    } else {
        updateToAddState();
    }

    connect(btn, &QPushButton::clicked, this, [isAddedState, updateToAddState, updateToRemoveState, onAddCallback, onRemoveCallback]() {
        if (*isAddedState) {
            onRemoveCallback();
            *isAddedState = false;
            updateToAddState();
        } else {
            onAddCallback();
            *isAddedState = true;
            updateToRemoveState();
        }
    });

    layout->addWidget(btn);
}

ParameterWindow::ParameterWindow(
    TrackID trackId,
    bridge::ITrackController* trackController,
    bridge::IAutomationController* automationController,
    QWidget* parent
) : QDialog(parent),
    m_trackId(trackId),
    m_trackController(trackController),
    m_automationController(automationController)
{
    setWindowTitle(tr("Configure Automation Lanes"));
    setModal(true);
    resize(360, 480);
    setStyleSheet("background-color: #1E222A; border: 1px solid #2B303C; border-radius: 8px;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Search Box
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Search parameters..."));
    m_searchBox->setFixedHeight(28);
    m_searchBox->setStyleSheet(
        "QLineEdit {"
        "  background-color: #252932;"
        "  color: #FFFFFF;"
        "  border: 1px solid #3B4252;"
        "  border-radius: 4px;"
        "  padding-left: 8px;"
        "  font-size: 11px;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #88C0D0;"
        "}"
    );
    connect(m_searchBox, &QLineEdit::textChanged, this, &ParameterWindow::onSearchTextChanged);
    mainLayout->addWidget(m_searchBox);

    // List View
    m_listWidget = new QListWidget(this);
    m_listWidget->setStyleSheet(
        "QListWidget {"
        "  background-color: #1A1C23;"
        "  border: 1px solid #2B303C;"
        "  border-radius: 4px;"
        "}"
    );
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    mainLayout->addWidget(m_listWidget);

    // Close Button
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("CLOSE"), this);
    closeBtn->setFixedWidth(100);
    closeBtn->setFixedHeight(28);
    closeBtn->setFont(theme::Font::monospace(8, QFont::Bold));
    closeBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #3B4252;"
        "  color: #E5E9F0;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #4C566A;"
        "}"
    );
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    populateList();
}

ParameterWindow::~ParameterWindow() = default;

void ParameterWindow::onSearchTextChanged(const QString& /*text*/) {
    populateList();
}

void ParameterWindow::populateList() {
    m_listWidget->clear();
    if (!m_trackController || !m_automationController) return;

    QString filter = m_searchBox->text().trimmed();

    // 1. Get all cached parameters for the track
    auto params = m_trackController->getCachedParameters(m_trackId);

    // 2. Get current visible automation sub-lanes
    auto trackState = m_trackController->getTrackState(m_trackId);

    for (const auto& item : params) {
        QString paramName = QString::fromUtf8(item.info.name);
        if (!filter.isEmpty() && !paramName.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        // Check if this parameter is already added as an automation lane
        bool alreadyAdded = false;
        for (uint32_t s = 0; s < trackState.activeSubLaneCount; ++s) {
            const auto& sub = trackState.subLanes[s];
            if (sub.targetNodeId == item.routingNodeId && sub.parameterIndex == item.parameterIndex) {
                alreadyAdded = true;
                break;
            }
        }

        auto* listItem = new QListWidgetItem(m_listWidget);
        listItem->setSizeHint(QSize(0, 36));

        auto* widget = new ParameterListItemWidget(
            paramName, 
            alreadyAdded, 
            [this, item]() {
                m_automationController->createAutomationLane(m_trackId, item.routingNodeId, item.subNodeId, item.parameterIndex);
                emit laneAdded();
            }, 
            [this, item]() {
                m_automationController->removeAutomationLane(m_trackId, item.routingNodeId, item.subNodeId, item.parameterIndex);
                emit laneAdded();
            },
            m_listWidget
        );

        m_listWidget->setItemWidget(listItem, widget);
    }
}

} // namespace presentation::views
