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

private:
    QLineEdit *titleEdit_ = nullptr;
    QPlainTextEdit *slidesEdit_ = nullptr;
    void validateAndAccept();
};
