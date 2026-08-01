#include "RenderSettingsDialog.h"
#include "Middle Bridge/engine/irender_controller.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include "Middle Bridge/timeline/iarrangement_controller.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>

namespace presentation::views {

RenderSettingsDialog::RenderSettingsDialog(
    bridge::IRenderController* renderCtrl,
    bridge::ITimelineController* timelineCtrl,
    bridge::IArrangementController* arrangementCtrl,
    QWidget* parent
)
    : QDialog(parent)
    , m_renderCtrl(renderCtrl)
    , m_timelineCtrl(timelineCtrl)
    , m_arrangementCtrl(arrangementCtrl)
{
    setWindowTitle(QStringLiteral("Render / Bounce Settings"));
    setMinimumSize(440, 540);
    setModal(true);

    setupUI();
    applyThemeStyle();

    m_simTimer = new QTimer(this);
    connect(m_simTimer, &QTimer::timeout, this, &RenderSettingsDialog::onSimulationTick);
}

void RenderSettingsDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_pagesStack = new QStackedWidget(this);

    // ==========================================
    // PAGE 0: Settings Panel
    // ==========================================
    auto* settingsPage = new QWidget(this);
    auto* settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(16, 16, 16, 16);
    settingsLayout->setSpacing(16);

    // Title label
    auto* headerLabel = new QLabel(QStringLiteral("OFFLINE RENDER ENGINE CONFIG"), settingsPage);
    headerLabel->setFont(theme::Font::monospace(11, QFont::Bold));
    headerLabel->setStyleSheet(QStringLiteral("color: #00FFCC;"));
    settingsLayout->addWidget(headerLabel);

    // Group 1: Format & Quality
    auto* qualityBox = new QGroupBox(QStringLiteral("Audio Format & Sample Precision"), settingsPage);
    qualityBox->setFont(theme::Font::monospace(10, QFont::Bold));
    auto* qualityLayout = new QGridLayout(qualityBox);
    qualityLayout->setContentsMargins(12, 16, 12, 12);
    qualityLayout->setSpacing(8);

    // File Format
    auto* formatLabel = new QLabel(QStringLiteral("Format:"), qualityBox);
    formatLabel->setFont(theme::Font::primary(11, QFont::Bold));
    formatLabel->setStyleSheet(QStringLiteral("color: #a0a5b5;"));
    m_formatCombo = new QComboBox(qualityBox);
    m_formatCombo->setFixedHeight(28);
    m_formatCombo->setFont(theme::Font::monospace(11));
    m_formatCombo->addItem(QStringLiteral("Waveform Audio File (.wav)"));
    m_formatCombo->addItem(QStringLiteral("Free Lossless Audio Codec (.flac)"));
    m_formatCombo->addItem(QStringLiteral("MPEG-1 Audio Layer III (.mp3)"));
    qualityLayout->addWidget(formatLabel, 0, 0);
    qualityLayout->addWidget(m_formatCombo, 0, 1);

    // Sample Rate
    auto* srLabel = new QLabel(QStringLiteral("Sample Rate:"), qualityBox);
    srLabel->setFont(theme::Font::primary(11, QFont::Bold));
    srLabel->setStyleSheet(QStringLiteral("color: #a0a5b5;"));
    m_sampleRateCombo = new QComboBox(qualityBox);
    m_sampleRateCombo->setFixedHeight(28);
    m_sampleRateCombo->setFont(theme::Font::monospace(11));
    m_sampleRateCombo->addItem(QStringLiteral("44.1 kHz"));
    m_sampleRateCombo->addItem(QStringLiteral("48.0 kHz"));
    m_sampleRateCombo->addItem(QStringLiteral("88.2 kHz"));
    m_sampleRateCombo->addItem(QStringLiteral("96.0 kHz"));
    m_sampleRateCombo->setCurrentIndex(1); // Default 48 kHz
    qualityLayout->addWidget(srLabel, 1, 0);
    qualityLayout->addWidget(m_sampleRateCombo, 1, 1);

    // Bit Depth
    auto* bdLabel = new QLabel(QStringLiteral("Bit Depth:"), qualityBox);
    bdLabel->setFont(theme::Font::primary(11, QFont::Bold));
    bdLabel->setStyleSheet(QStringLiteral("color: #a0a5b5;"));
    m_bitDepthCombo = new QComboBox(qualityBox);
    m_bitDepthCombo->setFixedHeight(28);
    m_bitDepthCombo->setFont(theme::Font::monospace(11));
    m_bitDepthCombo->addItem(QStringLiteral("16-bit Fixed"));
    m_bitDepthCombo->addItem(QStringLiteral("24-bit Fixed"));
    m_bitDepthCombo->addItem(QStringLiteral("32-bit Floating Point"));
    m_bitDepthCombo->setCurrentIndex(2); // Default 32-bit Float
    qualityLayout->addWidget(bdLabel, 2, 0);
    qualityLayout->addWidget(m_bitDepthCombo, 2, 1);

    settingsLayout->addWidget(qualityBox);

    // Group 2: Scope boundaries
    auto* scopeBox = new QGroupBox(QStringLiteral("Export Bounds"), settingsPage);
    scopeBox->setFont(theme::Font::monospace(10, QFont::Bold));
    auto* scopeLayout = new QVBoxLayout(scopeBox);
    scopeLayout->setContentsMargins(12, 16, 12, 12);
    scopeLayout->setSpacing(8);

    m_rangeGroup = new QButtonGroup(this);
    m_rangeGroup->setExclusive(true);

    m_fullSongRadio = new QRadioButton(QStringLiteral("Render entire arrangement range"), scopeBox);
    m_fullSongRadio->setChecked(true);
    m_fullSongRadio->setFont(theme::Font::primary(11));
    m_fullSongRadio->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    m_rangeGroup->addButton(m_fullSongRadio, 0);

    m_loopRegionRadio = new QRadioButton(QStringLiteral("Render active timeline loop selection"), scopeBox);
    m_loopRegionRadio->setFont(theme::Font::primary(11));
    m_loopRegionRadio->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    m_rangeGroup->addButton(m_loopRegionRadio, 1);

    scopeLayout->addWidget(m_fullSongRadio);
    scopeLayout->addWidget(m_loopRegionRadio);
    settingsLayout->addWidget(scopeBox);

    // Group 3: Options
    auto* optionsBox = new QGroupBox(QStringLiteral("Post-Processing Parameters"), settingsPage);
    optionsBox->setFont(theme::Font::monospace(10, QFont::Bold));
    auto* optionsLayout = new QVBoxLayout(optionsBox);
    optionsLayout->setContentsMargins(12, 16, 12, 12);
    optionsLayout->setSpacing(8);

    m_ditherCheck = new QCheckBox(QStringLiteral("Inject TPDF psychoacoustic dither (16-bit truncation)"), optionsBox);
    m_ditherCheck->setChecked(true);
    m_ditherCheck->setFont(theme::Font::primary(11));
    m_ditherCheck->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    optionsLayout->addWidget(m_ditherCheck);

    m_splitPlanarCheck = new QCheckBox(QStringLiteral("Export Split L/R Planar audio files"), optionsBox);
    m_splitPlanarCheck->setChecked(false);
    m_splitPlanarCheck->setFont(theme::Font::primary(11));
    m_splitPlanarCheck->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    optionsLayout->addWidget(m_splitPlanarCheck);

    settingsLayout->addWidget(optionsBox);

    // Controls Button bar
    auto* btnWidget = new QWidget(settingsPage);
    auto* btnLayout = new QHBoxLayout(btnWidget);
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(12);
    btnLayout->addStretch();

    m_btnCancel = new QPushButton(QStringLiteral("Cancel"), btnWidget);
    m_btnCancel->setFont(theme::Font::primary(11));
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    m_btnRender = new QPushButton(QStringLiteral("START BOUNCE"), btnWidget);
    m_btnRender->setFont(theme::Font::monospace(11, QFont::Bold));
    m_btnRender->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #1a2f2b;"
        "  border: 1px solid #00FFCC;"
        "  color: #00FFCC;"
        "  min-width: 100px;"
        "  height: 28px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #00FFCC;"
        "  color: #222831;"
        "}"
    ));
    connect(m_btnRender, &QPushButton::clicked, this, &RenderSettingsDialog::startRenderSimulation);

    btnLayout->addWidget(m_btnCancel);
    btnLayout->addWidget(m_btnRender);
    settingsLayout->addWidget(btnWidget);

    m_pagesStack->addWidget(settingsPage);

    // ==========================================
    // PAGE 1: Progress simulation Panel
    // ==========================================
    auto* progressPage = new QWidget(this);
    auto* progressLayout = new QVBoxLayout(progressPage);
    progressLayout->setContentsMargins(24, 24, 24, 24);
    progressLayout->setSpacing(20);
    progressLayout->addStretch();

    auto* bounceTitle = new QLabel(QStringLiteral("OFFLINE RENDER IN PROGRESS"), progressPage);
    bounceTitle->setFont(theme::Font::monospace(12, QFont::Bold));
    bounceTitle->setAlignment(Qt::AlignCenter);
    bounceTitle->setStyleSheet(QStringLiteral("color: #00FFCC;"));
    progressLayout->addWidget(bounceTitle);

    m_statusLabel = new QLabel(QStringLiteral("Initializing render buffers..."), progressPage);
    m_statusLabel->setFont(theme::Font::primary(11));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #a0a5b5;"));
    progressLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(progressPage);
    m_progressBar->setFixedHeight(16);
    m_progressBar->setFont(theme::Font::monospace(10, QFont::Bold));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setAlignment(Qt::AlignCenter);
    progressLayout->addWidget(m_progressBar);

    m_btnCancelProgress = new QPushButton(QStringLiteral("CANCEL BOUNCE"), progressPage);
    m_btnCancelProgress->setFont(theme::Font::monospace(11, QFont::Bold));
    m_btnCancelProgress->setFixedWidth(160);
    connect(m_btnCancelProgress, &QPushButton::clicked, this, &RenderSettingsDialog::cancelRender);
    
    auto* wrapProgressCancel = new QWidget(progressPage);
    auto* wrapProgressCancelLayout = new QHBoxLayout(wrapProgressCancel);
    wrapProgressCancelLayout->setContentsMargins(0, 0, 0, 0);
    wrapProgressCancelLayout->addWidget(m_btnCancelProgress, 0, Qt::AlignCenter);
    progressLayout->addWidget(wrapProgressCancel);

    progressLayout->addStretch();
    m_pagesStack->addWidget(progressPage);

    mainLayout->addWidget(m_pagesStack);
}

QString RenderSettingsDialog::getFileFormat() const
{
    return m_formatCombo->currentText();
}

QString RenderSettingsDialog::getSampleRate() const
{
    return m_sampleRateCombo->currentText();
}

QString RenderSettingsDialog::getBitDepth() const
{
    return m_bitDepthCombo->currentText();
}

bool RenderSettingsDialog::getEnableDither() const
{
    return m_ditherCheck->isChecked();
}

bool RenderSettingsDialog::getSplitPlanar() const
{
    return m_splitPlanarCheck->isChecked();
}

int RenderSettingsDialog::getRenderRange() const
{
    if (m_loopRegionRadio->isChecked()) return 1;
    return 0; // Full Song is 0
}

void RenderSettingsDialog::startRenderSimulation()
{
    if (!m_renderCtrl || !m_timelineCtrl) return;

    QString fileFilter;
    QString ext;
    int formatIdx = m_formatCombo->currentIndex();
    if (formatIdx == 0) {
        fileFilter = tr("Waveform Audio File (*.wav)");
        ext = ".wav";
    } else if (formatIdx == 1) {
        fileFilter = tr("Free Lossless Audio Codec (*.flac)");
        ext = ".flac";
    } else {
        fileFilter = tr("MPEG-1 Audio Layer III (*.mp3)");
        ext = ".mp3";
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Save Rendered File"), "", fileFilter);
    if (path.isEmpty()) return;

    if (!path.endsWith(ext, Qt::CaseInsensitive)) {
        path += ext;
    }

    RenderConfiguration config{};
    std::strncpy(config.outputFilePath, path.toUtf8().constData(), sizeof(config.outputFilePath) - 1);
    config.outputFilePath[sizeof(config.outputFilePath) - 1] = '\0';

    if (formatIdx == 0) {
        config.format = RenderFormat::WAV;
    } else if (formatIdx == 1) {
        config.format = RenderFormat::FLAC;
    } else {
        config.format = RenderFormat::MP3;
    }

    int depthIdx = m_bitDepthCombo->currentIndex();
    if (depthIdx == 0) config.bitDepth = 16;
    else if (depthIdx == 1) config.bitDepth = 24;
    else config.bitDepth = 32;

    config.enableDither = m_ditherCheck->isChecked();
    config.splitPlanar = m_splitPlanarCheck->isChecked();

    double sr = m_timelineCtrl->getSampleRate();
    int srIdx = m_sampleRateCombo->currentIndex();
    if (srIdx == 0) config.sampleRate = 44100;
    else if (srIdx == 1) config.sampleRate = 48000;
    else if (srIdx == 2) config.sampleRate = 88200;
    else if (srIdx == 3) config.sampleRate = 96000;
    else config.sampleRate = static_cast<uint32_t>(sr);

    if (m_loopRegionRadio->isChecked()) {
        config.startFrame = m_timelineCtrl->getLoopStart();
        config.endFrame = m_timelineCtrl->getLoopEnd();
    } else {
        config.startFrame = 0;
        uint64_t dynamicLength = m_arrangementCtrl ? m_arrangementCtrl->getArrangementLength() : 0;
        config.endFrame = (dynamicLength > 0) ? dynamicLength : (config.sampleRate * 300); // 5 min fallback
    }

    m_renderCtrl->startOfflineRender(config);

    m_progressBar->setValue(0);
    m_statusLabel->setText(QStringLiteral("Initializing render buffers..."));
    m_pagesStack->setCurrentIndex(1);
    m_simTimer->start(50);
}

void RenderSettingsDialog::onSimulationTick()
{
    if (!m_renderCtrl) return;

    char errorMsg[256];
    if (m_renderCtrl->hasFailed(errorMsg, sizeof(errorMsg))) {
        m_simTimer->stop();
        QMessageBox::critical(this, tr("Render Failed"), QString::fromUtf8(errorMsg));
        m_pagesStack->setCurrentIndex(0);
        return;
    }

    float progress = m_renderCtrl->getRenderProgress();
    m_progressBar->setValue(static_cast<int>(progress * 100.0f));
    m_statusLabel->setText(QString::fromUtf8(m_renderCtrl->getRenderStatusMessage()));

    if (!m_renderCtrl->isRenderingActive() && progress >= 1.0f) {
        m_simTimer->stop();
        m_statusLabel->setText(tr("Render complete!"));
        QTimer::singleShot(200, this, &QDialog::accept);
    }
}

void RenderSettingsDialog::cancelRender()
{
    if (m_renderCtrl) {
        m_renderCtrl->cancelOfflineRender();
    }
    m_simTimer->stop();
    m_progressBar->setValue(0);
    m_pagesStack->setCurrentIndex(0);
}

void RenderSettingsDialog::applyThemeStyle()
{
    setStyleSheet(QStringLiteral(
        "QDialog {"
        "  background-color: #222831;"
        "}"
        "QGroupBox {"
        "  border: 1px solid #526D82;"
        "  border-radius: 4px;"
        "  margin-top: 10px;"
        "  color: #9DB2BF;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 8px;"
        "  padding: 0px 4px;"
        "}"
        "QComboBox {"
        "  background-color: #303D49;"
        "  border: 1px solid #464F63;"
        "  border-radius: 2px;"
        "  color: #DDE6ED;"
        "  padding: 2px 6px;"
        "}"
        "QComboBox::drop-down {"
        "  border: none;"
        "  width: 16px;"
        "}"
        "QComboBox::down-arrow {"
        "  image: none;"
        "  border: 1px solid #464F63;"
        "  background-color: #526D82;"
        "  width: 4px;"
        "  height: 4px;"
        "}"
        "QPushButton {"
        "  background-color: #303D49;"
        "  border: 1px solid #464F63;"
        "  border-radius: 2px;"
        "  color: #a0a5b5;"
        "  min-width: 80px;"
        "  height: 28px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #526D82;"
        "  color: #DDE6ED;"
        "  border-color: #00FFCC;"
        "}"
        "QCheckBox::indicator, QRadioButton::indicator {"
        "  width: 14px;"
        "  height: 14px;"
        "  border: 1px solid #464F63;"
        "  background: #303D49;"
        "  border-radius: 2px;"
        "}"
        "QRadioButton::indicator {"
        "  border-radius: 7px;"
        "}"
        "QCheckBox::indicator:checked, QRadioButton::indicator:checked {"
        "  background: #00FFCC;"
        "  border-color: #00FFCC;"
        "}"
        "QProgressBar {"
        "  border: 1px solid #526D82;"
        "  border-radius: 4px;"
        "  background: #303D49;"
        "  color: #DDE6ED;"
        "  text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: #00FFCC;"
        "  border-radius: 2px;"
        "}"
    ));
}

} // namespace presentation::views
