#include "PresentationLookDialog.h"
#include "BackgroundLibraryDialog.h"
#include "RichTextSupport.h"
#include "SlideRenderer.h"
#include "../core/BuiltInBackgrounds.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontComboBox>
#include <QFontInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>

class LookPreviewWidget : public QWidget {
public:
    LivePresentation state;
    QPixmap background;
    QPixmap media;
    explicit LookPreviewWidget(QWidget *parent) : QWidget(parent) { setMinimumSize(320, 180); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        SlideRenderer::paintAudience(painter, rect(), state, background, media);
    }
};

namespace {
class LookEditCommand : public QUndoCommand {
public:
    LookEditCommand(std::function<void()> undo, std::function<void()> redo)
        : undo_(std::move(undo)), redo_(std::move(redo)) { setText(QStringLiteral("Apply Look / Reflow")); }
    void undo() override { undo_(); }
    void redo() override { redo_(); }
private:
    std::function<void()> undo_, redo_;
};
QSpinBox *spin(QWidget *parent, const QString &name, int maximum) {
    auto *control = new QSpinBox(parent);
    control->setObjectName(name);
    control->setRange(0, maximum);
    control->setSuffix(QStringLiteral(" px"));
    control->setKeyboardTracking(false);
    return control;
}
QString absoluteBackground(const QString &path, const LibraryRepository *library) {
    const QString resource = BuiltInBackgrounds::resourcePath(path);
    return resource.isEmpty() ? QDir(library->databaseDirectory()).filePath(path) : resource;
}
}

PresentationLookDialog::PresentationLookDialog(LibraryRepository *library, const Song &song, int sourceSlide, QWidget *parent)
    : QDialog(parent), library_(library), draft_(song), sourceStructure_(song.structure), undo_(this) {
    setObjectName(QStringLiteral("presentationLookDialog"));
    setWindowTitle(QStringLiteral("Looks / Reflow — %1").arg(song.title));
    resize(1120, 800);
    for (int i = 0; i < song.slides.size(); ++i) sourceIndices_.append(i);
    draft_.appearance.slideLookIds.resize(song.slides.size());
    draft_.appearance.richTextOverrides.resize(song.slides.size());
    draft_.appearance.fitText.resize(song.slides.size());
    draft_.looks.resize(song.slides.size());
    auto *layout = new QVBoxLayout(this);
    auto *savedRow = new QHBoxLayout;
    savedRow->addWidget(new QLabel(QStringLiteral("Saved Look"), this));
    looks_ = new QComboBox(this);
    looks_->setObjectName(QStringLiteral("savedLooks"));
    savedRow->addWidget(looks_, 1);
    auto *capture = new QPushButton(QStringLiteral("Capture Slide Formatting"), this);
    capture->setObjectName(QStringLiteral("captureSlideLook"));
    savedRow->addWidget(capture);
    auto *save = new QPushButton(QStringLiteral("Save as Look…"), this);
    save->setObjectName(QStringLiteral("savePresentationLook"));
    savedRow->addWidget(save);
    layout->addLayout(savedRow);
    auto *split = new QSplitter(this);
    layout->addWidget(split, 1);
    pages_ = new QListWidget(split);
    pages_->setObjectName(QStringLiteral("lookSourceSlides"));
    pages_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pages_->setMinimumWidth(180);
    auto *right = new QWidget(split);
    auto *rightLayout = new QVBoxLayout(right);
    preview_ = new LookPreviewWidget(right);
    preview_->setObjectName(QStringLiteral("lookPreview"));
    rightLayout->addWidget(preview_, 1);
    auto *tabs = new QTabWidget(right);
    auto *text = new QWidget(tabs);
    auto *textForm = new QFormLayout(text);
    family_ = new QFontComboBox(text);
    family_->setObjectName(QStringLiteral("lookFontFamily"));
    textForm->addRow(QStringLiteral("Font"), family_);
    auto *fontRow = new QHBoxLayout;
    size_ = spin(text, QStringLiteral("lookFontSize"), 512);
    size_->setMinimum(8);
    fontRow->addWidget(size_);
    weight_ = new QComboBox(text);
    weight_->setObjectName(QStringLiteral("lookFontWeight"));
    const QStringList weights{"Thin", "Extra Light", "Light", "Regular", "Medium", "Semi Bold", "Bold", "Extra Bold", "Black"};
    for (int i = 0; i < weights.size(); ++i) weight_->addItem(weights.at(i), (i + 1) * 100);
    fontRow->addWidget(weight_);
    italic_ = new QCheckBox(QStringLiteral("Italic"), text);
    italic_->setObjectName(QStringLiteral("lookItalic"));
    underline_ = new QCheckBox(QStringLiteral("Underline"), text);
    underline_->setObjectName(QStringLiteral("lookUnderline"));
    strikeOut_ = new QCheckBox(QStringLiteral("Strike out"), text);
    fontRow->addWidget(italic_); fontRow->addWidget(underline_); fontRow->addWidget(strikeOut_);
    textForm->addRow(QStringLiteral("Type"), fontRow);
    textColorButton_ = new QPushButton(QStringLiteral("Choose…"), text);
    textColorButton_->setObjectName(QStringLiteral("lookTextColor"));
    textForm->addRow(QStringLiteral("Text color"), textColorButton_);
    horizontal_ = new QComboBox(text);
    horizontal_->setObjectName(QStringLiteral("lookHorizontalAlignment"));
    horizontal_->addItems({QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    vertical_ = new QComboBox(text);
    vertical_->setObjectName(QStringLiteral("lookVerticalAlignment"));
    vertical_->addItems({QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")});
    auto *alignment = new QHBoxLayout;
    alignment->addWidget(horizontal_); alignment->addWidget(vertical_);
    textForm->addRow(QStringLiteral("Position"), alignment);
    preserveRich_ = new QCheckBox(QStringLiteral("Keep slide text formatting overrides"), text);
    preserveRich_->setObjectName(QStringLiteral("lookPreserveRichText"));
    textForm->addRow(preserveRich_);
    tabs->addTab(text, QStringLiteral("Text"));
    auto *geometry = new QWidget(tabs);
    auto *geometryForm = new QFormLayout(geometry);
    left_ = spin(geometry, QStringLiteral("lookMarginLeft"), 1920);
    right_ = spin(geometry, QStringLiteral("lookMarginRight"), 1920);
    top_ = spin(geometry, QStringLiteral("lookMarginTop"), 1080);
    bottom_ = spin(geometry, QStringLiteral("lookMarginBottom"), 1080);
    padding_ = spin(geometry, QStringLiteral("lookPadding"), 1080);
    auto *margins = new QHBoxLayout;
    for (const auto &pair : {qMakePair(QStringLiteral("Left"), left_), qMakePair(QStringLiteral("Right"), right_),
                            qMakePair(QStringLiteral("Top"), top_), qMakePair(QStringLiteral("Bottom"), bottom_)}) {
        margins->addWidget(new QLabel(pair.first, geometry)); margins->addWidget(pair.second);
    }
    geometryForm->addRow(QStringLiteral("Margins"), margins);
    geometryForm->addRow(QStringLiteral("Padding"), padding_);
    background_ = new QComboBox(geometry);
    background_->setObjectName(QStringLiteral("lookBackgroundMode"));
    background_->addItems({QStringLiteral("Presentation background"), QStringLiteral("Solid color"), QStringLiteral("Background image")});
    geometryForm->addRow(QStringLiteral("Background"), background_);
    backgroundColorButton_ = new QPushButton(QStringLiteral("Choose color…"), geometry);
    backgroundColorButton_->setObjectName(QStringLiteral("lookBackgroundColor"));
    auto *chooseImage = new QPushButton(QStringLiteral("Choose image…"), geometry);
    chooseImage->setObjectName(QStringLiteral("lookBackgroundImage"));
    auto *backgroundActions = new QHBoxLayout;
    backgroundActions->addWidget(backgroundColorButton_); backgroundActions->addWidget(chooseImage);
    geometryForm->addRow(backgroundActions);
    backgroundName_ = new QLabel(geometry);
    backgroundName_->setWordWrap(true);
    geometryForm->addRow(backgroundName_);
    tabs->addTab(geometry, QStringLiteral("Layout / Background"));
    rightLayout->addWidget(tabs);
    split->setSizes({220, 880});
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("lookStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *applyRow = new QHBoxLayout;
    auto *selected = new QPushButton(QStringLiteral("Apply to Selected Slides"), this);
    selected->setObjectName(QStringLiteral("applyLookToSelected"));
    auto *all = new QPushButton(QStringLiteral("Apply to Presentation"), this);
    all->setObjectName(QStringLiteral("applyLookToPresentation"));
    applyRow->addWidget(selected); applyRow->addWidget(all);
    auto *undo = new QPushButton(QStringLiteral("Undo Look / Split"), this);
    undo->setObjectName(QStringLiteral("undoLookChange"));
    auto *redo = new QPushButton(QStringLiteral("Redo Look / Split"), this);
    redo->setObjectName(QStringLiteral("redoLookChange"));
    applyRow->addWidget(undo); applyRow->addWidget(redo);
    undo->setEnabled(false); redo->setEnabled(false);
    connect(undo, &QPushButton::clicked, &undo_, &QUndoStack::undo);
    connect(redo, &QPushButton::clicked, &undo_, &QUndoStack::redo);
    connect(&undo_, &QUndoStack::canUndoChanged, undo, &QPushButton::setEnabled);
    connect(&undo_, &QUndoStack::canRedoChanged, redo, &QPushButton::setEnabled);
    layout->addLayout(applyRow);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    actions->setObjectName(QStringLiteral("lookDialogActions"));
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(selected, &QPushButton::clicked, this, [this] { applyFromUi(false); });
    connect(all, &QPushButton::clicked, this, [this] { applyFromUi(true); });
    connect(save, &QPushButton::clicked, this, &PresentationLookDialog::saveLook);
    connect(capture, &QPushButton::clicked, this, [this] {
        const int row = pages_->currentRow();
        if (row < 0) return;
        looks_->setCurrentIndex(0);
        setSettings(RichTextSupport::captureLook(draft_.slides.at(row), draft_.richText.value(row), draft_.looks.value(row)));
    });
    connect(looks_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        const auto saved = library_->look(looks_->currentData().toInt());
        if (saved.id) setSettings(saved.settings);
    });
    connect(pages_, &QListWidget::currentRowChanged, this, &PresentationLookDialog::refreshPreview);
    for (auto *control : {size_, left_, right_, top_, bottom_, padding_})
        connect(control, QOverload<int>::of(&QSpinBox::valueChanged), this, &PresentationLookDialog::refreshPreview);
    for (auto *control : {weight_, horizontal_, vertical_, background_})
        connect(control, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PresentationLookDialog::refreshPreview);
    for (auto *control : {italic_, underline_, strikeOut_, preserveRich_})
        connect(control, &QCheckBox::toggled, this, &PresentationLookDialog::refreshPreview);
    connect(family_, &QFontComboBox::currentFontChanged, this, [this] { if (!loading_) familyEdited_ = true; refreshPreview(); });
    connect(textColorButton_, &QPushButton::clicked, this, [this] {
        const auto color = QColorDialog::getColor(QColor(textColor_), this, QStringLiteral("Look text color"), QColorDialog::ShowAlphaChannel);
        if (color.isValid()) { textColor_ = color.name(QColor::HexArgb); refreshPreview(); }
    });
    connect(backgroundColorButton_, &QPushButton::clicked, this, [this] {
        const auto color = QColorDialog::getColor(QColor(backgroundColor_), this, QStringLiteral("Look background color"));
        if (color.isValid()) { backgroundColor_ = color.name(); refreshPreview(); }
    });
    connect(chooseImage, &QPushButton::clicked, this, [this] {
        BackgroundLibraryDialog chooser(library_->databaseDirectory(), backgroundPath_, this);
        connect(&chooser, &BackgroundLibraryDialog::fileImported, this, [this](const QString &path) { QString error; library_->recordManagedImport(path, &error); });
        if (chooser.exec() != QDialog::Accepted) return;
        backgroundPath_ = chooser.selectedPath();
        background_->setCurrentIndex(int(backgroundPath_.isEmpty() ? PresentationLook::Background::Presentation : PresentationLook::Background::Image));
        refreshPreview();
    });
    reloadLooks();
    refreshPages(qBound(0, sourceSlide, int(song.slides.size()) - 1));
    const auto look = song.looks.value(pages_->currentRow());
    if (look.id) { looks_->setCurrentIndex(looks_->findData(look.id)); setSettings(look.settings); }
    else setSettings(RichTextSupport::captureLook(song.slides.value(pages_->currentRow()), song.richText.value(pages_->currentRow())));
}

void PresentationLookDialog::reloadLooks(int selected) {
    const QSignalBlocker block(looks_);
    looks_->clear();
    looks_->addItem(QStringLiteral("Custom / captured formatting"), 0);
    const auto saved = library_->looks();
    for (const auto &look : saved) looks_->addItem(look.name, look.id);
    looks_->setCurrentIndex(qMax(0, looks_->findData(selected)));
}

PresentationLook PresentationLookDialog::settings() const {
    PresentationLook look;
    look.fontFamily = familyEdited_ ? family_->currentFont().family() : loadedFamily_;
    look.fontSize = size_->value(); look.fontWeight = weight_->currentData().toInt();
    look.italic = italic_->isChecked(); look.underline = underline_->isChecked(); look.strikeOut = strikeOut_->isChecked();
    look.textColor = textColor_; look.horizontalAlignment = horizontal_->currentIndex(); look.verticalAlignment = vertical_->currentIndex();
    look.marginLeft = left_->value(); look.marginRight = right_->value(); look.marginTop = top_->value(); look.marginBottom = bottom_->value();
    look.padding = padding_->value(); look.background = PresentationLook::Background(background_->currentIndex());
    look.backgroundColor = backgroundColor_; look.backgroundPath = backgroundPath_;
    return look;
}

void PresentationLookDialog::setSettings(const PresentationLook &look) {
    loading_ = true;
    loadedFamily_ = look.fontFamily; familyEdited_ = false;
    QFont font = RichTextSupport::defaultFont();
    if (!look.fontFamily.isEmpty()) font.setFamily(look.fontFamily);
    family_->setCurrentFont(font);
    int familyIndex = family_->findText(font.family(), Qt::MatchFixedString);
    if (familyIndex < 0) familyIndex = family_->findText(QFontInfo(font).family(), Qt::MatchFixedString);
    if (familyIndex >= 0) family_->setCurrentIndex(familyIndex);
    else family_->setEditText(look.fontFamily.isEmpty() || look.fontFamily.startsWith(QLatin1Char('.'))
        ? QStringLiteral("System font") : look.fontFamily);
    size_->setValue(look.fontSize);
    int weight = weight_->findData(look.fontWeight);
    if (weight < 0) { weight_->addItem(QString::number(look.fontWeight), look.fontWeight); weight = weight_->count() - 1; }
    weight_->setCurrentIndex(weight);
    italic_->setChecked(look.italic); underline_->setChecked(look.underline); strikeOut_->setChecked(look.strikeOut);
    textColor_ = look.textColor; horizontal_->setCurrentIndex(look.horizontalAlignment); vertical_->setCurrentIndex(look.verticalAlignment);
    left_->setValue(look.marginLeft); right_->setValue(look.marginRight); top_->setValue(look.marginTop); bottom_->setValue(look.marginBottom);
    padding_->setValue(look.padding); background_->setCurrentIndex(int(look.background));
    backgroundColor_ = look.backgroundColor; backgroundPath_ = look.backgroundPath;
    loading_ = false;
    refreshPreview();
}

QPixmap PresentationLookDialog::previewBackground(const SlideLook &look) const {
    const auto path = look.id > 0 && look.settings.background == PresentationLook::Background::Image
        ? look.settings.backgroundPath : draft_.backgroundPath;
    return path.isEmpty() ? QPixmap{} : SlideRenderer::backgroundImage(absoluteBackground(path, library_));
}

void PresentationLookDialog::refreshPages(int selected) {
    const QSignalBlocker block(pages_);
    pages_->clear();
    for (int i = 0; i < draft_.slides.size(); ++i) {
        const auto section = draft_.structure.sectionName(i);
        auto *item = new QListWidgetItem(QStringLiteral("Slide %1%2").arg(i + 1).arg(section.isEmpty() ? QString() : QStringLiteral(" • ") + section), pages_);
        item->setToolTip(draft_.slides.at(i));
    }
    pages_->setCurrentRow(qBound(0, selected, pages_->count() - 1));
    refreshPreview();
}

void PresentationLookDialog::refreshPreview() {
    if (loading_ || pages_->currentRow() < 0) return;
    const auto look = settings();
    QString error;
    if (!look.validate(&error)) { status_->setText(error); return; }
    const int row = pages_->currentRow();
    SlideLook style{1, look, preserveRich_->isChecked(), false, {}};
    preview_->state = {};
    preview_->state.slides = {draft_.slides.at(row)};
    preview_->state.richText = {draft_.richText.value(row)};
    preview_->state.looks = {style}; preview_->state.index = 0;
    preview_->background = previewBackground(style);
    auto media = draft_.media.value(row);
    if (media.kind != MediaKind::None) media.path = QDir(library_->databaseDirectory()).filePath(media.path);
    preview_->state.media = {media};
    preview_->media = media.kind == MediaKind::Image ? SlideRenderer::backgroundImage(media.path) : QPixmap{};
    preview_->update();
    backgroundName_->setText(backgroundPath_);
    textColorButton_->setText(textColor_); backgroundColorButton_->setText(backgroundColor_);
    status_->setText(RichTextSupport::overflows(draft_.slides.at(row), draft_.richText.value(row), style)
        ? QStringLiteral("Text exceeds this Look's text box. The preview uses safety fitting. Applying requires a reviewed split or explicit Fit Text.")
        : QStringLiteral("The preview shows the current Look controls. Apply a Look to update the draft, then Save to commit the presentation."));
}

void PresentationLookDialog::saveLook() {
    QString error;
    if (!settings().validate(&error)) { status_->setText(error); return; }
    bool accepted = false;
    const auto name = QInputDialog::getText(this, QStringLiteral("Save Look"), QStringLiteral("Look name"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    const int id = library_->createLook(name, settings(), &error);
    if (!id) { status_->setText(error); return; }
    reloadLooks(id);
}

int PresentationLookDialog::selectedLook() {
    const int id = looks_->currentData().toInt();
    const auto saved = library_->look(id);
    if (saved.id && saved.settings.toJson() == settings().toJson()) return id;
    saveLook();
    const auto result = library_->look(looks_->currentData().toInt());
    return result.id && result.settings.toJson() == settings().toJson() ? result.id : 0;
}

bool PresentationLookDialog::planLook(int id, const QVector<int> &pages, bool all, OverflowHandling handling, State *state, QString *error) const {
    if (error) error->clear();
    const auto saved = library_->look(id);
    if (!saved.id) { if (error) *error = library_->lastError().isEmpty() ? QStringLiteral("Choose a saved Look.") : library_->lastError(); return false; }
    QSet<int> targets(pages.cbegin(), pages.cend());
    if (all) for (int i = 0; i < draft_.slides.size(); ++i) targets.insert(i);
    if (targets.isEmpty()) { if (error) *error = QStringLiteral("Select at least one source slide."); return false; }
    for (int index : targets) if (index < 0 || index >= draft_.slides.size()) { if (error) *error = QStringLiteral("A selected source slide is invalid."); return false; }
    state->song = draft_; state->sources.clear(); state->selected = qMax(0, pages_->currentRow());
    auto &next = state->song;
    next.slides.clear(); next.richText.clear(); next.media.clear(); next.looks.clear();
    next.appearance.slideLookIds.clear(); next.appearance.richTextOverrides.clear(); next.appearance.fitText.clear();
    if (all) next.appearance.lookId = id;
    for (int i = 0; i < draft_.slides.size(); ++i) {
        auto style = draft_.looks.value(i);
        int slideLook = draft_.appearance.slideLookIds.value(i);
        if (targets.contains(i)) { style = {id, saved.settings, preserveRich_->isChecked(), false, {}}; slideLook = all ? 0 : id; }
        QVector<SplitTextPage> fragments{{draft_.slides.at(i), draft_.richText.value(i)}};
        if (targets.contains(i) && RichTextSupport::overflows(fragments.first().text, fragments.first().html, style)) {
            if (handling == OverflowHandling::Cancel) { if (error) *error = QStringLiteral("Text overflow detected. Review automatic splitting or choose Fit Text."); return false; }
            if (handling == OverflowHandling::Fit) style.fitText = true;
            else {
                const auto split = RichTextSupport::split(fragments.first().text, fragments.first().html, style);
                if (!split.error.isEmpty()) { if (error) *error = split.error; return false; }
                fragments = split.pages;
            }
        }
        for (const auto &fragment : fragments) {
            next.slides.append(fragment.text); next.richText.append(fragment.html); next.media.append(draft_.media.value(i)); next.looks.append(style);
            next.appearance.slideLookIds.append(slideLook); next.appearance.richTextOverrides.append(style.richTextOverrides); next.appearance.fitText.append(style.fitText);
            state->sources.append(sourceIndices_.at(i));
        }
        if (next.slides.size() > 10000) { if (error) *error = QStringLiteral("Reflow exceeds 10000 source slides."); return false; }
    }
    QVector<int> counts;
    for (int source : state->sources) {
        if (counts.size() <= source) counts.resize(source + 1);
        ++counts[source];
    }
    next.structure = sourceStructure_;
    int first = 0;
    for (auto &section : next.structure.sections) {
        const int originalCount = section.slideCount;
        section.reflow(counts.mid(first, originalCount));
        first += originalCount;
    }
    return next.structure.validate(int(next.slides.size()), error);
}

void PresentationLookDialog::restoreState(const State &state) {
    draft_ = state.song; sourceIndices_ = state.sources;
    refreshPages(state.selected);
    const auto style = draft_.looks.value(pages_->currentRow());
    { const QSignalBlocker block(looks_); looks_->setCurrentIndex(qMax(0, looks_->findData(style.id))); }
    preserveRich_->setChecked(style.richTextOverrides);
    setSettings(style.id > 0 ? style.settings
        : RichTextSupport::captureLook(draft_.slides.value(pages_->currentRow()), draft_.richText.value(pages_->currentRow())));
}

void PresentationLookDialog::commitState(const State &state) {
    const State previous{draft_, sourceIndices_, qMax(0, pages_->currentRow())};
    undo_.push(new LookEditCommand([this, previous] { restoreState(previous); }, [this, state] { restoreState(state); }));
}

bool PresentationLookDialog::applyLook(int id, const QVector<int> &pages, bool all, OverflowHandling handling, QString *error) {
    State state;
    if (!planLook(id, pages, all, handling, &state, error)) return false;
    commitState(state);
    return true;
}

bool PresentationLookDialog::reviewSplit(const State &state) {
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("lookSplitPreview"));
    dialog.setWindowTitle(QStringLiteral("Review Split Slides"));
    dialog.resize(900, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral("%1 source slides become %2. Every source character is retained. Repeated song sections use these shared pages. Media references are reused on split pages.")
        .arg(draft_.slides.size()).arg(state.song.slides.size()), &dialog);
    info->setWordWrap(true); layout->addWidget(info);
    auto *split = new QSplitter(&dialog);
    auto *list = new QListWidget(split);
    list->setObjectName(QStringLiteral("splitPreviewSlides"));
    auto *preview = new LookPreviewWidget(split);
    preview->setObjectName(QStringLiteral("splitSlidePreview"));
    for (int i = 0; i < state.song.slides.size(); ++i) {
        auto *item = new QListWidgetItem(QStringLiteral("%1 • %2").arg(i + 1).arg(state.song.structure.sectionName(i)), list);
        item->setToolTip(state.song.slides.at(i));
    }
    connect(list, &QListWidget::currentRowChanged, &dialog, [&, preview](int row) {
        if (row < 0) return;
        preview->state = {};
        preview->state.slides = {state.song.slides.at(row)}; preview->state.richText = {state.song.richText.at(row)};
        preview->state.looks = {state.song.looks.at(row)}; preview->state.index = 0;
        preview->background = previewBackground(state.song.looks.at(row));
        auto media = state.song.media.value(row);
        if (media.kind != MediaKind::None) media.path = QDir(library_->databaseDirectory()).filePath(media.path);
        preview->state.media = {media};
        preview->media = media.kind == MediaKind::Image ? SlideRenderer::backgroundImage(media.path) : QPixmap{};
        preview->update();
    });
    split->setSizes({220, 680}); layout->addWidget(split, 1); list->setCurrentRow(0);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    actions->setObjectName(QStringLiteral("splitPreviewActions"));
    actions->button(QDialogButtonBox::Apply)->setText(QStringLiteral("Apply Split"));
    connect(actions->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(actions);
    return dialog.exec() == QDialog::Accepted;
}

void PresentationLookDialog::applyFromUi(bool all) {
    const int id = selectedLook();
    if (!id) return;
    QVector<int> selected;
    for (const auto *item : pages_->selectedItems()) selected.append(pages_->row(item));
    State state;
    QString error;
    if (planLook(id, selected, all, OverflowHandling::Cancel, &state, &error)) { commitState(state); return; }
    if (!error.startsWith(QStringLiteral("Text overflow"))) { status_->setText(error); return; }
    QMessageBox warning(QMessageBox::Warning, QStringLiteral("Text Overflow"),
        QStringLiteral("This Look exceeds one or more text boxes. Review split pages or explicitly fit text before applying."), QMessageBox::Cancel, this);
    warning.setObjectName(QStringLiteral("lookOverflowWarning"));
    auto *split = warning.addButton(QStringLiteral("Preview Automatic Split…"), QMessageBox::ActionRole);
    split->setObjectName(QStringLiteral("previewLookSplit"));
    auto *fit = warning.addButton(QStringLiteral("Fit Text"), QMessageBox::AcceptRole);
    fit->setObjectName(QStringLiteral("fitLookText"));
    warning.exec();
    if (warning.clickedButton() != split && warning.clickedButton() != fit) return;
    const auto handling = warning.clickedButton() == split ? OverflowHandling::Split : OverflowHandling::Fit;
    if (!planLook(id, selected, all, handling, &state, &error)) { status_->setText(error); return; }
    if (handling == OverflowHandling::Split && !reviewSplit(state)) return;
    commitState(state);
}
