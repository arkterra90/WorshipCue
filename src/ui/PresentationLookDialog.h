#pragma once

#include "../data/LibraryRepository.h"
#include <QDialog>
#include <QUndoStack>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class LookPreviewWidget;

class PresentationLookDialog : public QDialog {
    Q_OBJECT
public:
    enum class OverflowHandling { Cancel, Split, Fit };
    PresentationLookDialog(LibraryRepository *library, const Song &song, int sourceSlide = 0, QWidget *parent = nullptr);
    const Song &draft() const { return draft_; }
    const QVector<int> &sourceIndices() const { return sourceIndices_; }
    bool applyLook(int id, const QVector<int> &pages, bool presentationWide, OverflowHandling handling, QString *error);

private:
    struct State { Song song; QVector<int> sources; int selected = 0; };
    LibraryRepository *library_;
    Song draft_;
    SongStructure sourceStructure_;
    QVector<int> sourceIndices_;
    QUndoStack undo_;
    QListWidget *pages_ = nullptr;
    LookPreviewWidget *preview_ = nullptr;
    QComboBox *looks_ = nullptr;
    QFontComboBox *family_ = nullptr;
    QSpinBox *size_ = nullptr;
    QComboBox *weight_ = nullptr;
    QCheckBox *italic_ = nullptr;
    QCheckBox *underline_ = nullptr;
    QCheckBox *strikeOut_ = nullptr;
    QComboBox *horizontal_ = nullptr;
    QComboBox *vertical_ = nullptr;
    QSpinBox *left_ = nullptr;
    QSpinBox *right_ = nullptr;
    QSpinBox *top_ = nullptr;
    QSpinBox *bottom_ = nullptr;
    QSpinBox *padding_ = nullptr;
    QComboBox *background_ = nullptr;
    QCheckBox *preserveRich_ = nullptr;
    QPushButton *textColorButton_ = nullptr;
    QPushButton *backgroundColorButton_ = nullptr;
    QLabel *backgroundName_ = nullptr;
    QLabel *status_ = nullptr;
    QString textColor_;
    QString backgroundColor_;
    QString backgroundPath_;
    QString loadedFamily_;
    bool familyEdited_ = false;
    bool loading_ = false;

    void reloadLooks(int selected = 0);
    PresentationLook settings() const;
    void setSettings(const PresentationLook &settings);
    void refreshPages(int selected = 0);
    void refreshPreview();
    QPixmap previewBackground(const SlideLook &look) const;
    void saveLook();
    void applyFromUi(bool all);
    int selectedLook();
    bool planLook(int id, const QVector<int> &pages, bool all, OverflowHandling handling, State *state, QString *error) const;
    void commitState(const State &state);
    bool reviewSplit(const State &state);
    void restoreState(const State &state);
};
