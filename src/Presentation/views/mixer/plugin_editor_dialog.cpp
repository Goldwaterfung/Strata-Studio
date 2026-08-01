#include "plugin_editor_dialog.h"
#include "common/system_primitives.h"
#include "rotary_dial.h"
#include "../theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QStyle>
#include <QGuiApplication>
#include <QScreen>
#include <QVariant>
#include <cmath>

namespace presentation::views {


//==============================================================================
// Mock Visualizer Widget for Premium Aesthetics
//==============================================================================
class PluginVisualizer : public QWidget {
public:
    PluginVisualizer(uint8_t category, const std::vector<RotaryDial*>& dials, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_category(category)
        , m_dials(dials)
    {
        setFixedHeight(80);
        setMinimumWidth(220);
        
        // Listen to all dials to trigger repaint on change
        for (auto* dial : m_dials) {
            connect(dial, &RotaryDial::valueChanged, this, [this](float) { update(); });
        }
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        double w = static_cast<double>(width());
        double h = static_cast<double>(height());

        // Background dark box
        theme::PaintHelper::drawGlassPanel(&painter, QRectF(0.0, 0.0, w, h), QColor("#0B0C0E"), 4.0);

        // Draw visualizer specific to category
        if (m_category == PluginCategory::EFFECT_EQ_FILTER) {
            // Draw a beautiful EQ response curve
            double low = m_dials.size() > 0 ? static_cast<double>(m_dials[0]->value()) : 0.5;
            double mid = m_dials.size() > 1 ? static_cast<double>(m_dials[1]->value()) : 0.5;
            double high = m_dials.size() > 2 ? static_cast<double>(m_dials[2]->value()) : 0.5;

            // Draw grid lines
            painter.setPen(QPen(QColor("#181B22"), 1.0));
            for (int i = 1; i < 4; ++i) {
                double x = w * i / 4.0;
                painter.drawLine(QPointF(x, 2.0), QPointF(x, h - 2.0));
            }
            painter.drawLine(QPointF(2.0, h / 2.0), QPointF(w - 2.0, h / 2.0));

            // Generate spline
            QPainterPath path;
            path.moveTo(0, h / 2.0);
            
            double p1y = h / 2.0 - (low - 0.5) * h * 0.8;
            double p2y = h / 2.0 - (mid - 0.5) * h * 0.8;
            double p3y = h / 2.0 - (high - 0.5) * h * 0.8;

            path.cubicTo(w * 0.25, p1y, w * 0.5, p2y, w * 0.75, p2y);
            path.cubicTo(w * 0.85, p2y, w * 0.9, p3y, w, p3y);

            // Draw filled gradient area
            QPainterPath fillPath = path;
            fillPath.lineTo(w, h);
            fillPath.lineTo(0, h);
            fillPath.closeSubpath();

            QLinearGradient grad(0, 0, 0, h);
            grad.setColorAt(0.0, QColor(0, 210, 180, 80)); // Cyan glow
            grad.setColorAt(1.0, QColor(0, 210, 180, 0));
            painter.fillPath(fillPath, grad);

            // Draw the response line
            painter.setPen(QPen(QColor("#00D2B4"), 2.0));
            painter.drawPath(path);

        } else if (m_category == PluginCategory::EFFECT_DYNAMICS) {
            // Draw Compressor curve
            double thresh = m_dials.size() > 0 ? static_cast<double>(m_dials[0]->value()) : 0.5;
            double ratio = m_dials.size() > 1 ? static_cast<double>(m_dials[1]->value()) : 0.5;

            // Grid lines
            painter.setPen(QPen(QColor("#181B22"), 1.0));
            painter.drawLine(QPointF(2.0, h - 2.0), QPointF(w - 2.0, 2.0)); // linear response

            // Compressor curve path
            QPainterPath path;
            path.moveTo(0, h);
            
            double threshX = w * (0.3 + thresh * 0.5);
            double threshY = h - threshX * (h / w);

            path.lineTo(threshX, threshY);

            // Ratio dampens the slope after threshold
            double slope = (1.0 - ratio * 0.8) * (h / w);
            double endX = w;
            double endY = threshY - (endX - threshX) * slope;

            path.lineTo(endX, std::max(2.0, endY));

            // Glow fill
            QPainterPath fillPath = path;
            fillPath.lineTo(w, h);
            fillPath.lineTo(0, h);
            fillPath.closeSubpath();

            QLinearGradient grad(0, 0, 0, h);
            grad.setColorAt(0.0, QColor(230, 92, 0, 80)); // Orange glow
            grad.setColorAt(1.0, QColor(230, 92, 0, 0));
            painter.fillPath(fillPath, grad);

            painter.setPen(QPen(QColor("#E65C00"), 2.0));
            painter.drawPath(path);

            // Draw Threshold dot
            painter.setBrush(QColor("#FF7700"));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPointF(threshX, threshY), 3.5, 3.5);

        } else if (m_category == PluginCategory::EFFECT_DELAY_REVERB) {
            // Reverb/Delay tail decay visualization
            double time = m_dials.size() > 0 ? static_cast<double>(m_dials[0]->value()) : 0.5;
            double feedback = m_dials.size() > 1 ? static_cast<double>(m_dials[1]->value()) : 0.5;

            // Generate decayed impulse sequence
            painter.setPen(QPen(QColor("#007ACC"), 1.5));
            int numPulses = 12 + static_cast<int>(time * 15);
            double decay = 0.4 + feedback * 0.55;

            for (int i = 0; i < numPulses; ++i) {
                double x = 10.0 + (w - 20.0) * (i / static_cast<double>(numPulses));
                double ampHeight = (h - 20.0) * std::pow(decay, i);
                if (ampHeight < 1.0) ampHeight = 1.0;
                
                // Draw impulse line with slight random scatter to simulate reverb reflections
                double scatterY = (i > 1) ? (cos(i * 3.0) * 4.0) : 0.0;
                painter.drawLine(QPointF(x, h - 10.0), QPointF(x, h - 10.0 - ampHeight + scatterY));
            }
        } else if (m_category == PluginCategory::EFFECT_DISTORTION) {
            // Clipped sine wave visualization
            double drive = m_dials.size() > 0 ? static_cast<double>(m_dials[0]->value()) : 0.5;
            double tone = m_dials.size() > 1 ? static_cast<double>(m_dials[1]->value()) : 0.5;

            double clipThresh = h / 2.0 - (1.0 - drive * 0.8) * (h / 2.0) * 0.9;
            
            // Draw grid
            painter.setPen(QPen(QColor("#1D1818"), 1.0));
            painter.drawLine(QPointF(0, h / 2.0), QPointF(w, h / 2.0));

            // Generate clipped sine wave path
            QPainterPath path;
            path.moveTo(0, h / 2.0);
            
            double cycles = 2.0;
            double filterAlpha = tone; // Tone smooths the edges

            for (double x = 0.0; x <= w; x += 1.0) {
                double angle = (x / w) * cycles * 2.0 * M_PI;
                double sine = sin(angle);
                double yVal = (h / 2.0) - sine * (h / 2.0) * 0.85;

                // Apply soft clipping
                if (sine > 0.0) {
                    double clipLimit = h / 2.0 - clipThresh;
                    if (yVal < clipLimit) {
                        yVal = clipLimit + (yVal - clipLimit) * (1.0 - filterAlpha * 0.9);
                    }
                } else {
                    double clipLimit = h / 2.0 + clipThresh;
                    if (yVal > clipLimit) {
                        yVal = clipLimit + (yVal - clipLimit) * (1.0 - filterAlpha * 0.9);
                    }
                }

                path.lineTo(x, yVal);
            }

            // Glow fill
            QPainterPath fillPath = path;
            fillPath.lineTo(w, h);
            fillPath.lineTo(0, h);
            fillPath.closeSubpath();

            QLinearGradient grad(0, 0, 0, h);
            grad.setColorAt(0.0, QColor(255, 59, 48, 80)); // Red clipping glow
            grad.setColorAt(1.0, QColor(255, 59, 48, 0));
            painter.fillPath(fillPath, grad);

            painter.setPen(QPen(QColor("#FF3B30"), 2.0));
            painter.drawPath(path);

        } else {
            // Default: beautiful scrolling wave pattern
            painter.setPen(QPen(QColor("#526D82"), 1.0));
            painter.drawLine(QPointF(0, h / 2.0), QPointF(w, h / 2.0));

            QPainterPath path;
            path.moveTo(0, h / 2.0);
            double val = m_dials.size() > 0 ? static_cast<double>(m_dials[0]->value()) : 0.5;

            for (double x = 0.0; x <= w; x += 2.0) {
                double wave = sin((x / 20.0) + (val * 10.0)) * cos(x / 40.0);
                path.lineTo(x, h / 2.0 + wave * (h / 2.0) * 0.7);
            }

            painter.setPen(QPen(QColor("#9DB2BF"), 1.5));
            painter.drawPath(path);
        }
    }

private:
    uint8_t m_category;
    const std::vector<RotaryDial*>& m_dials;
};

//==============================================================================
// NativeContainerWidget Implementation for Heavyweight Plugin Overlap Prevention
//==============================================================================
class NativeContainerWidget : public QWidget {
public:
    explicit NativeContainerWidget(QWidget* parent = nullptr) : QWidget(parent) {
        // Crucial: Force the creation of a dedicated native OS window handle for this specific sub-widget
        setAttribute(Qt::WA_NativeWindow, true);
        // CRITICAL: Prevent the native window from propagating up to the main DAW window,
        // which destroys Qt raster performance and drops the app to ~25fps.
        setAttribute(Qt::WA_DontCreateNativeAncestors, true);
        
        // Style placeholder dark to avoid flash-frames during plugin loading
        setStyleSheet("background-color: #0B0C0E; border: 1px solid #1E222B;");
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(100, 100);
    }
};

//==============================================================================
// PluginEditorDialog Implementation
//==============================================================================

PluginEditorDialog::PluginEditorDialog(bridge::ITrackController* controller, 
                                       TrackID trackId, 
                                       uint32_t slotIndex, 
                                       const QString& pluginName, 
                                       uint8_t category,
                                       QWidget* parent,
                                       bool isInstrument)
    : QDialog(parent, Qt::Tool | Qt::WindowCloseButtonHint)
    , m_controller(controller)
    , m_trackId(trackId)
    , m_slotIndex(slotIndex)
    , m_pluginName(pluginName)
    , m_category(category)
    , m_isInstrument(isInstrument)
{
    setModal(false);
    setWindowTitle(m_isInstrument ? QString("%1 — Instrument Slot").arg(pluginName)
                                  : QString("%1 — Insert Slot %2").arg(pluginName).arg(slotIndex + 1));
    setMinimumWidth(320);

    // Fetch initial bypass state
    if (m_controller) {
        auto state = m_controller->getTrackState(m_trackId);
        if (m_isInstrument) {
            m_bypassed = state.instrument.bypassed;
        } else {
            if (m_slotIndex < state.activePluginCount) {
                m_bypassed = state.plugins[m_slotIndex].bypassed;
            }
        }
    }

    buildUI();

    if (m_controller) {
        auto state = m_controller->getTrackState(m_trackId);
        if (state.lastTweaked.isValid) {
            m_lastTweakedLabel->setText(QString("LAST TWEAKED: %1 = %2")
                .arg(QString::fromUtf8(state.lastTweaked.paramName).toUpper())
                .arg(static_cast<double>(state.lastTweaked.lastValue), 0, 'f', 3));
            if (m_addAutomationBtn) m_addAutomationBtn->setVisible(true);
        }

        m_controller->subscribeToPluginParameterTweaks(m_trackId, m_slotIndex, m_isInstrument,
            [this](TrackID tid, uint32_t sIdx, const char* paramName, float val) {
                (void)tid; (void)sIdx;
                QMetaObject::invokeMethod(this, [this, nameStr = QString::fromUtf8(paramName), val]() {
                    if (m_lastTweakedLabel) {
                        m_lastTweakedLabel->setText(QString("LAST TWEAKED: %1 = %2")
                            .arg(nameStr.toUpper())
                            .arg(static_cast<double>(val), 0, 'f', 3));
                    }
                    if (m_addAutomationBtn) {
                        m_addAutomationBtn->setVisible(true);
                    }
                }, Qt::QueuedConnection);
            });
    }

    // Center window over the cursor screen
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen) {
        QRect screenGeometry = screen->geometry();
        int x = screenGeometry.left() + (screenGeometry.width() - width()) / 2;
        int y = screenGeometry.top() + (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
}

PluginEditorDialog::~PluginEditorDialog() {
    if (m_controller) {
        m_controller->unsubscribeFromPluginParameterTweaks(m_trackId, m_slotIndex, m_isInstrument);
        if (m_isInstrument) {
            m_controller->closeInstrumentEditor(m_trackId);
        } else {
            m_controller->closePluginEditor(m_trackId, m_slotIndex);
        }
    }
}

void PluginEditorDialog::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // ── 1. Custom Title/Header Row ─────────────────────────────────────────
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel(m_pluginName.toUpper(), this);
    m_titleLabel->setFont(theme::Font::primary(13, QFont::Bold, 110.0));
    headerLayout->addWidget(m_titleLabel, 1);

    // Bypass switch
    m_bypassBtn = new QPushButton("BYPASS", this);
    m_bypassBtn->setFont(theme::Font::monospace(9, QFont::Bold));
    m_bypassBtn->setFixedHeight(theme::Layout::PluginHeaderBtnHeight);
    m_bypassBtn->setProperty("class", "header-btn");
    connect(m_bypassBtn, &QPushButton::clicked, this, &PluginEditorDialog::onBypassToggled);
    updateBypassStyle();
    headerLayout->addWidget(m_bypassBtn);

    // Remove button
    m_removeBtn = new QPushButton("REMOVE", this);
    m_removeBtn->setFont(theme::Font::monospace(9, QFont::Bold));
    m_removeBtn->setFixedHeight(theme::Layout::PluginHeaderBtnHeight);
    m_removeBtn->setProperty("class", "header-btn");
    m_removeBtn->setProperty("variant", "danger");
    connect(m_removeBtn, &QPushButton::clicked, this, &PluginEditorDialog::onRemoveClicked);
    headerLayout->addWidget(m_removeBtn);

    mainLayout->addLayout(headerLayout);

    // ── 1.5 Last Tweaked Parameter Header (Moved above sidechain) ───────────
    buildLastTweakedHeader(mainLayout);

    // ── 1.6 Sidechain Control Header ─────────────────────────────────────────
    buildSidechainHeader(mainLayout);

    // ── 2. Native Editor Attachment via NativeContainerWidget Placeholder ───
    auto* placeholder = new NativeContainerWidget(this);
    
    // Force Qt to instantly materialize the native OS window handle
    placeholder->winId();
    void* nativeWinId = reinterpret_cast<void*>(placeholder->winId());

    int prefWidth = 640;
    int prefHeight = 480;

    if (m_controller) {
        if (m_isInstrument) {
            m_hasNativeEditor = m_controller->openInstrumentEditor(m_trackId, nativeWinId, prefWidth, prefHeight);
        } else {
            m_hasNativeEditor = m_controller->openPluginEditor(m_trackId, m_slotIndex, nativeWinId, prefWidth, prefHeight);
        }
    }

    if (m_hasNativeEditor) {
        mainLayout->addWidget(placeholder, 1); // Expand to fill dialog space
        
        // Account for dialog content margins, header heights, and spacing to fit placeholder exactly
        int headerHeights = 28 + theme::Layout::PluginTweakedBarHeight;
        if (!m_isInstrument && m_controller) {
            headerHeights += theme::Layout::PluginSidechainBarHeight + mainLayout->spacing();
        }
        int totalWidth = prefWidth + 20; // 10 margins on each side
        int totalHeight = prefHeight + 20 + headerHeights + (mainLayout->spacing() * 2);
        
        placeholder->setMinimumSize(prefWidth, prefHeight);
        resize(totalWidth, totalHeight);
    } else {
        // Fallback to generic UI controls, destroy placeholder
        delete placeholder;

        m_editorContent = new QWidget(this);
        m_editorContent->setObjectName("editorContent");
        auto* contentLayout = new QVBoxLayout(m_editorContent);
        contentLayout->setContentsMargins(12, 12, 12, 12);
        contentLayout->setSpacing(12);

        auto* knobsRow = new QHBoxLayout();
        knobsRow->setSpacing(12);
        knobsRow->addStretch();

        // Custom parameter names based on category
        std::vector<QString> paramNames;
        if (m_category == PluginCategory::EFFECT_EQ_FILTER) {
            paramNames = { "Low", "Mid", "High", "Freq", "Q" };
        } else if (m_category == PluginCategory::EFFECT_DYNAMICS) {
            paramNames = { "Thresh", "Ratio", "Attack", "Release", "Makeup" };
        } else if (m_category == PluginCategory::EFFECT_DELAY_REVERB) {
            paramNames = { "Time", "Feedback", "Mix", "Damp", "Size" };
        } else if (m_category == PluginCategory::EFFECT_DISTORTION) {
            paramNames = { "Drive", "Tone", "Output", "Crush", "Dry/Wet" };
        } else if (m_category == PluginCategory::INSTRUMENT) {
            paramNames = { "Cutoff", "Reso", "Attack", "Release", "Volume" };
        } else {
            paramNames = { "Input", "Output", "Mix", "Speed", "Depth" };
        }

        // Add dials
        for (size_t i = 0; i < paramNames.size(); ++i) {
            auto* container = new QWidget(m_editorContent);
            auto* vLayout = new QVBoxLayout(container);
            vLayout->setContentsMargins(0, 0, 0, 0);
            vLayout->setSpacing(4);

            auto* dial = new RotaryDial(container);
            dial->setFixedSize(48, 48);
            dial->setDefaultValue(0.5f);
            dial->resetToDefault();
            vLayout->addWidget(dial, 0, Qt::AlignCenter);
            m_dials.push_back(dial);

            auto* label = new QLabel(paramNames[i], container);
            label->setFont(theme::Font::monospace(9, QFont::Bold));
            label->setAlignment(Qt::AlignCenter);
            vLayout->addWidget(label, 0, Qt::AlignCenter);
            m_dialLabels.push_back(label);

            knobsRow->addWidget(container);
            
            // Connect to local knob changed handler
            connect(dial, &RotaryDial::valueChanged, this, &PluginEditorDialog::onKnobChanged);
        }
        knobsRow->addStretch();
        contentLayout->addLayout(knobsRow);

        // ── 3. Visualizer Screen Section ───────────────────────────────────────
        auto* visualizer = new PluginVisualizer(m_category, m_dials, m_editorContent);
        contentLayout->addWidget(visualizer);

        mainLayout->addWidget(m_editorContent, 1);
    }
}

void PluginEditorDialog::buildLastTweakedHeader(QVBoxLayout* mainLayout) {
    m_lastTweakedContainer = new QWidget(this);
    m_lastTweakedContainer->setObjectName("lastTweakedContainer");
    m_lastTweakedContainer->setFixedHeight(theme::Layout::PluginTweakedBarHeight);

    auto* ltLayout = new QHBoxLayout(m_lastTweakedContainer);
    ltLayout->setContentsMargins(8, 3, 8, 3);
    ltLayout->setSpacing(6);

    auto* statusDot = new QLabel("●", m_lastTweakedContainer);
    statusDot->setFont(theme::Font::monospace(theme::Font::SizeDetail, QFont::Bold));
    statusDot->setStyleSheet(QString("color: %1;").arg(theme::Color::SendPreFader.name()));
    ltLayout->addWidget(statusDot);

    m_lastTweakedLabel = new QLabel("LAST TWEAKED: NONE", m_lastTweakedContainer);
    m_lastTweakedLabel->setFont(theme::Font::monospace(theme::Font::SizeDetail, QFont::Bold));
    m_lastTweakedLabel->setStyleSheet(QString("color: %1;").arg(theme::Color::TextMuted.name()));
    ltLayout->addWidget(m_lastTweakedLabel, 1);

    m_addAutomationBtn = new QPushButton("+ Automation", m_lastTweakedContainer);
    m_addAutomationBtn->setFont(theme::Font::monospace(9, QFont::Bold));
    m_addAutomationBtn->setFixedHeight(theme::Layout::PluginHeaderBtnHeight);
    m_addAutomationBtn->setCursor(Qt::PointingHandCursor);
    m_addAutomationBtn->setVisible(false);
    m_addAutomationBtn->setProperty("class", "header-btn");
    m_addAutomationBtn->setProperty("variant", "action");

    connect(m_addAutomationBtn, &QPushButton::clicked, this, &PluginEditorDialog::onAddAutomationClicked);
    ltLayout->addWidget(m_addAutomationBtn);

    m_lastTweakedContainer->setStyleSheet(QString(
        "QWidget#lastTweakedContainer {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 4px;"
        "}"
    ).arg(theme::Color::BgBase.name())
     .arg(theme::Color::BgControl.name()));

    mainLayout->addWidget(m_lastTweakedContainer);
}

void PluginEditorDialog::onAddAutomationClicked() {
    if (m_controller) {
        m_controller->requestAutomationLaneForLastTweaked(m_trackId);
    }
}

void PluginEditorDialog::updateBypassStyle() {
    const QString nextVariant = m_bypassed ? "warning" : "accent";
    if (m_bypassBtn->property("variant").toString() != nextVariant) {
        m_bypassBtn->setProperty("variant", nextVariant);
        theme::Style::updateDynamic(m_bypassBtn);
    }
}

void PluginEditorDialog::onBypassToggled() {
    m_bypassed = !m_bypassed;
    if (m_controller) {
        if (m_isInstrument) {
            m_controller->setInstrumentBypassed(m_trackId, m_bypassed);
        } else {
            m_controller->setPluginBypassed(m_trackId, m_slotIndex, m_bypassed);
        }
    }
    updateBypassStyle();
}

void PluginEditorDialog::onRemoveClicked() {
    if (m_controller) {
        if (m_isInstrument) {
            m_controller->removeInstrument(m_trackId);
        } else {
            m_controller->removePlugin(m_trackId, m_slotIndex);
        }
    }
    close();
}

void PluginEditorDialog::onKnobChanged(float val) {
    // Simply print in tooltip to let user see actual dynamic values
    auto* dial = qobject_cast<RotaryDial*>(sender());
    if (!dial) return;

    int percent = static_cast<int>(val * 100.0f);
    dial->setToolTip(QString("%1%").arg(percent));
}

void PluginEditorDialog::paintEvent(QPaintEvent* event) {
    QDialog::paintEvent(event);
}

void PluginEditorDialog::buildSidechainHeader(QVBoxLayout* mainLayout) {
    if (m_isInstrument || !m_controller) {
        return;
    }

    auto sidechainState = m_controller->getPluginSidechainState(m_trackId, m_slotIndex);

    m_sidechainContainer = new QWidget(this);
    m_sidechainContainer->setObjectName("sidechainContainer");
    m_sidechainContainer->setFixedHeight(theme::Layout::PluginSidechainBarHeight);

    auto* scLayout = new QHBoxLayout(m_sidechainContainer);
    scLayout->setContentsMargins(8, 4, 8, 4);
    scLayout->setSpacing(8);

    auto* scLabel = new QLabel("SIDECHAIN INPUT", m_sidechainContainer);
    scLabel->setFont(theme::Font::monospace(theme::Font::SizeSecondary, QFont::Bold));
    scLabel->setStyleSheet(QString("color: %1;").arg(theme::Color::TextMuted.name()));
    scLayout->addWidget(scLabel);

    m_sidechainCombo = new QComboBox(m_sidechainContainer);
    m_sidechainCombo->setFont(theme::Font::primary(theme::Font::SizeSecondary, QFont::Normal));
    m_sidechainCombo->setMinimumWidth(140);

    // Add default "-- Off --" option
    m_sidechainCombo->addItem("-- Off --", QVariant::fromValue(static_cast<uint32_t>(0)));

    // Query available sidechain sources from Middle Bridge
    auto sources = m_controller->getAvailableSidechainSources(m_trackId);
    int selectedIndex = 0;
    int currentIndex = 1;
    for (const auto& src : sources) {
        m_sidechainCombo->addItem(QString::fromStdString(src.name), QVariant::fromValue(static_cast<uint32_t>(src.optionId)));
        if (sidechainState.isConnected && sidechainState.sourceTrackId.id == src.optionId) {
            selectedIndex = currentIndex;
        }
        currentIndex++;
    }

    m_sidechainCombo->setCurrentIndex(selectedIndex);
    scLayout->addWidget(m_sidechainCombo, 1);

    // Sidechain send level knob
    m_sidechainGainDial = new RotaryDial(m_sidechainContainer);
    m_sidechainGainDial->setFixedSize(36, 36);

    // Map sendGaindB (-60.0dB..+12.0dB) to normalized [0.0..1.0]
    float dbVal = sidechainState.sendGaindB;
    float normGain = (dbVal + 60.0f) / 72.0f;
    if (normGain < 0.0f) normGain = 0.0f;
    if (normGain > 1.0f) normGain = 1.0f;
    m_sidechainGainDial->setValue(normGain);
    m_sidechainGainDial->setEnabled(sidechainState.isConnected);

    scLayout->addWidget(m_sidechainGainDial);

    m_sidechainGainLabel = new QLabel("GAIN", m_sidechainContainer);
    m_sidechainGainLabel->setFont(theme::Font::monospace(theme::Font::SizeTiny, QFont::Bold));
    m_sidechainGainLabel->setStyleSheet(QString("color: %1;").arg(theme::Color::TextMuted.name()));
    scLayout->addWidget(m_sidechainGainLabel);

    // Set tooltip display in dB
    m_sidechainGainDial->setToolTip(QString("%1 dB").arg(static_cast<double>(dbVal), 0, 'f', 1));

    // Connect signals
    connect(m_sidechainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PluginEditorDialog::onSidechainSourceChanged);
    connect(m_sidechainGainDial, &RotaryDial::valueChanged,
            this, &PluginEditorDialog::onSidechainGainChanged);

    mainLayout->addWidget(m_sidechainContainer);
}

void PluginEditorDialog::onSidechainSourceChanged(int index) {
    if (!m_controller) return;

    if (index <= 0) {
        m_controller->clearPluginSidechainSource(m_trackId, m_slotIndex);
        if (m_sidechainGainDial) {
            m_sidechainGainDial->setEnabled(false);
        }
    } else {
        uint32_t rawId = m_sidechainCombo->itemData(index).toUInt();
        TrackID sourceTrackId{rawId, 1};

        float normVal = m_sidechainGainDial ? m_sidechainGainDial->value() : 0.8333f;
        float sendGaindB = normVal * 72.0f - 60.0f;

        m_controller->setPluginSidechainSource(m_trackId, m_slotIndex, sourceTrackId, sendGaindB);
        if (m_sidechainGainDial) {
            m_sidechainGainDial->setEnabled(true);
        }
    }
}

void PluginEditorDialog::onSidechainGainChanged(float val) {
    if (!m_controller || !m_sidechainCombo) return;

    float sendGaindB = val * 72.0f - 60.0f;
    if (m_sidechainGainDial) {
        m_sidechainGainDial->setToolTip(QString("%1 dB").arg(static_cast<double>(sendGaindB), 0, 'f', 1));
    }

    int index = m_sidechainCombo->currentIndex();
    if (index > 0) {
        uint32_t rawId = m_sidechainCombo->itemData(index).toUInt();
        TrackID sourceTrackId{rawId, 1};
        m_controller->setPluginSidechainSource(m_trackId, m_slotIndex, sourceTrackId, sendGaindB);
    }
}

} // namespace presentation::views

