// src/Presentation/views/pianoroll_window/PianoRollWindow.cpp
#include "PianoRollWindow.h"
#include "Presentation/views/theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QScrollBar>
#include "app/composition_root.h"
#include "Middle Bridge/engine/iinput_mode_controller.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include "Middle Bridge/timeline/iarrangement_controller.h"
#include "../shortcuts/ShortcutManager.h"

namespace {
    static constexpr uint64_t MAX_FRAME_LIMIT = 44100 * 600; // 10 minutes max limit
}

namespace presentation::views {

PianoRollWindow::PianoRollWindow(bridge::IMidiEditorController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setupUI();
    applyThemeSheet();
    setFocusPolicy(Qt::StrongFocus); // Capture keyboard typing events when active

    // Sync selections between canvas and velocity lane
    connect(canvas_, &PianoRollCanvas::selectionChanged, this, [this]() {
        velocityView_->setSelectedNoteIds(canvas_->getSelectedNoteIds());
    });
    connect(canvas_, &PianoRollCanvas::notesMutated, this, [this]() {
        velocityView_->update();
        if (controllerView_) controllerView_->update();
    });
    connect(velocityView_, &VelocityLaneView::velocitiesMutated, this, [this]() {
        canvas_->update();
    });
    connect(controllerView_, &ControllerLaneView::ccMutated, this, [this]() {
        canvas_->update();
    });

    // Synchronize scrollbars with viewport gestures
    connect(canvas_, &PianoRollCanvas::viewportRangeChanged, this, [this](uint64_t start, uint64_t end) {
        viewStartFrame_ = start;
        viewEndFrame_ = end;
        velocityView_->setViewportRange(start, end);
        if (controllerView_) controllerView_->setViewportRange(start, end);
        updateScrollBarPositions();
    });

    connect(canvas_, &PianoRollCanvas::pitchRangeChanged, this, [this](uint8_t /*minP*/, uint8_t /*maxP*/) {
        updateScrollBarPositions();
    });

    // Handle scrollbar user interaction
    connect(hScrollBar_, &QScrollBar::valueChanged, this, [this](int value) {
        uint64_t viewWidth = viewEndFrame_ - viewStartFrame_;
        uint64_t start = static_cast<uint64_t>(value);
        uint64_t end = start + viewWidth;
        
        viewStartFrame_ = start;
        viewEndFrame_ = end;
        
        canvas_->blockSignals(true);
        canvas_->setViewportRange(start, end);
        canvas_->blockSignals(false);
        
        velocityView_->setViewportRange(start, end);
        if (controllerView_) controllerView_->setViewportRange(start, end);
    });

    connect(vScrollBar_, &QScrollBar::valueChanged, this, [this](int value) {
        int pitchCount = canvas_->getMaxPitch() - canvas_->getMinPitch() + 1;
        uint8_t newMax = static_cast<uint8_t>(127 - value);
        uint8_t newMin = static_cast<uint8_t>(newMax - (pitchCount - 1));
        
        canvas_->blockSignals(true);
        canvas_->setPitchRange(newMin, newMax);
        canvas_->blockSignals(false);
    });

    installKeyboardShortcuts();
}

void PianoRollWindow::openMidiClip(TrackID trackId, bridge::RegionID regionId) {
    currentTrackId_ = trackId;
    currentRegionId_ = regionId;
    if (controller_) {
        controller_->openClip(trackId, regionId);
        
        // Reset viewport
        viewStartFrame_ = 0;
        viewEndFrame_ = 44100 * 4;
        canvas_->setViewportRange(viewStartFrame_, viewEndFrame_);
        velocityView_->setViewportRange(viewStartFrame_, viewEndFrame_);
        controllerView_->setViewportRange(viewStartFrame_, viewEndFrame_);
        
        canvas_->clearSelection();
        update();
    }
}

void PianoRollWindow::setPlayheadFrame(uint64_t frame) {
    canvas_->setPlayheadFrame(frame);
}

void PianoRollWindow::bindTimeline(bridge::ITimelineController* timeline) {
    timeline_ = timeline;
    canvas_->bindTimeline(timeline);
}

void PianoRollWindow::refreshView() {
    if (controller_) {
        if (!controller_->hasOpenClip() && currentTrackId_.isValid() && currentRegionId_.isValid()) {
            controller_->openClip(currentTrackId_, currentRegionId_);
        }
    }
    if (canvas_) canvas_->update();
    if (velocityView_) velocityView_->update();
    if (controllerView_) controllerView_->update();
    update();
}

void PianoRollWindow::onToolChanged(int id) {
    PianoRollTool tool = static_cast<PianoRollTool>(id);
    canvas_->setTool(tool);
}

void PianoRollWindow::onSnapChanged(int index) {
    // Tick conversion (960 PPQ)
    // 0 = 1/4 (240), 1 = 1/8 (120), 2 = 1/16 (60), 3 = Off (0)
    uint32_t resolutions[] = {240, 120, 60, 0};
    if (index >= 0 && index < 4) {
        canvas_->setSnapResolution(resolutions[index]);
    }
}

void PianoRollWindow::onLaneToggle(int index) {
    if (index == 0) {
        velocityView_->setVisible(true);
        controllerView_->setVisible(false);
    } else {
        velocityView_->setVisible(false);
        controllerView_->setVisible(true);
        
        // Set CC number (index 1 = Mod Wheel CC1, index 2 = Volume CC7, index 3 = Expression CC11)
        uint8_t ccNumbers[] = {0, 1, 7, 11};
        if (index >= 1 && index <= 3) {
            controllerView_->setControllerNumber(ccNumbers[index]);
        }
    }
}

void PianoRollWindow::onQuantizeClicked() {
    canvas_->triggerQuantize();
}

void PianoRollWindow::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Toolbar Panel
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("PRToolbar"));
    toolbar->setFixedHeight(44);
    
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(12, 4, 12, 4);
    toolbarLayout->setSpacing(8);

    // Tool Group
    btnSelect_ = new QPushButton(QStringLiteral("Select"), toolbar);
    btnSelect_->setCheckable(true);
    btnSelect_->setChecked(true);
    btnSelect_->setFixedHeight(32);
    
    btnDraw_ = new QPushButton(QStringLiteral("Draw"), toolbar);
    btnDraw_->setCheckable(true);
    btnDraw_->setFixedHeight(32);
    
    btnErase_ = new QPushButton(QStringLiteral("Erase"), toolbar);
    btnErase_->setCheckable(true);
    btnErase_->setFixedHeight(32);

    toolGroup_ = new QButtonGroup(toolbar);
    toolGroup_->addButton(btnSelect_, 0);
    toolGroup_->addButton(btnDraw_, 1);
    toolGroup_->addButton(btnErase_, 2);
    toolGroup_->setExclusive(true);

    connect(toolGroup_, &QButtonGroup::idClicked, this, &PianoRollWindow::onToolChanged);

    toolbarLayout->addWidget(btnSelect_);
    toolbarLayout->addWidget(btnDraw_);
    toolbarLayout->addWidget(btnErase_);

    // Divider
    auto* div1 = new QFrame(toolbar);
    div1->setFrameShape(QFrame::VLine);
    div1->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(div1);

    // Snap Selector
    auto* lblSnap = new QLabel(QStringLiteral("Snap:"), toolbar);
    comboSnap_ = new QComboBox(toolbar);
    comboSnap_->addItems({QStringLiteral("1/4 Beat"), QStringLiteral("1/8 Beat"), QStringLiteral("1/16 Beat"), QStringLiteral("Off")});
    comboSnap_->setCurrentIndex(2); // 1/16 Beat snap default
    comboSnap_->setFixedHeight(32);
    connect(comboSnap_, &QComboBox::currentIndexChanged, this, &PianoRollWindow::onSnapChanged);

    toolbarLayout->addWidget(lblSnap);
    toolbarLayout->addWidget(comboSnap_);

    // Quantize button
    btnQuantize_ = new QPushButton(QStringLiteral("Quantize"), toolbar);
    btnQuantize_->setFixedHeight(32);
    connect(btnQuantize_, &QPushButton::clicked, this, &PianoRollWindow::onQuantizeClicked);
    toolbarLayout->addWidget(btnQuantize_);

    // Spacer
    toolbarLayout->addStretch();

    // Lane View Toggle
    auto* lblLane = new QLabel(QStringLiteral("Lanes:"), toolbar);
    comboLaneSelect_ = new QComboBox(toolbar);
    comboLaneSelect_->addItems({
        QStringLiteral("Velocities"),
        QStringLiteral("Modulation (CC 1)"),
        QStringLiteral("Volume (CC 7)"),
        QStringLiteral("Expression (CC 11)")
    });
    comboLaneSelect_->setFixedHeight(32);
    connect(comboLaneSelect_, &QComboBox::currentIndexChanged, this, &PianoRollWindow::onLaneToggle);

    toolbarLayout->addWidget(lblLane);
    toolbarLayout->addWidget(comboLaneSelect_);

    mainLayout->addWidget(toolbar);

    // 2. Splitter for Canvas and Bottom Lanes
    verticalSplitter_ = new QSplitter(Qt::Vertical, this);
    verticalSplitter_->setHandleWidth(2);

    // Canvas container to host canvas and vertical scrollbar
    auto* canvasContainer = new QWidget(this);
    auto* canvasLayout = new QHBoxLayout(canvasContainer);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);

    canvas_ = new PianoRollCanvas(controller_, canvasContainer);
    vScrollBar_ = new QScrollBar(Qt::Vertical, canvasContainer);
    vScrollBar_->setSingleStep(1);

    canvasLayout->addWidget(canvas_);
    canvasLayout->addWidget(vScrollBar_);
    verticalSplitter_->addWidget(canvasContainer);

    // Bottom lane container to stack Velocity & CC lane
    auto* bottomContainer = new QWidget(this);
    auto* bottomLayout = new QVBoxLayout(bottomContainer);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    velocityView_ = new VelocityLaneView(controller_, this);
    controllerView_ = new ControllerLaneView(controller_, this);
    controllerView_->setVisible(false); // default velocity active

    bottomLayout->addWidget(velocityView_);
    bottomLayout->addWidget(controllerView_);

    verticalSplitter_->addWidget(bottomContainer);
    
    // Splitter sizes (70% canvas, 30% bottom lanes)
    verticalSplitter_->setSizes({350, 150});

    mainLayout->addWidget(verticalSplitter_);

    // Horizontal scrollbar aligned with notes grid
    auto* hScrollContainer = new QWidget(this);
    auto* hScrollLayout = new QHBoxLayout(hScrollContainer);
    hScrollLayout->setContentsMargins(0, 0, 0, 0);
    hScrollLayout->setSpacing(0);

    auto* leftSpacer = new QWidget(hScrollContainer);
    leftSpacer->setFixedWidth(48); // Match canvas KEY_WIDTH

    hScrollBar_ = new QScrollBar(Qt::Horizontal, hScrollContainer);

    auto* rightSpacer = new QWidget(hScrollContainer);
    int scrollBarWidth = vScrollBar_->sizeHint().width() > 0 ? vScrollBar_->sizeHint().width() : 16;
    rightSpacer->setFixedWidth(scrollBarWidth);

    hScrollLayout->addWidget(leftSpacer);
    hScrollLayout->addWidget(hScrollBar_);
    hScrollLayout->addWidget(rightSpacer);

    mainLayout->addWidget(hScrollContainer);

    // Initialize scrollbar ranges
    updateScrollBarPositions();
}

void PianoRollWindow::applyThemeSheet() {
    // Elegant deep cyber-industrial QSS
    QString qss = QString(
        "#PRToolbar {"
        "    background-color: %1;"
        "    border-bottom: 1px solid %2;"
        "}"
        "QLabel {"
        "    color: %3;"
        "    font-size: 14px;"
        "    font-weight: bold;"
        "}"
        "QPushButton {"
        "    background-color: %2;"
        "    border: 1px solid %4;"
        "    color: %5;"
        "    padding: 6px 10px;"
        "    border-radius: 4px;"
        "    font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #444444;"
        "    border-color: #5D6882;"
        "}"
        "QPushButton:checked {"
        "    background-color: %6;"
        "    border-color: %6;"
        "    color: %7;"
        "    font-weight: bold;"
        "}"
        "QComboBox {"
        "    background-color: %2;"
        "    border: 1px solid %4;"
        "    color: %5;"
        "    padding: 3px 20px 3px 6px;"
        "    border-radius: 4px;"
        "    font-size: 14px;"
        "}"
        "QComboBox::drop-down {"
        "    subcontrol-origin: padding;"
        "    subcontrol-position: top right;"
        "    width: 15px;"
        "    border-left-width: 0px;"
        "}"
        "QSplitter::handle {"
        "    background-color: %2;"
        "}"
    ).arg(theme::Color::BgSurface.name())
     .arg(theme::Color::BgControl.name())
     .arg(theme::Color::TextMuted.name())
     .arg(theme::Color::BgSurface.name())
     .arg(theme::Color::TextPrimary.name())
     .arg(theme::Color::AccentGlow.name())
     .arg(theme::Color::BgBase.name());
    setStyleSheet(qss);
}

void PianoRollWindow::keyPressEvent(QKeyEvent* event) {
    if (!controller_) {
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->matches(QKeySequence::Undo)) {
        auto* arrangement = app::CompositionRoot::instance().getArrangementController();
        if (arrangement && arrangement->undo()) {
            refreshView();
        }
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Redo)) {
        auto* arrangement = app::CompositionRoot::instance().getArrangementController();
        if (arrangement && arrangement->redo()) {
            refreshView();
        }
        event->accept();
        return;
    }

    if (canvas_ && !canvas_->getSelectedNoteIds().empty()) {
        const auto& selected = canvas_->getSelectedNoteIds();
        int key = event->key();
        
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            controller_->removeSelectedNotes(selected.data(), static_cast<uint32_t>(selected.size()));
            canvas_->clearSelection();
            canvas_->update();
            emit canvas_->notesMutated();
            return;
        }
        
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            int8_t semitones = (key == Qt::Key_Up) ? 1 : -1;
            if (event->modifiers() & Qt::ShiftModifier) {
                semitones *= 12;
            }
            controller_->transposeSelectedNotes(selected.data(), static_cast<uint32_t>(selected.size()), semitones);
            canvas_->update();
            emit canvas_->notesMutated();
            return;
        }

        if (key == Qt::Key_Left || key == Qt::Key_Right) {
            int64_t nudgeTicks = 60; // 1/16 note default
            int index = comboSnap_ ? comboSnap_->currentIndex() : 2;
            uint32_t resolutions[] = {240, 120, 60, 0};
            if (index >= 0 && index < 3) {
                nudgeTicks = resolutions[index];
            }
            if (nudgeTicks == 0) {
                nudgeTicks = 15; // default fine nudge if snap is off
            }

            int64_t deltaFrames = 0;
            if (timeline_) {
                uint64_t sample0 = timeline_->ticksToSamples(0);
                uint64_t sampleDelta = timeline_->ticksToSamples(static_cast<uint64_t>(nudgeTicks));
                deltaFrames = static_cast<int64_t>(sampleDelta - sample0);
            } else {
                deltaFrames = nudgeTicks * 23; // fallback 120bpm 44.1k
            }

            if (key == Qt::Key_Left) {
                deltaFrames = -deltaFrames;
            }

            controller_->shiftSelectedNotes(selected.data(), static_cast<uint32_t>(selected.size()), deltaFrames);
            canvas_->update();
            emit canvas_->notesMutated();
            return;
        }
    }

    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();

    // If modifier keys (Ctrl / Cmd) are pressed, delegate to standard QWidget shortcut processing
    if (mods & (Qt::ControlModifier | Qt::MetaModifier)) {
        QWidget::keyPressEvent(event);
        return;
    }

    // Check if global typing keyboard-to-piano mode is active
    auto* inputMode = app::CompositionRoot::instance().getInputModeController();
    if (inputMode && !inputMode->isTypingKeyboardToPiano()) {
        QWidget::keyPressEvent(event);
        return;
    }

    // Bypass auto-repeat to avoid rapid retriggering on hold
    if (event->isAutoRepeat()) {
        QWidget::keyPressEvent(event);
        return;
    }

    // Z and X shift Octaves down and up respectively
    if (key == Qt::Key_Z) {
        currentOctaveOffset_ = std::max(-3, currentOctaveOffset_ - 1);
        return;
    }
    if (key == Qt::Key_X) {
        currentOctaveOffset_ = std::min(3, currentOctaveOffset_ + 1);
        return;
    }

    // Chromatic QWERTY mapping (centered around C4 = pitch 60)
    static const std::unordered_map<int, int> kKeyToSemitone = {
        {Qt::Key_A, 0},   // C4
        {Qt::Key_W, 1},   // C#4
        {Qt::Key_S, 2},   // D4
        {Qt::Key_E, 3},   // D#4
        {Qt::Key_D, 4},   // E4
        {Qt::Key_F, 5},   // F4
        {Qt::Key_T, 6},   // F#4
        {Qt::Key_G, 7},   // G4
        {Qt::Key_Y, 8},   // G#4
        {Qt::Key_H, 9},   // A4
        {Qt::Key_U, 10},  // A#4
        {Qt::Key_J, 11},  // B4
        {Qt::Key_K, 12},  // C5
        {Qt::Key_O, 13},  // C#5
        {Qt::Key_L, 14},  // D5
        {Qt::Key_P, 15},  // D#5
        {Qt::Key_Semicolon, 16}, // E5
        {Qt::Key_Apostrophe, 17} // F5
    };

    auto it = kKeyToSemitone.find(key);
    if (it != kKeyToSemitone.end()) {
        int basePitch = 60 + currentOctaveOffset_ * 12;
        int pitch = basePitch + it->second;
        if (pitch >= 0 && pitch <= 127) {
            activeTypingKeys_[key] = true;
            controller_->noteOn(static_cast<uint8_t>(pitch), 100, 0);
            update();
        }
    } else {
        QWidget::keyPressEvent(event);
    }
}

void PianoRollWindow::keyReleaseEvent(QKeyEvent* event) {
    if (!controller_) {
        QWidget::keyReleaseEvent(event);
        return;
    }

    if (event->isAutoRepeat()) {
        QWidget::keyReleaseEvent(event);
        return;
    }

    int key = event->key();

    if (activeTypingKeys_[key]) {
        activeTypingKeys_[key] = false;

        // Chromatic QWERTY mapping
        static const std::unordered_map<int, int> kKeyToSemitone = {
            {Qt::Key_A, 0},
            {Qt::Key_W, 1},
            {Qt::Key_S, 2},
            {Qt::Key_E, 3},
            {Qt::Key_D, 4},
            {Qt::Key_F, 5},
            {Qt::Key_T, 6},
            {Qt::Key_G, 7},
            {Qt::Key_Y, 8},
            {Qt::Key_H, 9},
            {Qt::Key_U, 10},
            {Qt::Key_J, 11},
            {Qt::Key_K, 12},
            {Qt::Key_O, 13},
            {Qt::Key_L, 14},
            {Qt::Key_P, 15},
            {Qt::Key_Semicolon, 16},
            {Qt::Key_Apostrophe, 17}
        };

        auto it = kKeyToSemitone.find(key);
        if (it != kKeyToSemitone.end()) {
            int basePitch = 60 + currentOctaveOffset_ * 12;
            int pitch = basePitch + it->second;
            if (pitch >= 0 && pitch <= 127) {
                controller_->noteOff(static_cast<uint8_t>(pitch), 0);
                update();
            }
        }
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

void PianoRollWindow::updateScrollBarPositions() {
    if (hScrollBar_ && canvas_) {
        hScrollBar_->blockSignals(true);
        uint64_t viewWidth = viewEndFrame_ - viewStartFrame_;
        hScrollBar_->setPageStep(static_cast<int>(viewWidth));
        hScrollBar_->setRange(0, static_cast<int>(MAX_FRAME_LIMIT - viewWidth));
        hScrollBar_->setValue(static_cast<int>(viewStartFrame_));
        hScrollBar_->blockSignals(false);
    }
    if (vScrollBar_ && canvas_) {
        vScrollBar_->blockSignals(true);
        uint8_t minP = canvas_->getMinPitch();
        uint8_t maxP = canvas_->getMaxPitch();
        int pitchCount = maxP - minP + 1;
        vScrollBar_->setPageStep(pitchCount);
        vScrollBar_->setRange(0, 128 - pitchCount);
        vScrollBar_->setValue(127 - maxP);
        vScrollBar_->blockSignals(false);
    }
}

void PianoRollWindow::installKeyboardShortcuts() {
    auto& sm = presentation::shortcuts::ShortcutManager::instance();
    using presentation::shortcuts::ShortcutAction;

    sm.bind(this, ShortcutAction::PianoRoll_Quantize, [this]() {
        if (canvas_) canvas_->triggerQuantize();
    });

    sm.bind(this, ShortcutAction::PianoRoll_SelectAll, [this]() {
        if (canvas_) canvas_->selectAll();
    });

    sm.bindSequence(this, QKeySequence("Ctrl+E"), [this]() {
        if (canvas_ && timeline_) canvas_->splitNotesAtPlayhead(timeline_->getCurrentFrame());
    });

    sm.bindSequence(this, QKeySequence("Ctrl+D"), [this]() {
        if (canvas_) canvas_->duplicateSelection();
    });

    sm.bindSequence(this, QKeySequence("Alt+M"), [this]() {
        if (canvas_) canvas_->toggleMuteSelection();
    });

    sm.bindSequence(this, QKeySequence("Ctrl+J"), [this]() {
        if (canvas_) canvas_->glueSelection();
    });

    sm.bindSequence(this, QKeySequence("Ctrl+G"), [this]() {
        if (canvas_) canvas_->glueSelection();
    });

    sm.bindSequence(this, QKeySequence("Ctrl+L"), [this]() {
        if (canvas_) canvas_->forceLegatoSelection();
    });

    sm.bindSequence(this, QKeySequence("Ctrl+Shift+A"), [this]() {
        if (canvas_) canvas_->invertSelection();
    });
}

} // namespace presentation::views

