#pragma once

#include "../data/LibraryRepository.h"
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class ReorderListWidget;

class SongStructureDialog : public QDialog {
    Q_OBJECT
public:
    explicit SongStructureDialog(const Song &song = {}, QWidget *parent = nullptr);
    const Song &original() const { return original_; }
    Song draft() const;

private:
    struct SectionDraft {
        QString id;
        QString name;
        QStringList slides;
        QStringList richText;
        QStringList slideIds;
        QVector<int> sources;
    };
    Song original_;
    QVector<SectionDraft> sections_;
    SongStructure structure_;
    QLineEdit *title_ = nullptr;
    ReorderListWidget *sectionList_ = nullptr;
    QComboBox *sectionName_ = nullptr;
    QPlainTextEdit *lyrics_ = nullptr;
    ReorderListWidget *slideList_ = nullptr;
    QLabel *slideLabel_ = nullptr;
    QPushButton *removeSlide_ = nullptr;
    QPushButton *duplicateSlide_ = nullptr;
    QPushButton *splitSlide_ = nullptr;
    QComboBox *arrangement_ = nullptr;
    ReorderListWidget *sequence_ = nullptr;
    QPlainTextEdit *preview_ = nullptr;
    QLabel *validation_ = nullptr;
    bool loading_ = false;

    SongArrangement *activeArrangement();
    void loadSection();
    void saveSection();
    void rebuildSlides(int selected = 0);
    void loadSlide();
    void addSlide(bool duplicate = false);
    void removeSlide();
    void splitSlide();
    void rebuildSections(int selected = 0);
    void rebuildArrangements();
    void refreshSequence();
    void updatePreview();
    void addSection();
    void removeSection();
    void appendSection();
    void removeOccurrence();
    void createArrangement(bool copy);
    void renameArrangement();
    void deleteArrangement();
};
