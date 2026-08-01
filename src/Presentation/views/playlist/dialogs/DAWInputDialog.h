// src/Presentation/views/playlist/dialogs/DAWInputDialog.h
#pragma once

#include <QDialog>
#include <QString>
#include <QLineEdit>
#include <QTextEdit>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>

namespace presentation::views {

/**
 * @brief A unified, theme-compliant dialog for text, double, and multi-line inputs.
 * Replaces QInputDialog with the DAW's dark Cyber-Industrial aesthetic.
 */
class DAWInputDialog : public QDialog {
    Q_OBJECT

public:
    static QString getText(QWidget* parent, 
                           const QString& title, 
                           const QString& labelText, 
                           const QString& defaultValue = "", 
                           bool* ok = nullptr, 
                           int maxLength = -1);

    static double getDouble(QWidget* parent, 
                            const QString& title, 
                            const QString& labelText, 
                            double defaultValue = 0.0, 
                            double min = 0.0, 
                            double max = 100.0, 
                            int decimals = 2, 
                            bool* ok = nullptr);

    static QString getMultiLineText(QWidget* parent, 
                                    const QString& title, 
                                    const QString& labelText, 
                                    const QString& defaultValue = "", 
                                    bool* ok = nullptr);

private:
    explicit DAWInputDialog(const QString& title, QWidget* parent = nullptr);
    ~DAWInputDialog() override = default;

    void buildLayout(const QString& labelText, QWidget* inputWidget);

    QDialogButtonBox* m_buttonBox{nullptr};
};

} // namespace presentation::views
