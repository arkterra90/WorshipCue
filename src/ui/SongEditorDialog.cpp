#include "SongEditorDialog.h"
#include "../core/LyricsImporter.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringConverter>
#include <QVBoxLayout>

SongEditorDialog::SongEditorDialog(QWidget *parent, const Song &song) : QDialog(parent) {
    setWindowTitle(song.id ? QStringLiteral("Edit Song") : QStringLiteral("New Song"));
    resize(560, 540);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Title"), this));
    titleEdit_ = new QLineEdit(song.title, this);
    titleEdit_->setObjectName(QStringLiteral("songTitle"));
    titleEdit_->setPlaceholderText(QStringLiteral("Song title"));
    layout->addWidget(titleEdit_);
    auto *imports = new QHBoxLayout;
    auto *import = new QPushButton(QStringLiteral("Import Lyrics…"), this);
    import->setObjectName(QStringLiteral("importLyrics"));
    auto *paste = new QPushButton(QStringLiteral("Paste Lyrics"), this);
    paste->setObjectName(QStringLiteral("pasteLyrics"));
    imports->addWidget(import);
    imports->addWidget(paste);
    imports->addStretch();
    layout->addLayout(imports);
    connect(import, &QPushButton::clicked, this, &SongEditorDialog::chooseLyricsFile);
    connect(paste, &QPushButton::clicked, this, &SongEditorDialog::pasteLyrics);
    auto *hint = new QLabel(QStringLiteral(
        "Import a text or ChordPro file, or paste copied lyrics. Blank lines and section headings "
        "become slide breaks; chord annotations are removed. Review and edit the slides below before saving."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addWidget(new QLabel(QStringLiteral("Slides (--- on its own line separates slides)"), this));
    slidesEdit_ = new QPlainTextEdit(this);
    slidesEdit_->setObjectName(QStringLiteral("songLyrics"));
    slidesEdit_->setPlaceholderText(QStringLiteral("Verse 1\nYour lyrics here\n---\nChorus\nMore lyrics"));
    slidesEdit_->setPlainText(song.slides.join(QStringLiteral("\n---\n")));
    layout->addWidget(slidesEdit_, 1);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(actions, &QDialogButtonBox::accepted, this, &SongEditorDialog::validateAndAccept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(actions);
}

QString SongEditorDialog::title() const { return titleEdit_->text().trimmed(); }

bool SongEditorDialog::importLyricsFile(const QString &path, QString *error) {
    if (error) error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.size() > 5 * 1024 * 1024) {
        if (error) *error = QStringLiteral("Choose a lyrics file smaller than 5 MB.");
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error) *error = file.errorString();
        return false;
    }
    QStringDecoder decoder(QStringConverter::Utf8);
    const QString text = decoder(bytes);
    if (decoder.hasError()) {
        if (error) *error = QStringLiteral("This file is not UTF-8 text. Export the lyrics as a UTF-8 text file and try again.");
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool chordPro = suffix == QStringLiteral("pro") || suffix == QStringLiteral("chopro")
        || suffix == QStringLiteral("cho") || suffix == QStringLiteral("chordpro");
    const auto imported = LyricsImporter::parse(text,
        title().isEmpty() ? QFileInfo(path).completeBaseName() : title(), chordPro);
    if (imported.slides.isEmpty()) {
        if (error) *error = QStringLiteral("This file contains no lyrics to import.");
        return false;
    }
    imported_ = imported;
    titleEdit_->setText(imported.title);
    slidesEdit_->setPlainText(imported.slides.join(QStringLiteral("\n---\n")));
    return true;
}

void SongEditorDialog::importLyricsText(const QString &text, const QString &suggestedTitle) {
    const auto imported = LyricsImporter::parse(text, suggestedTitle.isEmpty() ? title() : suggestedTitle);
    if (imported.slides.isEmpty()) return;
    imported_ = imported;
    titleEdit_->setText(imported.title);
    slidesEdit_->setPlainText(imported.slides.join(QStringLiteral("\n---\n")));
}

bool SongEditorDialog::confirmReplaceLyrics() {
    return slidesEdit_->toPlainText().trimmed().isEmpty()
        || QMessageBox::question(this, QStringLiteral("Replace lyrics?"),
            QStringLiteral("Replace the lyrics in this editor with the imported lyrics? Your library changes only when you save."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}

void SongEditorDialog::chooseLyricsFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import Lyrics"), {},
        QStringLiteral("Lyrics (*.txt *.text *.pro *.chopro *.cho *.chordpro);;All files (*)"));
    if (path.isEmpty() || !confirmReplaceLyrics()) return;
    QString error;
    if (!importLyricsFile(path, &error))
        QMessageBox::warning(this, QStringLiteral("Cannot import lyrics"), error);
}

void SongEditorDialog::pasteLyrics() {
    const QString text = QApplication::clipboard()->text();
    if (text.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Paste Lyrics"), QStringLiteral("Copy some lyrics first, then choose Paste Lyrics."));
        return;
    }
    if (LyricsImporter::parse(text).slides.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Paste Lyrics"), QStringLiteral("The clipboard contains no lyrics to import."));
        return;
    }
    if (confirmReplaceLyrics()) importLyricsText(text);
}

QStringList SongEditorDialog::slides() const {
    QStringList result;
    const QStringList parts = slidesEdit_->toPlainText().split(
        QRegularExpression(QStringLiteral("(?:\\r?\\n)---[ \\t]*(?:\\r?\\n|$)")));
    for (const auto &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) result.append(trimmed);
    }
    return result;
}

SongStructure SongEditorDialog::songStructure(QStringList *sourceSlides) const {
    if (slides() == imported_.slides) return imported_.songStructure(sourceSlides);
    ImportedLyrics edited;
    edited.slides = slides();
    return edited.songStructure(sourceSlides);
}

void SongEditorDialog::validateAndAccept() {
    if (title().isEmpty() || slides().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing content"),
                             QStringLiteral("Enter a title and at least one nonempty slide."));
        return;
    }
    accept();
}
