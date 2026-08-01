// src/Presentation/views/browser_window/BrowserNavigationView.h
#pragma once

#include <QWidget>
#include <QButtonGroup>
#include <QPushButton>
#include "Middle Bridge/browser/ibrowser_controller.h"

namespace presentation::views {

class BrowserNavigationView : public QWidget {
    Q_OBJECT

public:
    explicit BrowserNavigationView(bridge::IBrowserController* controller, QWidget* parent = nullptr);
    ~BrowserNavigationView() override = default;

    /**
     * @brief Synchronizes tab button selection state from controller state.
     */
    void updateTabSelection();

signals:
    void tabChanged(bridge::BrowserTab tab);
    void collapseAllRequested();
    void refreshRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void onTabButtonClicked(int id);

    bridge::IBrowserController* m_controller{nullptr};
    
    QButtonGroup* m_tabGroup{nullptr};
    QPushButton* m_btnAll{nullptr};
    QPushButton* m_btnProject{nullptr};
    QPushButton* m_btnPlugins{nullptr};
    
    QPushButton* m_btnCollapse{nullptr};
    QPushButton* m_btnRefresh{nullptr};
};

} // namespace presentation::views
