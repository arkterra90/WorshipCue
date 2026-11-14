#pragma once

#include "../media/BatchRenamer.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

// Plans names before import, or updates presentation display names afterward.
// Nothing is copied or renamed until the caller commits the accepted plan.
class MediaImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaImportDialog(const QStringList &filenames, QWidget *parent = nullptr,
                               bool namingOnly = false);
    QString title() const;
    QStringList names() const { return plannedNames_; }
    bool loopVideos() const;
    bool createPresentation() const;
    bool addToService() const;
    void setPresentationTitle(const QString &title);

private:
    QStringList filenames_;
    QStringList plannedNames_;
    bool namingOnly_ = false;
    QString lastAutoName_;
    QLineEdit *title_;
    QComboBox *mode_;
    QLineEdit *text_;
    QLineEdit *find_;
    QLineEdit *replacement_;
    QLineEdit *add_;
    QComboBox *position_;
    QSpinBox *start_;
    QSpinBox *padding_;
    QCheckBox *loop_;
    QCheckBox *createPresentation_;
    QCheckBox *addToService_;
    QStackedWidget *options_;
    QTableWidget *preview_;
    QLabel *error_;
    QDialogButtonBox *actions_;
    void updatePreview();
};
