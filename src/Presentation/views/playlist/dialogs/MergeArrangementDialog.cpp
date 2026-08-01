#include "MergeArrangementDialog.h"
#include "Middle Bridge/timeline/iarrangement_manager_controller.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QGroupBox>

namespace presentation::views {

MergeArrangementDialog::MergeArrangementDialog(bridge::IArrangementManagerController* managerCtrl, QWidget* parent)
    : QDialog(parent)
    , m_managerCtrl(managerCtrl)
{
    setWindowTitle(QStringLiteral("Merge Arrangements"));
    setMinimumSize(420, 520);
    setModal(true);

    setupUI();
    applyThemeStyle();
}

void MergeArrangementDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // Title / Header label
    auto* headerLabel = new QLabel(QStringLiteral("MERGE ARRANGEMENT SOURCE"), this);
    headerLabel->setFont(theme::Font::monospace(11, QFont::Bold));
    headerLabel->setStyleSheet(QStringLiteral("color: #00FFCC;"));
    mainLayout->addWidget(headerLabel);

    // 1. Source Arrangement selection
    auto* sourceWidget = new QWidget(this);
    auto* sourceLayout = new QHBoxLayout(sourceWidget);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(8);

    auto* sourceLabel = new QLabel(QStringLiteral("Select Scene:"), sourceWidget);
    sourceLabel->setFont(theme::Font::primary(11, QFont::Bold));
    sourceLabel->setStyleSheet(QStringLiteral("color: #a0a5b5;"));
    
    m_sourceCombo = new QComboBox(sourceWidget);
    m_sourceCombo->setFixedHeight(28);
    m_sourceCombo->setFont(theme::Font::primary(11));

    if (m_managerCtrl) {
        auto arrangements = m_managerCtrl->getArrangements();
        m_activeId = m_managerCtrl->getActiveArrangement();
        
        for (const auto& arr : arrangements) {
            if (arr.id == m_activeId) continue; // Prevent self-merging
            m_sourceCombo->addItem(QString::fromUtf8(arr.name), QVariant(static_cast<uint>(arr.id.id)));
        }
    }

    sourceLayout->addWidget(sourceLabel);
    sourceLayout->addWidget(m_sourceCombo, 1);
    mainLayout->addWidget(sourceWidget);

    // 2. Alignment Mode Options
    auto* modeBox = new QGroupBox(QStringLiteral("Alignment Configuration"), this);
    modeBox->setFont(theme::Font::monospace(10, QFont::Bold));
    auto* modeLayout = new QVBoxLayout(modeBox);
    modeLayout->setContentsMargins(12, 16, 12, 12);
    modeLayout->setSpacing(8);

    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);

    m_overlayRadio = new QRadioButton(QStringLiteral("Overlay starting at frame 0 (overlapped mix)"), modeBox);
    m_overlayRadio->setChecked(true);
    m_overlayRadio->setFont(theme::Font::primary(11));
    m_overlayRadio->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    m_modeGroup->addButton(m_overlayRadio, 0);

    m_appendRadio = new QRadioButton(QStringLiteral("Append at the end of current arrangement"), modeBox);
    m_appendRadio->setFont(theme::Font::primary(11));
    m_appendRadio->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    m_modeGroup->addButton(m_appendRadio, 1);

    m_newTracksRadio = new QRadioButton(QStringLiteral("Import into newly created tracks"), modeBox);
    m_newTracksRadio->setFont(theme::Font::primary(11));
    m_newTracksRadio->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    m_modeGroup->addButton(m_newTracksRadio, 2);

    modeLayout->addWidget(m_overlayRadio);
    modeLayout->addWidget(m_appendRadio);
    modeLayout->addWidget(m_newTracksRadio);
    mainLayout->addWidget(modeBox);

    // 3. Filter Flags & Ranges
    auto* filterBox = new QGroupBox(QStringLiteral("Import Components & Bounds"), this);
    filterBox->setFont(theme::Font::monospace(10, QFont::Bold));
    auto* filterLayout = new QGridLayout(filterBox);
    filterLayout->setContentsMargins(12, 16, 12, 12);
    filterLayout->setSpacing(8);

    m_audioCheck = new QCheckBox(QStringLiteral("Import Audio Clips"), filterBox);
    m_audioCheck->setChecked(true);
    m_audioCheck->setFont(theme::Font::primary(11));
    m_audioCheck->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    filterLayout->addWidget(m_audioCheck, 0, 0);

    m_midiCheck = new QCheckBox(QStringLiteral("Import MIDI Patterns"), filterBox);
    m_midiCheck->setChecked(true);
    m_midiCheck->setFont(theme::Font::primary(11));
    m_midiCheck->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    filterLayout->addWidget(m_midiCheck, 0, 1);

    m_autoCheck = new QCheckBox(QStringLiteral("Import Automation"), filterBox);
    m_autoCheck->setChecked(true);
    m_autoCheck->setFont(theme::Font::primary(11));
    m_autoCheck->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    filterLayout->addWidget(m_autoCheck, 1, 0);

    m_mixerCheck = new QCheckBox(QStringLiteral("Import Track Inserts"), filterBox);
    m_mixerCheck->setChecked(false);
    m_mixerCheck->setFont(theme::Font::primary(11));
    m_mixerCheck->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    filterLayout->addWidget(m_mixerCheck, 1, 1);

    m_limitToLoopCheck = new QCheckBox(QStringLiteral("Limit import to active loop boundaries"), filterBox);
    m_limitToLoopCheck->setChecked(false);
    m_limitToLoopCheck->setFont(theme::Font::primary(11));
    m_limitToLoopCheck->setStyleSheet(QStringLiteral("color: #DDE6ED;"));
    filterLayout->addWidget(m_limitToLoopCheck, 2, 0, 1, 2);

    mainLayout->addWidget(filterBox);

    // Buttons
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(m_buttonBox);
}

ArrangementID MergeArrangementDialog::getSourceArrangementId() const
{
    int idx = m_sourceCombo->currentIndex();
    if (idx < 0) return ArrangementID::invalid();
    uint rawId = m_sourceCombo->itemData(idx).toUInt();
    return ArrangementID{rawId};
}

ArrangementID MergeArrangementDialog::getDestinationArrangementId() const
{
    return m_activeId;
}

int MergeArrangementDialog::getMergeMode() const
{
    if (m_appendRadio->isChecked()) return 1;
    if (m_newTracksRadio->isChecked()) return 2;
    return 0; // Overlay is 0
}

bool MergeArrangementDialog::getImportAudio() const
{
    return m_audioCheck->isChecked();
}

bool MergeArrangementDialog::getImportMIDI() const
{
    return m_midiCheck->isChecked();
}

bool MergeArrangementDialog::getImportAutomation() const
{
    return m_autoCheck->isChecked();
}

bool MergeArrangementDialog::getImportMixerSettings() const
{
    return m_mixerCheck->isChecked();
}

bool MergeArrangementDialog::getLimitToLoop() const
{
    return m_limitToLoopCheck->isChecked();
}

void MergeArrangementDialog::applyThemeStyle()
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
    ));
}

} // namespace presentation::views
