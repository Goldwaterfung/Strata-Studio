// src/Presentation/views/playlist/ProjectPickerScreen.cpp
#include "ProjectPickerScreen.h"
#include <QPainter>
#include <QPaintEvent>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QDir>
#include <QDebug>
#include "theme.h"

namespace presentation::views {

ProjectPickerScreen::ProjectPickerScreen(
    bridge::IProjectLifecycleController* lifecycle,
    bridge::IBrowserController*          browser,
    QWidget* parent)
    : QWidget(parent)
    , m_lifecycle(lifecycle)
    , m_browser(browser)
{
    setObjectName(QStringLiteral("ProjectPickerScreen"));
    hide(); // Hidden by default

    // Outer Layout
    auto* masterLayout = new QHBoxLayout(this);
    masterLayout->setContentsMargins(60, 60, 60, 60);
    masterLayout->setSpacing(40);

    // Left Pane (Logo & Progress Status Deck)
    auto* leftPane = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("CYBER_CORE // PROJECT_SELECT"), this);
    title->setFont(theme::Font::monospace(14, QFont::Bold));
    title->setStyleSheet(QStringLiteral("color: #00FFCC; letter-spacing: 2px;"));

    auto* desc = new QLabel(QStringLiteral("ASYNCHRONOUS SESSION DECK SELECTOR\nSECTOR_101_SYSTEMS"), this);
    desc->setFont(theme::Font::monospace(9, QFont::Normal));
    desc->setStyleSheet(QStringLiteral("color: #72798C;"));

    leftLayout->addWidget(title);
    leftLayout->addWidget(desc);
    leftLayout->addStretch();

    // Progress Bar Indicator
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFont(theme::Font::monospace(8, QFont::Bold));
    m_progressBar->setFixedHeight(18);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar {"
        "  background-color: #303D49;"
        "  border: 1px solid #526D82;"
        "  color: #00FFCC;"
        "  text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: #00FFCC;"
        "  border-radius: 3px;"
        "}"
    ));
    leftLayout->addWidget(m_progressBar);

    auto* escPrompt = new QLabel(QStringLiteral("[ ESC ] DISMISS DECK OVERLAY"), this);
    escPrompt->setFont(theme::Font::monospace(8, QFont::Bold));
    escPrompt->setStyleSheet(QStringLiteral("color: #FF3B30; letter-spacing: 1px;"));
    leftLayout->addWidget(escPrompt);

    // Right Pane (List of Projects)
    auto* rightPane = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto* listHeader = new QLabel(QStringLiteral("AVAILABLE_ARRANGEMENTS:"), this);
    listHeader->setFont(theme::Font::monospace(9, QFont::Bold));
    listHeader->setStyleSheet(QStringLiteral("color: #A0A5B5; letter-spacing: 1px;"));
    rightLayout->addWidget(listHeader);

    m_projectsList = new QListWidget(this);
    m_projectsList->setFont(theme::Font::monospace(10, QFont::Normal));
    m_projectsList->setSpacing(6);
    m_projectsList->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background-color: #303D49;"
        "  border: 1px solid #526D82;"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "  color: #F0F1F5;"
        "}"
        "QListWidget::item {"
        "  background-color: #526D82;"
        "  border: 1px solid #4A5060;"
        "  border-radius: 4px;"
        "  padding: 10px 14px;"
        "  margin-bottom: 2px;"
        "}"
        "QListWidget::item:hover {"
        "  background-color: #464F63;"
        "  border-color: #00FFCC;"
        "  color: #00FFCC;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: #4A5060;"
        "  border-color: #00FFCC;"
        "  color: #00FFCC;"
        "}"
    ));
    rightLayout->addWidget(m_projectsList);

    masterLayout->addWidget(leftPane, 2);
    masterLayout->addWidget(rightPane, 3);

    // Progress Poller Setup
    m_progressPoller = new QTimer(this);
    connect(m_progressPoller, &QTimer::timeout, this, &ProjectPickerScreen::onProgressUpdated);

    connect(m_projectsList, &QListWidget::itemDoubleClicked, this, &ProjectPickerScreen::onProjectClicked);
    connect(m_projectsList, &QListWidget::itemClicked, this, &ProjectPickerScreen::onProjectClicked);

    applyAestheticStyle();
}

void ProjectPickerScreen::showOverlay()
{
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    populateProjectsList();
    raise();
    show();
    setFocus();
}

void ProjectPickerScreen::hideOverlay()
{
    hide();
    m_progressPoller->stop();
    m_progressBar->setVisible(false);
}

void ProjectPickerScreen::onProjectClicked(QListWidgetItem* item)
{
    if (!item || !m_lifecycle) return;
    
    m_loadingPath = item->data(Qt::UserRole).toString();
    
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    
    // Start Asynchronous project loading on the middle bridge
    m_lifecycle->loadProject(m_loadingPath.toUtf8().constData());
    
    // Trigger 100ms background thread polling
    m_progressPoller->start(100);
    
    emit projectLoadRequested(m_loadingPath);
}

void ProjectPickerScreen::onProgressUpdated()
{
    if (!m_lifecycle) return;

    if (m_lifecycle->isOperationPending()) {
        float progress = m_lifecycle->getOperationProgress();
        m_progressBar->setValue(static_cast<int>(progress * 100.0f));
    } else {
        m_progressPoller->stop();
        m_progressBar->setValue(100);
        
        // Polished 300ms visual delay so the producer sees 100% complete loading transition
        QTimer::singleShot(300, this, [this]() {
            hideOverlay();
        });
    }
}

void ProjectPickerScreen::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideOverlay();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ProjectPickerScreen::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    // Draw neon Cyberpunk glass-panel overlay
    p.fillRect(rect(), QColor(10, 11, 14, 225));
}

void ProjectPickerScreen::resizeEvent(QResizeEvent* /*event*/)
{
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
}

void ProjectPickerScreen::populateProjectsList()
{
    m_projectsList->clear();

    // 1. Scan filesystem under standard project workspace directory
    QDir projectDir(QStringLiteral("./projects"));
    if (projectDir.exists()) {
        QStringList filters;
        filters << QStringLiteral("*.adaw") << QStringLiteral("*.json");
        QFileInfoList list = projectDir.entryInfoList(filters, QDir::Files);
        for (const QFileInfo& info : list) {
            auto* item = new QListWidgetItem(QStringLiteral("💾  ") + info.fileName(), m_projectsList);
            item->setData(Qt::UserRole, info.absoluteFilePath());
        }
    }
}

void ProjectPickerScreen::applyAestheticStyle()
{
    // Configure translucent widget flags
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

} // namespace presentation::views
