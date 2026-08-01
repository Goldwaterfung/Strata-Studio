// src/Presentation/views/browser_window/BrowserSearchView.h
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include "Middle Bridge/browser/ibrowser_controller.h"

namespace presentation::views {

class BrowserSearchView : public QWidget {
    Q_OBJECT

public:
    explicit BrowserSearchView(bridge::IBrowserController* controller, QWidget* parent = nullptr);
    ~BrowserSearchView() override = default;

    /**
     * @brief Clears the active search query and unchecks filter tags.
     */
    void clearFilters();

signals:
    void searchTriggered(const QString& queryText, uint32_t tagFilterMask);
    void searchCleared();

private:
    void setupUI();
    void onSearchChanged();
    void onTagToggled();

    bridge::IBrowserController* m_controller{nullptr};

    QLineEdit* m_searchEdit{nullptr};
    QPushButton* m_btnClear{nullptr};

    // Filter Buttons
    QPushButton* m_btnLoops{nullptr};
    QPushButton* m_btnOneShots{nullptr};
    QPushButton* m_btnInstruments{nullptr};
    QPushButton* m_btnEffects{nullptr};
};

} // namespace presentation::views
