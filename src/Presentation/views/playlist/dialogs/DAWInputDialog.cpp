// src/Presentation/views/playlist/dialogs/DAWInputDialog.cpp
#include "DAWInputDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "../../theme.h"

namespace presentation::views {

DAWInputDialog::DAWInputDialog(const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("DAWInputDialog"));
    setWindowTitle(title);
    setMinimumWidth(360);
}

void DAWInputDialog::buildLayout(const QString& labelText, QWidget* inputWidget)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(0);

    if (!labelText.isEmpty()) {
        auto* label = new QLabel(labelText.toUpper(), this);
        label->setFont(theme::Font::primary(8, QFont::Bold, 112.0));
        mainLayout->addSpacing(4);
        mainLayout->addWidget(label);
        mainLayout->addSpacing(10);
    }

    inputWidget->setFont(theme::Font::primary(10, QFont::Normal));
    mainLayout->addWidget(inputWidget);

    mainLayout->addSpacing(20);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setFont(theme::Font::primary(9, QFont::Bold));
    m_buttonBox->button(QDialogButtonBox::Cancel)->setFont(theme::Font::primary(9, QFont::Bold));

    // OK button is marked as default to activate the glow stylesheet
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(m_buttonBox);
}

QString DAWInputDialog::getText(QWidget* parent, 
                                 const QString& title, 
                                 const QString& labelText, 
                                 const QString& defaultValue, 
                                 bool* ok, 
                                 int maxLength)
{
    DAWInputDialog dlg(title, parent);
    auto* edit = new QLineEdit(defaultValue, &dlg);
    if (maxLength > 0) {
        edit->setMaxLength(maxLength);
    }
    edit->selectAll();

    dlg.buildLayout(labelText, edit);

    int result = dlg.exec();
    if (ok) {
        *ok = (result == QDialog::Accepted);
    }
    return (result == QDialog::Accepted) ? edit->text() : QString();
}

double DAWInputDialog::getDouble(QWidget* parent, 
                                  const QString& title, 
                                  const QString& labelText, 
                                  double defaultValue, 
                                  double min, 
                                  double max, 
                                  int decimals, 
                                  bool* ok)
{
    DAWInputDialog dlg(title, parent);
    auto* spin = new QDoubleSpinBox(&dlg);
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setValue(defaultValue);

    dlg.buildLayout(labelText, spin);

    int result = dlg.exec();
    if (ok) {
        *ok = (result == QDialog::Accepted);
    }
    return (result == QDialog::Accepted) ? spin->value() : 0.0;
}

QString DAWInputDialog::getMultiLineText(QWidget* parent, 
                                          const QString& title, 
                                          const QString& labelText, 
                                          const QString& defaultValue, 
                                          bool* ok)
{
    DAWInputDialog dlg(title, parent);
    dlg.setMinimumHeight(240);
    auto* edit = new QTextEdit(defaultValue, &dlg);
    edit->setAcceptRichText(false);

    dlg.buildLayout(labelText, edit);

    int result = dlg.exec();
    if (ok) {
        *ok = (result == QDialog::Accepted);
    }
    return (result == QDialog::Accepted) ? edit->toPlainText() : QString();
}

} // namespace presentation::views
