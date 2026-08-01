// src/Presentation/views/top_control_panel/top_control_panel.cpp
#include "top_control_panel.h"
#include "transport_controls.h"
#include "input_mode_controls.h"
#include "workspace_controls.h"
#include "../theme.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>

namespace presentation::views {

TopControlPanel::TopControlPanel(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(48);
    setStyleSheet("background: transparent; border: none;");

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Input Mode Controls (left) ---
    m_inputModeControls = new InputModeControls(this);
    layout->addWidget(m_inputModeControls, 1); // stretch factor 1 to push transport to center

    // --- Vertical separator ---
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet("background-color: #526D82; min-width: 1px; max-width: 1px; border: none;");
    layout->addWidget(sep1);

    // --- Transport Controls (center) ---
    m_transportControls = new TransportControls(this);
    layout->addWidget(m_transportControls, 0, Qt::AlignCenter); // center aligned, no stretch

    // --- Vertical separator ---
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("background-color: #526D82; min-width: 1px; max-width: 1px; border: none;");
    layout->addWidget(sep2);

    // --- Workspace Controls (right) ---
    m_workspaceControls = new WorkspaceControls(this);
    layout->addWidget(m_workspaceControls, 1); // stretch factor 1 to push transport to center

    // Forward the settingsClicked signal to MainWindow
    connect(m_workspaceControls, &WorkspaceControls::settingsClicked, this, &TopControlPanel::settingsClicked);
    connect(m_workspaceControls, &WorkspaceControls::windowToggleClicked, this, &TopControlPanel::windowToggleClicked);
    connect(m_workspaceControls, &WorkspaceControls::analyzeLoudnessClicked, this, &TopControlPanel::analyzeLoudnessClicked);
}

void TopControlPanel::bind(bridge::ITimelineController* timelineCtrl,
                            bridge::IInputModeController* inputCtrl,
                            bridge::IWorkspaceController* workspaceCtrl)
{
    m_transportControls->bind(timelineCtrl);
    m_inputModeControls->bind(timelineCtrl, inputCtrl);
    m_workspaceControls->bind(workspaceCtrl);
}

void TopControlPanel::updateFromBridge()
{
    m_transportControls->updateFromBridge();
    m_inputModeControls->updateFromBridge();
    m_workspaceControls->updateFromBridge();
}

void TopControlPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw glass panel background
    QRectF bounds(0.0, 0.0, static_cast<double>(width()), static_cast<double>(height()));
    theme::PaintHelper::drawGlassPanel(&painter, bounds, theme::Color::BgBase, 0.0);

    // Draw bottom border
    QColor borderColor = theme::Color::BgControl;
    borderColor.setAlphaF(0.6f);
    QPen pen(borderColor, 1.0);
    painter.setPen(pen);
    double lineY = height() - 1.0;
    painter.drawLine(QPointF(0.0, lineY), QPointF(static_cast<double>(width()), lineY));
}

} // namespace presentation::views
