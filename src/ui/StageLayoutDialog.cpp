#include "StageLayoutDialog.h"
#include "OutputManager.h"
#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
    setObjectName("stageControlsDialog"); setWindowTitle("Stage Messages / Timers"); resize(520,420);
    auto *root = new QVBoxLayout(this);
    auto *stage = new QComboBox(this); stage->setObjectName("stageControlOutput");
    stage->addItem(outputName(OutputId::Stage1), int(OutputId::Stage1)); stage->addItem(outputName(OutputId::Stage2), int(OutputId::Stage2)); root->addWidget(stage);
    auto *message = new QPlainTextEdit(this); message->setObjectName("stageMessage"); message->setPlaceholderText("Message for this stage screen"); root->addWidget(message);
    auto *messages = new QHBoxLayout; root->addLayout(messages);
    auto *send = new QPushButton("Send Message", this); send->setObjectName("sendStageMessage"); messages->addWidget(send);
    auto *clear = new QPushButton("Clear Message", this); clear->setObjectName("clearStageMessage"); messages->addWidget(clear);
    auto *seconds = new QSpinBox(this); seconds->setObjectName("stageCountdownSeconds"); seconds->setRange(1,86400); seconds->setSuffix(" seconds"); root->addWidget(seconds);
    auto *remaining = new QLabel(this); remaining->setObjectName("stageCountdownRemaining"); root->addWidget(remaining);
    auto *timers = new QHBoxLayout; root->addLayout(timers);
    const auto selected = [outputs, stage]() -> DisplayWindow & { return outputs->output(OutputId(stage->currentData().toInt())); };
    const auto update = [selected, remaining] {
        const auto runtime = selected().stageRuntime(); StageElement element; element.kind = StageElement::Kind::Countdown;
        remaining->setText(stageElementText(element, {}, runtime) + (runtime.running ? " · running" : " · paused/stopped"));
    };
    const auto load = [selected, message, seconds, update] { message->setPlainText(selected().stageRuntime().message); seconds->setValue(selected().countdownSeconds()); update(); };
    connect(stage, QOverload<int>::of(&QComboBox::currentIndexChanged), this, load);
    auto *messageStatus = new QLabel(this); messageStatus->setWordWrap(true); root->addWidget(messageStatus);
    connect(send, &QPushButton::clicked, this, [selected, message, messageStatus] {
        if (!selected().setStageMessage(message->toPlainText())) messageStatus->setText("Use a message of at most 4000 characters.");
        else messageStatus->setText("Message sent to the selected stage screen.");
    });
    connect(clear, &QPushButton::clicked, this, [selected, message] { selected().setStageMessage({}); message->clear(); });
    for (const auto &label : {QStringLiteral("Start"), QStringLiteral("Pause"), QStringLiteral("Resume"), QStringLiteral("Reset")}) {
        auto *button = new QPushButton(label, this); button->setObjectName("stageCountdown" + label); timers->addWidget(button);
        connect(button, &QPushButton::clicked, this, [selected, seconds, update, label] {
            if (label == "Start") { selected().setCountdownSeconds(seconds->value()); selected().startCountdown(); }
            if (label == "Pause") selected().pauseCountdown();
            if (label == "Resume") selected().resumeCountdown();
            if (label == "Reset") selected().setCountdownSeconds(seconds->value());
            update();
        });
    }
    auto *hint = new QLabel("Messages and timers are independent for each stage screen. Add Stage Message and Countdown elements to its assigned custom layout to show them. These controls never alter audience output or the live cue.", this);
    hint->setWordWrap(true); root->addWidget(hint);
    auto *tick = new QTimer(this); connect(tick, &QTimer::timeout, this, update); tick->start(200);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); load();
}
