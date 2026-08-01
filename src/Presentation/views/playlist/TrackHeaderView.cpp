// src/Presentation/views/playlist/TrackHeaderView.cpp
#include "TrackHeaderView.h"
#include "TrackRowRenderer.h"
#include "TakeRowRenderer.h"
#include "AutomationRowRenderer.h"
#include "../theme.h"
#include "timeline/iarrangement_controller.h"
#include "dialogs/DAWInputDialog.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <QColorDialog>
#include <QMenu>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QApplication>
#include <algorithm>
#include "../mixer/instrument_slot_widget.h"
#include "../mixer/input_slot_widget.h"
#include "Middle Bridge/automation/automation_helpers.h"

namespace presentation::views {

TrackHeaderView::TrackHeaderView(bridge::ITrackController* track,
                                 bridge::IMeteringProvider* metering,
                                 bridge::IArrangementController* arrangement,
                                 bridge::IAutomationController* automation,
                                 QWidget* parent)
    : QWidget(parent)
    , m_track(track)
    , m_metering(metering)
    , m_arrangement(arrangement)
    , m_automation(automation)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setAcceptDrops(true);

    // Initialize Add Track button (physical widget, cheap since only 1 exists)
    m_addTrackBtn = new QPushButton(QString::fromUtf8("+ Add Track"), this);
    m_addTrackBtn->setFixedHeight(kFooterHeight);
    m_addTrackBtn->setCursor(Qt::PointingHandCursor);
    m_addTrackBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %1;"
        "  border-radius: 4px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: %3;"
        "  color: #FFFFFF;"
        "}"
    ).arg(theme::Color::BgSurface.name())
     .arg(theme::Color::TextMuted.name())
     .arg(theme::Color::BgControl.name()));

    connect(m_addTrackBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        menu.addAction(tr("Add Audio Track"));
        menu.addAction(tr("Add Instrument Track"));
        menu.addAction(tr("Add Folder Track"));
        QAction* sel = menu.exec(m_addTrackBtn->mapToGlobal(QPoint(0, -m_addTrackBtn->height())));
        if (!sel) return;
        if (sel->text() == tr("Add Audio Track")) {
            emit addAudioTrackRequested();
        } else if (sel->text() == tr("Add Instrument Track")) {
            emit addInstrumentTrackRequested();
        } else if (sel->text() == tr("Add Folder Track")) {
            emit addFolderTrackRequested();
        }
    });

    // Initialize shared rename editor overlay
    m_renameEditor = new QLineEdit(this);
    m_renameEditor->setVisible(false);
    m_renameEditor->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2a2a2a;"
        "  color: #ffffff;"
        "  border: 1px solid #00ffcc;"
        "  border-radius: 3px;"
        "  padding: 1px;"
        "  font-size: 9pt;"
        "  font-weight: bold;"
        "}"
    );
    connect(m_renameEditor, &QLineEdit::editingFinished, this, [this]() {
        if (m_renameEditor->isVisible()) {
            QString name = m_renameEditor->text().trimmed();
            if (m_renameTrackId.isValid() && !name.isEmpty()) {
                emit trackRenameRequested(m_renameTrackId, name);
            }
            m_renameEditor->setVisible(false);
        }
    });
}

TrackHeaderView::~TrackHeaderView()
{
    for (auto& pair : m_instrumentWidgets) {
        if (pair.second) pair.second->deleteLater();
    }
    for (auto& pair : m_audioInputWidgets) {
        if (pair.second) pair.second->deleteLater();
    }
}

void TrackHeaderView::setTrackList(const std::vector<bridge::TrackUIState>& tracks,
                                  const std::map<std::pair<uint64_t, uint32_t>, int>& takesLaneHeights)
{
    m_tracks = tracks;
    m_takesLaneHeights = takesLaneHeights;

    // Maintain cached groups
    m_groupedTracks.clear();

    rebuildLayouts();
}

void TrackHeaderView::updateTrackStates(const std::vector<bridge::TrackUIState>& tracks,
                                       const std::map<std::pair<uint64_t, uint32_t>, int>& takesLaneHeights)
{
    // Rebuild full layout if row count or structural ordering has changed
    if (tracks.size() != m_tracks.size()) {
        setTrackList(tracks, takesLaneHeights);
        return;
    }

    m_tracks = tracks;
    m_takesLaneHeights = takesLaneHeights;

    // Detect if heights changed under the hood and update list cache
    for (const auto& t : m_tracks) {
        int targetH = static_cast<int>(bridge::mainLaneHeightForTrack(t));
        auto it = m_trackHeights.find(t.trackId.toRaw());
        if (it != m_trackHeights.end()) {
            targetH = it->second;
        }
        m_trackHeights[t.trackId.toRaw()] = targetH;
    }

    rebuildLayouts();
}

void TrackHeaderView::updateMeters()
{
    if (!m_metering) return;
    bool changed = false;
    for (const auto& t : m_tracks) {
        bridge::MeterLevel level = m_metering->getTrackLevels(t.trackId);
        auto& state = m_trackMeters[t.trackId.toRaw()];

        if (!state.initialized) {
            // attack: 5ms, release: 350ms, sampleRate: ~60fps step
            state.ballistics[0].init(16.0, 5.0, 350.0);
            state.ballistics[1].init(16.0, 5.0, 350.0);
            state.initialized = true;
        }

        float oldPeakL = state.meter.peakLeft;
        float oldPeakR = state.meter.peakRight;

        state.meter.peakLeft = state.ballistics[0].filter(level.peakLeft, state.meter.peakLeft);
        state.meter.peakRight = state.ballistics[1].filter(level.peakRight, state.meter.peakRight);
        state.meter.rmsLeft = level.rmsLeft;
        state.meter.rmsRight = level.rmsRight;
        state.meter.clipLeft = level.clipLeft;
        state.meter.clipRight = level.clipRight;

        if (state.meter.peakLeft != oldPeakL || state.meter.peakRight != oldPeakR) {
            changed = true;
        }
    }
    if (changed) {
        update();
    }
}

void TrackHeaderView::updateMeters(const std::vector<bridge::MeterLevel>& levels)
{
    bool changed = false;
    for (size_t i = 0; i < levels.size() && i < m_tracks.size(); ++i) {
        const auto& t = m_tracks[i];
        auto& state = m_trackMeters[t.trackId.toRaw()];

        if (!state.initialized) {
            state.ballistics[0].init(16.0, 5.0, 350.0);
            state.ballistics[1].init(16.0, 5.0, 350.0);
            state.initialized = true;
        }

        float oldPeakL = state.meter.peakLeft;
        float oldPeakR = state.meter.peakRight;

        state.meter.peakLeft = state.ballistics[0].filter(levels[i].peakLeft, state.meter.peakLeft);
        state.meter.peakRight = state.ballistics[1].filter(levels[i].peakRight, state.meter.peakRight);
        state.meter.rmsLeft = levels[i].rmsLeft;
        state.meter.rmsRight = levels[i].rmsRight;
        state.meter.clipLeft = levels[i].clipLeft;
        state.meter.clipRight = levels[i].clipRight;

        if (state.meter.peakLeft != oldPeakL || state.meter.peakRight != oldPeakR) {
            changed = true;
        }
    }
    if (changed) {
        update();
    }
}

void TrackHeaderView::setVerticalOffset(int offsetPx)
{
    if (m_verticalOffsetPx != offsetPx) {
        m_verticalOffsetPx = offsetPx;
        rebuildLayouts();
    }
}

void TrackHeaderView::clearAll()
{
    m_tracks.clear();
    m_layoutGeometries.clear();
    m_trackHeights.clear();
    m_takesLaneHeights.clear();
    m_autoSubLaneHeights.clear();
    m_trackMeters.clear();
    m_groupedTracks.clear();
    for (auto& pair : m_instrumentWidgets) {
        if (pair.second) pair.second->deleteLater();
    }
    m_instrumentWidgets.clear();
    for (auto& pair : m_audioInputWidgets) {
        if (pair.second) pair.second->deleteLater();
    }
    m_audioInputWidgets.clear();
    if (m_renameEditor) m_renameEditor->setVisible(false);
    update();
}

void TrackHeaderView::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Fill base background
    p.fillRect(event->rect(), theme::Color::BgSurface);

    // 1. Draw Folder groupings (in background)
    drawFolderTrays(p);

    // 2. Iterate and draw each visible row
    const double visibleTop = 0.0;
    const double visibleBottom = height();

    for (const auto& row : m_layoutGeometries) {
        // Skip rendering off-screen rows
        if (row.rect.bottom() < visibleTop || row.rect.top() > visibleBottom) {
            continue;
        }

        if (row.type == RowType::Track) {
            // Find matched state
            const bridge::TrackUIState* foundState = nullptr;
            for (const auto& t : m_tracks) {
                if (t.trackId == row.trackId) {
                    foundState = &t;
                    break;
                }
            }
            if (!foundState) continue;

            // Load properties
            int depth = getTrackDepth(row.trackId);
            bool isGrouped = (depth > 0);
            
            // Check icon preset
            QString iconPreset = QStringLiteral("Audio");
            auto itIcon = m_trackIcons.find(row.trackId.toRaw());
            if (itIcon != m_trackIcons.end()) {
                iconPreset = itIcon->second;
            } else {
                if (foundState->type == composition::TrackType::AUDIO) {
                    iconPreset = QStringLiteral("Audio");
                } else if (foundState->type == composition::TrackType::INSTRUMENT) {
                    iconPreset = QStringLiteral("MIDI");
                }
            }

            // Load peak meter normalized state
            float leftNorm = 0.0f;
            float rightNorm = 0.0f;
            auto itMeter = m_trackMeters.find(row.trackId.toRaw());
            if (itMeter != m_trackMeters.end()) {
                auto dbToNorm = [](float db) -> float {
                    if (db <= -60.0f) return 0.0f;
                    if (db >= 6.0f) return 1.0f;
                    return (db + 60.0f) / 66.0f;
                };
                leftNorm = dbToNorm(itMeter->second.meter.peakLeft);
                rightNorm = dbToNorm(itMeter->second.meter.peakRight);
            }

            // Hover and Pressed mappings
            VirtualControl hCtrl = (m_hoveredTrackId == row.trackId && m_hoveredRowType == RowType::Track) ? m_hoveredControl : VirtualControl::None;
            VirtualControl pCtrl = (m_pressedTrackId == row.trackId && m_pressedRowType == RowType::Track) ? m_pressedControl : VirtualControl::None;

            TrackRowRenderer::paint(
                p, row.rect, *foundState,
                row.muteRect, row.soloRect, row.armRect, row.monitorRect,
                row.autoComboRect, row.autoExpandRect, row.takesExpandRect,
                row.nameRect, foundState->isSelected, isGrouped, depth, iconPreset,
                leftNorm, rightNorm, hCtrl, pCtrl
            );
        }
        else if (row.type == RowType::Take) {
            VirtualControl hCtrl = (m_hoveredTrackId == row.trackId && m_hoveredRowType == RowType::Take && m_hoveredRowIndex == row.index) ? m_hoveredControl : VirtualControl::None;
            VirtualControl pCtrl = (m_pressedTrackId == row.trackId && m_pressedRowType == RowType::Take && m_pressedRowIndex == row.index) ? m_pressedControl : VirtualControl::None;

            // Resolve total lanes
            uint32_t totalLanes = 0;
            for (const auto& t : m_tracks) {
                if (t.trackId == row.trackId) {
                    totalLanes = t.audioLanesCount;
                    break;
                }
            }

            TakeRowRenderer::paint(
                p, row.rect, row.index, totalLanes, row.promoteRect, hCtrl, pCtrl
            );
        }
        else if (row.type == RowType::Automation) {
            // Find matching sub-lane state info
            const bridge::TrackUIState* foundState = nullptr;
            for (const auto& t : m_tracks) {
                if (t.trackId == row.trackId) {
                    foundState = &t;
                    break;
                }
            }
            if (!foundState || row.index >= foundState->activeSubLaneCount) continue;

            const auto& sl = foundState->subLanes[row.index];
            QString paramName = QString::fromUtf8(sl.parameterName);
            uint8_t recMode = sl.recordMode;

            VirtualControl hCtrl = (m_hoveredTrackId == row.trackId && m_hoveredRowType == RowType::Automation && m_hoveredRowIndex == row.index) ? m_hoveredControl : VirtualControl::None;
            VirtualControl pCtrl = (m_pressedTrackId == row.trackId && m_pressedRowType == RowType::Automation && m_pressedRowIndex == row.index) ? m_pressedControl : VirtualControl::None;

            AutomationRowRenderer::paint(
                p, row.rect, paramName, recMode, foundState->colorARGB, hCtrl, pCtrl
            );
        }
    }

    // 3. Render Drag & Drop Overlay Indicator
    if (m_dropAction == DropAction::InsertBefore || m_dropAction == DropAction::InsertAfter) {
        p.setPen(QPen(theme::Color::AccentGlow, 3.0, Qt::SolidLine, Qt::RoundCap));
        double startX = 8.0;
        double endX = static_cast<double>(width()) - 8.0;
        p.drawLine(QPointF(startX, m_dropIndicatorY), QPointF(endX, m_dropIndicatorY));

        p.setBrush(theme::Color::AccentGlow);
        p.drawEllipse(QPointF(startX, m_dropIndicatorY), 3.0, 3.0);
        p.drawEllipse(QPointF(endX, m_dropIndicatorY), 3.0, 3.0);
    } else if (m_dropAction == DropAction::DropInto) {
        QColor highlightCol = theme::Color::AccentGlow;
        highlightCol.setAlpha(51); // 20% opacity
        p.setBrush(QBrush(highlightCol));

        QColor borderCol = theme::Color::AccentGlow;
        borderCol.setAlpha(204); // 80% opacity
        p.setPen(QPen(borderCol, 1.5, Qt::DashLine));
        p.drawRoundedRect(m_dropRect.adjusted(2.0, 2.0, -2.0, -2.0), 4.0, 4.0);
    }
}

void TrackHeaderView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF clickPos = event->position();
        m_dragStartPos = event->position().toPoint();

        // 1. Check if click is on any bottom border resize handle
        for (size_t i = 0; i < m_layoutGeometries.size(); ++i) {
            const auto& row = m_layoutGeometries[i];

            QRectF borderZone(row.rect.x(), row.rect.bottom() - 4.0, row.rect.width(), 4.0);
            if (borderZone.contains(clickPos)) {
                m_resizeRowIndex = static_cast<int>(i);
                m_resizeDragStartY = static_cast<int>(event->globalPosition().y());
                m_resizeDragStartHeight = static_cast<int>(row.height);
                setCursor(Qt::SizeVerCursor);
                event->accept();
                return;
            }
        }

        // 2. Otherwise, hit-test normal controls
        for (const auto& row : m_layoutGeometries) {
            if (row.rect.contains(clickPos)) {
                m_pressedTrackId = row.trackId;
                m_pressedRowType = row.type;
                m_pressedRowIndex = row.index;

                if (row.type == RowType::Track) {
                    m_pressedControl = TrackRowRenderer::hitTest(
                        clickPos, row.rect, row.muteRect, row.soloRect, row.armRect, row.monitorRect,
                        row.autoComboRect, row.autoExpandRect, row.takesExpandRect,
                        row.nameRect
                    );

                    // Toggle selection if name area is clicked
                    if (m_pressedControl == VirtualControl::None || m_pressedControl == VirtualControl::NameLabel) {
                        bool multi = (event->modifiers() & Qt::ControlModifier) || (event->modifiers() & Qt::MetaModifier);
                        bool range = (event->modifiers() & Qt::ShiftModifier);
                        onSelectionRequested(row.trackId, multi, range);
                    }
                }
                else if (row.type == RowType::Take) {
                    m_pressedControl = TakeRowRenderer::hitTest(clickPos, row.rect, row.promoteRect);
                }
                else if (row.type == RowType::Automation) {
                    m_pressedControl = AutomationRowRenderer::hitTest(clickPos, row.rect);
                    
                    // Direct selection of active automation parameter on click
                    if (m_automation) {
                        const bridge::TrackUIState* foundState = nullptr;
                        for (const auto& t : m_tracks) {
                            if (t.trackId == row.trackId) {
                                foundState = &t;
                                break;
                            }
                        }
                        if (foundState && row.index < foundState->activeSubLaneCount) {
                            const auto& sl = foundState->subLanes[row.index];
                            if (sl.targetNodeId.isValid()) {
                                m_automation->selectActiveAutomationLane(
                                    row.trackId, sl.targetNodeId, sl.subNodeId, static_cast<int32_t>(sl.parameterIndex)
                                );
                            }
                        }
                    }
                }

                update();
                event->accept();
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void TrackHeaderView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF clickPos = event->position();
        for (const auto& row : m_layoutGeometries) {
            if (row.type == RowType::Track && row.nameRect.contains(clickPos)) {
                // Double click name rect triggers inline rename overlay
                m_renameTrackId = row.trackId;
                
                // Position matching nameRect
                m_renameEditor->setGeometry(row.nameRect.toRect());
                
                // Populate name
                for (const auto& t : m_tracks) {
                    if (t.trackId == row.trackId) {
                        m_renameEditor->setText(QString::fromUtf8(t.name));
                        break;
                    }
                }
                m_renameEditor->setVisible(true);
                m_renameEditor->setFocus();
                m_renameEditor->selectAll();
                event->accept();
                return;
            }
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TrackHeaderView::mouseMoveEvent(QMouseEvent* event)
{
    QPointF pos = event->position();

    // 1. Handle live height drag-resize
    if (m_resizeRowIndex != -1 && m_resizeRowIndex < static_cast<int>(m_layoutGeometries.size())) {
        double currentGlobalY = event->globalPosition().y();
        double deltaY = currentGlobalY - m_resizeDragStartY;
        int newHeight = m_resizeDragStartHeight + static_cast<int>(deltaY);

        const auto& row = m_layoutGeometries[static_cast<size_t>(m_resizeRowIndex)];
        if (row.type == RowType::Track) {
            newHeight = std::clamp(newHeight, theme::Layout::MinTrackHeight, theme::Layout::MaxTrackHeight);
            m_trackHeights[row.trackId.toRaw()] = newHeight;
            rebuildLayouts();
            emit trackHeightChanged(row.trackId, newHeight);
        }
        else if (row.type == RowType::Take) {
            newHeight = std::clamp(newHeight, theme::Layout::MinSubLaneHeight, theme::Layout::MaxSubLaneHeight);
            m_takesLaneHeights[{row.trackId.toRaw(), row.index}] = newHeight;
            rebuildLayouts();
            emit takesLaneHeightChanging(row.trackId, row.index, newHeight);
        }
        else if (row.type == RowType::Automation) {
            newHeight = std::clamp(newHeight, theme::Layout::MinSubLaneHeight, theme::Layout::MaxSubLaneHeight);
            m_autoSubLaneHeights[{row.trackId.toRaw(), row.index}] = newHeight;
            rebuildLayouts();
            emit automationSubLaneHeightChanging(row.trackId, row.index, newHeight);
        }

        event->accept();
        return;
    }

    // 2. Handle QDrag initiation for track reordering
    if ((event->buttons() & Qt::LeftButton) && m_pressedTrackId.isValid() && m_pressedRowType == RowType::Track && m_resizeRowIndex == -1) {
        if (m_pressedControl == VirtualControl::None || m_pressedControl == VirtualControl::NameLabel) {
            if ((event->position().toPoint() - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
                TrackID draggedId = m_pressedTrackId;

                // Reset press states before entering QDrag event loop
                m_pressedTrackId = TrackID::invalid();
                m_pressedControl = VirtualControl::None;

                QDrag* drag = new QDrag(this);
                QMimeData* mimeData = new QMimeData();

                QByteArray data;
                QDataStream stream(&data, QIODevice::WriteOnly);
                stream << draggedId.toRaw();
                mimeData->setData("application/x-daw-mixer-track-id", data);
                mimeData->setData("application/x-daw-track-id", data);
                drag->setMimeData(mimeData);

                for (const auto& row : m_layoutGeometries) {
                    if (row.type == RowType::Track && row.trackId == draggedId) {
                        QRect rowRect = row.rect.toRect();
                        if (rowRect.width() > 0 && rowRect.height() > 0) {
                            QPixmap pixmap = grab(rowRect);

                            // Create compact preview thumbnail (max 180x36)
                            QSize targetSize(std::min(rowRect.width(), 180), std::min(rowRect.height(), 36));
                            QPixmap scaledPixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

                            QPixmap semiTrans(scaledPixmap.size());
                            semiTrans.fill(Qt::transparent);
                            QPainter pTrans(&semiTrans);
                            pTrans.setRenderHint(QPainter::Antialiasing);
                            pTrans.setOpacity(0.75);
                            pTrans.drawPixmap(0, 0, scaledPixmap);
                            pTrans.setPen(QPen(theme::Color::AccentGlow, 1.5));
                            pTrans.drawRoundedRect(semiTrans.rect().adjusted(0, 0, -1, -1), 4.0, 4.0);
                            pTrans.end();

                            drag->setPixmap(semiTrans);

                            QPoint localClick = event->position().toPoint() - rowRect.topLeft();
                            double scaleX = static_cast<double>(scaledPixmap.width()) / static_cast<double>(rowRect.width());
                            double scaleY = static_cast<double>(scaledPixmap.height()) / static_cast<double>(rowRect.height());
                            drag->setHotSpot(QPoint(static_cast<int>(localClick.x() * scaleX), static_cast<int>(localClick.y() * scaleY)));
                        }
                        break;
                    }
                }

                drag->exec(Qt::MoveAction);
                return;
            }
        }
    }

    // 3. Otherwise update hover and cursor checks
    bool hoverBorder = false;
    for (const auto& row : m_layoutGeometries) {
        QRectF borderZone(row.rect.x(), row.rect.bottom() - 4.0, row.rect.width(), 4.0);
        if (borderZone.contains(pos)) {
            hoverBorder = true;
            break;
        }
    }
    setCursor(hoverBorder ? Qt::SizeVerCursor : Qt::ArrowCursor);

    // Map control hovered state
    TrackID prevHTrackId = m_hoveredTrackId;
    VirtualControl prevHCtrl = m_hoveredControl;

    m_hoveredControl = VirtualControl::None;
    for (const auto& row : m_layoutGeometries) {
        if (row.rect.contains(pos)) {
            m_hoveredTrackId = row.trackId;
            m_hoveredRowType = row.type;
            m_hoveredRowIndex = row.index;

            if (row.type == RowType::Track) {
                m_hoveredControl = TrackRowRenderer::hitTest(
                    pos, row.rect, row.muteRect, row.soloRect, row.armRect, row.monitorRect,
                    row.autoComboRect, row.autoExpandRect, row.takesExpandRect,
                    row.nameRect
                );
            }
            else if (row.type == RowType::Take) {
                m_hoveredControl = TakeRowRenderer::hitTest(pos, row.rect, row.promoteRect);
            }
            else if (row.type == RowType::Automation) {
                m_hoveredControl = AutomationRowRenderer::hitTest(pos, row.rect);
            }
            break;
        }
    }

    if (m_hoveredTrackId != prevHTrackId || m_hoveredControl != prevHCtrl) {
        update();
    }
}

void TrackHeaderView::mouseReleaseEvent(QMouseEvent* event)
{
    // 1. Commit live resize drag
    if (m_resizeRowIndex != -1 && m_resizeRowIndex < static_cast<int>(m_layoutGeometries.size())) {
        const auto& row = m_layoutGeometries[static_cast<size_t>(m_resizeRowIndex)];
        m_resizeRowIndex = -1;
        setCursor(Qt::ArrowCursor);

        if (row.type == RowType::Track) {
            // Height already committed in moveEvent
        }
        else if (row.type == RowType::Take) {
            int takeH = m_takesLaneHeights[{row.trackId.toRaw(), row.index}];
            emit takesLaneHeightChanged(row.trackId, row.index, takeH);
        }
        else if (row.type == RowType::Automation) {
            // persist height and reload
            auto it = m_autoSubLaneHeights.find({row.trackId.toRaw(), row.index});
            int autoH = (it != m_autoSubLaneHeights.end()) ? it->second : theme::Layout::DefaultSubLaneHeight;
            emit automationSubLaneHeightChanged(row.trackId, row.index, autoH);
        }

        event->accept();
        return;
    }

    // 2. Fire actions on click release
    if (event->button() == Qt::LeftButton) {
        QPointF pos = event->position();
        
        TrackID prevPressedId = m_pressedTrackId;
        RowType prevPressedType = m_pressedRowType;
        uint32_t prevPressedIdx = m_pressedRowIndex;
        VirtualControl prevPressedCtrl = m_pressedControl;

        m_pressedTrackId = TrackID::invalid();
        m_pressedControl = VirtualControl::None;

        for (const auto& row : m_layoutGeometries) {
            if (row.trackId == prevPressedId && row.type == prevPressedType && row.index == prevPressedIdx) {
                if (row.rect.contains(pos)) {
                    VirtualControl currentCtrl = VirtualControl::None;
                    if (row.type == RowType::Track) {
                        currentCtrl = TrackRowRenderer::hitTest(
                            pos, row.rect, row.muteRect, row.soloRect, row.armRect, row.monitorRect,
                            row.autoComboRect, row.autoExpandRect, row.takesExpandRect,
                            row.nameRect
                        );
                    }
                    else if (row.type == RowType::Take) {
                        currentCtrl = TakeRowRenderer::hitTest(pos, row.rect, row.promoteRect);
                    }

                    if (currentCtrl == prevPressedCtrl) {
                        // Dispatch actions
                        if (row.type == RowType::Track) {
                            if (currentCtrl == VirtualControl::MuteButton) {
                                bool isMuted = false;
                                for (const auto& t : m_tracks) {
                                    if (t.trackId == row.trackId) { isMuted = t.isMuted; break; }
                                }
                                emit trackMuteToggled(row.trackId, !isMuted);
                            }
                            else if (currentCtrl == VirtualControl::SoloButton) {
                                bool isSolo = false;
                                for (const auto& t : m_tracks) {
                                    if (t.trackId == row.trackId) { isSolo = t.isSoloed; break; }
                                }
                                emit trackSoloToggled(row.trackId, !isSolo);
                            }
                            else if (currentCtrl == VirtualControl::ArmButton) {
                                bool isArmed = false;
                                for (const auto& t : m_tracks) {
                                    if (t.trackId == row.trackId) { isArmed = t.isRecordArmed; break; }
                                }
                                emit trackArmToggled(row.trackId, !isArmed);
                            }
                            else if (currentCtrl == VirtualControl::MonitorButton) {
                                bool isMon = false;
                                for (const auto& t : m_tracks) {
                                    if (t.trackId == row.trackId) { isMon = t.isInputMonitoring; break; }
                                }
                                emit trackInputMonitorToggled(row.trackId, !isMon);
                            }
                            else if (currentCtrl == VirtualControl::AutomationCombo) {
                                // Spawn automation parameter mapping popup menu
                                QMenu modeMenu(this);
                                modeMenu.addAction(tr("Off"));
                                modeMenu.addAction(tr("Read"));
                                modeMenu.addAction(tr("Touch"));
                                modeMenu.addAction(tr("Latch"));
                                modeMenu.addAction(tr("Write"));
                                modeMenu.addAction(tr("Trim"));
                                QPoint menuPos = mapToGlobal(row.autoComboRect.bottomLeft().toPoint());
                                QAction* sel = modeMenu.exec(menuPos);
                                if (sel) {
                                    int index = 0;
                                    if (sel->text() == tr("Off")) index = 0;
                                    else if (sel->text() == tr("Read")) index = 1;
                                    else if (sel->text() == tr("Touch")) index = 2;
                                    else if (sel->text() == tr("Latch")) index = 3;
                                    else if (sel->text() == tr("Write")) index = 4;
                                    else if (sel->text() == tr("Trim")) index = 5;

                                    if (m_automation) {
                                        // Select and set record mode
                                        const bridge::TrackUIState* foundState = nullptr;
                                        for (const auto& t : m_tracks) {
                                            if (t.trackId == row.trackId) { foundState = &t; break; }
                                        }
                                        if (foundState && foundState->channelStripNode.isValid()) {
                                            m_automation->selectActiveAutomationLane(row.trackId, foundState->channelStripNode, 0, 0);
                                            m_automation->setRecorderMode(static_cast<AutomationMode>(index));
                                        }
                                    }
                                }
                            }
                            else if (currentCtrl == VirtualControl::AutomationExpand) {
                                bool isExpanded = false;
                                for (const auto& t : m_tracks) {
                                    if (t.trackId == row.trackId) { isExpanded = t.isAutomationExpanded; break; }
                                }
                                m_track->setAutomationExpanded(row.trackId, !isExpanded);
                                emit automationExpansionToggled(row.trackId);
                            }
                            else if (currentCtrl == VirtualControl::TakesExpand) {
                                bool isExpanded = false;
                                for (const auto& t : m_tracks) {
                                    if (t.trackId == row.trackId) { isExpanded = t.isTakesExpanded; break; }
                                }
                                m_track->setTakesExpanded(row.trackId, !isExpanded);
                                emit takesExpansionToggled(row.trackId);
                            }
                        }
                        else if (row.type == RowType::Take) {
                            if (currentCtrl == VirtualControl::PromoteButton) {
                                emit takePromoteRequested(row.trackId, row.index);
                            }
                        }
                    }
                }
                break;
            }
        }
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void TrackHeaderView::leaveEvent(QEvent* event)
{
    m_hoveredTrackId = TrackID::invalid();
    m_hoveredControl = VirtualControl::None;
    m_pressedTrackId = TrackID::invalid();
    m_pressedControl = VirtualControl::None;
    setCursor(Qt::ArrowCursor);
    update();
    QWidget::leaveEvent(event);
}

void TrackHeaderView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildLayouts();
}

void TrackHeaderView::contextMenuEvent(QContextMenuEvent* event)
{
    QPoint clickPos = event->pos();
    for (const auto& row : m_layoutGeometries) {
        if (row.type == RowType::Track && row.rect.contains(clickPos)) {
            showContextMenu(row.trackId, event->globalPos());
            event->accept();
            return;
        }
    }
    QWidget::contextMenuEvent(event);
}

void TrackHeaderView::rebuildLayouts()
{
    m_layoutGeometries.clear();
    double yOffset = 0.0;
    const double w = width();

    // Map of active tracks to clean up deleted ones
    std::unordered_set<uint64_t> activeTrackIds;

    for (const auto& t : m_tracks) {
        activeTrackIds.insert(t.trackId.toRaw());

        int height = static_cast<int>(bridge::mainLaneHeightForTrack(t));
        auto it = m_trackHeights.find(t.trackId.toRaw());
        if (it != m_trackHeights.end()) {
            height = it->second;
        } else {
            m_trackHeights[t.trackId.toRaw()] = height;
        }

        RowLayout trackRow;
        trackRow.type = RowType::Track;
        trackRow.trackId = t.trackId;
        trackRow.index = 0;
        trackRow.localY = yOffset;
        trackRow.height = height;
        trackRow.rect = QRectF(0.0, yOffset - m_verticalOffsetPx, w, height);

        int depth = getTrackDepth(t.trackId);
        TrackRowRenderer::calculateControlRects(
            trackRow.rect, t, depth,
            trackRow.muteRect, trackRow.soloRect, trackRow.armRect, trackRow.monitorRect,
            trackRow.autoComboRect, trackRow.autoExpandRect, trackRow.takesExpandRect,
            trackRow.nameRect
        );

        // Position and update slot widgets
        if (t.hasInstrumentSlot) {
            auto*& slot = m_instrumentWidgets[t.trackId.toRaw()];
            if (!slot) {
                slot = new InstrumentSlotWidget(this);
                slot->bind(m_track, t.trackId);
                connect(slot, &InstrumentSlotWidget::pluginChanged, this, [this]() {
                    if (m_track) {
                        updateTrackStates(m_track->getAllTracks(), m_takesLaneHeights);
                    }
                });
            }

            if (t.instrument.pluginNodeId.isValid()) {
                slot->updateFromState(t.instrument);
            } else {
                slot->setAsPlusButton();
            }

            if (height < 90.0) {
                slot->setVisible(false);
            } else {
                slot->setVisible(true);
                slot->setGeometry(QRect(12, static_cast<int>(trackRow.rect.y() + 32.0), static_cast<int>(w - 56.0), 20));
            }
        } else {
            auto itSlot = m_instrumentWidgets.find(t.trackId.toRaw());
            if (itSlot != m_instrumentWidgets.end()) {
                if (itSlot->second) itSlot->second->deleteLater();
                m_instrumentWidgets.erase(itSlot);
            }
        }

        if (t.trackInput.hasInputSlot && !t.hasInstrumentSlot) {
            auto*& slot = m_audioInputWidgets[t.trackId.toRaw()];
            if (!slot) {
                slot = new AudioInputSlotWidget(this);
                slot->bind(m_track, t.trackId);
            }

            slot->updateFromState(t.trackInput);

            if (height < 90.0) {
                slot->setVisible(false);
            } else {
                slot->setVisible(true);
                slot->setGeometry(QRect(12, static_cast<int>(trackRow.rect.y() + 32.0), static_cast<int>(w - 56.0), 20));
            }
        } else {
            auto itSlot = m_audioInputWidgets.find(t.trackId.toRaw());
            if (itSlot != m_audioInputWidgets.end()) {
                if (itSlot->second) itSlot->second->deleteLater();
                m_audioInputWidgets.erase(itSlot);
            }
        }

        m_layoutGeometries.push_back(trackRow);
        yOffset += height;

        // Take Lanes
        if (t.isTakesExpanded && t.audioLanesCount > 1) {
            for (uint32_t s = 1; s < t.audioLanesCount; ++s) {
                auto itHeights = m_takesLaneHeights.find({t.trackId.toRaw(), s});
                int takeH = (itHeights != m_takesLaneHeights.end()) ? itHeights->second : theme::Layout::DefaultSubLaneHeight;

                RowLayout takeRow;
                takeRow.type = RowType::Take;
                takeRow.trackId = t.trackId;
                takeRow.index = s;
                takeRow.localY = yOffset;
                takeRow.height = takeH;
                takeRow.rect = QRectF(0.0, yOffset - m_verticalOffsetPx, w, takeH);
                takeRow.promoteRect = QRectF(w - 28.0, yOffset - m_verticalOffsetPx + (takeH - 20.0) / 2.0, 20.0, 20.0);

                m_layoutGeometries.push_back(takeRow);
                yOffset += takeH;
            }
        }

        // Automation Lanes
        if (t.isAutomationExpanded) {
            for (uint32_t s = 0; s < t.activeSubLaneCount; ++s) {
                if (t.subLanes[s].isExpanded) {
                    auto itHeights = m_autoSubLaneHeights.find({t.trackId.toRaw(), s});
                    int autoH = (itHeights != m_autoSubLaneHeights.end()) ? itHeights->second : static_cast<int>(t.subLanes[s].heightPx);

                    RowLayout autoRow;
                    autoRow.type = RowType::Automation;
                    autoRow.trackId = t.trackId;
                    autoRow.index = s;
                    autoRow.localY = yOffset;
                    autoRow.height = autoH;
                    autoRow.rect = QRectF(0.0, yOffset - m_verticalOffsetPx, w, autoH);

                    m_layoutGeometries.push_back(autoRow);
                    yOffset += autoH;
                }
            }
        }
    }

    // Clean up widgets for tracks that no longer exist
    for (auto it = m_instrumentWidgets.begin(); it != m_instrumentWidgets.end();) {
        if (activeTrackIds.find(it->first) == activeTrackIds.end()) {
            if (it->second) it->second->deleteLater();
            it = m_instrumentWidgets.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_audioInputWidgets.begin(); it != m_audioInputWidgets.end();) {
        if (activeTrackIds.find(it->first) == activeTrackIds.end()) {
            if (it->second) it->second->deleteLater();
            it = m_audioInputWidgets.erase(it);
        } else {
            ++it;
        }
    }

    updateAddTrackButtonGeometry();
    update();
}

void TrackHeaderView::updateAddTrackButtonGeometry()
{
    if (!m_addTrackBtn) return;
    double totalHeight = 0.0;
    if (!m_layoutGeometries.empty()) {
        const auto& last = m_layoutGeometries.back();
        totalHeight = last.localY + last.height;
    }
    int btnY = static_cast<int>(totalHeight - m_verticalOffsetPx + 4);
    m_addTrackBtn->setGeometry(8, btnY, width() - 16, kFooterHeight - 8);
    m_addTrackBtn->setVisible(true);
}

void TrackHeaderView::showContextMenu(TrackID trackId, const QPoint& globalPos)
{
    QMenu menu(this);

    auto* renameAct = menu.addAction(tr("Rename"));
    auto* colorAct = menu.addAction(tr("Change Color..."));
    
    menu.addSeparator();

    // Scale Track Size
    auto* scaleMenu = menu.addMenu(tr("Scale Track Size"));

    auto* scale25Act = scaleMenu->addAction(tr("25%"));
    auto* scale50Act = scaleMenu->addAction(tr("50%"));
    auto* scale75Act = scaleMenu->addAction(tr("75%"));
    auto* scale100Act = scaleMenu->addAction(tr("100%"));
    auto* scale150Act = scaleMenu->addAction(tr("150%"));
    auto* scale200Act = scaleMenu->addAction(tr("200%"));

    menu.addSeparator();

    auto* copyAct = menu.addAction(tr("Copy Track"));
    auto* muteAllClipsAct = menu.addAction(tr("Mute All Clips"));
    auto* unmuteAllClipsAct = menu.addAction(tr("Unmute All Clips"));

    menu.addSeparator();
    auto* showParamsAct = menu.addAction(tr("Configure Automation..."));

    menu.addSeparator();

    std::vector<bridge::TrackUIState> allTracks = m_track->getAllTracks();
    std::vector<TrackID> selectedTracks;
    for (const auto& t : allTracks) {
        if (t.isSelected) selectedTracks.push_back(t.trackId);
    }
    if (!m_track->getTrackState(trackId).isSelected) {
        selectedTracks.clear();
        selectedTracks.push_back(trackId);
        m_track->clearTrackSelection();
        m_track->setTrackSelected(trackId, true);
    }

    bool multiDelete = selectedTracks.size() > 1;
    auto* deleteAct = menu.addAction(multiDelete ? tr("Delete Selected Tracks") : tr("Delete Track"));

    QAction* selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == showParamsAct) {
        emit configureAutomationRequested(trackId);
        return;
    }

    // Find current index
    std::vector<bridge::TrackUIState> tracks = m_track->getAllTracks();
    uint32_t currentIndex = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].trackId == trackId) {
            currentIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    if (selected == renameAct) {
        bool ok = false;
        QString oldName = QString::fromUtf8(m_track->getTrackState(trackId).name);
        QString newName = DAWInputDialog::getText(this, tr("Rename Track"),
                                                tr("New Name:"),
                                                oldName, &ok);
        if (ok && !newName.trimmed().isEmpty()) {
            emit trackRenameRequested(trackId, newName.trimmed());
        }
    } else if (selected == colorAct) {
        QColor oldColor = QColor::fromRgba(m_track->getTrackState(trackId).colorARGB);
        QColor newColor = QColorDialog::getColor(oldColor, this, tr("Select Track Color"));
        if (newColor.isValid()) {
            for (TrackID selId : selectedTracks) {
                emit trackColorChangeRequested(selId, newColor.rgba());
            }
        }
    } else if (selected == scale25Act) {
        int newH = 18;
        for (TrackID selId : selectedTracks) {
            m_trackHeights[selId.toRaw()] = newH;
            emit trackHeightChanged(selId, newH);
        }
        rebuildLayouts();
    } else if (selected == scale50Act) {
        int newH = 36;
        for (TrackID selId : selectedTracks) {
            m_trackHeights[selId.toRaw()] = newH;
            emit trackHeightChanged(selId, newH);
        }
        rebuildLayouts();
    } else if (selected == scale75Act) {
        int newH = 54;
        for (TrackID selId : selectedTracks) {
            m_trackHeights[selId.toRaw()] = newH;
            emit trackHeightChanged(selId, newH);
        }
        rebuildLayouts();
    } else if (selected == scale100Act) {
        int newH = static_cast<int>(bridge::kMainLaneHeightDefault);
        for (TrackID selId : selectedTracks) {
            m_trackHeights[selId.toRaw()] = newH;
            emit trackHeightChanged(selId, newH);
        }
        rebuildLayouts();
    } else if (selected == scale150Act) {
        int newH = 108;
        for (TrackID selId : selectedTracks) {
            m_trackHeights[selId.toRaw()] = newH;
            emit trackHeightChanged(selId, newH);
        }
        rebuildLayouts();
    } else if (selected == scale200Act) {
        int newH = 144;
        for (TrackID selId : selectedTracks) {
            m_trackHeights[selId.toRaw()] = newH;
            emit trackHeightChanged(selId, newH);
        }
        rebuildLayouts();
    } else if (selected == copyAct) {
        m_track->cloneTrack(trackId);
        TrackID parentId = m_track->getTrackState(trackId).parentFolderId;
        emit trackMoveRequested(trackId, currentIndex, parentId);
    } else if (selected == muteAllClipsAct) {
        for (TrackID selId : selectedTracks) {
            m_track->muteAllClips(selId, true);
        }
    } else if (selected == unmuteAllClipsAct) {
        for (TrackID selId : selectedTracks) {
            m_track->muteAllClips(selId, false);
        }
    } else if (selected == deleteAct) {
        for (auto id : selectedTracks) {
            emit trackDeleteRequested(id);
        }
    }
}

void TrackHeaderView::drawFolderTrays(QPainter& p)
{
    if (!m_track) return;
    const double widgetW = static_cast<double>(width());

    auto isDescendantOf = [](TrackID childId, TrackID targetParentId, const std::vector<bridge::TrackUIState>& allTracks) -> bool {
        TrackID current = childId;
        int maxDepth = 1000;
        while (current.isValid() && maxDepth-- > 0) {
            bool found = false;
            for (const auto& t : allTracks) {
                if (t.trackId == current) {
                    if (t.parentFolderId == targetParentId) return true;
                    current = t.parentFolderId;
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        return false;
    };

    struct FolderTrayInfo {
        bridge::TrackUIState folderTrack;
        int depth;
        double minY;
        double maxY;
    };
    std::vector<FolderTrayInfo> foldersToDraw;

    for (const auto& folderTrack : m_tracks) {
        if (folderTrack.type != composition::TrackType::FOLDER) continue;

        double minY = -1.0;
        double maxY = -1.0;

        // Find geometry of folder track row
        for (const auto& row : m_layoutGeometries) {
            if (row.type == RowType::Track && row.trackId == folderTrack.trackId) {
                minY = row.localY;
                maxY = row.localY + row.height;
                break;
            }
        }
        if (minY < 0.0) continue;

        // Update minY/maxY with descendants
        for (const auto& child : m_tracks) {
            if (isDescendantOf(child.trackId, folderTrack.trackId, m_tracks)) {
                for (const auto& row : m_layoutGeometries) {
                    if (row.trackId == child.trackId) {
                        minY = std::min(minY, row.localY);
                        maxY = std::max(maxY, row.localY + row.height);
                    }
                }
            }
        }

        // Also check if folder track has its own takes or automation expanded
        for (const auto& row : m_layoutGeometries) {
            if (row.trackId == folderTrack.trackId) {
                minY = std::min(minY, row.localY);
                maxY = std::max(maxY, row.localY + row.height);
            }
        }

        // Compute depth
        int depth = 0;
        TrackID curr = folderTrack.parentFolderId;
        int maxDepth = 1000;
        while (curr.isValid() && maxDepth-- > 0) {
            depth++;
            bool found = false;
            for (const auto& t : m_tracks) {
                if (t.trackId == curr) {
                    curr = t.parentFolderId;
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }

        foldersToDraw.push_back({folderTrack, depth, minY, maxY});
    }

    std::sort(foldersToDraw.begin(), foldersToDraw.end(), [](const FolderTrayInfo& a, const FolderTrayInfo& b) {
        return a.depth < b.depth;
    });

    for (const auto& info : foldersToDraw) {
        double trayX = 4.0;
        double trayW = widgetW - 8.0;
        double trayY = info.minY - m_verticalOffsetPx;
        double trayH = info.maxY - info.minY;

        QRectF trayRect(trayX, trayY, trayW, trayH);
        QColor color = QColor::fromRgba(info.folderTrack.colorARGB);

        QColor fillColor = color;
        fillColor.setAlpha(40);
        p.setBrush(QBrush(fillColor));

        QColor borderCol = color;
        borderCol.setAlpha(90);
        p.setPen(QPen(borderCol, 1.0, Qt::SolidLine));

        p.drawRoundedRect(trayRect, 12.0, 12.0);
    }
}

void TrackHeaderView::onSelectionRequested(TrackID trackId, bool multiSelect, bool rangeSelect)
{
    int currentIndex = -1;
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].trackId == trackId) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }
    if (currentIndex == -1) return;

    if (rangeSelect && m_lastSelectedTrackIndex != -1) {
        m_track->clearTrackSelection();
        int start = std::min(currentIndex, m_lastSelectedTrackIndex);
        int end = std::max(currentIndex, m_lastSelectedTrackIndex);
        for (int i = start; i <= end; ++i) {
            m_track->setTrackSelected(m_tracks[static_cast<size_t>(i)].trackId, true);
        }
    } else if (multiSelect) {
        bool isCurrentlySelected = m_track->getTrackState(trackId).isSelected;
        m_track->setTrackSelected(trackId, !isCurrentlySelected);
        m_lastSelectedTrackIndex = currentIndex;
    } else {
        m_track->clearTrackSelection();
        m_track->setTrackSelected(trackId, true);
        m_lastSelectedTrackIndex = currentIndex;
    }

    // Instantly refresh UI state
    std::vector<bridge::TrackUIState> updated = m_track->getAllTracks();
    updateTrackStates(updated, m_takesLaneHeights);
}

int TrackHeaderView::getTrackDepth(TrackID trackId) const
{
    int depth = 0;
    if (!m_track) return 0;
    TrackID parentId = m_track->getTrackState(trackId).parentFolderId;
    while (parentId.isValid()) {
        depth++;
        if (depth > 10) break;
        parentId = m_track->getTrackState(parentId).parentFolderId;
    }
    return depth;
}

void TrackHeaderView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-daw-mixer-track-id") ||
        event->mimeData()->hasFormat("application/x-daw-track-id")) {
        event->acceptProposedAction();
    }
}

void TrackHeaderView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasFormat("application/x-daw-mixer-track-id") &&
        !event->mimeData()->hasFormat("application/x-daw-track-id")) return;

    QByteArray data = event->mimeData()->data("application/x-daw-mixer-track-id");
    if (data.isEmpty()) {
        data = event->mimeData()->data("application/x-daw-track-id");
    }
    QDataStream stream(&data, QIODevice::ReadOnly);
    uint64_t rawId = 0;
    stream >> rawId;
    TrackID draggedId = TrackID::fromRaw(rawId);

    QPointF pos = event->position();
    TrackID dropTargetId = TrackID::invalid();
    DropAction dropAction = DropAction::None;
    double dropY = 0.0;
    QRectF dropRect;

    for (size_t i = 0; i < m_layoutGeometries.size(); ++i) {
        const auto& row = m_layoutGeometries[i];

        if (pos.y() >= row.rect.top() && pos.y() <= row.rect.bottom()) {
            dropTargetId = row.trackId;

            const RowLayout* trackRow = &row;
            if (row.type != RowType::Track) {
                for (const auto& r : m_layoutGeometries) {
                    if (r.type == RowType::Track && r.trackId == row.trackId) {
                        trackRow = &r;
                        break;
                    }
                }
            }

            const bridge::TrackUIState* state = nullptr;
            for (const auto& t : m_tracks) {
                if (t.trackId == row.trackId) {
                    state = &t;
                    break;
                }
            }

            double relY = (pos.y() - row.rect.top()) / row.rect.height();

            bool isFolder = (state && state->type == composition::TrackType::FOLDER);
            bool isDraggedFolderOrSelf = (draggedId == row.trackId || isTrackDescendantOf(row.trackId, draggedId));

            if (isFolder && !isDraggedFolderOrSelf) {
                if (relY < 0.25) {
                    dropAction = DropAction::InsertBefore;
                    dropY = trackRow->rect.top();
                } else if (relY > 0.75) {
                    dropAction = DropAction::InsertAfter;
                    dropY = trackRow->rect.bottom();
                } else {
                    dropAction = DropAction::DropInto;
                    dropRect = trackRow->rect;
                }
            } else {
                if (relY < 0.50) {
                    dropAction = DropAction::InsertBefore;
                    dropY = trackRow->rect.top();
                } else {
                    dropAction = DropAction::InsertAfter;
                    dropY = trackRow->rect.bottom();
                }
            }
            break;
        }
    }

    if (dropAction == DropAction::None && !m_layoutGeometries.empty()) {
        const auto& lastRow = m_layoutGeometries.back();
        if (pos.y() > lastRow.rect.bottom()) {
            dropTargetId = lastRow.trackId;
            dropAction = DropAction::InsertAfter;
            dropY = lastRow.rect.bottom();
        }
    }

    if (m_dropTargetId != dropTargetId || m_dropAction != dropAction || m_dropIndicatorY != dropY || m_dropRect != dropRect) {
        m_dropTargetId = dropTargetId;
        m_dropAction = dropAction;
        m_dropIndicatorY = dropY;
        m_dropRect = dropRect;
        update();
    }

    event->acceptProposedAction();
}

void TrackHeaderView::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_dropTargetId = TrackID::invalid();
    m_dropAction = DropAction::None;
    update();
    event->accept();
}

void TrackHeaderView::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat("application/x-daw-mixer-track-id") &&
        !event->mimeData()->hasFormat("application/x-daw-track-id")) return;

    QByteArray data = event->mimeData()->data("application/x-daw-mixer-track-id");
    if (data.isEmpty()) {
        data = event->mimeData()->data("application/x-daw-track-id");
    }
    QDataStream stream(&data, QIODevice::ReadOnly);
    uint64_t rawId = 0;
    stream >> rawId;
    TrackID draggedId = TrackID::fromRaw(rawId);

    TrackID finalTargetId = m_dropTargetId;
    DropAction finalAction = m_dropAction;

    m_dropTargetId = TrackID::invalid();
    m_dropAction = DropAction::None;
    update();

    handleTrackDrop(draggedId, finalTargetId, finalAction);
    event->acceptProposedAction();
}

void TrackHeaderView::handleTrackDrop(TrackID draggedId, TrackID targetId, DropAction action)
{
    if (!m_track || !targetId.isValid() || action == DropAction::None || draggedId == targetId) return;

    auto tracks = m_track->getAllTracks();
    uint32_t targetIndex = 0;
    TrackID targetParent = TrackID::invalid();
    bool found = false;

    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].trackId == targetId) {
            targetIndex = static_cast<uint32_t>(i);
            targetParent = tracks[i].parentFolderId;
            found = true;
            break;
        }
    }

    if (found) {
        uint32_t newIndex = targetIndex;
        TrackID newParent = targetParent;

        if (action == DropAction::InsertBefore) {
            newIndex = targetIndex;
            newParent = targetParent;
        } else if (action == DropAction::InsertAfter) {
            newIndex = targetIndex + 1;
            // If target is the last track in the list or the last descendant of its parent folder,
            // dropping InsertAfter un-nests to the parent folder's parent (root level if parent is root).
            if (targetIndex + 1 == tracks.size() ||
                (targetIndex + 1 < tracks.size() && tracks[targetIndex + 1].parentFolderId != targetParent)) {
                TrackID rootParent = TrackID::invalid();
                if (targetParent.isValid()) {
                    for (const auto& t : tracks) {
                        if (t.trackId == targetParent) {
                            rootParent = t.parentFolderId;
                            break;
                        }
                    }
                }
                newParent = rootParent;
            } else {
                newParent = targetParent;
            }
        } else if (action == DropAction::DropInto) {
            newIndex = targetIndex + 1;
            newParent = targetId;
        }

        emit trackMoveRequested(draggedId, newIndex, newParent);

        TrackID oldParent = TrackID::invalid();
        for (const auto& t : tracks) {
            if (t.trackId == draggedId) {
                oldParent = t.parentFolderId;
                break;
            }
        }
        if (oldParent != newParent) {
            m_track->setTrackOutputRouting(draggedId, newParent);
        }
    }
}

bool TrackHeaderView::isTrackDescendantOf(TrackID childId, TrackID potentialAncestorId) const
{
    if (!childId.isValid() || !potentialAncestorId.isValid()) return false;
    TrackID curr = childId;
    int maxDepth = 1000;
    while (curr.isValid() && maxDepth-- > 0) {
        if (curr == potentialAncestorId) return true;
        bool found = false;
        for (const auto& t : m_tracks) {
            if (t.trackId == curr) {
                curr = t.parentFolderId;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    return false;
}

} // namespace presentation::views
