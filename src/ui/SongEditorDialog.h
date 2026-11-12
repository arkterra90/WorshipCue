#pragma once

#include "../data/LibraryRepository.h"
#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

class SongEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit SongEditorDialog(QWidget *parent = nullptr, const Song &song = {});
    QString title() const;
    QStringList slides() const;
    bool importLyricsFile(const QString &path, QString *error = nullptr);
    void importLyricsText(const QString &text, const QString &suggestedTitle = {});

private:
    QLineEdit *titleEdit_ = nullptr;
    QPlainTextEdit *slidesEdit_ = nullptr;
    void chooseLyricsFile();
    void pasteLyrics();
    bool confirmReplaceLyrics();
    void validateAndAccept();
};
