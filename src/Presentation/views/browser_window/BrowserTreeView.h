// src/Presentation/views/browser_window/BrowserTreeView.h
#pragma once

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include "Middle Bridge/browser/ibrowser_controller.h"

namespace presentation::views {

class BrowserTreeView : public QTreeWidget {
    Q_OBJECT

public:
    explicit BrowserTreeView(bridge::IBrowserController* controller, QWidget* parent = nullptr);
    ~BrowserTreeView() override = default;

    /**
     * @brief Clears and repopulates the tree structure based on the active tab.
     */
    void reloadRootItems();

    /**
     * @brief Collapses all open tree folders.
     */
    void collapseAllFolders();

    /**
     * @brief Forces tree population from search results instead of root/hierarchy.
     */
    void displaySearchResults(const std::vector<bridge::BrowserItem>& results);

signals:
    void itemImportRequested(MediaID mediaId);
    void previewStateChanged(MediaID mediaId, bool active);

private slots:
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemSelectionChanged();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& pos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void setupUI();
    
    /**
     * @brief Helper to reconstruct a BrowserItem from QTreeWidgetItem data roles.
     */
    bridge::BrowserItem getBrowserItemFromNode(QTreeWidgetItem* item) const;

    /**
     * @brief Helper to create and style a tree node.
     */
    QTreeWidgetItem* createNode(const bridge::BrowserItem& item, QTreeWidgetItem* parent = nullptr);

    /**
     * @brief Resolve standard stringId names to human readable strings.
     */
    QString getDisplayName(uint32_t stringId) const;

    void startDragFromItem(QTreeWidgetItem* item);
    QPoint m_dragStartPos;

    bridge::IBrowserController* m_controller{nullptr};
};

} // namespace presentation::views
