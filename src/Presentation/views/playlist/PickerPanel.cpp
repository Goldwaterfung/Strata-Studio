// src/Presentation/views/playlist/PickerPanel.cpp
#include "PickerPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include "Middle Bridge/browser/dnd_primitives.h"

namespace presentation::views {

// ─────────────────────────────────────────────────────────────────────────────
// PickerListWidget Implementation
// ─────────────────────────────────────────────────────────────────────────────

PickerListWidget::PickerListWidget(QWidget* parent)
    : QListWidget(parent)
{
    setDragEnabled(true);
    setViewMode(QListView::ListMode);
    setFont(theme::Font::monospace(11));
    setCursor(Qt::PointingHandCursor);
    
    // Cyber-industrial scroll bar styling
    setStyleSheet(QString(
        "QListWidget {"
        "  background-color: #222831;"
        "  border: none;"
        "  color: #DDE6ED;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 10px;"
        "  border-bottom: 1px solid #303D49;"
        "}"
        "QListWidget::item:hover {"
        "  background-color: #1A2F2B;"
        "  color: #00FFCC;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: #1A2F2B;"
        "  color: #00FFCC;"
        "  border-left: 2px solid #00FFCC;"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 4px;"
        "  margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %1;"
        "  min-height: 20px;"
        "  border-radius: 2px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: %2;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
    )
    .arg(theme::Color::TextMuted.name())
    .arg(theme::Color::AccentGlow.name()));
}

void PickerListWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->position().toPoint();
    }
    QListWidget::mousePressEvent(event);
}

void PickerListWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        QListWidget::mouseMoveEvent(event);
        return;
    }
    if ((event->position().toPoint() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
        QListWidget::mouseMoveEvent(event);
        return;
    }

    QListWidgetItem* item = currentItem();
    if (!item) return;

    const QString name = item->text();
    const int itemType = item->data(Qt::UserRole + 0).toInt();
    const uint64_t mediaIdRaw = item->data(Qt::UserRole + 1).toULongLong();

    emit itemDragStarted(name, itemType, mediaIdRaw);

    auto* drag = new QDrag(this);
    auto* mimeData = new QMimeData();

    // Pack data for standard drag transfer
    mimeData->setData(bridge::kMimeClipType, QByteArray::number(itemType));
    mimeData->setData(bridge::kMimeClipName, name.toUtf8());
    mimeData->setData(bridge::kMimeClipId, QByteArray::number(mediaIdRaw));
    mimeData->setData(bridge::kMimeMediaId, QByteArray::number(mediaIdRaw));

    drag->setMimeData(mimeData);

    // Create a sleek glowing neon drag thumbnail
    QPixmap pixmap(140, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // Draw background glass box (using double literals for Wdouble-promotion compliance)
    painter.fillRect(pixmap.rect(), QColor(26, 47, 43, 200));
    painter.setPen(QPen(QColor(0, 255, 204), 1.0));
    painter.drawRect(0, 0, 139, 23);

    // Draw text inside
    painter.setFont(theme::Font::monospace(10, QFont::Bold));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, name);
    painter.end();

    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(70, 12));

    drag->exec(Qt::CopyAction);
}

// ─────────────────────────────────────────────────────────────────────────────
// PickerPanel Implementation
// ─────────────────────────────────────────────────────────────────────────────

PickerPanel::PickerPanel(bridge::IBrowserController* browser,
                         bridge::IPatternDataProvider* patternData,
                         QWidget* parent)
    : QWidget(parent)
    , m_browser(browser)
    , m_patternData(patternData)
{
    setObjectName(QStringLiteral("PickerPanel"));
    setMinimumWidth(160);
    setMaximumWidth(320);

    setupUI();
    applyThemeStyle();

    // Initial population of all three asset lists
    reloadAudio();
    reloadAutomation();
    reloadPatterns();
}

void PickerPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Header label widget
    auto* headerWidget = new QWidget(this);
    headerWidget->setFixedHeight(36);
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(8, 0, 8, 0);

    auto* headerLabel = new QLabel(QStringLiteral("PICKER"), headerWidget);
    headerLabel->setFont(theme::Font::monospace(11, QFont::Bold));
    headerLabel->setStyleSheet(QStringLiteral("color: #00FFCC; background: transparent;"));
    headerLayout->addWidget(headerLabel, 0, Qt::AlignVCenter);
    
    // Minimalist cyber dot to indicate system active
    auto* activeDot = new QFrame(headerWidget);
    activeDot->setFixedSize(4, 4);
    activeDot->setStyleSheet(QStringLiteral("background-color: #00FFCC; border-radius: 2px;"));
    headerLayout->addWidget(activeDot, 0, Qt::AlignVCenter | Qt::AlignRight);

    mainLayout->addWidget(headerWidget);

    // Horizontal Divider below title
    auto* divTitle = new QFrame(this);
    divTitle->setFrameShape(QFrame::HLine);
    divTitle->setFixedHeight(1);
    divTitle->setStyleSheet(QStringLiteral("background-color: #526D82; border: none;"));
    mainLayout->addWidget(divTitle);

    // 2. Tab buttons bar
    auto* tabWidget = new QWidget(this);
    tabWidget->setFixedHeight(36);
    auto* tabLayout = new QHBoxLayout(tabWidget);
    tabLayout->setContentsMargins(4, 4, 4, 4);
    tabLayout->setSpacing(4);

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);

    m_audioTab = new QPushButton(QStringLiteral("AUD"), tabWidget);
    m_audioTab->setCheckable(true);
    m_audioTab->setChecked(true);
    m_audioTab->setFixedHeight(28);
    m_audioTab->setFocusPolicy(Qt::NoFocus);
    m_audioTab->setFont(theme::Font::monospace(10, QFont::Bold));
    m_audioTab->setToolTip(QStringLiteral("Show Audio Samples"));
    m_tabGroup->addButton(m_audioTab, 0);

    m_autoTab = new QPushButton(QStringLiteral("AUT"), tabWidget);
    m_autoTab->setCheckable(true);
    m_autoTab->setFixedHeight(28);
    m_autoTab->setFocusPolicy(Qt::NoFocus);
    m_autoTab->setFont(theme::Font::monospace(10, QFont::Bold));
    m_autoTab->setToolTip(QStringLiteral("Show Automatable Parameters"));
    m_tabGroup->addButton(m_autoTab, 1);

    m_patternTab = new QPushButton(QStringLiteral("PAT"), tabWidget);
    m_patternTab->setCheckable(true);
    m_patternTab->setFixedHeight(28);
    m_patternTab->setFocusPolicy(Qt::NoFocus);
    m_patternTab->setFont(theme::Font::monospace(10, QFont::Bold));
    m_patternTab->setToolTip(QStringLiteral("Show MIDI Patterns"));
    m_tabGroup->addButton(m_patternTab, 2);

    tabLayout->addWidget(m_audioTab);
    tabLayout->addWidget(m_autoTab);
    tabLayout->addWidget(m_patternTab);
    mainLayout->addWidget(tabWidget);

    // Horizontal Divider below tabs
    auto* divTabs = new QFrame(this);
    divTabs->setFrameShape(QFrame::HLine);
    divTabs->setFixedHeight(1);
    divTabs->setStyleSheet(QStringLiteral("background-color: #526D82; border: none;"));
    mainLayout->addWidget(divTabs);

    // 3. Stacked widget representing lists for each tab
    m_stackedWidget = new QStackedWidget(this);

    m_audioList = new PickerListWidget(this);
    m_automationList = new PickerListWidget(this);
    m_patternList = new PickerListWidget(this);

    m_stackedWidget->addWidget(m_audioList);
    m_stackedWidget->addWidget(m_automationList);
    m_stackedWidget->addWidget(m_patternList);

    mainLayout->addWidget(m_stackedWidget, 1);

    // Connect drag starts to forward outwards
    connect(m_audioList, &PickerListWidget::itemDragStarted, this, &PickerPanel::handleItemDrag);
    connect(m_automationList, &PickerListWidget::itemDragStarted, this, &PickerPanel::handleItemDrag);
    connect(m_patternList, &PickerListWidget::itemDragStarted, this, &PickerPanel::handleItemDrag);

    // Connect tab buttons clicks
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_tabGroup, &QButtonGroup::idClicked, this, &PickerPanel::onTabClicked);
#else
    connect(m_tabGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this, &PickerPanel::onTabClicked);
#endif
}

void PickerPanel::onTabClicked(int id)
{
    m_stackedWidget->setCurrentIndex(id);
}

void PickerPanel::handleItemDrag(const QString& /*name*/, int type, uint64_t mediaIdRaw)
{
    // Emit standard DAW clip drag signal
    emit clipDragStarted(MediaID::fromRaw(mediaIdRaw), type);
}

void PickerPanel::reloadAudio()
{
    m_audioList->clear();
    if (!m_browser) return;

    std::vector<bridge::BrowserItem> samples;
    std::vector<bridge::BrowserItem> projectRoots = m_browser->getRootItems(bridge::BrowserTab::CurrentProject);
    bridge::BrowserItem samplesFolderItem{};
    bool foundSamples = false;

    for (const auto& item : projectRoots) {
        std::string name;
        if (m_browser->getItemName(item.stringId, name)) {
            if (name == "Samples") {
                samplesFolderItem = item;
                foundSamples = true;
                break;
            }
        }
    }

    if (foundSamples) {
        samples = m_browser->getChildren(samplesFolderItem);
    }

    // Fallback: Favorites audio files
    if (samples.empty()) {
        std::vector<bridge::BrowserItem> favs = m_browser->getRootItems(bridge::BrowserTab::Favorites);
        for (const auto& item : favs) {
            if (item.type == bridge::BrowserItemType::AudioFile) {
                samples.push_back(item);
            }
        }
    }

    for (const auto& item : samples) {
        std::string name;
        m_browser->getItemName(item.stringId, name);
        auto* listItem = new QListWidgetItem(m_audioList);
        listItem->setText(QStringLiteral("♬ ") + QString::fromStdString(name));
        listItem->setData(Qt::UserRole + 0, static_cast<int>(item.type));
        listItem->setData(Qt::UserRole + 1, static_cast<qlonglong>(item.mediaId.toRaw()));
        
        if (item.colorARGB != 0) {
            QColor itemColor(
                static_cast<int>((item.colorARGB >> 16) & 0xFF),
                static_cast<int>((item.colorARGB >> 8) & 0xFF),
                static_cast<int>(item.colorARGB & 0xFF)
            );
            listItem->setForeground(QBrush(itemColor));
        } else {
            listItem->setForeground(QBrush(theme::Color::TextPrimary));
        }
    }
}

void PickerPanel::reloadAutomation()
{
    m_automationList->clear();
    if (!m_browser) return;

    std::vector<bridge::BrowserItem> favs = m_browser->getRootItems(bridge::BrowserTab::Favorites);
    for (const auto& item : favs) {
        if (item.type == bridge::BrowserItemType::PresetFile) {
            std::string name;
            m_browser->getItemName(item.stringId, name);
            auto* listItem = new QListWidgetItem(m_automationList);
            listItem->setText(QStringLiteral("⚙ ") + QString::fromStdString(name));
            listItem->setData(Qt::UserRole + 0, static_cast<int>(item.type));
            listItem->setData(Qt::UserRole + 1, static_cast<qlonglong>(item.mediaId.toRaw()));
            listItem->setForeground(QBrush(theme::Color::TextPrimary));
        }
    }
}

void PickerPanel::reloadPatterns()
{
    m_patternList->clear();
    if (!m_browser) return;

    std::vector<bridge::BrowserItem> favs = m_browser->getRootItems(bridge::BrowserTab::Favorites);
    for (const auto& item : favs) {
        if (item.type == bridge::BrowserItemType::MidiFile) {
            std::string name;
            m_browser->getItemName(item.stringId, name);
            auto* listItem = new QListWidgetItem(m_patternList);
            listItem->setText(QStringLiteral("🎹 ") + QString::fromStdString(name));
            listItem->setData(Qt::UserRole + 0, static_cast<int>(item.type));
            listItem->setData(Qt::UserRole + 1, static_cast<qlonglong>(item.mediaId.toRaw()));
            listItem->setForeground(QBrush(theme::Color::TextPrimary));
        }
    }
}

void PickerPanel::applyThemeStyle()
{
    setStyleSheet(QStringLiteral(
        "QWidget#PickerPanel {"
        "  background-color: #222831;"
        "  border-right: 1px solid #526D82;"
        "}"
        "QPushButton {"
        "  background-color: #303D49;"
        "  border: 1px solid #464F63;"
        "  border-radius: 2px;"
        "  color: #a0a5b5;"
        "  padding: 0px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #526D82;"
        "  color: #f0f1f5;"
        "  border-color: #00FFCC;"
        "}"
        "QPushButton:checked {"
        "  background-color: #1A2F2B;"
        "  color: #00FFCC;"
        "  border-color: #00FFCC;"
        "}"
    ));
}

} // namespace presentation::views
