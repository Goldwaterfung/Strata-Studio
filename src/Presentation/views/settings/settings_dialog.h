// src/Presentation/views/settings/settings_dialog.h
#pragma once

#include <QDialog>
#include <QComboBox>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QListWidget>
#include <QListWidgetItem>
#include "Middle Bridge/engine/ihardware_settings_facade.h"

namespace presentation::views {

/**
 * @brief Premium Glassmorphic Audio/MIDI Hardware Settings Dialog
 */
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(bridge::IHardwareSettingsFacade* facade, QWidget* parent = nullptr);
    ~SettingsDialog() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;

private Q_SLOTS:
    void onApplyPressed();
    void onOkPressed();
    void onCancelPressed();
    void onTelemetryTick();
    void onInputDeviceChanged(int index);
    void onOutputDeviceChanged(int index);

private:
    void setupUI();
    void loadCurrentConfig();
    void refreshTelemetry();
    void showError(const QString& message);
    void clearError();

    bridge::IHardwareSettingsFacade* m_facade;
    QTimer* m_telemetryTimer = nullptr;

    QComboBox* m_inputDeviceCombo = nullptr;
    QComboBox* m_outputDeviceCombo = nullptr;
    QComboBox* m_sampleRateCombo = nullptr;
    QComboBox* m_bufferSizeCombo = nullptr;

    QProgressBar* m_cpuProgressBar = nullptr;
    QLabel* m_cpuLabel = nullptr;
    QLabel* m_latencyLabel = nullptr;
    QLabel* m_xrunsLabel = nullptr;
    QLabel* m_errorLabel = nullptr;

    bridge::HardwareConfig m_initialConfig;
    std::vector<bridge::AudioDeviceDescriptor> m_devices;

    QListWidget* m_midiListWidget = nullptr;
    std::vector<bridge::MidiPortDescriptor> m_midiPorts;
    std::vector<uint32_t> m_initialOpenMidiPorts;
};

} // namespace presentation::views
