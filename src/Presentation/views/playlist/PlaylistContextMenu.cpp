// src/Presentation/views/playlist/PlaylistContextMenu.cpp
#include "PlaylistContextMenu.h"
#include "PlaylistClipCanvas.h"
#include "dialogs/DAWInputDialog.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include "theme.h"
#include "common/system_primitives.h"

namespace presentation::views {

// ─────────────────────────────────────────────────────────────────────────────
// Embedded Premium Cyber Dialog for Fades Configuration
// ─────────────────────────────────────────────────────────────────────────────
class FadeConfigDialog : public QDialog {
public:
    FadeConfigDialog(uint32_t currentIn, uint32_t currentOut, QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Fade Configuration"));
        setModal(true);
        setFixedWidth(280);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(10);

        auto* labelIn = new QLabel(QStringLiteral("FADE IN FRAMES:"), this);
        spinIn = new QSpinBox(this);
        spinIn->setRange(0, 9600000); // Massive limits for long tracks
        spinIn->setValue(static_cast<int>(currentIn));

        auto* labelOut = new QLabel(QStringLiteral("FADE OUT FRAMES:"), this);
        spinOut = new QSpinBox(this);
        spinOut->setRange(0, 9600000);
        spinOut->setValue(static_cast<int>(currentOut));

        layout->addWidget(labelIn);
        layout->addWidget(spinIn);
        layout->addWidget(labelOut);
        layout->addWidget(spinOut);

        auto* btnLayout = new QHBoxLayout();
        auto* cancelBtn = new QPushButton(QStringLiteral("CANCEL"), this);
        auto* okBtn = new QPushButton(QStringLiteral("APPLY"), this);
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(okBtn);

        layout->addLayout(btnLayout);

        connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    QSpinBox* spinIn{nullptr};
    QSpinBox* spinOut{nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// PlaylistContextMenu Factory Methods
// ─────────────────────────────────────────────────────────────────────────────

QMenu* PlaylistContextMenu::buildForRegion(
    const bridge::VisualRegion& region,
    PlaylistEditTool /*activeTool*/,
    uint64_t frameAtClick,
    const std::vector<bridge::TrackUIState>& tracks,
    bridge::IArrangementController* arrangement,
    QWidget* parent)
{
    auto* menu = new QMenu(parent);

    // Title / info action (disabled, serves as premium styled label)
    QAction* infoAct = menu->addAction(QStringLiteral("CLIP: %1").arg(QString::fromUtf8(region.name)));
    infoAct->setEnabled(false);
    menu->addSeparator();

    // --- Action 1: Split ---
    QAction* splitAct = menu->addAction(QStringLiteral("Split Clip Here"));
    QObject::connect(splitAct, &QAction::triggered, [arrangement, region, frameAtClick]() {
        if (arrangement) {
            arrangement->splitRegion(region.id, frameAtClick);
        }
    });

    // --- Action 2: Delete ---
    QAction* deleteAct = menu->addAction(QStringLiteral("Delete Clip"));
    QObject::connect(deleteAct, &QAction::triggered, [arrangement, region, parent]() {
        if (arrangement) {
            if (auto* canvas = qobject_cast<PlaylistClipCanvas*>(parent)) {
                canvas->invalidateMedia(MediaID::fromRaw(region.mediaId));
            }
            arrangement->deleteRegion(region.id);
        }
    });

    // --- Action 3: Mute / Unmute Toggle ---
    QString muteText = region.isMuted ? QStringLiteral("Unmute Clip") : QStringLiteral("Mute Clip");
    QAction* muteAct = menu->addAction(muteText);
    QObject::connect(muteAct, &QAction::triggered, [arrangement, region]() {
        if (arrangement) {
            arrangement->setRegionMuted(region.id, !region.isMuted);
        }
    });

    menu->addSeparator();

    // --- Action 4: Set Gain ---
    QAction* gainAct = menu->addAction(QStringLiteral("Set Gain..."));
    QObject::connect(gainAct, &QAction::triggered, [arrangement, region, parent]() {
        if (!arrangement) return;
        bool ok = false;
        double gain = DAWInputDialog::getDouble(
            parent,
            QStringLiteral("Set Linear Gain"),
            QStringLiteral("Enter Gain Coefficient (0.0 = silence, 1.0 = unity, max 4.0):"),
            static_cast<double>(region.gainLinear),
            0.0, 4.0, 2, &ok
        );
        if (ok) {
            arrangement->setRegionGain(region.id, static_cast<float>(gain));
        }
    });

    // --- Action 5: Fade In / Out Config ---
    QAction* fadeAct = menu->addAction(QStringLiteral("Configure Fades..."));
    QObject::connect(fadeAct, &QAction::triggered, [arrangement, region, parent]() {
        if (!arrangement) return;
        FadeConfigDialog dlg(region.fadeInFrames, region.fadeOutFrames, parent);
        if (dlg.exec() == QDialog::Accepted) {
            uint32_t fIn = static_cast<uint32_t>(dlg.spinIn->value());
            uint32_t fOut = static_cast<uint32_t>(dlg.spinOut->value());
            arrangement->setRegionFades(region.id, fIn, fOut);
        }
    });

    // --- Action 5b: Rename Clip ---
    QAction* renameAct = menu->addAction(QStringLiteral("Rename..."));
    QObject::connect(renameAct, &QAction::triggered, [arrangement, region, parent]() {
        if (!arrangement) return;
        bool ok = false;
        QString newName = DAWInputDialog::getText(
            parent,
            QStringLiteral("Rename"),
            QStringLiteral("Rename:"),
            QString::fromUtf8(region.name),
            &ok,
            static_cast<int>(MAX_NAME_LENGTH - 1)
        );
        if (ok) {
            std::string nameStr = newName.toStdString();
            arrangement->updateRegionMetadata(region.id, nameStr.c_str(), region.comment, region.colorARGB);
            if (parent) {
                parent->update();
            }
        }
    });

    // --- Action 5c: Edit Comment ---
    QAction* commentAct = menu->addAction(QStringLiteral("Edit Comment..."));
    QObject::connect(commentAct, &QAction::triggered, [arrangement, region, parent]() {
        if (!arrangement) return;
        bool ok = false;
        QString newComment = DAWInputDialog::getMultiLineText(
            parent,
            QStringLiteral("Edit Comment"),
            QStringLiteral("Edit Comment:"),
            QString::fromUtf8(region.comment),
            &ok
        );
        if (ok) {
            QString trimmed = newComment.left(static_cast<int>(MAX_COMMENT_LENGTH - 1));
            std::string commentStr = trimmed.toStdString();
            arrangement->updateRegionMetadata(region.id, region.name, commentStr.c_str(), region.colorARGB);
            if (parent) {
                parent->update();
            }
        }
    });

    menu->addSeparator();

    if (region.clipType == composition::RegionType::AUDIO) {
        auto* stretchMenu = menu->addMenu(QStringLiteral("Audio Time Stretching"));
        
        auto* actBypass = stretchMenu->addAction(QStringLiteral("Bypass (No Stretching)"));
        actBypass->setCheckable(true);
        actBypass->setChecked(region.warpMode == WarpMode::BYPASS);
        QObject::connect(actBypass, &QAction::triggered, [arrangement, region]() {
            if (arrangement) arrangement->setRegionWarpMode(region.id, WarpMode::BYPASS);
        });

        auto* actTonal = stretchMenu->addAction(QStringLiteral("Tonal Stretching"));
        actTonal->setCheckable(true);
        actTonal->setChecked(region.warpMode == WarpMode::SYNC_TO_TEMPO);
        QObject::connect(actTonal, &QAction::triggered, [arrangement, region]() {
            if (arrangement) arrangement->setRegionWarpMode(region.id, WarpMode::SYNC_TO_TEMPO);
        });

        auto* actBeat = stretchMenu->addAction(QStringLiteral("Transient Beat-Slice"));
        actBeat->setCheckable(true);
        actBeat->setChecked(region.warpMode == WarpMode::SCRUB);
        QObject::connect(actBeat, &QAction::triggered, [arrangement, region]() {
            if (arrangement) arrangement->setRegionWarpMode(region.id, WarpMode::SCRUB);
        });
        
        menu->addSeparator();
    }

    // --- Action 6: Move to Track Submenu ---
    auto* moveSub = menu->addMenu(QStringLiteral("Move to track"));
    for (const auto& trackState : tracks) {
        // Dot marker to show where the clip is currently sitting
        QString prefix = (trackState.trackId == region.trackId) ? QStringLiteral("● ") : QStringLiteral("  ");
        QAction* act = moveSub->addAction(prefix + QString::fromUtf8(trackState.name));
        
        bool compatible = false;
        if (region.clipType == composition::RegionType::AUDIO) {
            compatible = (trackState.type == composition::TrackType::AUDIO);
        } else if (region.clipType == composition::RegionType::MIDI) {
            compatible = (trackState.type == composition::TrackType::INSTRUMENT ||
                          trackState.type == composition::TrackType::MIDI);
        } else {
            compatible = true; // Fallback
        }

        if (!compatible) {
            act->setEnabled(false);
        } else {
            QObject::connect(act, &QAction::triggered, [arrangement, region, trackState]() {
                if (arrangement) {
                    arrangement->moveRegion(region.id, trackState.trackId, static_cast<int64_t>(region.startFrame));
                }
            });
        }
    }

    return menu;
}

QMenu* PlaylistContextMenu::buildForArrangementLane(
    TrackID trackId,
    composition::TrackType trackType,
    uint64_t frameAtClick,
    bridge::IArrangementController* arrangement,
    QWidget* parent)
{
    auto* menu = new QMenu(parent);

    QAction* titleAct = menu->addAction(QStringLiteral("ARRANGE"));
    titleAct->setEnabled(false);
    menu->addSeparator();

    // --- Action 1: Insert Audio Clip ---
    if (trackType == composition::TrackType::AUDIO) {
        QAction* insertAudioAct = menu->addAction(QStringLiteral("Insert Audio Clip..."));
        QObject::connect(insertAudioAct, &QAction::triggered, [arrangement, trackId, frameAtClick, parent]() {
            if (!arrangement) return;
            QString filePath = QFileDialog::getOpenFileName(
                parent,
                QStringLiteral("Select Audio Clip to Import"),
                QString(),
                QStringLiteral("Audio Files (*.wav *.aif *.aiff *.mp3 *.ogg *.flac);;All Files (*)")
            );
            if (!filePath.isEmpty()) {
                arrangement->importAudioClip(trackId, filePath.toUtf8().constData(), frameAtClick);
            }
        });
    } else {
        QAction* insertAudioAct = menu->addAction(QStringLiteral("Insert Audio Clip (Audio tracks only)"));
        insertAudioAct->setEnabled(false);
    }

    // --- Action 2: Insert MIDI Clip ---
    if (trackType == composition::TrackType::MIDI || trackType == composition::TrackType::INSTRUMENT) {
        QAction* insertMidiAct = menu->addAction(QStringLiteral("Insert MIDI Clip"));
        QObject::connect(insertMidiAct, &QAction::triggered, [arrangement, trackId, frameAtClick]() {
            if (!arrangement) return;
            uint64_t defaultDuration = 44100 * 4;
            arrangement->insertMidiClip(trackId, frameAtClick, defaultDuration);
        });
    } else {
        QAction* insertMidiAct = menu->addAction(QStringLiteral("Insert MIDI Clip (MIDI/Instrument tracks only)"));
        insertMidiAct->setEnabled(false);
    }

    return menu;
}

QMenu* PlaylistContextMenu::buildForAutomationSegment(
    const bridge::VisualAutomationPoint& leftPoint,
    std::function<void(uint8_t)> onShapeChanged,
    QWidget* parent)
{
    auto* menu = new QMenu(parent);

    QAction* titleAct = menu->addAction(QStringLiteral("CURVE SHAPE"));
    titleAct->setEnabled(false);
    menu->addSeparator();

    auto addShapeAction = [&](const QString& text, AutomationPoint::Shape shape) {
        QAction* act = menu->addAction(text);
        act->setCheckable(true);
        if (leftPoint.curveShape == static_cast<uint8_t>(shape)) {
            act->setChecked(true);
        }
        QObject::connect(act, &QAction::triggered, [onShapeChanged, shape]() {
            if (onShapeChanged) {
                onShapeChanged(static_cast<uint8_t>(shape));
            }
        });
    };

    addShapeAction(QStringLiteral("Linear"), AutomationPoint::Shape::LINEAR);
    addShapeAction(QStringLiteral("Exponential"), AutomationPoint::Shape::EXPONENTIAL);
    addShapeAction(QStringLiteral("Ease-In"), AutomationPoint::Shape::EASE_IN);
    addShapeAction(QStringLiteral("Ease-Out"), AutomationPoint::Shape::EASE_OUT);
    addShapeAction(QStringLiteral("Ease-In-Out"), AutomationPoint::Shape::EASE_IN_OUT);
    addShapeAction(QStringLiteral("Sine"), AutomationPoint::Shape::SINE);
    addShapeAction(QStringLiteral("Square"), AutomationPoint::Shape::SQUARE);
    addShapeAction(QStringLiteral("Step"), AutomationPoint::Shape::STEP);

    return menu;
}

QMenu* PlaylistContextMenu::buildForAutomationSubLane(
    bool hasHighlight,
    std::function<void()> onCopyPoints,
    QWidget* parent)
{
    auto* menu = new QMenu(parent);

    QAction* titleAct = menu->addAction(QStringLiteral("AUTOMATION LANE"));
    titleAct->setEnabled(false);
    menu->addSeparator();

    QAction* copyAct = menu->addAction(
        hasHighlight ? QStringLiteral("Copy Highlighted Points")
                     : QStringLiteral("Copy All Points"));
    
    QObject::connect(copyAct, &QAction::triggered, [onCopyPoints]() {
        if (onCopyPoints) {
            onCopyPoints();
        }
    });

    return menu;
}

} // namespace presentation::views
