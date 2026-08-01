// src/Presentation/views/browser_window/BrowserWidget.h
#pragma once

#include <QWidget>
#include "Middle Bridge/browser/ibrowser_controller.h"

namespace presentation::views {

class BrowserNavigationView;
class BrowserSearchView;
class BrowserTreeView;
class BrowserPreviewDeck;

/**
 * @brief Main Container Coordinator layout for the Browser Window module.
 *        Assembles and wires the navigation, search, listing, and playback subviews.
 */
class BrowserWidget : public QWidget {
    Q_OBJECT

public:
    explicit BrowserWidget(bridge::IBrowserController* controller, QWidget* parent = nullptr);
    ~BrowserWidget() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void wireSignals();

    bridge::IBrowserController* m_controller{nullptr};

    BrowserNavigationView* m_navigationView{nullptr};
    BrowserSearchView* m_searchView{nullptr};
    BrowserTreeView* m_treeView{nullptr};
    BrowserPreviewDeck* m_previewDeck{nullptr};
};

} // namespace presentation::views
