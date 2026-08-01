// src/Presentation/views/top_control_panel/workspace_controls.cpp
#include "workspace_controls.h"
#include "../theme.h"
#include <QHBoxLayout>
#include <QPainter>

namespace presentation::views {

namespace {

QString getToggleStyle()
{
    return QString(
        "QPushButton {"
        "    color: %1;"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 4px;"
        "    padding: 4px 4px;"
        "}"
        "QPushButton:hover {"
        "    color: %4;"
        "    background-color: #444444;"
        "    border: 1px solid %5;"
        "}"
        "QPushButton:checked {"
        "    color: %5;"
        "    background-color: %3;"
        "    border: 1px solid %5;"
        "}"
    ).arg(theme::Color::TextMuted.name())
     .arg(theme::Color::BgControl.name())
     .arg(theme::Color::BgSurface.name())
     .arg(theme::Color::TextPrimary.name())
     .arg(theme::Color::AccentGlow.name());
}

} // anonymous namespace

WorkspaceControls::WorkspaceControls(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background: transparent; border: none;");
    setupUI();
}

void WorkspaceControls::setupUI()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    m_pianoRollBtn = createWindowToggle(theme::PaintHelper::createSvgIcon(":/icons/piano.svg"), "PIANO", "Piano Roll editor",
                                         bridge::WorkspaceWindow::PianoRoll);
    layout->addWidget(m_pianoRollBtn);

    m_analyzeLoudnessBtn = new QPushButton(this);
    m_analyzeLoudnessBtn->setIcon(theme::PaintHelper::createSvgIcon(":/icons/diagnose.svg"));
    m_analyzeLoudnessBtn->setIconSize(QSize(20, 20));
    m_analyzeLoudnessBtn->setFixedSize(36, 36);
    m_analyzeLoudnessBtn->setToolTip("Analyze Session Loudness");
    m_analyzeLoudnessBtn->setCursor(Qt::PointingHandCursor);
    m_analyzeLoudnessBtn->setStyleSheet(getToggleStyle());
    connect(m_analyzeLoudnessBtn, &QPushButton::clicked, this, &WorkspaceControls::analyzeLoudnessClicked);
    layout->addWidget(m_analyzeLoudnessBtn);

    m_mixerBtn = createWindowToggle(theme::PaintHelper::createSvgIcon(":/icons/mixer.svg"), "MIXER", "Mixer window",
                                     bridge::WorkspaceWindow::Mixer);
    layout->addWidget(m_mixerBtn);

    m_browserBtn = createWindowToggle(theme::PaintHelper::createSvgIcon(":/icons/collection.svg"), "BROWSER", "File Browser / Library",
                                       bridge::WorkspaceWindow::Browser);
    layout->addWidget(m_browserBtn);

    // Sleek Settings gear button matching the premium industrial styling
    auto* settingsBtn = new QPushButton(this);
    settingsBtn->setIcon(theme::PaintHelper::createSvgIcon(":/icons/settings.svg"));
    settingsBtn->setIconSize(QSize(20, 20));
    settingsBtn->setFixedSize(36, 36);
    settingsBtn->setToolTip("Open audio/MIDI hardware preferences");
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setStyleSheet(getToggleStyle());
    connect(settingsBtn, &QPushButton::clicked, this, &WorkspaceControls::settingsClicked);
    layout->addWidget(settingsBtn);

    layout->addStretch(); // Push controls to the left (towards center)
}

QPushButton* WorkspaceControls::createWindowToggle(const QIcon& icon,
                                                     const QString& text,
                                                     const QString& tooltip,
                                                     bridge::WorkspaceWindow window)
{
    auto* btn = new QPushButton(this);
    if (!icon.isNull()) {
        btn->setIcon(icon);
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(36, 36);
    } else {
        btn->setText(text);
        btn->setFixedHeight(36);
        btn->setFont(theme::Font::primary(8, QFont::Bold));
    }
    btn->setCheckable(true);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(getToggleStyle());

    connect(btn, &QPushButton::clicked, this, [this, window]() {
        if (m_controller) {
            bool wasVisible = m_controller->isWindowVisible(window);
            m_controller->toggleWindowVisibility(window);
            if (!wasVisible) {
                m_controller->bringWindowToFront(window);
            }
        }
        Q_EMIT windowToggleClicked(window);
    });

    return btn;
}

void WorkspaceControls::bind(bridge::IWorkspaceController* controller)
{
    m_controller = controller;
    updateFromBridge();
}

void WorkspaceControls::updateFromBridge()
{
    if (!m_controller) return;

    m_pianoRollBtn->setChecked(m_controller->isWindowVisible(bridge::WorkspaceWindow::PianoRoll));
    m_mixerBtn->setChecked(m_controller->isWindowVisible(bridge::WorkspaceWindow::Mixer));
    m_browserBtn->setChecked(m_controller->isWindowVisible(bridge::WorkspaceWindow::Browser));
}

void WorkspaceControls::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor dividerColor = theme::Color::BgControl;
    dividerColor.setAlphaF(0.5f);
    QPen pen(dividerColor, 1.0);
    painter.setPen(pen);
    double lineY = height() - 1.0;
    painter.drawLine(QPointF(0.0, lineY), QPointF(static_cast<double>(width()), lineY));
}

} // namespace presentation::views
