// src/Presentation/views/browser_window/BrowserTreeView.cpp
#include "BrowserTreeView.h"
#include "../theme.h"
#include <QHeaderView>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include "Middle Bridge/browser/dnd_primitives.h"
#include "BrowserItem.h"

namespace presentation::views {

BrowserTreeView::BrowserTreeView(bridge::IBrowserController* controller, QWidget* parent)
    : QTreeWidget(parent)
    , m_controller(controller) {
    setupUI();
    reloadRootItems();
}

void BrowserTreeView::setupUI() {
    setColumnCount(1);
    QStringList headers;
    headers << "Name";
    setHeaderLabels(headers);
    
    // Sleek header styling
    header()->setFont(theme::Font::primary(theme::Font::SizeTiny, QFont::Bold));

    // Set layout proportions
    header()->setSectionResizeMode(0, QHeaderView::Stretch);

    // Tree styling matching cyber industrial aesthetic
    setFont(theme::Font::primary(theme::Font::SizeSecondary));
    setStyleSheet(theme::Style::getListViewStyleSheet());
    setIndentation(12);
    setAnimated(true);
    setCursor(Qt::PointingHandCursor);

    // Connect tree interactions
    connect(this, &QTreeWidget::itemExpanded, this, &BrowserTreeView::onItemExpanded);
    connect(this, &QTreeWidget::itemSelectionChanged, this, &BrowserTreeView::onItemSelectionChanged);
    connect(this, &QTreeWidget::itemDoubleClicked, this, &BrowserTreeView::onItemDoubleClicked);

    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);

    setItemDelegate(new BrowserItem(this));

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTreeWidget::customContextMenuRequested, this, &BrowserTreeView::showContextMenu);
}

QString BrowserTreeView::getDisplayName(uint32_t stringId) const {
    if (!m_controller) return "Unknown";
    std::string name;
    if (m_controller->getItemName(stringId, name)) {
        return QString::fromStdString(name);
    }
    return QString("Item ID %1").arg(stringId);
}

QTreeWidgetItem* BrowserTreeView::createNode(const bridge::BrowserItem& item, QTreeWidgetItem* parent) {
    QTreeWidgetItem* node = nullptr;
    if (parent) {
        node = new QTreeWidgetItem(parent);
    } else {
        node = new QTreeWidgetItem(this);
    }

    // 1. Resolve Display Name
    QString name = getDisplayName(item.stringId);
    if (item.isFavorite) {
        name = QStringLiteral("★ ") + name;
    }
    node->setText(0, name);

    // 2. Set Color highlights using colorARGB or AccentGlow for favorites
    if (item.isFavorite) {
        node->setForeground(0, QBrush(theme::Color::AccentGlow));
    } else if (item.colorARGB != 0) {
        // ARGB packing
        QColor itemColor(
            static_cast<int>((item.colorARGB >> 16) & 0xFF),
            static_cast<int>((item.colorARGB >> 8) & 0xFF),
            static_cast<int>(item.colorARGB & 0xFF),
            static_cast<int>((item.colorARGB >> 24) & 0xFF)
        );
        node->setForeground(0, QBrush(itemColor));
    } else {
        node->setForeground(0, QBrush(theme::Color::TextPrimary));
    }

    // Set tooltip if path is available
    std::string mediaPath;
    if (m_controller && item.mediaId.isValid() && m_controller->getMediaPath(item.mediaId, mediaPath)) {
        node->setToolTip(0, QString::fromStdString(mediaPath));
    }

    // 3. Save metadata in UserRole fields
    node->setData(0, Qt::UserRole + 0, item.stringId);
    // Safe double-to-raw casting or handle representation
    node->setData(0, Qt::UserRole + 1, static_cast<qlonglong>(item.mediaId.toRaw()));
    node->setData(0, Qt::UserRole + 2, static_cast<int>(item.type));
    node->setData(0, Qt::UserRole + 3, item.colorARGB);
    node->setData(0, Qt::UserRole + 4, item.isFavorite);

    // If it's a folder, mark lazy loaded state as false
    if (item.type == bridge::BrowserItemType::Folder) {
        node->setData(0, Qt::UserRole + 5, false); // isLoaded = false
        node->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    } else {
        node->setData(0, Qt::UserRole + 5, true); // Files are always "loaded"
        node->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }

    return node;
}

bridge::BrowserItem BrowserTreeView::getBrowserItemFromNode(QTreeWidgetItem* item) const {
    bridge::BrowserItem bItem{};
    if (!item) return bItem;

    bItem.stringId = item->data(0, Qt::UserRole + 0).toUInt();
    qlonglong rawMedia = item->data(0, Qt::UserRole + 1).toLongLong();
    bItem.mediaId = MediaID::fromRaw(static_cast<uint64_t>(rawMedia));
    bItem.type = static_cast<bridge::BrowserItemType>(item->data(0, Qt::UserRole + 2).toInt());
    bItem.colorARGB = item->data(0, Qt::UserRole + 3).toUInt();
    bItem.isFavorite = item->data(0, Qt::UserRole + 4).toBool();

    return bItem;
}

void BrowserTreeView::reloadRootItems() {
    clear();
    if (!m_controller) return;

    bridge::BrowserTab active = m_controller->getActiveTab();
    std::vector<bridge::BrowserItem> roots = m_controller->getRootItems(active);

    for (const auto& root : roots) {
        createNode(root);
    }
}

void BrowserTreeView::onItemExpanded(QTreeWidgetItem* item) {
    if (!item || !m_controller) return;

    bool isLoaded = item->data(0, Qt::UserRole + 5).toBool();
    if (isLoaded) return;

    bridge::BrowserItem parentItem = getBrowserItemFromNode(item);
    std::vector<bridge::BrowserItem> children = m_controller->getChildren(parentItem);

    for (const auto& child : children) {
        createNode(child, item);
    }

    // Mark folder as loaded to prevent duplicate traversal
    item->setData(0, Qt::UserRole + 5, true);
}

void BrowserTreeView::onItemSelectionChanged() {
    if (!m_controller) return;

    QList<QTreeWidgetItem*> selected = selectedItems();
    if (selected.isEmpty()) {
        m_controller->stopAudioPreview();
        emit previewStateChanged(MediaID::invalid(), false);
        return;
    }

    bridge::BrowserItem item = getBrowserItemFromNode(selected.first());
    if (item.type == bridge::BrowserItemType::AudioFile && item.mediaId.isValid()) {
        // Triggers background auditioning for audio files
        m_controller->startAudioPreview(item.mediaId);
        emit previewStateChanged(item.mediaId, true);
    } else {
        m_controller->stopAudioPreview();
        emit previewStateChanged(MediaID::invalid(), false);
    }
}

void BrowserTreeView::onItemDoubleClicked(QTreeWidgetItem* item, int) {
    if (!item || !m_controller) return;

    bridge::BrowserItem bItem = getBrowserItemFromNode(item);
    if (bItem.type != bridge::BrowserItemType::Folder && bItem.mediaId.isValid()) {
        // Trigger asynchronous import via Middle Bridge
        m_controller->triggerImport(bItem.mediaId);
        emit itemImportRequested(bItem.mediaId);
    }
}

void BrowserTreeView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->position().toPoint();
    }
    QTreeWidget::mousePressEvent(event);
}

void BrowserTreeView::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QTreeWidget::mouseMoveEvent(event);
        return;
    }
    if ((event->position().toPoint() - m_dragStartPos).manhattanLength()
         < QApplication::startDragDistance()) {
        QTreeWidget::mouseMoveEvent(event);
        return;
    }

    QTreeWidgetItem* item = itemAt(m_dragStartPos);
    if (!item) return;
    startDragFromItem(item);
}

void BrowserTreeView::startDragFromItem(QTreeWidgetItem* item) {
    bridge::BrowserItem bItem = getBrowserItemFromNode(item);
    if (bItem.type == bridge::BrowserItemType::Folder) return;

    const QString name = item->text(0);
    const int itemType = static_cast<int>(bItem.type);
    const uint64_t mediaIdRaw = bItem.mediaId.toRaw();

    auto* drag = new QDrag(this);
    auto* mimeData = new QMimeData();

    mimeData->setData(bridge::kMimeClipType, QByteArray::number(itemType));
    mimeData->setData(bridge::kMimeClipName, name.toUtf8());
    mimeData->setData(bridge::kMimeClipId,  QByteArray::number(mediaIdRaw));
    mimeData->setData(bridge::kMimeMediaId, QByteArray::number(mediaIdRaw));

    if (bItem.type == bridge::BrowserItemType::PluginEffect ||
        bItem.type == bridge::BrowserItemType::PluginGenerator) {
        if (m_controller) {
            uint32_t pluginId = m_controller->getPluginIdForMedia(bItem.mediaId);
            if (pluginId != UINT32_MAX) {
                mimeData->setData(bridge::kMimePluginId, QByteArray::number(
                    static_cast<unsigned long long>(pluginId)));
            }
        }
    }

    drag->setMimeData(mimeData);

    QFont font = theme::Font::primary(theme::Font::SizeTiny, QFont::Bold);
    QFontMetrics fm(font);
    int textWidth = static_cast<int>(fm.horizontalAdvance(name));
    int pixmapWidth = qMax(140, textWidth + 24);

    QPixmap pixmap(pixmapWidth, 24);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QRectF bounds(0.0, 0.0, static_cast<double>(pixmapWidth), 24.0);
        theme::PaintHelper::drawGlassPanel(&painter, bounds, theme::Color::BgBase, 4.0);
        painter.setFont(font);
        painter.setPen(theme::Color::TextPrimary);
        painter.drawText(bounds, Qt::AlignCenter, name);
    }
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(pixmapWidth / 2, 12));

    drag->exec(Qt::CopyAction);
}

void BrowserTreeView::collapseAllFolders() {
    collapseAll();
    if (m_controller) {
        m_controller->collapseAll();
    }
}

void BrowserTreeView::displaySearchResults(const std::vector<bridge::BrowserItem>& results) {
    clear();
    for (const auto& item : results) {
        createNode(item);
    }
}

void BrowserTreeView::showContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = itemAt(pos);
    if (!item) return;

    bridge::BrowserItem bItem = getBrowserItemFromNode(item);
    if (!bItem.mediaId.isValid()) return;

    QMenu menu(this);
    QAction* actFavorite = nullptr;
    if (bItem.isFavorite) {
        actFavorite = menu.addAction(QStringLiteral("Remove from Favorites"));
    } else {
        actFavorite = menu.addAction(QStringLiteral("Add to Favorites"));
    }

    std::string path;
    QAction* actReveal = nullptr;
    if (m_controller && m_controller->getMediaPath(bItem.mediaId, path)) {
        actReveal = menu.addAction(QStringLiteral("Reveal in Finder"));
    }

    QAction* selectedAction = menu.exec(mapToGlobal(pos));
    if (!selectedAction) return;

    if (selectedAction == actFavorite) {
        m_controller->setFavorite(bItem.mediaId, !bItem.isFavorite);
        reloadRootItems();
    } else if (selectedAction == actReveal) {
        QFileInfo fileInfo(QString::fromStdString(path));
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
    }
}

} // namespace presentation::views
