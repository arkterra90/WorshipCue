#include "SongStructureDialog.h"
#include "ReorderListWidget.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QPushButton *action(const QString &label, const QString &name, QBoxLayout *layout, QWidget *parent) {
    auto *button = new QPushButton(label, parent);
    button->setObjectName(name);
    layout->addWidget(button);
    return button;
}
}

SongStructureDialog::SongStructureDialog(const Song &song, QWidget *parent) : QDialog(parent), original_(song) {
    setObjectName(QStringLiteral("songStructureEditor"));
    setWindowTitle(song.id ? QStringLiteral("Song Sections / Arrangements") : QStringLiteral("New Song"));
    resize(1000, 720);
    if (original_.slides.isEmpty()) {
        original_.slides = {QStringLiteral("")};
        original_.richText = {QStringLiteral("")};
    }
    if (original_.richText.isEmpty()) original_.richText.resize(original_.slides.size());
    if (original_.structure.isEmpty()) original_.structure = SongStructure::fromSlides(int(original_.slides.size()));
    if (!song.id && original_.structure.sections.size() == 1) original_.structure.sections[0].name = QStringLiteral("Verse 1");
    structure_ = original_.structure;
    int start = 0;
    for (const auto &section : structure_.sections) {
        QStringList ids;
        QVector<int> sources;
        for (int page = 0; page < section.slideCount; ++page) { ids.append(section.slideId(page)); sources.append(start + page); }
        sections_.append({section.id, section.name, original_.slides.mid(start, section.slideCount),
                          original_.richText.mid(start, section.slideCount), ids, sources});
        start += section.slideCount;
    }
    auto *layout = new QVBoxLayout(this);
    title_ = new QLineEdit(song.title, this);
    title_->setObjectName(QStringLiteral("structuredSongTitle"));
    title_->setMaxLength(256);
    title_->setPlaceholderText(QStringLiteral("Song title"));
    layout->addWidget(title_);
    auto *hint = new QLabel(QStringLiteral("A section contains one or more slides. Edit its slides once, then arrange or repeat the whole section. Changes appear in every occurrence."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *split = new QSplitter(this);
    layout->addWidget(split, 1);
    auto *sectionPanel = new QWidget(split);
    auto *sectionLayout = new QVBoxLayout(sectionPanel);
    sectionLayout->addWidget(new QLabel(QStringLiteral("Sections"), sectionPanel));
    sectionList_ = new ReorderListWidget(sectionPanel);
    sectionList_->setObjectName(QStringLiteral("songSections"));
    sectionList_->setDragDropMode(QAbstractItemView::InternalMove);
    sectionLayout->addWidget(sectionList_, 1);
    auto *sectionActions = new QHBoxLayout;
    sectionLayout->addLayout(sectionActions);
    connect(action(QStringLiteral("Add"), QStringLiteral("addSongSection"), sectionActions, sectionPanel), &QPushButton::clicked, this, &SongStructureDialog::addSection);
    connect(action(QStringLiteral("Remove"), QStringLiteral("removeSongSection"), sectionActions, sectionPanel), &QPushButton::clicked, this, &SongStructureDialog::removeSection);
    auto *editPanel = new QWidget(split);
    auto *editLayout = new QVBoxLayout(editPanel);
    editLayout->addWidget(new QLabel(QStringLiteral("Section name"), editPanel));
    sectionName_ = new QComboBox(editPanel);
    sectionName_->setObjectName(QStringLiteral("songSectionName"));
    sectionName_->setEditable(true);
    sectionName_->addItems({QStringLiteral("Verse 1"), QStringLiteral("Verse 2"), QStringLiteral("Verse 3"), QStringLiteral("Chorus"),
        QStringLiteral("Pre-Chorus"), QStringLiteral("Bridge"), QStringLiteral("Intro"), QStringLiteral("Outro"),
        QStringLiteral("Tag"), QStringLiteral("Refrain"), QStringLiteral("Instrumental")});
    sectionName_->lineEdit()->setMaxLength(100);
    editLayout->addWidget(sectionName_);
    slideList_ = new ReorderListWidget(editPanel);
    slideList_->setObjectName(QStringLiteral("songSectionSlides"));
    slideList_->setDragDropMode(QAbstractItemView::InternalMove);
    slideList_->setMinimumHeight(100);
    editLayout->addWidget(slideList_, 1);
    auto *slideActions = new QHBoxLayout;
    editLayout->addLayout(slideActions);
    connect(action(QStringLiteral("Add Slide"), QStringLiteral("addSectionSlide"), slideActions, editPanel), &QPushButton::clicked, this, [this] { addSlide(); });
    duplicateSlide_ = action(QStringLiteral("Duplicate"), QStringLiteral("duplicateSectionSlide"), slideActions, editPanel);
    connect(duplicateSlide_, &QPushButton::clicked, this, [this] { addSlide(true); });
    removeSlide_ = action(QStringLiteral("Remove"), QStringLiteral("removeSectionSlide"), slideActions, editPanel);
    connect(removeSlide_, &QPushButton::clicked, this, &SongStructureDialog::removeSlide);
    auto *slideOrder = new QHBoxLayout;
    editLayout->addLayout(slideOrder);
    connect(action(QStringLiteral("↑"), QStringLiteral("moveSectionSlideUp"), slideOrder, editPanel), &QPushButton::clicked, this, [this] { slideList_->moveItem(slideList_->currentRow(), slideList_->currentRow() - 1); });
    connect(action(QStringLiteral("↓"), QStringLiteral("moveSectionSlideDown"), slideOrder, editPanel), &QPushButton::clicked, this, [this] { slideList_->moveItem(slideList_->currentRow(), slideList_->currentRow() + 1); });
    splitSlide_ = action(QStringLiteral("Split at Cursor"), QStringLiteral("splitSectionSlide"), slideOrder, editPanel);
    connect(splitSlide_, &QPushButton::clicked, this, &SongStructureDialog::splitSlide);
    slideLabel_ = new QLabel(editPanel);
    slideLabel_->setObjectName(QStringLiteral("sectionSlideLabel"));
    editLayout->addWidget(slideLabel_);
    lyrics_ = new QPlainTextEdit(editPanel);
    lyrics_->setObjectName(QStringLiteral("songSectionLyrics"));
    lyrics_->setPlaceholderText(QStringLiteral("Lyrics for the selected slide"));
    editLayout->addWidget(lyrics_, 2);
    auto *splitShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Return), lyrics_);
    splitShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(splitShortcut, &QShortcut::activated, this, &SongStructureDialog::splitSlide);
    connect(slideList_, &QListWidget::currentRowChanged, this, &SongStructureDialog::loadSlide);
    connect(slideList_, &ReorderListWidget::orderChanged, this, [this] {
        const int sectionRow = sectionList_->currentRow();
        if (sectionRow < 0) return;
        auto &section = sections_[sectionRow];
        QStringList texts, rich, ids;
        QVector<int> sources;
        for (int row = 0; row < slideList_->count(); ++row) {
            const auto id = slideList_->item(row)->data(Qt::UserRole).toString();
            const int source = section.slideIds.indexOf(id);
            texts.append(section.slides.at(source)); rich.append(section.richText.at(source)); ids.append(id); sources.append(section.sources.at(source));
        }
        section.slides = texts; section.richText = rich; section.slideIds = ids; section.sources = sources;
        rebuildSlides(slideList_->currentRow());
        refreshSequence();
    });
    auto *sequencePanel = new QWidget(split);
    auto *sequenceLayout = new QVBoxLayout(sequencePanel);
    sequenceLayout->addWidget(new QLabel(QStringLiteral("Saved arrangement"), sequencePanel));
    arrangement_ = new QComboBox(sequencePanel);
    arrangement_->setObjectName(QStringLiteral("songArrangements"));
    sequenceLayout->addWidget(arrangement_);
    auto *arrangementActions = new QHBoxLayout;
    sequenceLayout->addLayout(arrangementActions);
    connect(action(QStringLiteral("New"), QStringLiteral("newSongArrangement"), arrangementActions, sequencePanel), &QPushButton::clicked, this, [this] { createArrangement(false); });
    connect(action(QStringLiteral("Copy"), QStringLiteral("copySongArrangement"), arrangementActions, sequencePanel), &QPushButton::clicked, this, [this] { createArrangement(true); });
    connect(action(QStringLiteral("Rename"), QStringLiteral("renameSongArrangement"), arrangementActions, sequencePanel), &QPushButton::clicked, this, &SongStructureDialog::renameArrangement);
    connect(action(QStringLiteral("Delete"), QStringLiteral("deleteSongArrangement"), arrangementActions, sequencePanel), &QPushButton::clicked, this, &SongStructureDialog::deleteArrangement);
    sequence_ = new ReorderListWidget(sequencePanel);
    sequence_->setObjectName(QStringLiteral("songArrangementSequence"));
    sequence_->setDragDropMode(QAbstractItemView::InternalMove);
    sequenceLayout->addWidget(sequence_, 1);
    auto *sequenceActions = new QHBoxLayout;
    sequenceLayout->addLayout(sequenceActions);
    connect(action(QStringLiteral("Add Section →"), QStringLiteral("appendSongSection"), sequenceActions, sequencePanel), &QPushButton::clicked, this, &SongStructureDialog::appendSection);
    connect(action(QStringLiteral("Remove"), QStringLiteral("removeSongOccurrence"), sequenceActions, sequencePanel), &QPushButton::clicked, this, &SongStructureDialog::removeOccurrence);
    preview_ = new QPlainTextEdit(sequencePanel);
    preview_->setObjectName(QStringLiteral("songSectionPreview"));
    preview_->setReadOnly(true);
    sequenceLayout->addWidget(preview_, 1);
    split->setSizes({200, 380, 400});
    validation_ = new QLabel(this);
    validation_->setWordWrap(true);
    layout->addWidget(validation_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("songStructureActions"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const auto song = draft();
        QString error;
        if (song.title.trimmed().isEmpty() || !song.structure.validate(int(song.slides.size()), &error)) {
            validation_->setText(error.isEmpty() ? QStringLiteral("Enter a song title.") : error);
            return;
        }
        accept();
    });
    connect(sectionList_, &QListWidget::currentRowChanged, this, &SongStructureDialog::loadSection);
    connect(sectionList_, &ReorderListWidget::orderChanged, this, [this] {
        QVector<SectionDraft> ordered;
        for (int i = 0; i < sectionList_->count(); ++i)
            for (const auto &section : sections_)
                if (section.id == sectionList_->item(i)->data(Qt::UserRole).toString()) ordered.append(section);
        sections_ = ordered;
        loadSection();
    });
    connect(sectionName_, &QComboBox::currentTextChanged, this, &SongStructureDialog::saveSection);
    connect(lyrics_, &QPlainTextEdit::textChanged, this, &SongStructureDialog::saveSection);
    connect(arrangement_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        structure_.activeArrangementId = arrangement_->currentData().toString();
        refreshSequence();
    });
    connect(sequence_, &ReorderListWidget::orderChanged, this, [this] {
        auto *arrangement = activeArrangement();
        if (!arrangement) return;
        arrangement->sectionIds.clear();
        for (int i = 0; i < sequence_->count(); ++i) arrangement->sectionIds.append(sequence_->item(i)->data(Qt::UserRole).toString());
        updatePreview();
    });
    connect(sequence_, &QListWidget::currentRowChanged, this, &SongStructureDialog::updatePreview);
    rebuildSections();
    rebuildArrangements();
}

SongArrangement *SongStructureDialog::activeArrangement() {
    for (auto &arrangement : structure_.arrangements)
        if (arrangement.id == structure_.activeArrangementId) return &arrangement;
    return nullptr;
}

Song SongStructureDialog::draft() const {
    Song song = original_;
    song.title = title_->text().trimmed();
    song.slides.clear();
    song.richText.clear();
    song.media.clear();
    song.appearance.slideLookIds.clear();
    song.appearance.richTextOverrides.clear();
    song.appearance.fitText.clear();
    song.looks.clear();
    song.structure = structure_;
    song.structure.sections.clear();
    for (const auto &section : sections_) {
        song.structure.sections.append({section.id, section.name.trimmed(), int(section.slides.size()), section.slideIds});
        song.slides.append(section.slides);
        song.richText.append(section.richText);
        for (int i = 0; i < section.slides.size(); ++i) {
            song.media.append(SlideMedia{});
            const int source = section.sources.at(i);
            song.appearance.slideLookIds.append(original_.appearance.slideLookIds.value(source));
            song.appearance.richTextOverrides.append(!section.richText.at(i).isEmpty() && original_.appearance.richTextOverrides.value(source));
            song.appearance.fitText.append(original_.appearance.fitText.value(source));
            song.looks.append(original_.looks.value(source));
        }
    }
    return song;
}

void SongStructureDialog::rebuildSections(int selected) {
    const QSignalBlocker block(sectionList_);
    sectionList_->clear();
    for (const auto &section : sections_) {
        auto *item = new QListWidgetItem(QStringLiteral("%1 • %2 slides").arg(section.name).arg(section.slides.size()), sectionList_);
        item->setData(Qt::UserRole, section.id);
    }
    sectionList_->setCurrentRow(qMin(selected, int(sections_.size()) - 1));
    loadSection();
}

void SongStructureDialog::loadSection() {
    const int row = sectionList_->currentRow();
    loading_ = true;
    sectionName_->setEnabled(row >= 0);
    sectionName_->setCurrentText(row >= 0 ? sections_.at(row).name : QString());
    loading_ = false;
    rebuildSlides();
}

void SongStructureDialog::rebuildSlides(int selected) {
    const QSignalBlocker block(slideList_);
    slideList_->clear();
    const int sectionRow = sectionList_->currentRow();
    if (sectionRow >= 0) {
        const auto &section = sections_.at(sectionRow);
        for (int page = 0; page < section.slides.size(); ++page) {
            auto *item = new QListWidgetItem(QStringLiteral("%1 • Slide %2").arg(section.name).arg(page + 1), slideList_);
            item->setData(Qt::UserRole, section.slideIds.at(page));
            item->setToolTip(section.slides.at(page));
        }
        slideList_->setCurrentRow(qBound(0, selected, slideList_->count() - 1));
    }
    loadSlide();
}

void SongStructureDialog::loadSlide() {
    const int section = sectionList_->currentRow();
    const int page = slideList_->currentRow();
    const bool valid = section >= 0 && page >= 0;
    loading_ = true;
    lyrics_->setEnabled(valid);
    lyrics_->setPlainText(valid ? sections_.at(section).slides.at(page) : QString());
    slideLabel_->setText(valid ? QStringLiteral("%1 • Slide %2").arg(sections_.at(section).name).arg(page + 1) : QString());
    removeSlide_->setEnabled(valid && sections_.at(section).slides.size() > 1);
    duplicateSlide_->setEnabled(valid);
    splitSlide_->setEnabled(valid);
    loading_ = false;
}

void SongStructureDialog::saveSection() {
    const int row = sectionList_->currentRow();
    const int page = slideList_->currentRow();
    if (loading_ || row < 0 || page < 0) return;
    auto &section = sections_[row];
    section.name = sectionName_->currentText();
    const auto text = lyrics_->toPlainText();
    if (section.slides.at(page) != text) {
        section.slides[page] = text;
        section.richText[page].clear();
    }
    sectionList_->currentItem()->setText(QStringLiteral("%1 • %2 slides").arg(section.name).arg(section.slides.size()));
    for (int i = 0; i < slideList_->count(); ++i) {
        slideList_->item(i)->setText(QStringLiteral("%1 • Slide %2").arg(section.name).arg(i + 1));
        slideList_->item(i)->setToolTip(section.slides.at(i));
    }
    slideLabel_->setText(slideList_->currentItem()->text());
    refreshSequence();
}

void SongStructureDialog::addSlide(bool duplicate) {
    const int row = sectionList_->currentRow();
    if (row < 0) return;
    auto &section = sections_[row];
    if (section.slides.size() >= 1000) return;
    const int current = slideList_->currentRow();
    const int insert = current + 1;
    section.slides.insert(insert, duplicate ? section.slides.value(current) : QStringLiteral(""));
    section.richText.insert(insert, duplicate ? section.richText.value(current) : QStringLiteral(""));
    section.slideIds.insert(insert, newId());
    section.sources.insert(insert, duplicate ? section.sources.value(current, -1) : -1);
    rebuildSlides(insert);
    saveSection();
    lyrics_->setFocus();
}

void SongStructureDialog::removeSlide() {
    const int row = sectionList_->currentRow();
    const int page = slideList_->currentRow();
    if (row < 0 || page < 0 || sections_.at(row).slides.size() <= 1) return;
    if (QMessageBox::question(this, QStringLiteral("Remove slide?"),
        QStringLiteral("Remove this slide from the section and every occurrence in this song's arrangements?")) != QMessageBox::Yes) return;
    auto &section = sections_[row];
    section.slides.removeAt(page); section.richText.removeAt(page); section.slideIds.removeAt(page); section.sources.removeAt(page);
    rebuildSlides(qMin(page, int(section.slides.size()) - 1));
    saveSection();
}

void SongStructureDialog::splitSlide() {
    const int row = sectionList_->currentRow();
    const int page = slideList_->currentRow();
    if (row < 0 || page < 0 || sections_.at(row).slides.size() >= 1000) return;
    auto &section = sections_[row];
    const int cursor = lyrics_->textCursor().position();
    const auto text = section.slides.at(page);
    if (cursor <= 0 || cursor >= text.size()) return;
    section.slides[page] = text.left(cursor);
    section.slides.insert(page + 1, text.mid(cursor));
    // Plain-text section editing clears rich formatting only on the split page.
    section.richText[page].clear(); section.richText.insert(page + 1, QStringLiteral(""));
    section.slideIds.insert(page + 1, newId());
    section.sources.insert(page + 1, section.sources.at(page));
    rebuildSlides(page + 1);
    saveSection();
    lyrics_->setFocus();
}

void SongStructureDialog::rebuildArrangements() {
    const QSignalBlocker block(arrangement_);
    arrangement_->clear();
    for (const auto &arrangement : structure_.arrangements) arrangement_->addItem(arrangement.name, arrangement.id);
    arrangement_->setCurrentIndex(arrangement_->findData(structure_.activeArrangementId));
    refreshSequence();
}

void SongStructureDialog::refreshSequence() {
    const QSignalBlocker block(sequence_);
    const int selected = sequence_->currentRow();
    sequence_->clear();
    const auto *arrangement = activeArrangement();
    if (arrangement) for (const auto &id : arrangement->sectionIds) {
        for (const auto &section : sections_) {
            if (section.id != id) continue;
            auto *item = new QListWidgetItem(QStringLiteral("%1 • %2 %3").arg(section.name).arg(section.slides.size())
                .arg(section.slides.size() == 1 ? QStringLiteral("slide") : QStringLiteral("slides")), sequence_);
            item->setData(Qt::UserRole, id);
            item->setToolTip(section.slides.join(QStringLiteral("\n---\n")));
        }
    }
    if (sequence_->count()) sequence_->setCurrentRow(qBound(0, selected, sequence_->count() - 1));
    updatePreview();
}

void SongStructureDialog::updatePreview() {
    const auto *item = sequence_->currentItem();
    QString text;
    if (item) for (const auto &section : sections_)
        if (section.id == item->data(Qt::UserRole).toString()) {
            QStringList pages;
            for (int page = 0; page < section.slides.size(); ++page)
                pages.append(QStringLiteral("%1 • Slide %2\n%3").arg(section.name).arg(page + 1).arg(section.slides.at(page)));
            text = pages.join(QStringLiteral("\n\n"));
        }
    preview_->setPlainText(text);
}

void SongStructureDialog::addSection() {
    int number = int(sections_.size()) + 1;
    QString name;
    bool duplicate;
    do {
        name = QStringLiteral("Section %1").arg(number++);
        duplicate = false;
        for (const auto &section : sections_) duplicate = duplicate || section.name == name;
    } while (duplicate);
    sections_.append({newId(), name, {QStringLiteral("")}, {QStringLiteral("")}, {newId()}, {-1}});
    rebuildSections(int(sections_.size()) - 1);
    lyrics_->setFocus();
}

void SongStructureDialog::removeSection() {
    const int row = sectionList_->currentRow();
    if (row < 0 || sections_.size() <= 1) return;
    if (QMessageBox::question(this, QStringLiteral("Remove section?"),
        QStringLiteral("Remove this section and its occurrences from every arrangement in this draft?")) != QMessageBox::Yes) return;
    const QString id = sections_.takeAt(row).id;
    for (auto &arrangement : structure_.arrangements) arrangement.sectionIds.removeAll(id);
    rebuildSections(qMin(row, int(sections_.size()) - 1));
    refreshSequence();
}

void SongStructureDialog::appendSection() {
    const int row = sectionList_->currentRow();
    auto *arrangement = activeArrangement();
    if (row < 0 || !arrangement) return;
    arrangement->sectionIds.append(sections_.at(row).id);
    refreshSequence();
    sequence_->setCurrentRow(sequence_->count() - 1);
}

void SongStructureDialog::removeOccurrence() {
    auto *arrangement = activeArrangement();
    if (!arrangement || sequence_->currentRow() < 0) return;
    arrangement->sectionIds.removeAt(sequence_->currentRow());
    refreshSequence();
}

void SongStructureDialog::createArrangement(bool copy) {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Arrangement"), QStringLiteral("Arrangement name"),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    const auto *source = activeArrangement();
    const QStringList ids = copy && source ? source->sectionIds : QStringList{};
    const QString id = newId();
    structure_.arrangements.append({id, name, ids});
    structure_.activeArrangementId = id;
    rebuildArrangements();
}

void SongStructureDialog::renameArrangement() {
    auto *arrangement = activeArrangement();
    if (!arrangement) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, QStringLiteral("Rename Arrangement"), QStringLiteral("Arrangement name"),
        QLineEdit::Normal, arrangement->name, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    arrangement->name = name;
    rebuildArrangements();
}

void SongStructureDialog::deleteArrangement() {
    if (structure_.arrangements.size() <= 1) return;
    const int index = arrangement_->currentIndex();
    if (index < 0) return;
    structure_.arrangements.removeAt(index);
    structure_.activeArrangementId = structure_.arrangements.first().id;
    rebuildArrangements();
}
