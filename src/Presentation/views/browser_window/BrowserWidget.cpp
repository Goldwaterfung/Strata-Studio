// src/Presentation/views/browser_window/BrowserWidget.cpp
#include "BrowserWidget.h"
#include "BrowserNavigationView.h"
#include "BrowserSearchView.h"
#include "BrowserTreeView.h"
#include "BrowserPreviewDeck.h"
#include "../theme.h"
#include <QVBoxLayout>
#include <QPainter>

namespace presentation::views {

BrowserWidget::BrowserWidget(bridge::IBrowserController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller) {
    setupUI();
    wireSignals();
}

void BrowserWidget::setupUI() {
    // Sizing guidelines matching browser side-panel bounds
    setMinimumWidth(240);
    setStyleSheet("background: transparent; border: none;");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    // 1. Navigation Panel (Top)
    m_navigationView = new BrowserNavigationView(m_controller, this);
    layout->addWidget(m_navigationView);

    // 2. Search & Tag Filters (Middle)
    m_searchView = new BrowserSearchView(m_controller, this);
    layout->addWidget(m_searchView);

    // 3. Tree hierarchy view (Center - fills space)
    m_treeView = new BrowserTreeView(m_controller, this);
    m_treeView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_treeView);

    // 4. Audition playback bar (Bottom)
    m_previewDeck = new BrowserPreviewDeck(m_controller, this);
    layout->addWidget(m_previewDeck);
}

void BrowserWidget::wireSignals() {
    if (!m_controller) return;

    // Navigation -> Tree tab changes
    connect(m_navigationView, &BrowserNavigationView::tabChanged, this, [this]() {
        m_searchView->clearFilters();
        m_treeView->reloadRootItems();
        m_previewDeck->setSelectedMedia(MediaID::invalid());
    });

    // Navigation -> Collapse Request
    connect(m_navigationView, &BrowserNavigationView::collapseAllRequested, m_treeView, &BrowserTreeView::collapseAllFolders);

    // Navigation -> Refresh Request
    connect(m_navigationView, &BrowserNavigationView::refreshRequested, this, [this]() {
        m_controller->refreshScanner();
        m_treeView->reloadRootItems();
    });

    // Search query -> Tree filter
    connect(m_searchView, &BrowserSearchView::searchTriggered, this, [this]() {
        auto results = m_controller->getSearchResults();
        m_treeView->displaySearchResults(results);
    });

    // Search clear -> Restore standard hierarchy
    connect(m_searchView, &BrowserSearchView::searchCleared, m_treeView, &BrowserTreeView::reloadRootItems);

    // Tree item selection -> Audition player playhead hook
    connect(m_treeView, &BrowserTreeView::previewStateChanged, this, [this](MediaID mediaId, bool active) {
        if (active) {
            m_previewDeck->setSelectedMedia(mediaId);
        } else {
            m_previewDeck->setSelectedMedia(MediaID::invalid());
        }
    });
}

void BrowserWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw main glass outline border around the entire module shell
    QRectF bounds(0.0, 0.0, static_cast<double>(width()), static_cast<double>(height()));
    theme::PaintHelper::drawGlassPanel(&painter, bounds, theme::Color::BgBase, 6.0);
}

} // namespace presentation::views
