// src/Presentation/views/browser_window/BrowserSearchView.cpp
#include "BrowserSearchView.h"
#include "../theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>

namespace presentation::views {

BrowserSearchView::BrowserSearchView(bridge::IBrowserController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller) {
    setupUI();
}

void BrowserSearchView::setupUI() {
    // Transparent base background
    setStyleSheet("background: transparent; border: none;");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(6);

    // Search bar row
    QHBoxLayout* searchRow = new QHBoxLayout();
    searchRow->setSpacing(4);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search samples, plugins, projects...");
    m_searchEdit->setFont(theme::Font::primary(theme::Font::SizeDetail));
    m_searchEdit->setFixedHeight(28);
    searchRow->addWidget(m_searchEdit);

    m_btnClear = new QPushButton("Clear", this);
    m_btnClear->setFont(theme::Font::primary(theme::Font::SizeDetail, QFont::Bold));
    m_btnClear->setCursor(Qt::PointingHandCursor);
    m_btnClear->setFixedHeight(28);
    searchRow->addWidget(m_btnClear);

    mainLayout->addLayout(searchRow);

    // Tag filter buttons row
    QHBoxLayout* tagRow = new QHBoxLayout();
    tagRow->setSpacing(4);

    auto createTagButton = [this](const QString& text, const QString& tooltip) {
        QPushButton* btn = new QPushButton(text, this);
        btn->setCheckable(true);
        btn->setFont(theme::Font::primary(theme::Font::SizeDetail, QFont::Bold));
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        return btn;
    };

    m_btnLoops = createTagButton("LOOPS", "Filter audio loops (e.g. tempo-synced content)");
    m_btnOneShots = createTagButton("SHOTS", "Filter drum and instrument one-shots");
    m_btnInstruments = createTagButton("SYNTH", "Filter generator plug-ins");
    m_btnEffects = createTagButton("FX", "Filter effect plug-ins");

    tagRow->addWidget(m_btnLoops);
    tagRow->addWidget(m_btnOneShots);
    tagRow->addWidget(m_btnInstruments);
    tagRow->addWidget(m_btnEffects);
    tagRow->addStretch();

    mainLayout->addLayout(tagRow);

    // Connect slots
    connect(m_searchEdit, &QLineEdit::textChanged, this, &BrowserSearchView::onSearchChanged);
    connect(m_btnClear, &QPushButton::clicked, this, &BrowserSearchView::clearFilters);

    connect(m_btnLoops, &QPushButton::toggled, this, &BrowserSearchView::onTagToggled);
    connect(m_btnOneShots, &QPushButton::toggled, this, &BrowserSearchView::onTagToggled);
    connect(m_btnInstruments, &QPushButton::toggled, this, &BrowserSearchView::onTagToggled);
    connect(m_btnEffects, &QPushButton::toggled, this, &BrowserSearchView::onTagToggled);
}

void BrowserSearchView::onSearchChanged() {
    onTagToggled(); // Re-calculate mask and execute search
}

void BrowserSearchView::onTagToggled() {
    if (!m_controller) return;

    QString query = m_searchEdit->text();
    uint32_t mask = 0;

    if (m_btnLoops->isChecked()) mask |= (1 << 0);
    if (m_btnOneShots->isChecked()) mask |= (1 << 1);
    if (m_btnInstruments->isChecked()) mask |= (1 << 2);
    if (m_btnEffects->isChecked()) mask |= (1 << 3);

    m_controller->executeSearch(query.toUtf8().constData(), mask);
    emit searchTriggered(query, mask);
}

void BrowserSearchView::clearFilters() {
    m_searchEdit->blockSignals(true);
    m_searchEdit->clear();
    m_searchEdit->blockSignals(false);

    m_btnLoops->blockSignals(true);
    m_btnOneShots->blockSignals(true);
    m_btnInstruments->blockSignals(true);
    m_btnEffects->blockSignals(true);

    m_btnLoops->setChecked(false);
    m_btnOneShots->setChecked(false);
    m_btnInstruments->setChecked(false);
    m_btnEffects->setChecked(false);

    m_btnLoops->blockSignals(false);
    m_btnOneShots->blockSignals(false);
    m_btnInstruments->blockSignals(false);
    m_btnEffects->blockSignals(false);

    if (m_controller) {
        m_controller->clearSearch();
    }

    emit searchCleared();
}

} // namespace presentation::views
