// src/Presentation/views/mixer/mixer_window.cpp
#include "mixer_window.h"
#include "../theme.h"

#include <QPainter>
#include <QPen>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QSizePolicy>
#include <algorithm>

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#include <QShortcut>
#include "../shortcuts/ShortcutManager.h"
#include <QLineEdit>
#include <QTextEdit>

namespace presentation::views {

class MixerScrollContent : public QWidget {
public:
    explicit MixerScrollContent(MixerWindow* win, QWidget* parent = nullptr)
        : QWidget(parent), m_mixerWindow(win)
    {
        setAcceptDrops(true);
        setFocusPolicy(Qt::ClickFocus);
    }

    TrackID m_dropTargetId{};
    presentation::views::DropAction m_dropAction{presentation::views::DropAction::None};
    int m_dropIndicatorX{0};
    QRectF m_dropRect;
    MixerWindow* m_mixerWindow{nullptr};

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat("application/x-daw-mixer-track-id")) {
            event->acceptProposedAction();
        }
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (!event->mimeData()->hasFormat("application/x-daw-mixer-track-id")) return;
        if (!m_mixerWindow || !m_mixerWindow->controller()) return;

        TrackID dropTargetId = TrackID::invalid();
        presentation::views::DropAction dropAction = presentation::views::DropAction::None;
        int dropX = 8;
        QRectF dropRect;

        const auto& strips = m_mixerWindow->trackStrips();
        for (size_t i = 0; i < strips.size(); ++i) {
            ChannelStripWidget* w = strips[i];
            if (w->isHidden()) continue;

            if (event->position().x() >= w->geometry().left() && event->position().x() <= w->geometry().right()) {
                dropTargetId = w->trackId();
                double relX = static_cast<double>(event->position().x() - w->geometry().left()) / static_cast<double>(w->width());
                if (relX < 0.25) {
                    dropAction = presentation::views::DropAction::InsertBefore;
                    dropX = w->geometry().left() - 3;
                } else if (relX > 0.75) {
                    dropAction = presentation::views::DropAction::InsertAfter;
                    dropX = w->geometry().right() + 3;
                } else {
                    auto state = m_mixerWindow->controller()->getTrackState(w->trackId());
                    if (state.type == composition::TrackType::FOLDER) {
                        dropAction = presentation::views::DropAction::DropInto;
                        dropRect = w->geometry();
                    } else {
                        if (relX < 0.5) {
                            dropAction = presentation::views::DropAction::InsertBefore;
                            dropX = w->geometry().left() - 3;
                        } else {
                            dropAction = presentation::views::DropAction::InsertAfter;
                            dropX = w->geometry().right() + 3;
                        }
                    }
                }
                break;
            } else if (event->position().x() < w->geometry().left()) {
                dropTargetId = w->trackId();
                dropAction = presentation::views::DropAction::InsertBefore;
                dropX = w->geometry().left() - 3;
                break;
            }
        }

        if (dropAction == presentation::views::DropAction::None && !strips.empty()) {
            for (auto it = strips.rbegin(); it != strips.rend(); ++it) {
                if (!(*it)->isHidden()) {
                    dropTargetId = (*it)->trackId();
                    dropAction = presentation::views::DropAction::InsertAfter;
                    dropX = (*it)->geometry().right() + 3;
                    break;
                }
            }
        }

        if (m_dropTargetId != dropTargetId || m_dropAction != dropAction || m_dropIndicatorX != dropX || m_dropRect != dropRect) {
            m_dropTargetId = dropTargetId;
            m_dropAction = dropAction;
            m_dropIndicatorX = dropX;
            m_dropRect = dropRect;
            update();
        }

        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        m_dropTargetId = TrackID::invalid();
        m_dropAction = presentation::views::DropAction::None;
        update();
        event->accept();
    }

    void dropEvent(QDropEvent* event) override {
        if (!event->mimeData()->hasFormat("application/x-daw-mixer-track-id")) return;
        if (!m_mixerWindow) return;

        QByteArray data = event->mimeData()->data("application/x-daw-mixer-track-id");
        QDataStream stream(&data, QIODevice::ReadOnly);
        uint64_t rawId;
        stream >> rawId;
        TrackID draggedId = TrackID::fromRaw(rawId);

        TrackID finalDropTargetId = m_dropTargetId;
        presentation::views::DropAction finalDropAction = m_dropAction;
        
        m_dropTargetId = TrackID::invalid();
        m_dropAction = presentation::views::DropAction::None;
        update();

        m_mixerWindow->handleTrackDrop(draggedId, finalDropTargetId, finalDropAction);
        event->acceptProposedAction();
    }

    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
        if (m_mixerWindow) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            m_mixerWindow->drawFolderTrays(painter);

            if (m_dropAction == presentation::views::DropAction::InsertBefore || m_dropAction == presentation::views::DropAction::InsertAfter) {
                painter.setPen(QPen(theme::Color::AccentGlow, 4.0, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(m_dropIndicatorX, 10, m_dropIndicatorX, height() - 10);
            } else if (m_dropAction == presentation::views::DropAction::DropInto) {
                painter.save();
                QColor hoverBg = theme::Color::AccentGlow;
                hoverBg.setAlpha(30);
                QColor hoverPen = theme::Color::AccentGlow;
                hoverPen.setAlpha(160);
                painter.fillRect(m_dropRect, hoverBg);
                painter.setPen(QPen(hoverPen, 2.0));
                painter.drawRect(m_dropRect);
                painter.restore();
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MixerWindow::MixerWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Mixer");
    setMinimumWidth(400);
    setFocusPolicy(Qt::StrongFocus);
    
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        resize(screen->availableGeometry().width(), 600);
    } else {
        resize(800, 600); // Set a good default initial size
    }
    
    buildLayout();
    updateMinimumHeight();
    installKeyboardShortcuts();
    
    // Position the mixer window at the bottom of the screen
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QRect avail = screen->availableGeometry();
        move(avail.x(), avail.y() + avail.height() - height());
    }
}

void MixerWindow::buildLayout()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // ── Options Bar ──────────────────────────────────────────────────────
    m_optionsBar = new QWidget(this);
    m_optionsBar->setFixedHeight(36);
    m_optionsBar->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;").arg(theme::Color::BgBase.name()).arg(theme::Color::BgControl.name()));

    auto* optLayout = new QHBoxLayout(m_optionsBar);
    optLayout->setContentsMargins(8, 2, 8, 2);
    optLayout->setSpacing(8);

    QLabel* title = new QLabel("MIXER", m_optionsBar);
    title->setFont(theme::Font::monospace(7, QFont::Bold));
    title->setStyleSheet(QString("color: %1; background: transparent;").arg(theme::Color::TextMuted.name()));
    optLayout->addWidget(title);

    optLayout->addStretch();

    m_sendsToggleBtn = new QPushButton("⚙ Sends", m_optionsBar);
    m_sendsToggleBtn->setFont(theme::Font::monospace(7));
    m_sendsToggleBtn->setCheckable(true);
    m_sendsToggleBtn->setChecked(true);
    m_sendsToggleBtn->setFixedHeight(28);
    m_sendsToggleBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 4px;"
        "  padding: 0 8px;"
        "}"
        "QPushButton:checked {"
        "  background-color: #211B35;"
        "  color: %4;"
        "  border-color: %4;"
        "}"
    ).arg(theme::Color::BgControl.name())
     .arg(theme::Color::TextMuted.name())
     .arg(theme::Color::BgSurface.name())
     .arg(theme::Color::AccentGlow.name()));
    optLayout->addWidget(m_sendsToggleBtn);
    connect(m_sendsToggleBtn, &QPushButton::toggled,
            this, &MixerWindow::toggleSendsVisible);

    m_rootLayout->addWidget(m_optionsBar);

    // ── Main Content: scrollable strips + master strip side by side ───────
    auto* mainContent = new QWidget(this);
    auto* mainRow     = new QHBoxLayout(mainContent);
    mainRow->setContentsMargins(0, 0, 0, 0);
    mainRow->setSpacing(0);

    // Scrollable area for track strips
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_scrollArea->setStyleSheet(QString(
        "QScrollArea { background-color: %1; }"
        "QScrollBar:horizontal {"
        "  background: %2; height: 8px; border: none;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: %3; border-radius: 4px; min-width: 20px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "  background: %4;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "}")
        .arg(theme::Color::BgBase.name())
        .arg(theme::Color::BgSurface.name())
        .arg(theme::Color::TextMuted.name())
        .arg(theme::Color::AccentGlow.name()));

    m_scrollContent = new MixerScrollContent(this);
    m_scrollContent->setStyleSheet(QString("background-color: %1;").arg(theme::Color::BgBase.name()));
    m_stripLayout = new QHBoxLayout(m_scrollContent);
    m_stripLayout->setContentsMargins(8, 8, 8, 36);
    m_stripLayout->setSpacing(6);
    m_stripLayout->addStretch(); // Push strips left; stretch absorbs empty space

    m_scrollArea->setWidget(m_scrollContent);
    m_scrollArea->setWidgetResizable(true);
    mainRow->addWidget(m_scrollArea, 1);

    // 1 px vertical divider
    m_divider = new QFrame(this);
    m_divider->setFrameShape(QFrame::VLine);
    m_divider->setStyleSheet(QString("background-color: %1; min-width: 1px; max-width: 1px; border: none;").arg(theme::Color::BgControl.name()));
    mainRow->addWidget(m_divider);

    // Master strip (fixed, anchored right)
    m_masterStrip = new ChannelStripWidget(true /*isMaster*/, this);
    QMargins masterMargins(4, 8, 8, 8);
    m_masterStrip->setContentsMargins(masterMargins);
    mainRow->addWidget(m_masterStrip);

    m_rootLayout->addWidget(mainContent, 1);
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void MixerWindow::bind(bridge::ITrackController*  controller,
                       bridge::IMeteringProvider* meteringProvider,
                       bridge::IAutomationController* automation)
{
    m_controller       = controller;
    m_meteringProvider = meteringProvider;
    m_automation       = automation;

    if (m_masterStrip) {
        m_masterStrip->bind(controller, automation, TrackID{0, 0}, "MASTER", 0xFFFF0000);
        if (m_controller) {
            m_masterStrip->updateFromState(m_controller->getTrackState(TrackID{0, 0}));
        }
        if (meteringProvider) {
            m_masterStrip->meter()->setMeteringProvider(meteringProvider, TrackID{0, 0}, true);
        }
    }

    rebuildStrips();
}

void MixerWindow::rebuildStrips()
{
    if (!m_controller) return;

    // Remove existing strips (don't delete — Qt parent ownership handles it)
    for (auto* strip : m_strips) {
        m_stripLayout->removeWidget(strip);
        strip->deleteLater();
    }
    m_strips.clear();
    m_stripMap.clear();

    // Remove the stretch placeholder before re-inserting strips
    QLayoutItem* stretch = m_stripLayout->takeAt(m_stripLayout->count() - 1);

    // Build one strip per track
    auto tracks = m_controller->getAllTracks();
    for (auto& state : tracks) {
        auto* strip = new ChannelStripWidget(false /*isMaster*/, m_scrollContent);
        QString name = QString::fromUtf8(state.name);
        strip->bind(m_controller, m_automation, state.trackId, name, state.colorARGB);
        strip->updateFromState(state);

        if (m_meteringProvider) {
            strip->meter()->setMeteringProvider(m_meteringProvider, state.trackId);
        }

        // Restore collapsed state if already in our collapsed list
        if (m_collapsedFolders.count(state.trackId.toRaw())) {
            strip->setCollapsedState(true);
        }

        // Connect collapse state toggle signal
        connect(strip, &ChannelStripWidget::collapseStateToggled,
                this, &MixerWindow::onFolderCollapseToggled);
        connect(strip, &ChannelStripWidget::selectionRequested,
                this, &MixerWindow::onSelectionRequested);

        m_stripMap[state.trackId.toRaw()] = strip;

        // Apply visibility depending on ancestor collapse state
        const bool visible = isTrackVisible(state.trackId, tracks);
        strip->setVisible(visible);

        m_stripLayout->addWidget(strip);
        m_strips.push_back(strip);
    }

    // Re-append stretch
    if (stretch) {
        m_stripLayout->addItem(stretch);
    } else {
        m_stripLayout->addStretch();
    }

    // Force layout activation so sizeHint() returns correct values
    m_stripLayout->activate();
    m_scrollContent->adjustSize();

    // Resize scroll content to fit only visible strips
    int totalWidth = 8 + 8; // margins
    for (auto* s : m_strips) {
        if (!s->isHidden()) {
            totalWidth += s->minimumWidth() + 6;
        }
    }
    m_scrollContent->setMinimumWidth(totalWidth);

    updateMinimumHeight();
    m_scrollContent->update(); // Redraw folder trays overlay
}

// ---------------------------------------------------------------------------
// 60 Hz update from bridge
// ---------------------------------------------------------------------------

void MixerWindow::updateTrackStates(const std::vector<bridge::TrackUIState>& tracks)
{
    bool needsHeightUpdate = false;
    for (auto* strip : m_strips) {
        for (const auto& state : tracks) {
            if (state.trackId == strip->trackId()) {
                strip->updateFromState(state);
                needsHeightUpdate = true;
                break;
            }
        }
    }
    
    if (m_masterStrip && m_controller) {
        m_masterStrip->updateFromState(m_controller->getTrackState(TrackID{0, 0}));
    }

    if (needsHeightUpdate) {
        updateMinimumHeight();
    }
}

void MixerWindow::updateFromBridge()
{
    if (!m_controller) return;

    // Loop through the pre-existing strips and retrieve their dynamic state lock-free
    for (auto* strip : m_strips) {
        NodeID node = strip->channelStripNode();
        if (node.isValid()) {
            strip->updateDynamicState(m_controller->getDynamicState(node));
        }
    }

    // Update the master strip dynamic state
    if (m_masterStrip) {
        NodeID masterNode = m_masterStrip->channelStripNode();
        if (masterNode.isValid()) {
            m_masterStrip->updateDynamicState(m_controller->getDynamicState(masterNode));
        }
    }
}

void MixerWindow::handleTrackDrop(TrackID draggedId, TrackID targetId, presentation::views::DropAction action) {
    if (!m_controller || !targetId.isValid() || action == presentation::views::DropAction::None) return;

    auto tracks = m_controller->getAllTracks();
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

        if (action == presentation::views::DropAction::InsertBefore) {
            newIndex = targetIndex;
            newParent = targetParent;
        } else if (action == presentation::views::DropAction::InsertAfter) {
            newIndex = targetIndex + 1;
            newParent = targetParent;
        } else if (action == presentation::views::DropAction::DropInto) {
            newIndex = targetIndex + 1;
            newParent = targetId;
        }

        Q_EMIT trackMoveRequested(draggedId, newIndex, newParent);

        TrackID oldParent = TrackID::invalid();
        for (const auto& t : tracks) {
            if (t.trackId == draggedId) {
                oldParent = t.parentFolderId;
                break;
            }
        }
        if (oldParent != newParent) {
            m_controller->setTrackOutputRouting(draggedId, newParent);
        }
    }
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

void MixerWindow::toggleSendsVisible(bool visible)
{
    m_sendsVisible = visible;
    // Sends are inside ChannelStripWidget's SendSlotContainers;
    // show/hide via child widget search
    for (auto* strip : m_strips) {
        // Find SendSlotContainer children by type and toggle visibility
        for (auto* child : strip->findChildren<SendSlotContainer*>()) {
            child->setVisible(visible);
        }
    }
    // Resize scroll content to reflect collapsed height
    m_scrollContent->adjustSize();
    updateMinimumHeight();
}

void MixerWindow::updateMinimumHeight()
{
    int maxMinHeight = m_masterStrip ? m_masterStrip->minimumSizeHint().height() : 200;
    for (auto* strip : m_strips) {
        if (strip->isVisible()) {
            maxMinHeight = std::max(maxMinHeight, strip->minimumSizeHint().height());
        }
    }
    // Options bar height (36) + scroll content margins (8 + 8 = 16) + scroll area buffer (8) + extra margin (60)
    int totalMinHeight = maxMinHeight + 36 + 16 + 8 + 60;
    int oldHeight = height();
    setMinimumHeight(totalMinHeight);
    if (height() < totalMinHeight) {
        resize(width(), totalMinHeight);
    }

    if (isWindow() && height() != oldHeight) {
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            QRect avail = screen->availableGeometry();
            move(x(), avail.y() + avail.height() - height());
        }
    }
}

// ---------------------------------------------------------------------------
// Paint — subtle dark background
// ---------------------------------------------------------------------------

void MixerWindow::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme::Color::BgBase);
}

void MixerWindow::closeEvent(QCloseEvent* event)
{
    QWidget::closeEvent(event);
    Q_EMIT closedByUser();
}

void MixerWindow::onFolderCollapseToggled(TrackID folderId, bool collapsed)
{
    if (collapsed) {
        m_collapsedFolders.insert(folderId.toRaw());
    } else {
        m_collapsedFolders.erase(folderId.toRaw());
    }

    if (!m_controller) return;
    auto tracks = m_controller->getAllTracks();

    // Update child strip visibility in-place to avoid deleting the sender widget and crashing
    for (auto* strip : m_strips) {
        const bool visible = isTrackVisible(strip->trackId(), tracks);
        strip->setVisible(visible);
    }

    // Force layout recalculation and scroll size update
    m_stripLayout->activate();
    m_scrollContent->adjustSize();

    int totalWidth = 8 + 8; // margins
    for (auto* s : m_strips) {
        if (!s->isHidden()) {
            totalWidth += s->minimumWidth() + 6;
        }
    }
    m_scrollContent->setMinimumWidth(totalWidth);

    updateMinimumHeight();
    m_scrollContent->update(); // Redraw folder trays
}

int MixerWindow::calculateTrackDepth(TrackID id, const std::vector<bridge::TrackUIState>& tracks) const
{
    int depth = 0;
    TrackID currentParent = getParentFolderId(id, tracks);
    while (currentParent.isValid()) {
        depth++;
        if (depth > 12) break;
        currentParent = getParentFolderId(currentParent, tracks);
    }
    return depth;
}

TrackID MixerWindow::getParentFolderId(TrackID id, const std::vector<bridge::TrackUIState>& tracks) const
{
    for (const auto& t : tracks) {
        if (t.trackId == id) {
            return t.parentFolderId;
        }
    }
    return TrackID{};
}

bool MixerWindow::isDescendantOf(TrackID childId, TrackID parentId, const std::vector<bridge::TrackUIState>& tracks) const
{
    TrackID currentParent = getParentFolderId(childId, tracks);
    while (currentParent.isValid()) {
        if (currentParent == parentId) {
            return true;
        }
        currentParent = getParentFolderId(currentParent, tracks);
    }
    return false;
}

bool MixerWindow::isTrackVisible(TrackID id, const std::vector<bridge::TrackUIState>& tracks) const
{
    TrackID currentParent = getParentFolderId(id, tracks);
    while (currentParent.isValid()) {
        if (m_collapsedFolders.count(currentParent.toRaw()) > 0) {
            return false;
        }
        currentParent = getParentFolderId(currentParent, tracks);
    }
    return true;
}

void MixerWindow::drawFolderTrays(QPainter& p)
{
    if (!m_controller) return;
    auto tracks = m_controller->getAllTracks();
    const double widgetH = static_cast<double>(m_scrollContent->height());

    struct FolderTrayInfo {
        bridge::TrackUIState folderTrack;
        int depth;
        int minX;
        int maxX;
    };
    std::vector<FolderTrayInfo> foldersToDraw;

    for (const auto& folderTrack : tracks) {
        if (folderTrack.type != composition::TrackType::FOLDER) continue;

        auto itFolder = m_stripMap.find(folderTrack.trackId.toRaw());
        if (itFolder == m_stripMap.end() || itFolder->second->isHidden()) continue;

        int minX = itFolder->second->x();
        int maxX = minX + itFolder->second->width();

        for (const auto& child : tracks) {
            if (isDescendantOf(child.trackId, folderTrack.trackId, tracks)) {
                auto itChild = m_stripMap.find(child.trackId.toRaw());
                if (itChild != m_stripMap.end() && !itChild->second->isHidden()) {
                    minX = std::min(minX, itChild->second->x());
                    maxX = std::max(maxX, itChild->second->x() + itChild->second->width());
                }
            }
        }

        int depth = calculateTrackDepth(folderTrack.trackId, tracks);
        foldersToDraw.push_back({folderTrack, depth, minX, maxX});
    }

    // Sort by depth ascending so outermost folders are drawn first, and children are drawn on top.
    std::sort(foldersToDraw.begin(), foldersToDraw.end(), [](const FolderTrayInfo& a, const FolderTrayInfo& b) {
        return a.depth < b.depth;
    });

    for (const auto& info : foldersToDraw) {
        double trayX = static_cast<double>(info.minX) - 3.0;
        double trayW = static_cast<double>(info.maxX - info.minX) + 6.0;
        double trayY = 4.0;
        double trayH = widgetH - 16.0;

        QRectF trayRect(trayX, trayY, trayW, trayH);
        QColor color = QColor::fromRgba(info.folderTrack.colorARGB);

        // Subtly colored background (approx. 6% opacity)
        QColor fillColor = color;
        fillColor.setAlpha(15);
        p.setBrush(QBrush(fillColor));

        // Subtly colored border (approx. 12% opacity)
        QColor borderCol = color;
        borderCol.setAlpha(30);
        p.setPen(QPen(borderCol, 1.0, Qt::SolidLine));

        // Draw rounded grouping card
        p.drawRoundedRect(trayRect, 6.0, 6.0);
    }
}

void MixerWindow::onSelectionRequested(TrackID trackId, bool multiSelect, bool rangeSelect)
{
    if (!m_controller) return;
    
    std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
    int currentIndex = -1;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].trackId == trackId) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }
    if (currentIndex == -1) return;

    if (rangeSelect && m_lastSelectedTrackIndex != -1) {
        m_controller->clearTrackSelection();
        int start = std::min(currentIndex, m_lastSelectedTrackIndex);
        int end = std::max(currentIndex, m_lastSelectedTrackIndex);
        for (int i = start; i <= end; ++i) {
            m_controller->setTrackSelected(tracks[static_cast<size_t>(i)].trackId, true);
        }
    } else if (multiSelect) {
        bool isCurrentlySelected = m_controller->getTrackState(trackId).isSelected;
        m_controller->setTrackSelected(trackId, !isCurrentlySelected);
        m_lastSelectedTrackIndex = currentIndex;
    } else {
        m_controller->clearTrackSelection();
        m_controller->setTrackSelected(trackId, true);
        m_lastSelectedTrackIndex = currentIndex;
    }
    
    // Instantly refresh UI state across all strips
    std::vector<bridge::TrackUIState> updatedTracks = m_controller->getAllTracks();
    updateTrackStates(updatedTracks);
}

void MixerWindow::keyPressEvent(QKeyEvent* event) {
    if (!m_controller) {
        QWidget::keyPressEvent(event);
        return;
    }
    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();

    // Ctrl+Shift+N: Create New Track
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && key == Qt::Key_N) {
        m_controller->addAudioTrack("Audio Track", 2, 0xFF3399FF);
        emit tracksChangedRequested();
        return;
    }

    // Ctrl+Shift+Delete: Delete Selected Track(s)
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && (key == Qt::Key_Delete || key == Qt::Key_Backspace)) {
        auto tracks = m_controller->getAllTracks();
        bool removed = false;
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->removeTrack(tr.trackId);
                removed = true;
            }
        }
        if (removed) {
            emit tracksChangedRequested();
        }
        return;
    }

    // Alt+S / Ctrl+Alt+S: Clear All Solos
    if ((mods & Qt::AltModifier) && key == Qt::Key_S) {
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            m_controller->setSolo(tr.trackId, false);
        }
        updateFromBridge();
        return;
    }

    // M: Toggle Mute on Selected Track(s)
    if (key == Qt::Key_M && !(mods & Qt::AltModifier) && !(mods & Qt::ControlModifier)) {
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->setMute(tr.trackId, !tr.isMuted);
            }
        }
        updateFromBridge();
        return;
    }

    // S: Toggle Solo on Selected Track(s)
    if (key == Qt::Key_S && !(mods & Qt::AltModifier) && !(mods & Qt::ControlModifier)) {
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->setSolo(tr.trackId, !tr.isSoloed);
            }
        }
        updateFromBridge();
        return;
    }

    // C or Shift+R: Toggle Record Arm on Selected Track(s)
    if (key == Qt::Key_C || (key == Qt::Key_R && (mods & Qt::ShiftModifier))) {
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->setRecordArmed(tr.trackId, !tr.isRecordArmed);
            }
        }
        updateFromBridge();
        return;
    }

    // F2: Inline Rename on Focused/Selected Track
    if (key == Qt::Key_F2) {
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                ChannelStripWidget* s = strip(tr.trackId);
                if (s) {
                    s->triggerInlineRename();
                    break;
                }
            }
        }
        return;
    }

    QWidget::keyPressEvent(event);
}

void MixerWindow::installKeyboardShortcuts() {
    auto& sm = presentation::shortcuts::ShortcutManager::instance();
    using presentation::shortcuts::ShortcutAction;

    sm.bind(this, ShortcutAction::Mixer_ToggleMute, [this]() {
        if (!m_controller) return;
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->setMute(tr.trackId, !tr.isMuted);
            }
        }
        updateFromBridge();
    });

    sm.bind(this, ShortcutAction::Mixer_ToggleSolo, [this]() {
        if (!m_controller) return;
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->setSolo(tr.trackId, !tr.isSoloed);
            }
        }
        updateFromBridge();
    });

    sm.bind(this, ShortcutAction::Mixer_ToggleRecordArm, [this]() {
        if (!m_controller) return;
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->setRecordArmed(tr.trackId, !tr.isRecordArmed);
            }
        }
        updateFromBridge();
    });

    sm.bindSequence(this, QKeySequence("Shift+R"), [this]() {
        if (!m_controller) return;
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->setRecordArmed(tr.trackId, !tr.isRecordArmed);
            }
        }
        updateFromBridge();
    });

    sm.bind(this, ShortcutAction::Mixer_ClearAllSolos, [this]() {
        if (!m_controller) return;
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            m_controller->setSolo(tr.trackId, false);
        }
        updateFromBridge();
    });

    sm.bindSequence(this, QKeySequence("F2"), [this]() {
        if (!m_controller) return;
        auto tracks = m_controller->getAllTracks();
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                ChannelStripWidget* s = strip(tr.trackId);
                if (s) {
                    s->triggerInlineRename();
                    break;
                }
            }
        }
    });

    sm.bind(this, ShortcutAction::Mixer_CreateTrack, [this]() {
        if (!m_controller) return;
        m_controller->addAudioTrack("Audio Track", 2, 0xFF3399FF);
        emit tracksChangedRequested();
    });

    sm.bind(this, ShortcutAction::Mixer_DeleteTrack, [this]() {
        if (!m_controller) return;
        auto tracks = m_controller->getAllTracks();
        bool removed = false;
        for (const auto& tr : tracks) {
            if (tr.isSelected) {
                m_controller->removeTrack(tr.trackId);
                removed = true;
            }
        }
        if (removed) {
            emit tracksChangedRequested();
        }
    });
}

} // namespace presentation::views
