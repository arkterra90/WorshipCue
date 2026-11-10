#include "MediaImportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

MediaImportDialog::MediaImportDialog(const QStringList &filenames, QWidget *parent, bool namingOnly)
    : QDialog(parent), filenames_(filenames), namingOnly_(namingOnly) {
    setWindowTitle(namingOnly ? QStringLiteral("Rename Media Slides") : QStringLiteral("Import Pictures and Video"));
    resize(760, 620);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(namingOnly
        ? QStringLiteral("Rename this presentation’s slide labels together. Stored media locations stay stable so live presentations keep working.")
        : QStringLiteral("Create a presentation from your selected files. WorshipCue copies them into its media library using the names below; your original files stay unchanged."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *titleForm = new QFormLayout;
    title_ = new QLineEdit(QStringLiteral("Picture Presentation"), this);
    title_->setObjectName(QStringLiteral("presentationTitle"));
    auto *titleLabel = new QLabel(QStringLiteral("Presentation title"), this);
    titleForm->addRow(titleLabel, title_);
    layout->addLayout(titleForm);
    title_->setVisible(!namingOnly);
    titleLabel->setVisible(!namingOnly);
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("renameMode"));
    mode_->addItem(QStringLiteral("Name and Number"), int(BatchRenameOptions::Mode::Format));
    mode_->addItem(QStringLiteral("Replace Text"), int(BatchRenameOptions::Mode::Replace));
    mode_->addItem(QStringLiteral("Add Text"), int(BatchRenameOptions::Mode::Add));
    mode_->addItem(QStringLiteral("Keep Current Names"), int(BatchRenameOptions::Mode::Keep));
    auto *modeForm = new QFormLayout;
    modeForm->addRow(QStringLiteral("Rename format"), mode_);
    layout->addLayout(modeForm);
    options_ = new QStackedWidget(this);
    auto *formatPage = new QWidget(options_);
    auto *formatForm = new QFormLayout(formatPage);
    formatForm->setContentsMargins(0, 0, 0, 0);
    text_ = new QLineEdit(title_->text(), formatPage);
    text_->setObjectName(QStringLiteral("renameText"));
    lastAutoName_ = title_->text();
    start_ = new QSpinBox(formatPage);
    start_->setObjectName(QStringLiteral("renameStart"));
    start_->setRange(0, 1000000000);
    start_->setValue(1);
    padding_ = new QSpinBox(formatPage);
    padding_->setObjectName(QStringLiteral("renamePadding"));
    padding_->setRange(1, 10);
    padding_->setValue(2);
    formatForm->addRow(QStringLiteral("Name"), text_);
    formatForm->addRow(QStringLiteral("Start numbers at"), start_);
    formatForm->addRow(QStringLiteral("Minimum number digits"), padding_);
    options_->addWidget(formatPage);
    auto *replacePage = new QWidget(options_);
    auto *replaceForm = new QFormLayout(replacePage);
    replaceForm->setContentsMargins(0, 0, 0, 0);
    find_ = new QLineEdit(replacePage);
    find_->setObjectName(QStringLiteral("renameFind"));
    replacement_ = new QLineEdit(replacePage);
    replacement_->setObjectName(QStringLiteral("renameReplacement"));
    replaceForm->addRow(QStringLiteral("Find"), find_);
    replaceForm->addRow(QStringLiteral("Replace with"), replacement_);
    options_->addWidget(replacePage);
    auto *addPage = new QWidget(options_);
    auto *addForm = new QFormLayout(addPage);
    addForm->setContentsMargins(0, 0, 0, 0);
    add_ = new QLineEdit(addPage);
    add_->setObjectName(QStringLiteral("renameAddText"));
    position_ = new QComboBox(addPage);
    position_->setObjectName(QStringLiteral("renamePosition"));
    position_->addItems({QStringLiteral("Before name"), QStringLiteral("After name")});
    addForm->addRow(QStringLiteral("Add text"), add_);
    addForm->addRow(QStringLiteral("Position"), position_);
    options_->addWidget(addPage);
    auto *keepPage = new QWidget(options_);
    auto *keepLayout = new QVBoxLayout(keepPage);
    keepLayout->setContentsMargins(0, 0, 0, 0);
    keepLayout->addWidget(new QLabel(QStringLiteral("Use the current filenames. Names must be unique within this presentation."), keepPage));
    options_->addWidget(keepPage);
    layout->addWidget(options_);
    auto *previewLabel = new QLabel(QStringLiteral("Review names before applying — extensions are preserved"), this);
    layout->addWidget(previewLabel);
    preview_ = new QTableWidget(filenames.size(), 2, this);
    preview_->setObjectName(QStringLiteral("mediaRenamePreview"));
    preview_->setHorizontalHeaderLabels({QStringLiteral("Current name"), QStringLiteral("New name")});
    preview_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    preview_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_->setSelectionBehavior(QAbstractItemView::SelectRows);
    preview_->setAlternatingRowColors(true);
    preview_->setSortingEnabled(false);
    preview_->verticalHeader()->setVisible(true);
    for (int row = 0; row < filenames.size(); ++row) {
        preview_->setItem(row, 0, new QTableWidgetItem(QFileInfo(filenames.at(row)).fileName()));
        preview_->setItem(row, 1, new QTableWidgetItem);
    }
    layout->addWidget(preview_, 1);
    loop_ = new QCheckBox(QStringLiteral("Loop videos in this presentation"), this);
    loop_->setObjectName(QStringLiteral("loopVideos"));
    loop_->setVisible(!namingOnly);
    layout->addWidget(loop_);
    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("renameError"));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #edb986;"));
    layout->addWidget(error_);
    actions_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    actions_->setObjectName(QStringLiteral("mediaImportActions"));
    actions_->button(QDialogButtonBox::Ok)->setText(namingOnly ? QStringLiteral("Rename Slides") : QStringLiteral("Import Presentation"));
    layout->addWidget(actions_);
    connect(actions_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(actions_, &QDialogButtonBox::accepted, this, [this] {
        updatePreview();
        if (actions_->button(QDialogButtonBox::Ok)->isEnabled()) accept();
    });
    connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        options_->setCurrentIndex(index);
        updatePreview();
    });
    for (auto *edit : {text_, find_, replacement_, add_})
        connect(edit, &QLineEdit::textChanged, this, &MediaImportDialog::updatePreview);
    connect(start_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MediaImportDialog::updatePreview);
    connect(padding_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MediaImportDialog::updatePreview);
    connect(position_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MediaImportDialog::updatePreview);
    connect(title_, &QLineEdit::textChanged, this, [this](const QString &title) {
        if (text_->text() == lastAutoName_) text_->setText(title);
        lastAutoName_ = title;
        updatePreview();
    });
    updatePreview();
}

QString MediaImportDialog::title() const { return title_->text().trimmed(); }
bool MediaImportDialog::loopVideos() const { return loop_->isChecked(); }
void MediaImportDialog::setPresentationTitle(const QString &title) { title_->setText(title); }

void MediaImportDialog::updatePreview() {
    BatchRenameOptions options;
    options.mode = BatchRenameOptions::Mode(mode_->currentData().toInt());
    options.text = options.mode == BatchRenameOptions::Mode::Add ? add_->text() : text_->text();
    options.find = find_->text();
    options.replacement = replacement_->text();
    options.prefix = position_->currentIndex() == 0;
    options.start = start_->value();
    options.padding = padding_->value();
    QString error;
    plannedNames_ = BatchRenamer::names(filenames_, options, &error);
    for (int row = 0; row < filenames_.size(); ++row)
        preview_->item(row, 1)->setText(row < plannedNames_.size() ? plannedNames_.at(row) : QStringLiteral("—"));
    if (!namingOnly_ && title().isEmpty()) error = QStringLiteral("Enter a presentation title.");
    error_->setText(error);
    actions_->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty() && !plannedNames_.isEmpty());
}
