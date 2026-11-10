#include "SongEditorDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

SongEditorDialog::SongEditorDialog(QWidget *parent, const Song &song) : QDialog(parent) {
    setWindowTitle(song.id ? QStringLiteral("Edit Song") : QStringLiteral("New Song"));
    resize(560, 540);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Title"), this));
    titleEdit_ = new QLineEdit(song.title, this);
    titleEdit_->setPlaceholderText(QStringLiteral("Song title"));
    layout->addWidget(titleEdit_);
    layout->addWidget(new QLabel(QStringLiteral("Slides (put --- on its own line between slides)"), this));
    slidesEdit_ = new QPlainTextEdit(this);
    slidesEdit_->setPlaceholderText(QStringLiteral("Verse 1\nYour lyrics here\n---\nChorus\nMore lyrics"));
    slidesEdit_->setPlainText(song.slides.join(QStringLiteral("\n---\n")));
    layout->addWidget(slidesEdit_, 1);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(actions, &QDialogButtonBox::accepted, this, &SongEditorDialog::validateAndAccept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(actions);
}

QString SongEditorDialog::title() const { return titleEdit_->text().trimmed(); }

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

void SongEditorDialog::validateAndAccept() {
    if (title().isEmpty() || slides().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing content"),
                             QStringLiteral("Enter a title and at least one nonempty slide."));
        return;
    }
    accept();
}
