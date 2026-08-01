// src/Presentation/views/top_control_panel/input_mode_controls.cpp
#include "input_mode_controls.h"
#include "../theme.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>

namespace presentation::views {

namespace {

QString getCheckableStyle()
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

QString getDropdownStyle()
{
    return QString(
        "QPushButton {"
        "    color: %1;"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 4px;"
        "    padding: 4px 8px;"
        "}"
        "QPushButton:hover {"
        "    color: %4;"
        "    background-color: #444444;"
        "    border: 1px solid %5;"
        "}"
        "QPushButton::menu-indicator {"
        "    image: none;"
        "    width: 0px;"
        "}"
    ).arg(theme::Color::TextMuted.name())
     .arg(theme::Color::BgControl.name())
     .arg(theme::Color::BgSurface.name())
     .arg(theme::Color::TextPrimary.name())
     .arg(theme::Color::AccentGlow.name());
}

QString getMenuStyle()
{
    return QString(
        "QMenu {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 4px;"
        "    padding: 4px;"
        "    font-size: 11pt;"
        "}"
        "QMenu::item {"
        "    padding: 6px 20px;"
        "    border-radius: 2px;"
        "}"
        "QMenu::item:selected {"
        "    background-color: %4;"
        "    color: %5;"
        "}"
        "QMenu::item:checked {"
        "    color: %4;"
        "}"
    ).arg(theme::Color::BgSurface.name())
     .arg(theme::Color::TextPrimary.name())
     .arg(theme::Color::BgControl.name())
     .arg(theme::Color::AccentGlow.name())
     .arg(theme::Color::BgBase.name());
}

} // anonymous namespace

InputModeControls::InputModeControls(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background: transparent; border: none;");
    setupUI();
}

void InputModeControls::setupUI()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    layout->addStretch(); // Push controls to the right (towards center)

    // --- Metronome Toggle ---
    m_metronomeBtn = createCheckableButton(theme::PaintHelper::createSvgIcon(":/icons/metronome.svg"), "METRO", "Metronome");
    connect(m_metronomeBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (m_timelineCtrl) m_timelineCtrl->setMetronomeEnabled(checked);
    });
    layout->addWidget(m_metronomeBtn);

    // --- Separator ---
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet(QString("background-color: %1; min-width: 1px; max-width: 1px; border: none;").arg(theme::Color::BgControl.name()));
    layout->addWidget(sep1);

    // --- Snap Mode Dropdown ---
    m_snapBtn = createDropdownButton("SNAP: Free", "Grid snap mode");
    m_snapBtn->setFixedWidth(140);
    m_snapMenu = new QMenu(this);
    m_snapMenu->setStyleSheet(getMenuStyle());

    auto addSnapAction = [this](const QString& text, bridge::SnapMode mode) {
        auto* action = m_snapMenu->addAction(text);
        action->setCheckable(true);
        action->setData(static_cast<int>(mode));
        connect(action, &QAction::triggered, this, [this, mode, text]() {
            if (m_inputCtrl) m_inputCtrl->setSnapMode(mode);
            m_snapBtn->setText("SNAP: " + text);
            // Uncheck all others
            for (auto* a : m_snapMenu->actions()) {
                a->setChecked(a->data().toInt() == static_cast<int>(mode));
            }
        });
    };

    addSnapAction("Free", bridge::SnapMode::Free);
    addSnapAction("Bar", bridge::SnapMode::Bar);
    addSnapAction("1/2 Note", bridge::SnapMode::Note_1_2);
    addSnapAction("1/4 Note", bridge::SnapMode::Note_1_4);
    addSnapAction("1/8 Note", bridge::SnapMode::Note_1_8);
    addSnapAction("1/16 Note", bridge::SnapMode::Note_1_16);
    addSnapAction("1/32 Note", bridge::SnapMode::Note_1_32);
    addSnapAction("1/64 Note", bridge::SnapMode::Note_1_64);
    addSnapAction("1/2T Note", bridge::SnapMode::Note_1_2_Triplet);
    addSnapAction("1/4T Note", bridge::SnapMode::Note_1_4_Triplet);
    addSnapAction("1/8T Note", bridge::SnapMode::Note_1_8_Triplet);
    addSnapAction("1/16T Note", bridge::SnapMode::Note_1_16_Triplet);
    addSnapAction("1/32T Note", bridge::SnapMode::Note_1_32_Triplet);
    addSnapAction("1/64T Note", bridge::SnapMode::Note_1_64_Triplet);
    m_snapMenu->actions().first()->setChecked(true);

    m_snapBtn->setMenu(m_snapMenu);
    layout->addWidget(m_snapBtn);
}

QPushButton* InputModeControls::createCheckableButton(const QIcon& icon, const QString& text, const QString& tooltip)
{
    auto* btn = new QPushButton(this);
    if (!icon.isNull()) {
        btn->setIcon(icon);
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(36, 36);
    } else {
        btn->setText(text);
        btn->setFixedHeight(36);
        btn->setFont(theme::Font::primary(10, QFont::Bold));
    }
    btn->setCheckable(true);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(getCheckableStyle());
    return btn;
}

QPushButton* InputModeControls::createDropdownButton(const QString& text, const QString& tooltip)
{
    auto* btn = new QPushButton(text, this);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(36);
    btn->setFont(theme::Font::primary(10));
    btn->setStyleSheet(getDropdownStyle());
    return btn;
}

void InputModeControls::bind(bridge::ITimelineController* timelineCtrl,
                              bridge::IInputModeController* inputCtrl)
{
    m_timelineCtrl = timelineCtrl;
    m_inputCtrl = inputCtrl;

    if (m_timelineCtrl) {
        m_metronomeBtn->setChecked(m_timelineCtrl->isMetronomeEnabled());
    }
    if (m_inputCtrl) {

        // Sync snap mode
        auto snap = m_inputCtrl->getSnapMode();
        for (auto* a : m_snapMenu->actions()) {
            bool match = (a->data().toInt() == static_cast<int>(snap));
            a->setChecked(match);
            if (match) m_snapBtn->setText("SNAP: " + a->text());
        }

    }
}

void InputModeControls::updateFromBridge()
{
    // Sync toggle states from bridge (in case changed externally)
    if (m_timelineCtrl) {
        m_metronomeBtn->setChecked(m_timelineCtrl->isMetronomeEnabled());
    }
    if (m_inputCtrl) {

        // Sync snap mode
        auto snap = m_inputCtrl->getSnapMode();
        for (auto* a : m_snapMenu->actions()) {
            bool match = (a->data().toInt() == static_cast<int>(snap));
            a->setChecked(match);
            if (match) m_snapBtn->setText("SNAP: " + a->text());
        }
    }
}

void InputModeControls::paintEvent(QPaintEvent* /*event*/)
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
