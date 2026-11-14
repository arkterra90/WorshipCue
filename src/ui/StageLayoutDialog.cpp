#include "StageLayoutDialog.h"
#include "OutputManager.h"
#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDateTimeEdit>
#include <QTabWidget>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace {
class LayoutEditor : public QWidget {
    OutputManager *outputs_;
    StageLayout draft_ = StageLayout::standard();
    QComboBox *layouts_, *kind_, *alignment_;
    QLineEdit *name_, *background_, *color_;
    QListWidget *elements_;
    QPlainTextEdit *text_;
    QFontComboBox *font_;
    QSpinBox *size_;
    QCheckBox *bold_;
    QDoubleSpinBox *x_, *y_, *width_, *height_;
    QLabel *preview_, *status_;
    QPushButton *save_;
    bool loading_ = true;
    bool invalidPreview_ = false;
    bool fontEdited_ = false;
    QDoubleSpinBox *percent(const char *name) {
        auto *spin = new QDoubleSpinBox(this); spin->setObjectName(name); spin->setRange(0,100); spin->setDecimals(1); spin->setSuffix(" %");
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { changed(); }); return spin;
    }
    QPushButton *button(const QString &label, const char *name, QBoxLayout *row) {
        auto *button = new QPushButton(label, this); button->setObjectName(name); row->addWidget(button); return button;
    }
public:
    LayoutEditor(OutputManager *outputs, QWidget *parent) : QWidget(parent), outputs_(outputs) {
        auto *root = new QVBoxLayout(this);
        auto *saved = new QHBoxLayout; root->addLayout(saved);
        layouts_ = new QComboBox(this); layouts_->setObjectName("savedStageLayouts"); saved->addWidget(layouts_, 1);
        connect(button("New", "newStageLayout", saved), &QPushButton::clicked, this, [this] { draft_ = StageLayout::standard(); makeCopy("New Stage Layout"); });
        connect(button("Copy", "copyStageLayout", saved), &QPushButton::clicked, this, [this] { makeCopy(draft_.name + " Copy"); });
        connect(button("Delete", "deleteStageLayout", saved), &QPushButton::clicked, this, [this] {
            QString error;
            if (!outputs_->deleteStageLayout(layouts_->currentData().toString(), &error)) { status_->setText(error); return; }
            reload("standard");
        });
        auto *definition = new QFormLayout; root->addLayout(definition);
        name_ = new QLineEdit(this); name_->setObjectName("stageLayoutName"); name_->setMaxLength(100); definition->addRow("Layout name", name_);
        background_ = new QLineEdit(this); background_->setObjectName("stageLayoutBackground"); background_->setMaxLength(32); definition->addRow("Background color", background_);
        auto *body = new QHBoxLayout; root->addLayout(body, 1);
        auto *list = new QVBoxLayout; body->addLayout(list, 1);
        list->addWidget(new QLabel("Elements (bottom to top)", this));
        elements_ = new QListWidget(this); elements_->setObjectName("stageLayoutElements"); list->addWidget(elements_, 1);
        auto *tools = new QHBoxLayout; list->addLayout(tools);
        connect(button("Add", "addStageElement", tools), &QPushButton::clicked, this, [this] {
            if (draft_.elements.size() >= 64) return;
            draft_.elements.append(StageElement{}); rebuild(int(draft_.elements.size()) - 1);
        });
        connect(button("Remove", "removeStageElement", tools), &QPushButton::clicked, this, [this] {
            const int row = elements_->currentRow(); if (row < 0 || draft_.elements.size() <= 1) return;
            draft_.elements.removeAt(row); rebuild(qMin(row, int(draft_.elements.size()) - 1));
        });
        auto *order = new QHBoxLayout; list->addLayout(order);
        connect(button("↑", "raiseStageElement", order), &QPushButton::clicked, this, [this] {
            int row = elements_->currentRow(); if (row > 0) { draft_.elements.swapItemsAt(row, row - 1); rebuild(row - 1); }
        });
        connect(button("↓", "lowerStageElement", order), &QPushButton::clicked, this, [this] {
            int row = elements_->currentRow(); if (row >= 0 && row + 1 < draft_.elements.size()) { draft_.elements.swapItemsAt(row, row + 1); rebuild(row + 1); }
        });
        auto *properties = new QFormLayout; body->addLayout(properties, 1);
        kind_ = new QComboBox(this); kind_->setObjectName("stageElementKind");
        for (int kind = 0; kind <= 6; ++kind) kind_->addItem(StageLayout::elementName(StageElement::Kind(kind)), kind);
        properties->addRow("Content", kind_);
        x_ = percent("stageElementX"); y_ = percent("stageElementY"); width_ = percent("stageElementWidth"); height_ = percent("stageElementHeight");
        width_->setMinimum(1); height_->setMinimum(1);
        properties->addRow("Left", x_); properties->addRow("Top", y_); properties->addRow("Width", width_); properties->addRow("Height", height_);
        text_ = new QPlainTextEdit(this); text_->setObjectName("stageElementText"); text_->setMaximumHeight(90); properties->addRow("Custom text", text_);
        font_ = new QFontComboBox(this); font_->setObjectName("stageElementFont"); properties->addRow("Font", font_);
        size_ = new QSpinBox(this); size_->setObjectName("stageElementFontSize"); size_->setRange(8,500); properties->addRow("Font size (1080 canvas)", size_);
        bold_ = new QCheckBox("Bold", this); bold_->setObjectName("stageElementBold"); properties->addRow(QString(), bold_);
        color_ = new QLineEdit(this); color_->setObjectName("stageElementColor"); color_->setMaxLength(32); properties->addRow("Text color", color_);
        alignment_ = new QComboBox(this); alignment_->setObjectName("stageElementAlignment"); alignment_->addItems({"Left", "Center", "Right"}); properties->addRow("Alignment", alignment_);
        auto *right = new QVBoxLayout; body->addLayout(right, 2);
        right->addWidget(new QLabel("Live-content layout preview", this));
        preview_ = new QLabel(this); preview_->setObjectName("stageLayoutPreview"); preview_->setMinimumSize(320,180); preview_->setAlignment(Qt::AlignCenter); right->addWidget(preview_, 1);
        auto *scope = new QLabel("Copy Standard Stage to save a custom layout. Position and size are percentages of the output canvas. Saved edits update any stage output assigned this layout. Assign layouts in Screens / Output Settings.", this);
        scope->setWordWrap(true); right->addWidget(scope);
        status_ = new QLabel(this); status_->setObjectName("stageLayoutStatus"); status_->setWordWrap(true); root->addWidget(status_);
        save_ = new QPushButton("Save Layout", this); save_->setObjectName("saveStageLayout"); root->addWidget(save_);
        connect(save_, &QPushButton::clicked, this, [this] {
            QString error;
            if (!outputs_->saveStageLayout(draft_, &error)) { status_->setText(error); return; }
            reload(draft_.id); status_->setText("Layout saved. Assign it independently to either stage screen.");
        });
        connect(layouts_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            draft_ = outputs_->stageLayout(layouts_->currentData().toString()); load();
        });
        connect(elements_, &QListWidget::currentRowChanged, this, [this] { loadElement(); });
        for (auto *edit : {name_, background_, color_}) connect(edit, &QLineEdit::textChanged, this, [this] { changed(); });
        for (auto *combo : {kind_, alignment_}) connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { changed(); });
        connect(text_, &QPlainTextEdit::textChanged, this, [this] { changed(); });
        connect(font_, &QFontComboBox::currentFontChanged, this, [this] { if (!loading_) fontEdited_ = true; changed(); });
        connect(size_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { changed(); });
        connect(bold_, &QCheckBox::toggled, this, [this] { changed(); });
        auto *tick = new QTimer(this); connect(tick, &QTimer::timeout, this, [this] { render(); }); tick->start(1000);
        reload("standard");
    }
    void makeCopy(const QString &name) { draft_.id = QUuid::createUuid().toString(QUuid::WithoutBraces); draft_.name = name; load(); }
    void reload(const QString &id) {
        const QSignalBlocker block(layouts_); layouts_->clear();
        for (const auto &layout : outputs_->stageLayouts()) layouts_->addItem(layout.name, layout.id);
        layouts_->setCurrentIndex(qMax(0, layouts_->findData(id))); draft_ = outputs_->stageLayout(layouts_->currentData().toString()); load();
    }
    void load() {
        loading_ = true; name_->setText(draft_.name); background_->setText(draft_.background); loading_ = false;
        save_->setEnabled(draft_.id != "standard"); rebuild(0);
    }
    void rebuild(int selected) {
        { const QSignalBlocker block(elements_); elements_->clear();
          for (const auto &element : draft_.elements) elements_->addItem(StageLayout::elementName(element.kind));
          elements_->setCurrentRow(qBound(0, selected, elements_->count() - 1)); }
        loadElement(); render();
    }
    void loadElement() {
        const int row = elements_->currentRow(); if (row < 0) return;
        const auto &e = draft_.elements.at(row); loading_ = true;
        kind_->setCurrentIndex(int(e.kind)); x_->setValue(e.rect.x() * 100); y_->setValue(e.rect.y() * 100);
        width_->setValue(e.rect.width() * 100); height_->setValue(e.rect.height() * 100); text_->setPlainText(e.text);
        fontEdited_ = false;
        font_->setCurrentFont(e.fontFamily.isEmpty() ? QApplication::font() : QFont(e.fontFamily));
        font_->lineEdit()->setText(e.fontFamily.isEmpty() ? QStringLiteral("System font") : e.fontFamily);
        size_->setValue(e.fontSize); bold_->setChecked(e.bold);
        color_->setText(e.color); alignment_->setCurrentIndex(e.alignment);
        loading_ = false; text_->setEnabled(e.kind == StageElement::Kind::CustomText);
    }
    void changed() {
        if (loading_) return;
        draft_.name = name_->text(); draft_.background = background_->text();
        const int row = elements_->currentRow();
        if (row >= 0) {
            auto &e = draft_.elements[row]; e.kind = StageElement::Kind(kind_->currentIndex());
            e.rect = {x_->value()/100., y_->value()/100., width_->value()/100., height_->value()/100.};
            e.text = text_->toPlainText();
            if (fontEdited_) e.fontFamily = font_->currentFont().family();
            e.fontSize = size_->value();
            e.bold = bold_->isChecked(); e.color = color_->text(); e.alignment = alignment_->currentIndex();
            elements_->item(row)->setText(StageLayout::elementName(e.kind)); text_->setEnabled(e.kind == StageElement::Kind::CustomText);
        }
        render();
    }
    void render() {
        QString error;
        if (!draft_.validate(&error)) { status_->setText(error); invalidPreview_ = true; return; }
        if (invalidPreview_) { status_->clear(); invalidPreview_ = false; }
        QPixmap image(640,360); image.fill(Qt::black); QPainter painter(&image);
        paintStageLayout(painter, image.rect(), draft_, outputs_->stage().presentationState(), outputs_->stage().stageRuntime());
        painter.end(); preview_->setPixmap(image.scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
};
}
StageLayoutDialog::StageLayoutDialog(OutputManager *outputs, QWidget *parent) : QDialog(parent) {
    setObjectName("stageLayoutDialog"); setWindowTitle("Stage Display Layouts"); resize(1100,750);
    auto *root = new QVBoxLayout(this); root->addWidget(new LayoutEditor(outputs, this), 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
StageControlsDialog::StageControlsDialog(OutputManager *outputs, QWidget *parent) : QDialog(parent) {
    setObjectName("stageControlsDialog");
    setWindowTitle("Timers / Confidence Monitor");
    setModal(false);
    resize(920, 650);
    auto *root = new QVBoxLayout(this);
    auto *outputRow = new QHBoxLayout;
    root->addLayout(outputRow);
    auto *stage = new QComboBox(this);
    stage->setObjectName("stageControlOutput");
    stage->addItem(outputName(OutputId::Stage1), int(OutputId::Stage1));
    stage->addItem(outputName(OutputId::Stage2), int(OutputId::Stage2));
    stage->setAccessibleName("Confidence monitor");
    outputRow->addWidget(stage);
    auto *route = new QLabel(this);
    route->setObjectName("stageTimerRoute");
    route->setWordWrap(true);
    outputRow->addWidget(route, 1);
    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);
    auto *timerPage = new QWidget(tabs);
    tabs->addTab(timerPage, "Timer");
    auto *body = new QHBoxLayout(timerPage);
    auto *controls = new QVBoxLayout;
    body->addLayout(controls, 1);
    auto *form = new QFormLayout;
    controls->addLayout(form);
    auto *name = new QLineEdit(this);
    name->setObjectName("stageTimerName");
    name->setMaxLength(100);
    form->addRow("Timer name", name);
    auto *mode = new QComboBox(this);
    mode->setObjectName("stageTimerMode");
    mode->addItems({"Countdown", "Countdown to Time", "Elapsed Time"});
    form->addRow("Type", mode);
    auto *duration = new QWidget(this);
    auto *durationRow = new QHBoxLayout(duration);
    durationRow->setContentsMargins(0, 0, 0, 0);
    auto *hours = new QSpinBox(this);
    hours->setObjectName("stageCountdownHours"); hours->setRange(0, 99); hours->setSuffix(" h");
    auto *minutes = new QSpinBox(this);
    minutes->setObjectName("stageCountdownMinutes"); minutes->setRange(0, 59); minutes->setSuffix(" m");
    auto *seconds = new QSpinBox(this);
    seconds->setObjectName("stageCountdownSeconds"); seconds->setRange(0, 59); seconds->setSuffix(" s");
    durationRow->addWidget(hours); durationRow->addWidget(minutes); durationRow->addWidget(seconds);
    auto *durationLabel = new QLabel("Duration", this);
    form->addRow(durationLabel, duration);
    auto *target = new QDateTimeEdit(this);
    target->setObjectName("stageTimerTarget");
    target->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    target->setCalendarPopup(true);
    auto *targetLabel = new QLabel("Target (local time)", this);
    form->addRow(targetLabel, target);
    auto *limited = new QCheckBox("Use an elapsed limit", this);
    limited->setObjectName("stageTimerElapsedLimit");
    form->addRow(limited);
    auto *overrun = new QCheckBox("Continue past the end (overrun)", this);
    overrun->setObjectName("stageTimerOverrun");
    form->addRow(overrun);
    auto *warning = new QSpinBox(this);
    warning->setObjectName("stageTimerWarningSeconds");
    warning->setRange(0, 359999); warning->setSuffix(" s");
    form->addRow("Warn when time left is", warning);
    auto *visible = new QCheckBox("Show on confidence monitor", this);
    visible->setObjectName("stageTimerVisible");
    controls->addWidget(visible);
    auto *remaining = new QLabel(this);
    remaining->setObjectName("stageCountdownRemaining");
    remaining->setAlignment(Qt::AlignCenter);
    controls->addWidget(remaining);
    auto *state = new QLabel(this);
    state->setObjectName("stageTimerState");
    state->setAlignment(Qt::AlignCenter);
    controls->addWidget(state);
    auto *start = new QPushButton("Start && Show", this);
    start->setObjectName("stageCountdownStart");
    controls->addWidget(start);
    auto *buttonsRow = new QHBoxLayout;
    controls->addLayout(buttonsRow);
    auto *pause = new QPushButton("Pause", this);
    pause->setObjectName("stageCountdownPause"); buttonsRow->addWidget(pause);
    auto *resume = new QPushButton("Resume", this);
    resume->setObjectName("stageCountdownResume"); buttonsRow->addWidget(resume);
    auto *reset = new QPushButton("Reset", this);
    reset->setObjectName("stageCountdownReset"); buttonsRow->addWidget(reset);
    auto *hint = new QLabel(this);
    hint->setWordWrap(true);
    controls->addWidget(hint);
    controls->addStretch();
    auto *previewColumn = new QVBoxLayout;
    body->addLayout(previewColumn, 1);
    previewColumn->addWidget(new QLabel("Confidence monitor preview", this));
    auto *preview = new QLabel(this);
    preview->setObjectName("stageTimerPreview");
    preview->setMinimumSize(360, 210);
    preview->setAlignment(Qt::AlignCenter);
    previewColumn->addWidget(preview, 1);
    auto *previewHint = new QLabel("The timer uses an existing Timer element in a custom layout, or a footer beneath the slide content. Assign a physical display in Screens / Output Settings.", this);
    previewHint->setWordWrap(true);
    previewColumn->addWidget(previewHint);
    auto *messagePage = new QWidget(tabs);
    tabs->addTab(messagePage, "Stage Message");
    auto *messageLayout = new QVBoxLayout(messagePage);
    auto *message = new QPlainTextEdit(this);
    message->setObjectName("stageMessage");
    message->setPlaceholderText("Message for this stage screen");
    messageLayout->addWidget(message);
    auto *messages = new QHBoxLayout;
    messageLayout->addLayout(messages);
    auto *send = new QPushButton("Send Message", this);
    send->setObjectName("sendStageMessage"); messages->addWidget(send);
    auto *clear = new QPushButton("Clear Message", this);
    clear->setObjectName("clearStageMessage"); messages->addWidget(clear);
    auto *messageHint = new QLabel("Include a Stage Message element in a custom layout to show messages on the selected screen.", this);
    messageHint->setWordWrap(true); messageLayout->addWidget(messageHint);
    auto *status = new QLabel(this);
    status->setObjectName("stageTimerStatus");
    status->setWordWrap(true);
    root->addWidget(status);
    const auto selectedId = [stage] { return OutputId(stage->currentData().toInt()); };
    const auto selected = [outputs, selectedId]() -> DisplayWindow & { return outputs->output(selectedId()); };
    const auto updateMode = [=] {
        const bool absolute = mode->currentIndex() == int(StageTimerConfiguration::Mode::CountdownToTime);
        const bool elapsed = mode->currentIndex() == int(StageTimerConfiguration::Mode::Elapsed);
        duration->setVisible(!absolute); durationLabel->setVisible(!absolute);
        durationLabel->setText(elapsed ? "Elapsed limit" : "Duration");
        limited->setVisible(elapsed);
        duration->setEnabled(!elapsed || limited->isChecked());
        target->setVisible(absolute); targetLabel->setVisible(absolute);
        overrun->setEnabled(!elapsed || limited->isChecked());
        warning->setEnabled(!elapsed || limited->isChecked());
        hint->setText(absolute
            ? "Uses the selected local date/time. Resume catches up to that scheduled time. Edits apply with Start & Show or Reset."
            : "Start restarts from the configured value. Resume continues a paused timer. Edits apply with Start & Show or Reset.");
    };
    const auto update = [=] {
        const auto reading = selected().stageTimerReading();
        const QString color = reading.atEnd ? "#ff6b6b"
            : reading.warning ? "#f5c252" : "#eaf0f8";
        remaining->setText(reading.text);
        remaining->setStyleSheet("font-size: 36px; font-weight: 700; color: " + color + ";");
        const QStringList states{"Ready", "Running", "Paused", "Finished"};
        state->setText(states.at(int(reading.state)) + (reading.overrun ? " · Over time" : QString()));
        pause->setEnabled(reading.state == StageTimerReading::State::Running);
        resume->setEnabled(reading.state == StageTimerReading::State::Paused);
        start->setText(reading.state == StageTimerReading::State::Ready ? "Start && Show" : "Restart && Show");
        route->setText(outputs->status(selectedId()));
        QPixmap image(640, 360);
        image.fill(Qt::black);
        { QPainter painter(&image); selected().paintOutput(painter, image.rect()); }
        preview->setPixmap(image.scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    const auto load = [=] {
        const auto configuration = selected().stageTimerConfiguration();
        name->setText(configuration.name);
        mode->setCurrentIndex(int(configuration.mode));
        const int total = configuration.mode == StageTimerConfiguration::Mode::Elapsed && configuration.elapsedLimitSeconds > 0
            ? configuration.elapsedLimitSeconds : configuration.durationSeconds;
        hours->setValue(total / 3600); minutes->setValue((total / 60) % 60); seconds->setValue(total % 60);
        target->setDateTime(configuration.target.toLocalTime());
        limited->setChecked(configuration.elapsedLimitSeconds > 0);
        overrun->setChecked(configuration.allowOverrun);
        warning->setValue(configuration.warningSeconds);
        { const QSignalBlocker block(visible); visible->setChecked(selected().stageTimerVisible()); }
        message->setPlainText(selected().stageRuntime().message);
        status->clear(); updateMode(); update();
    };
    const auto apply = [=](bool run) {
        StageTimerConfiguration configuration;
        configuration.name = name->text().trimmed();
        configuration.mode = StageTimerConfiguration::Mode(mode->currentIndex());
        const int total = hours->value() * 3600 + minutes->value() * 60 + seconds->value();
        const bool needsDuration = configuration.mode == StageTimerConfiguration::Mode::Countdown
            || (configuration.mode == StageTimerConfiguration::Mode::Elapsed && limited->isChecked());
        if (needsDuration && total == 0) { status->setText("Enter a duration greater than zero."); return; }
        configuration.durationSeconds = qMax(1, total);
        configuration.elapsedLimitSeconds = configuration.mode == StageTimerConfiguration::Mode::Elapsed && limited->isChecked() ? total : 0;
        configuration.target = target->dateTime();
        configuration.allowOverrun = overrun->isChecked();
        configuration.warningSeconds = warning->value();
        QString error;
        if (!outputs->configureStageTimer(selectedId(), configuration, &error)) { status->setText(error); return; }
        if (run) {
            outputs->showStageTimer(selectedId(), true);
            { const QSignalBlocker block(visible); visible->setChecked(true); }
            selected().startCountdown();
        }
        status->setText(run ? QString() : QStringLiteral("Timer settings saved and reset."));
        update();
    };
    connect(stage, QOverload<int>::of(&QComboBox::currentIndexChanged), this, load);
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateMode);
    connect(limited, &QCheckBox::toggled, this, updateMode);
    connect(start, &QPushButton::clicked, this, [=] { apply(true); });
    connect(reset, &QPushButton::clicked, this, [=] { apply(false); });
    connect(pause, &QPushButton::clicked, this, [=] { selected().pauseCountdown(); update(); });
    connect(resume, &QPushButton::clicked, this, [=] { selected().resumeCountdown(); update(); });
    connect(visible, &QCheckBox::toggled, this, [=](bool show) { outputs->showStageTimer(selectedId(), show); update(); });
    connect(send, &QPushButton::clicked, this, [=] {
        status->setText(selected().setStageMessage(message->toPlainText())
            ? "Message sent to the selected stage screen." : "Use a message of at most 4000 characters.");
    });
    connect(clear, &QPushButton::clicked, this, [=] { selected().setStageMessage({}); message->clear(); });
    connect(outputs, &OutputManager::statusChanged, this, update);
    connect(outputs, &OutputManager::layoutsChanged, this, [=] {
        const QSignalBlocker block(visible); visible->setChecked(selected().stageTimerVisible()); update();
    });
    auto *tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, update);
    tick->start(200);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    load();
}
