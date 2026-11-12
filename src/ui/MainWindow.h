#pragma once

#include "../core/PresentationController.h"
#include "../data/LibraryRepository.h"
#include "OutputManager.h"
#include "../media/MediaPlayback.h"
#include <QKeySequence>
#include <QMainWindow>
#include <memory>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QSplitter;
class QTabWidget;
class QSlider;
class SlideTextDelegate;
class QAbstractItemDelegate;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class SlidePreview;
class MediaManager;
class QProgressDialog;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool initialize(QString *error, const QString &databasePath = {});
    bool importMediaFiles(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    LibraryRepository library_;
    PresentationController controller_;
    OutputManager outputs_;
    MediaPlayback playback_;
    std::unique_ptr<MediaManager> mediaManager_;
    QListWidget *songs_ = nullptr;
    QListWidget *service_ = nullptr;
    QListWidget *slides_ = nullptr;
    QLineEdit *search_ = nullptr;
    QComboBox *services_ = nullptr;
    QComboBox *mode_ = nullptr;
    SlidePreview *selectedPreview_ = nullptr;
    SlidePreview *livePreview_ = nullptr;
    SlidePreview *nextPreview_ = nullptr;
    QLabel *selectedInfo_ = nullptr;
    QLabel *liveInfo_ = nullptr;
    QLabel *nextInfo_ = nullptr;
    QLabel *outputInfo_ = nullptr;
    QLabel *saveInfo_ = nullptr;
    QLabel *mediaStatus_ = nullptr;
    QLabel *timingStatus_ = nullptr;
    QLabel *selectedTimingInfo_ = nullptr;
    QPushButton *pauseAutoAdvance_ = nullptr;
    QPushButton *editSlide_ = nullptr;
    QComboBox *globalTransitionType_ = nullptr;
    QDoubleSpinBox *globalTransitionTime_ = nullptr;
    QComboBox *slideViewMode_ = nullptr;
    QSlider *slideSize_ = nullptr;
    QSplitter *operatorSplitter_ = nullptr;
    QSplitter *libraryServiceSplitter_ = nullptr;
    QTabWidget *previewTabs_ = nullptr;
    SlideTextDelegate *textDelegate_ = nullptr;
    QAbstractItemDelegate *gridDelegate_ = nullptr;
    QPushButton *returnToLive_ = nullptr;
    QPushButton *importButton_ = nullptr;
    QPushButton *pauseVideo_ = nullptr;
    QPushButton *restartVideo_ = nullptr;
    QPushButton *muteVideo_ = nullptr;
    QAction *importAction_ = nullptr;
    QProgressDialog *importProgress_ = nullptr;
    QString importTitle_;
    int importServiceId_ = 0;
    QComboBox *audienceTarget_ = nullptr;
    QComboBox *stageTarget_ = nullptr;
    QPushButton *blackoutButton_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    int currentServiceId_ = 0;
    bool rebuilding_ = false;
    qint64 thumbnailBackgroundKey_ = -1;
    QKeySequence blackoutShortcut_;
    QKeySequence clearShortcut_;
    PresentationSettings programSettings_;
    QPixmap selectedBackground_;
    QString selectedBackgroundPath_;

    void buildUi();
    void reloadSongs();
    void reloadServices(int selectServiceId = 0);
    void reloadService(int selectEntryId = 0, int selectSlide = 0);
    void showSelectedService(int selectSlide = 0);
    void showSelectedSlide();
    void createService();
    void renameService();
    void duplicateService();
    void createSong();
    void importLyrics();
    void editSong();
    void editSelectedSlide();
    void editPresentation(int songId, int sourceSlide = 0);
    void duplicateSong();
    void chooseMediaFiles();
    void renameMedia();
    void renameMediaPresentation(int id);
    void updatePlaybackUi();
    void configurePresentation(int songId);
    void configureProgramTransitions();
    void saveProgramTransitions();
    void updateProgramTransitionUi();
    void updateSlideView();
    void revealLiveCue();
    void assignBackground(int songId);
    void updateTimingUi();
    void deleteSong();
    void appendSong();
    void removeServiceEntry();
    void duplicateServiceEntry();
    void moveService(int delta);
    void persistServiceOrder();
    void persistSlideOrder();
    void moveSlide(int delta);
    void resetSlideOrder();
    void goLive();
    void advance();
    void rewind();
    void syncLiveSelection();
    void updateLiveUi();
    void refreshThumbnails();
    void refreshOutputOptions();
    void updateOutputStatus();
    void configureShortcuts();
    void focusOperator();
    void markSaved();
    void reportError(const QString &error);
    int selectedSongId() const;
    int selectedEntryId() const;
    QStringList selectedSlides() const;
    QStringList selectedRichText() const;
    QVector<int> selectedSlideOrder() const;
    QVector<SlideMedia> selectedMedia() const;
    SlideMedia resolveMedia(SlideMedia media) const;
    QPixmap mediaThumbnail(const SlideMedia &media) const;
    QString resolveBackground(const QString &path) const;
    QPixmap backgroundPixmap(const QString &absolutePath) const;
};
