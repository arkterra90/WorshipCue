#pragma once

#include "../data/LibraryRepository.h"
#include <QDialog>
#include <QPixmap>
#include <QVector>

class QDialogButtonBox;
class QFontComboBox;
class QGraphicsPixmapItem;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTextCharFormat;
class QTextDocument;
class QToolButton;
class SlideCanvasView;
class SlideTextItem;

// A draft editor. The library and live presentation are changed only by its
// caller after Save, never while navigating or formatting inside this dialog.
class SlideEditorDialog : public QDialog {
    Q_OBJECT
public:
    SlideEditorDialog(const Song &song, int sourceSlide = 0, QWidget *parent = nullptr,
                      const QPixmap &background = {}, const QVector<QPixmap> &mediaFrames = {});
    QString title() const;
    QStringList slides() const;
    QStringList richText() const;
    int selectedSlideIndex() const;

private:
    struct Draft {
        QTextDocument *document = nullptr;
        QString originalHtml;
        bool changed = false;
    };
    Song source_;
    QVector<Draft> drafts_;
    QVector<QPixmap> mediaFrames_;
    QPixmap background_;
    QLineEdit *titleEdit_ = nullptr;
    QListWidget *navigator_ = nullptr;
    SlideCanvasView *canvas_ = nullptr;
    SlideTextItem *textItem_ = nullptr;
    QGraphicsPixmapItem *backgroundItem_ = nullptr;
    QLabel *slideInfo_ = nullptr;
    QFontComboBox *fontFamily_ = nullptr;
    QSpinBox *fontSize_ = nullptr;
    QToolButton *bold_ = nullptr;
    QToolButton *italic_ = nullptr;
    QToolButton *underline_ = nullptr;
    QToolButton *color_ = nullptr;
    QToolButton *left_ = nullptr;
    QToolButton *center_ = nullptr;
    QToolButton *right_ = nullptr;
    QToolButton *undo_ = nullptr;
    QToolButton *redo_ = nullptr;
    bool loading_ = false;
    int activeSlide_ = -1;

    QTextDocument *currentDocument() const;
    Draft createDraft(const QString &plain, const QString &html);
    void documentChanged(QTextDocument *document);
    void loadSlide(int index);
    void updateTextGeometry();
    void updateFormatControls();
    void refreshThumbnail(int index);
    void ensureMediaFrame(int index);
    void applyCharacterFormat(const QTextCharFormat &format);
    void applyAlignment(Qt::Alignment alignment);
    void chooseTextColor();
    void addSlide(bool duplicate);
    void removeSlide();
    void validateAndAccept();
};
