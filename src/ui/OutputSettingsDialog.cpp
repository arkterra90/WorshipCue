#include "OutputSettingsDialog.h"
#include "OutputManager.h"
#include "StageLayoutDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

namespace {
class FormatPreview : public QWidget {
public:
    std::function<void(QPainter &, const QRect &)> paint;
    explicit FormatPreview(QWidget *parent) : QWidget(parent) { setMinimumSize(240, 135); }
    void paintEvent(QPaintEvent *) override { QPainter painter(this); if (paint) paint(painter, rect()); }
};
class OutputPage : public QWidget {
    OutputManager *outputs_;
    OutputId id_;
    QComboBox *target_, *signal_, *rotation_, *scaling_, *layout_;
    QSpinBox *width_, *height_, *aspectWidth_, *aspectHeight_;
    QLabel *capabilities_, *status_;
    FormatPreview *preview_;
    OutputConfiguration saved_;
    QSpinBox *spin(const QString &name, int min, int max, int value) {
        auto *control = new QSpinBox(this); control->setObjectName(name + QString::number(int(id_)));
        control->setRange(min, max); control->setValue(value);
        connect(control, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { preview_->update(); }); return control;
    }
public:
    OutputPage(OutputManager *outputs, OutputId id, QWidget *parent) : QWidget(parent), outputs_(outputs), id_(id), saved_(outputs->configuration(id)) {
        setObjectName(QStringLiteral("outputPage%1").arg(int(id)));
        auto *root = new QHBoxLayout(this);
        auto *fields = new QVBoxLayout; root->addLayout(fields, 2);
        auto *form = new QFormLayout; fields->addLayout(form);
        target_ = new QComboBox(this); target_->setObjectName(QStringLiteral("outputDisplay%1").arg(int(id)));
        form->addRow("Display assignment", target_);
        signal_ = new QComboBox(this); signal_->setObjectName(QStringLiteral("outputSignal%1").arg(int(id)));
        form->addRow("Physical signal\nresolution · refresh · scan", signal_);
        capabilities_ = new QLabel(this); capabilities_->setWordWrap(true); fields->addWidget(capabilities_);
        auto *renderHint = new QLabel("Logical canvas and rotation affect this output's rendering. They do not change the physical display signal.", this);
        renderHint->setWordWrap(true); fields->addWidget(renderHint);
        auto *format = new QFormLayout; fields->addLayout(format);
        preview_ = new FormatPreview(this);
        width_ = spin("outputWidth", 160, 8192, saved_.canvas.width());
        height_ = spin("outputHeight", 90, 8192, saved_.canvas.height());
        aspectWidth_ = spin("outputAspectWidth", 1, 10000, saved_.aspectWidth);
        aspectHeight_ = spin("outputAspectHeight", 1, 10000, saved_.aspectHeight);
        format->addRow("Logical width (pixels)", width_); format->addRow("Logical height (pixels)", height_);
        format->addRow("Content aspect width", aspectWidth_); format->addRow("Content aspect height", aspectHeight_);
        rotation_ = new QComboBox(this); rotation_->setObjectName(QStringLiteral("outputRotation%1").arg(int(id)));
        for (int angle : {0,90,180,270}) rotation_->addItem(QStringLiteral("%1° rendering rotation").arg(angle), angle);
        rotation_->setCurrentIndex(rotation_->findData(saved_.rotation)); format->addRow("Orientation", rotation_);
        scaling_ = new QComboBox(this); scaling_->setObjectName(QStringLiteral("outputScaling%1").arg(int(id)));
        scaling_->addItems({"Fit / letterbox", "Fill / crop", "Stretch"}); scaling_->setCurrentIndex(int(saved_.scaling)); format->addRow("Scaling", scaling_);
        layout_ = new QComboBox(this); layout_->setObjectName(QStringLiteral("outputStageLayout%1").arg(int(id)));
        if (isStageOutput(id)) format->addRow("Stage layout", layout_); else layout_->hide();
        for (auto *control : {rotation_, scaling_, layout_}) connect(control, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { preview_->update(); });
        auto *right = new QVBoxLayout; root->addLayout(right, 2);
        right->addWidget(new QLabel("Output preview", this)); right->addWidget(preview_, 1);
        auto *previewHint = new QLabel("Preview follows live content and draft format settings. Saving updates only this output and keeps the live cue and media playhead.", this);
        previewHint->setWordWrap(true); right->addWidget(previewHint);
        preview_->paint = [this](QPainter &painter, const QRect &rect) {
            auto config = draft(); const auto stageLayout = outputs_->stageLayout(config.stageLayout);
            if (config.validate()) outputs_->output(id_).paintOutput(painter, rect, &config, &stageLayout);
            else painter.fillRect(rect, Qt::black);
        };
        connect(&outputs_->audience(), &DisplayWindow::audienceFrameChanged, preview_, QOverload<>::of(&QWidget::update));
        auto *tick = new QTimer(this); connect(tick, &QTimer::timeout, preview_, QOverload<>::of(&QWidget::update)); tick->start(1000);
        status_ = new QLabel(this); status_->setWordWrap(true); fields->addWidget(status_);
        auto *save = new QPushButton("Save This Output", this); save->setObjectName(QStringLiteral("saveOutput%1").arg(int(id))); fields->addWidget(save);
        connect(save, &QPushButton::clicked, this, [this] { this->save(); });
        connect(target_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { reloadModes({}); preview_->update(); });
        connect(signal_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { preview_->update(); });
        connect(outputs_, &OutputManager::targetsChanged, this, [this] { reloadTargets(target_->currentData().toString()); });
        connect(outputs_, &OutputManager::layoutsChanged, this, [this] { reloadLayouts(layout_->currentData().toString()); });
        reloadTargets(saved_.target); reloadModes(saved_.signalMode); reloadLayouts(saved_.stageLayout);
    }
    OutputConfiguration draft() const {
        OutputConfiguration config;
        config.target = target_->currentData().toString(); config.signalMode = signal_->currentData().toString();
        config.canvas = {width_->value(), height_->value()}; config.aspectWidth = aspectWidth_->value(); config.aspectHeight = aspectHeight_->value();
        config.rotation = rotation_->currentData().toInt(); config.scaling = OutputConfiguration::Scaling(scaling_->currentIndex());
        config.stageLayout = isStageOutput(id_) ? layout_->currentData().toString() : saved_.stageLayout; return config;
    }
    bool dirty() const { return draft().toJson() != saved_.toJson(); }
    void reloadTargets(const QString &selected) {
        const QSignalBlocker block(target_); target_->clear();
        for (const auto &target : outputs_->availableTargets()) target_->addItem(target.label, target.id);
        if (target_->findData(selected) < 0) target_->addItem("Unavailable: " + selected, selected);
        target_->setCurrentIndex(target_->findData(selected));
        reloadModes(signal_->currentData().toString());
    }
    void reloadModes(const QString &selected) {
        const QSignalBlocker block(signal_); signal_->clear(); signal_->addItem("Keep OS desktop mode", QString());
        const auto caps = outputs_->capabilities(target_->currentData().toString());
        for (const auto &mode : caps.modes) signal_->addItem(mode.label() + (mode.id == caps.currentMode ? " · current" : ""), mode.id);
        if (!selected.isEmpty() && signal_->findData(selected) < 0) signal_->addItem("Unavailable saved mode: " + selected, selected);
        signal_->setCurrentIndex(qMax(0, signal_->findData(selected))); signal_->setEnabled(caps.canSetMode);
        capabilities_->setText(caps.explanation);
    }
    void reloadLayouts(const QString &selected) {
        const QSignalBlocker block(layout_); layout_->clear();
        for (const auto &layout : outputs_->stageLayouts()) layout_->addItem(layout.name, layout.id);
        layout_->setCurrentIndex(qMax(0, layout_->findData(selected))); preview_->update();
    }
    void save() {
        const auto config = draft(); const auto previous = outputs_->configuration(id_); QString error;
        const auto previousSignal = config.signalMode.isEmpty() ? QString() : outputs_->capabilities(config.target).currentMode;
        if (!outputs_->setConfiguration(id_, config, &error)) { status_->setText(error); return; }
        if (!config.signalMode.isEmpty() && config.signalMode != previousSignal) {
            QMessageBox keep(QMessageBox::Question, "Keep display mode?", "Keep this physical display mode? It will revert in 15 seconds.", QMessageBox::Yes | QMessageBox::No, this);
            keep.setDefaultButton(QMessageBox::No);
            QTimer timeout; timeout.setSingleShot(true);
            connect(&timeout, &QTimer::timeout, &keep, [&keep] { keep.reject(); }); timeout.start(15000);
            if (keep.exec() != QMessageBox::Yes) {
                if (!outputs_->setConfiguration(id_, previous, &error)) status_->setText("Could not revert mode: " + error);
                else { status_->setText("Physical mode reverted. Draft settings remain available for review."); }
                return;
            }
        }
        saved_ = config; status_->setText("Saved. This output is " + outputs_->status(id_) + "."); preview_->update();
    }
};
}
OutputSettingsDialog::OutputSettingsDialog(OutputManager *outputs, QWidget *parent) : QDialog(parent) {
    setObjectName("outputSettingsDialog"); setWindowTitle("Screens / Output Settings"); resize(1000, 700);
    auto *root = new QVBoxLayout(this);
    auto *hint = new QLabel("Configure four independent outputs. Save each output separately; unsaved format changes appear only in its preview.", this);
    hint->setWordWrap(true); root->addWidget(hint);
    auto *tabs = new QTabWidget(this); tabs->setObjectName("outputSettingsTabs"); root->addWidget(tabs, 1);
    QVector<OutputPage *> pages;
    for (auto id : outputIds) { auto *page = new OutputPage(outputs, id, tabs); pages.append(page); tabs->addTab(page, outputName(id)); }
    auto *tools = new QHBoxLayout; root->addLayout(tools);
    auto *layouts = new QPushButton("Stage Layouts…", this); layouts->setObjectName("openStageLayouts"); tools->addWidget(layouts);
    connect(layouts, &QPushButton::clicked, this, [this, outputs] { StageLayoutDialog dialog(outputs, this); dialog.exec(); });
    auto *controls = new QPushButton("Stage Messages / Timers…", this); controls->setObjectName("openStageControls"); tools->addWidget(controls);
    connect(controls, &QPushButton::clicked, this, [this, outputs] { StageControlsDialog dialog(outputs, this); dialog.exec(); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Saving is explicit per output; Close/Escape discard only unsaved preview edits.
}
