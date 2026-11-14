#include "MainWindow.h"
#include "MidiDialogs.h"
#include "LibraryCleanupDialog.h"
#include <QUuid>
#include "SlideRenderer.h"
#include "PresentationLookDialog.h"
#include "RichTextSupport.h"
#include "ReorderListWidget.h"
#include "SongEditorDialog.h"
#include "SongStructureDialog.h"
#include "SlideEditorDialog.h"
#include "SlideTextDelegate.h"
#include "SlideGridDelegate.h"
#include "PresentationSettingsDialog.h"
#include "OutputSettingsDialog.h"
#include "StageLayoutDialog.h"
#include "MediaImportDialog.h"
#include "MediaAssetListWidget.h"
#include <QCheckBox>
#include "BackgroundLibraryDialog.h"
#include "../core/BuiltInBackgrounds.h"
#include "../media/MediaManager.h"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QImageReader>
#include <QCollator>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmapCache>
#include <QPushButton>
#include <QProgressDialog>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSplitter>
#include <QSlider>
#include <QTabWidget>
#include <QTabBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

class SlidePreview : public QWidget {
public:
    explicit SlidePreview(QWidget *parent = nullptr, bool mediaLabels = true) : QWidget(parent), mediaLabels_(mediaLabels) {
        setMinimumSize(220, 124);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    QSize sizeHint() const override { return {320, 180}; }
    void setAudienceSource(DisplayWindow *source) {
        audienceSource_ = source;
        connect(source, &DisplayWindow::audienceFrameChanged, this, QOverload<>::of(&QWidget::update));
    }
    void setSlide(const LivePresentation &state, const QPixmap &background, const QPixmap &frame = {}) {
        state_ = state;
        background_ = background;
        frame_ = frame;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        if (audienceSource_) {
            audienceSource_->paintOutput(p, rect());
            return;
        }
        SlideRenderer::paintAudience(p, rect(), state_, background_, frame_);
        const auto media = state_.currentMedia();
        if (mediaLabels_ && frame_.isNull() && media.kind != MediaKind::None) {
            p.setPen(Qt::white);
            p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                QStringLiteral("%1\n%2").arg(media.kind == MediaKind::Audio ? QStringLiteral("AUDIO")
                    : media.kind == MediaKind::Video ? QStringLiteral("▶ VIDEO") : QStringLiteral("PICTURE PREVIEW"), media.name));
        }
    }
private:
    LivePresentation state_;
    QPixmap background_;
    QPixmap frame_;
    bool mediaLabels_;
    DisplayWindow *audienceSource_ = nullptr;
};

namespace {
QLabel *header(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("panelHeader"));
    label->setWordWrap(true);
    return label;
}
QPushButton *button(const QString &text, QWidget *parent, const QString &name = {}) {
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QWidget *column(QWidget *parent, QVBoxLayout **out) {
    auto *w = new QWidget(parent);
    *out = new QVBoxLayout(w);
    (*out)->setContentsMargins(10, 10, 10, 10);
    (*out)->setSpacing(8);
    return w;
}
LivePresentation previewState(const QString &text, const SlideMedia &media = {}, const QString &html = {}, const SlideLook &look = {}) {
    LivePresentation state;
    state.slides = {text};
    state.index = 0;
    state.media = {media};
    state.richText = {html};
    state.looks = {look};
    return state;
}
QStringList arrangedSourceKeys(const Song &song, const QVector<int> &order) {
    const auto source = song.structure.slideKeys();
    if (source.isEmpty()) return {};
    QStringList keys;
    for (int index : order) keys.append(source.value(index));
    return keys;
}
bool sameMediaContent(const QVector<SlideMedia> &a, const QVector<SlideMedia> &b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a.at(i).kind != b.at(i).kind || a.at(i).path != b.at(i).path || a.at(i).loop != b.at(i).loop) return false;
    return true;
}
QString transitionName(SlideTransitionType type) {
    switch (type) {
    case SlideTransitionType::Fade: return QStringLiteral("Fade");
    case SlideTransitionType::SlideLeft: return QStringLiteral("Slide Left");
    case SlideTransitionType::SlideRight: return QStringLiteral("Slide Right");
    default: return QStringLiteral("Cut");
    }
}
}

MainWindow::MainWindow(QWidget *parent, std::unique_ptr<MidiBackend> midiBackend)
    : QMainWindow(parent), controller_(this), outputs_(&controller_, this), playback_(&controller_, this), midi_(this, std::move(midiBackend)) {
    cleanupLeaseOwner_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setWindowTitle(QStringLiteral("WorshipCue | Service Control"));
    resize(1440, 860);
    setMinimumSize(1080, 680);
    controller_.setObjectName(QStringLiteral("presentationController"));
    const QSettings settings;
    blackoutShortcut_ = QKeySequence(settings.value(QStringLiteral("shortcuts/blackout"),
                                       QStringLiteral("Ctrl+B")).toString());
    clearShortcut_ = QKeySequence(settings.value(QStringLiteral("shortcuts/clear"),
                                    QStringLiteral("Ctrl+Shift+C")).toString());
    programSettings_.transition = SlideTransitionType(settings.value(QStringLiteral("presentation/transitionType"), 0).toInt());
    programSettings_.transitionDurationMs = settings.value(QStringLiteral("presentation/transitionDurationMs"), 500).toInt();
    if (!programSettings_.isValid()) programSettings_ = {};
    controller_.setGlobalTransitionDefaults(programSettings_);
    buildUi();
    midi_.setObjectName(QStringLiteral("midiEngine"));
    midi_.setDispatchGate([this] { return mode_->currentIndex() == 0 && !QApplication::activeModalWidget() && !QApplication::activePopupWidget(); });
    connect(&midi_, &MidiEngine::commandRequested, this, &MainWindow::dispatchMidi);
    connect(&controller_, &PresentationController::stateChanged, this, [this] {
        const auto &state = controller_.state();
        recordLiveUsage();
        if (state.index >= 0) midi_.activateCue(state.cueRevision, state.currentMidiActions());
    });
    playback_.setObjectName(QStringLiteral("mediaPlayback"));
    outputs_.audience().setMediaPlayback(&playback_);
    connect(&controller_, &PresentationController::stateChanged, this, &MainWindow::updateLiveUi);
    connect(&controller_, &PresentationController::autoAdvanceChanged, this, &MainWindow::updateTimingUi);
    auto *countdown = new QTimer(this);
    countdown->setInterval(100);
    connect(countdown, &QTimer::timeout, this, &MainWindow::updateTimingUi);
    countdown->start();
    connect(&playback_, &MediaPlayback::frameChanged, this, [this] {
        livePreview_->setSlide(controller_.state(), outputs_.audience().backgroundPixmap(), playback_.frame());
    });
    connect(&playback_, &MediaPlayback::statusChanged, this, &MainWindow::updatePlaybackUi);
    connect(&outputs_, &OutputManager::targetsChanged, this, &MainWindow::refreshOutputOptions);
    connect(&outputs_, &OutputManager::statusChanged, this, [this] {
        refreshOutputOptions();
        updateOutputStatus();
    });
    connect(&outputs_, &OutputManager::operatorFocusRequested, this, &MainWindow::focusOperator);
    restoreGeometry(settings.value(QStringLiteral("control/geometry")).toByteArray());
    outputs_.restoreTargets();
    refreshOutputOptions();
    updateLiveUi();
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow() { midi_.shutdown(); library_.clearCleanupLease(cleanupLeaseOwner_); }

bool MainWindow::initialize(QString *error, const QString &databasePath) {
    if (libraryRefreshTimer_) libraryRefreshTimer_->stop();
    if (!library_.open(error, databasePath)) return false;
    transitionUndo_.clear();
    transitionRedo_.clear();
    updateTransitionActions();
    externalDataVersion_ = library_.dataVersion();
    databasePath_ = databasePath == QStringLiteral(":memory:") ? databasePath
        : databasePath.isEmpty() ? QDir(library_.databaseDirectory()).filePath(QStringLiteral("library.sqlite3"))
        : QFileInfo(databasePath).absoluteFilePath();
    mediaManager_ = std::make_unique<MediaManager>(library_.databaseDirectory());
    connect(mediaManager_.get(), &MediaManager::thumbnailReady, this, [this](const QString &path) {
        for (int i = 0; i < slides_->count(); ++i) {
            auto *item = slides_->item(i);
            if (item->data(Qt::UserRole + 3).value<SlideMedia>().path == path)
                item->setData(Qt::UserRole + 2, QVariant());
        }
        refreshThumbnails();
        showSelectedSlide();
        updateLiveUi();
    });
    connect(mediaManager_.get(), &MediaManager::progressChanged, this, [this](int done, int total) {
        if (importProgress_) { importProgress_->setMaximum(total); importProgress_->setValue(done); }
    });
    connect(mediaManager_.get(), &MediaManager::importFinished, this, [this](const MediaImportResult &result) {
        if (importProgress_) { importProgress_->close(); importProgress_->deleteLater(); importProgress_ = nullptr; }
        importButton_->setEnabled(true);
        mediaImportButton_->setEnabled(true);
        importAction_->setEnabled(true);
        if (result.canceled) { statusBar()->showMessage(QStringLiteral("Media import canceled."), 5000); return; }
        if (!result.error.isEmpty()) return reportError(result.error);
        QString error;
        int presentationId = 0;
        QVector<int> assetIds;
        if (importAsPresentation_) {
            QStringList texts;
            for (int i = 0; i < result.media.size(); ++i) texts.append(QStringLiteral(""));
            presentationId = library_.createPresentation(importTitle_, texts, result.media, &error);
            if (presentationId && !library_.recordPresentationImport(presentationId, &error)) return reportError(error);
        } else {
            assetIds = library_.registerMediaAssets(result.media, &error);
        }
        if ((!importAsPresentation_ && assetIds.isEmpty()) || (importAsPresentation_ && !presentationId)) {
            mediaManager_->discardImport(result.directory);
            return reportError(error);
        }
        bool added = true;
        if (importServiceId_) {
            if (presentationId) added = library_.appendToService(importServiceId_, presentationId, &error);
            else for (int id : assetIds) {
                if (!library_.appendMediaToService(importServiceId_, id, &error)) { added = false; break; }
            }
        }
        search_->clear();
        mediaSearch_->clear();
        reloadSongs();
        reloadMedia();
        if (presentationId) {
            for (int i = 0; i < songs_->count(); ++i)
                if (songs_->item(i)->data(Qt::UserRole).toInt() == presentationId) songs_->setCurrentRow(i);
        } else {
            mediaFolders_->setCurrentIndex(0);
            sidebarTabs_->setCurrentIndex(2);
            for (int i = 0; i < mediaAssets_->count(); ++i)
                if (mediaAssets_->item(i)->data(Qt::UserRole).toInt() == assetIds.first()) mediaAssets_->setCurrentRow(i);
            showSelectedMedia();
        }
        if (currentServiceId_ == importServiceId_) {
            const auto entries = library_.service(currentServiceId_);
            if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
            if (presentationId) { browsingMedia_ = false; browsingLibrary_ = false; }
            reloadService(entries.isEmpty() ? 0 : entries.last().id);
        }
        markSaved();
        if (!added) return reportError(QStringLiteral("The media was imported, but adding it to the service stopped. All imported items are available in the library. %1").arg(error));
        statusBar()->showMessage(QStringLiteral("Imported %1 media items.").arg(result.media.size()), 10000);
    });
    reloadSongs();
    reloadMedia();
    reloadServices(QSettings().value(QStringLiteral("service/selected")).toInt());
    if (!library_.lastError().isEmpty()) {
        if (error) *error = library_.lastError();
        return false;
    }
    if (!libraryRefreshTimer_) {
        libraryRefreshTimer_ = new QTimer(this);
        libraryRefreshTimer_->setObjectName(QStringLiteral("externalLibraryRefresh"));
        libraryRefreshTimer_->setInterval(1000);
        connect(libraryRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshExternalChanges);
    }
    if (!cleanupLeaseTimer_) {
        cleanupLeaseTimer_ = new QTimer(this);
        cleanupLeaseTimer_->setInterval(10000);
        connect(cleanupLeaseTimer_, &QTimer::timeout, this, &MainWindow::recordLiveUsage);
    }
    cleanupLeaseTimer_->start();
    reloadMidiBindings();
    midi_.restoreSettings();
    libraryRefreshTimer_->start();
    statusBar()->showMessage(QStringLiteral("Show: click a slide to take it live. Edit: preview and drag slides to arrange."), 10000);
    return true;
}

void MainWindow::buildUi() {
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget { background: #171d28; color: #eaf0f8; }
        QLabel#panelHeader { font-size: 14px; font-weight: 700; padding: 4px 0; }
        QLabel#hint { color: #a5b2c5; font-size: 12px; }
        QLabel#saveStatus { color: #64dca3; }
        QLabel#outputStatus { color: #b8c9e0; padding: 4px; }
        QListWidget, QComboBox, QLineEdit, QPlainTextEdit { background: #202a3a;
            border: 1px solid #35445c; border-radius: 5px; padding: 5px; }
        QSpinBox, QDoubleSpinBox { background: #202a3a; color: #eaf0f8; border: 1px solid #35445c;
            border-radius: 4px; padding: 5px; min-height: 22px; }
        QTableWidget { background: #202a3a; alternate-background-color: #27364b;
            color: #eaf0f8; gridline-color: #35445c; border: 1px solid #35445c; }
        QTableWidget::item:selected { background: #284d74; color: white; }
        QHeaderView::section, QTableCornerButton::section { background: #263347;
            color: #eaf0f8; padding: 6px; border: 1px solid #35445c; }
        QListWidget::item { padding: 7px 5px; border: 1px solid transparent; border-radius: 4px; }
        QListWidget::item:selected { background: #284d74; border-color: #508bc4; color: white; }
        QListWidget::item:hover { background: #2b3c53; }
        QPushButton { background: #2c3b51; border: 1px solid #455976;
            border-radius: 5px; padding: 7px 10px; font-weight: 600; }
        QPushButton:hover { background: #3b5170; }
        QPushButton:pressed { background: #235380; }
        QPushButton:disabled { color: #768394; }
        QPushButton#takeLive { background: #237651; border-color: #4ac28c; }
        QPushButton:checked { background: #913c3c; border-color: #e58d82; }
        QMenuBar, QMenu, QStatusBar { background: #202a3a; }
        QTabWidget::pane { border: 0; }
        QTabBar::tab { background: #202a3a; color: #b8c9e0; padding: 5px 10px;
            border: 1px solid #35445c; }
        QTabBar::tab:selected { background: #284d74; color: white; }
        QSplitter::handle { background: #354257; width: 2px; }
        QToolTip { background: #111827; color: white; border: 1px solid #526884; }
    )"));
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto *lyricsAction = fileMenu->addAction(QStringLiteral("Import Song Lyrics…"));
    lyricsAction->setObjectName(QStringLiteral("importLyricsAction"));
    connect(lyricsAction, &QAction::triggered, this, &MainWindow::importLyrics);
    importAction_ = fileMenu->addAction(QStringLiteral("Import Media…"));
    importAction_->setObjectName(QStringLiteral("importMediaAction"));
    connect(importAction_, &QAction::triggered, this, &MainWindow::chooseMediaFiles);
    auto *renameMediaAction = fileMenu->addAction(QStringLiteral("Batch Rename Media…"));
    renameMediaAction->setObjectName(QStringLiteral("renameMediaAction"));
    connect(renameMediaAction, &QAction::triggered, this, &MainWindow::renameMedia);
    auto *timingAction = fileMenu->addAction(QStringLiteral("Library Presentation Timing && Transitions…"));
    timingAction->setObjectName(QStringLiteral("presentationSettingsAction"));
    connect(timingAction, &QAction::triggered, this, [this] { configurePresentation(selectedSongId()); });
    fileMenu->addSeparator();
    connect(fileMenu->addAction(QStringLiteral("New Service…")), &QAction::triggered, this, &MainWindow::createService);
    connect(fileMenu->addAction(QStringLiteral("Copy Service…")), &QAction::triggered, this, &MainWindow::duplicateService);
    fileMenu->addSeparator();
    auto *backgroundAction = fileMenu->addAction(QStringLiteral("Assign Background to Library Presentation…"));
    backgroundAction->setObjectName(QStringLiteral("assignBackgroundAction"));
    connect(backgroundAction, &QAction::triggered, this, [this] { assignBackground(selectedSongId()); });
    fileMenu->addSeparator();
    auto *programTransitionAction = fileMenu->addAction(QStringLiteral("Global Slide Transitions…"));
    programTransitionAction->setObjectName(QStringLiteral("programTransitionsAction"));
    connect(programTransitionAction, &QAction::triggered, this, &MainWindow::configureProgramTransitions);
    auto *settingsMenu = menuBar()->addMenu(QStringLiteral("&Settings"));
    settingsMenu->addAction(programTransitionAction);
    auto *screensAction = settingsMenu->addAction(QStringLiteral("Screens / Output Settings…"));
    screensAction->setObjectName(QStringLiteral("outputSettingsAction"));
    connect(screensAction, &QAction::triggered, this, [this] { OutputSettingsDialog dialog(&outputs_, this); dialog.exec(); });
    auto *stageLayoutAction = settingsMenu->addAction(QStringLiteral("Stage Display Layouts…"));
    stageLayoutAction->setObjectName(QStringLiteral("stageLayoutsAction"));
    connect(stageLayoutAction, &QAction::triggered, this, [this] { StageLayoutDialog dialog(&outputs_, this); dialog.exec(); });
    auto *stageControlsAction = settingsMenu->addAction(QStringLiteral("Stage Messages / Timers…"));
    stageControlsAction->setObjectName(QStringLiteral("stageControlsAction"));
    connect(stageControlsAction, &QAction::triggered, this, [this] { StageControlsDialog dialog(&outputs_, this); dialog.exec(); });
    auto *cleanupAction = settingsMenu->addAction(QStringLiteral("Library Cleanup / Storage…"));
    cleanupAction->setObjectName(QStringLiteral("libraryCleanupAction"));
    connect(cleanupAction, &QAction::triggered, this, &MainWindow::configureCleanup);
    auto *midiAction = settingsMenu->addAction(QStringLiteral("MIDI Devices / Mappings…"));
    midiAction->setObjectName(QStringLiteral("midiSettingsAction"));
    connect(midiAction, &QAction::triggered, this, [this] { editMidiSettings(this, midi_, library_); reloadMidiBindings(); });
    auto *mcpAction = settingsMenu->addAction(QStringLiteral("AI / MCP Setup…"));
    mcpAction->setObjectName(QStringLiteral("mcpSetupAction"));
    connect(mcpAction, &QAction::triggered, this, &MainWindow::configureMcp);
    auto *findAction = new QAction(QStringLiteral("Find in Library"), this);
    findAction->setObjectName(QStringLiteral("focusLibrarySearch"));
    findAction->setShortcuts(QKeySequence::Find);
    auto *editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(findAction);
    connect(findAction, &QAction::triggered, this, [this] {
        sidebarTabs_->setCurrentIndex(0);
        search_->setFocus();
        search_->selectAll();
    });
    connect(fileMenu->addAction(QStringLiteral("Keyboard Shortcuts…")), &QAction::triggered, this, &MainWindow::configureShortcuts);
    connect(fileMenu->addAction(QStringLiteral("Quit")), &QAction::triggered, this, &QWidget::close);

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);
    setCentralWidget(root);
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(header(QStringLiteral("WORSHIPCUE"), root));
    mode_ = new QComboBox(root);
    mode_->setObjectName(QStringLiteral("operatorMode"));
    mode_->addItems({QStringLiteral("Show"), QStringLiteral("Edit")});
    mode_->setToolTip(QStringLiteral("Show: click a slide to go live. Edit: double-click to edit text; drag to arrange. Mode changes keep the live output."));
    toolbar->addWidget(mode_);
    returnToLive_ = button(QStringLiteral("Return to Live"), root, QStringLiteral("revealLiveCue"));
    returnToLive_->setToolTip(QStringLiteral("Reveal the saved service and slide corresponding to live output without taking it live again."));
    toolbar->addWidget(returnToLive_);
    connect(returnToLive_, &QPushButton::clicked, this, &MainWindow::revealLiveCue);
    toolbar->addStretch();
    toolbar->addWidget(new QLabel(QStringLiteral("Audience 1"), root));
    audienceTarget_ = new QComboBox(root);
    audienceTarget_->setObjectName(QStringLiteral("audienceTarget"));
    audienceTarget_->setMinimumWidth(155);
    audienceTarget_->setMaximumWidth(230);
    toolbar->addWidget(audienceTarget_);
    toolbar->addWidget(new QLabel(QStringLiteral("Stage 1"), root));
    stageTarget_ = new QComboBox(root);
    stageTarget_->setObjectName(QStringLiteral("stageTarget"));
    stageTarget_->setMinimumWidth(155);
    stageTarget_->setMaximumWidth(230);
    toolbar->addWidget(stageTarget_);
    auto *screensButton = button(QStringLiteral("Screens…"), root, QStringLiteral("outputSettingsButton"));
    toolbar->addWidget(screensButton);
    connect(screensButton, &QPushButton::clicked, screensAction, &QAction::trigger);
    layout->addLayout(toolbar);
    outputInfo_ = new QLabel(root);
    outputInfo_->setObjectName(QStringLiteral("outputStatus"));
    outputInfo_->setWordWrap(true);
    layout->addWidget(outputInfo_);
    auto setOutput = [this](DisplayWindow::Kind kind, QComboBox *combo) {
        QString error;
        if (!outputs_.setTarget(kind, combo->currentData().toString(), &error)) {
            refreshOutputOptions();
            reportError(error);
        }
        focusOperator();
    };
    connect(audienceTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [=] { setOutput(DisplayWindow::Kind::Audience, audienceTarget_); });
    connect(stageTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [=] { setOutput(DisplayWindow::Kind::Stage, stageTarget_); });

    auto *split = new QSplitter(Qt::Horizontal, root);
    operatorSplitter_ = split;
    split->setObjectName(QStringLiteral("operatorSplitter"));
    split->setChildrenCollapsible(false);
    layout->addWidget(split, 1);
    QVBoxLayout *left;
    sidebarTabs_ = new QTabWidget(split);
    sidebarTabs_->setObjectName(QStringLiteral("sidebarTabs"));
    sidebarTabs_->setMinimumWidth(350);
    sidebarTabs_->tabBar()->setAcceptDrops(true);
    sidebarTabs_->tabBar()->setChangeCurrentOnDrag(true);
    sidebarTabs_->tabBar()->setExpanding(false);
    sidebarTabs_->tabBar()->setUsesScrollButtons(true);
    sidebarTabs_->setDocumentMode(true);
    auto *sidebar = column(sidebarTabs_, &left);
    sidebarTabs_->addTab(sidebar, QStringLiteral("Content Library"));
    libraryDestination_ = new QLabel(sidebar);
    libraryDestination_->setObjectName(QStringLiteral("libraryDestination"));
    libraryDestination_->setWordWrap(true);
    left->addWidget(libraryDestination_);
    search_ = new QLineEdit(sidebar);
    search_->setObjectName(QStringLiteral("songSearch"));
    search_->setPlaceholderText(QStringLiteral("Search titles, lyrics, media names…"));
    search_->setClearButtonEnabled(true);
    left->addWidget(search_);
    songs_ = new QListWidget(sidebar);
    songs_->setObjectName(QStringLiteral("songLibrary"));
    songs_->setToolTip(QStringLiteral("Select to view slides. Double-click to add to this service. Right-click for presentation actions."));
    songs_->setContextMenuPolicy(Qt::CustomContextMenu);
    left->addWidget(songs_, 2);
    auto *songRow = new QHBoxLayout;
    auto *newSong = button(QStringLiteral("+ New"), sidebar, QStringLiteral("newSong"));
    auto *edit = button(QStringLiteral("Edit"), sidebar, QStringLiteral("editSong"));
    auto *add = button(QStringLiteral("Add →"), sidebar, QStringLiteral("addSong"));
    songRow->addWidget(newSong);
    songRow->addWidget(edit);
    songRow->addWidget(add);
    left->addLayout(songRow);
    auto *lyricsImport = button(QStringLiteral("Import Song Lyrics…"), sidebar, QStringLiteral("importLyrics"));
    left->addWidget(lyricsImport);
    connect(lyricsImport, &QPushButton::clicked, this, &MainWindow::importLyrics);
    importButton_ = button(QStringLiteral("Import Media…"), sidebar, QStringLiteral("importMedia"));
    left->addWidget(importButton_);
    connect(importButton_, &QPushButton::clicked, this, &MainWindow::chooseMediaFiles);
    auto *backgroundLibrary = button(QStringLiteral("Background Library…"), sidebar, QStringLiteral("backgroundLibrary"));
    backgroundLibrary->setToolTip(QStringLiteral("Choose an included background or import your own, then assign it to the selected presentation."));
    left->addWidget(backgroundLibrary);
    connect(backgroundLibrary, &QPushButton::clicked, this, [this] { assignBackground(selectedSongId()); });
    connect(search_, &QLineEdit::textChanged, this, [this] { reloadSongs(); });
    connect(newSong, &QPushButton::clicked, this, &MainWindow::createSong);
    connect(edit, &QPushButton::clicked, this, &MainWindow::editSong);
    connect(add, &QPushButton::clicked, this, &MainWindow::appendSong);
    connect(songs_, &QListWidget::currentRowChanged, this, [this] { showSelectedLibrary(); });
    connect(songs_, &QListWidget::itemClicked, this, [this] {
        if (!browsingLibrary_) showSelectedLibrary();
    });
    connect(songs_, &QListWidget::itemDoubleClicked, this, [this] { appendSong(); });
    connect(songs_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        if (auto *item = songs_->itemAt(point)) songs_->setCurrentItem(item); else return;
        QMenu menu(this);
        menu.addAction(QStringLiteral("Add to Service"), this, &MainWindow::appendSong);
        menu.addAction(QStringLiteral("Edit Slides…"), this, &MainWindow::editSong);
        menu.addAction(QStringLiteral("Song Sections / Arrangements…"), this, [this] { showSelectedLibrary(); editSongStructure(); });
        menu.addAction(QStringLiteral("Duplicate Song"), this, &MainWindow::duplicateSong);
        menu.addAction(QStringLiteral("Batch Rename Media…"), this, &MainWindow::renameMedia);
        menu.addAction(QStringLiteral("Timing && Transitions…"), this, [this] { configurePresentation(selectedSongId()); });
        menu.addAction(QStringLiteral("Assign Background…"), this, [this] { assignBackground(selectedSongId()); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Delete from Library…"), this, &MainWindow::deleteSong);
        menu.exec(songs_->viewport()->mapToGlobal(point));
    });

    sidebar = column(sidebarTabs_, &left);
    sidebarTabs_->addTab(sidebar, QStringLiteral("Service Playlist"));
    services_ = new QComboBox(sidebar);
    services_->setObjectName(QStringLiteral("savedServices"));
    left->addWidget(services_);
    auto *planRow = new QHBoxLayout;
    auto *newPlan = button(QStringLiteral("New"), sidebar, QStringLiteral("newService"));
    auto *rename = button(QStringLiteral("Rename"), sidebar, QStringLiteral("renameService"));
    auto *copy = button(QStringLiteral("Copy"), sidebar, QStringLiteral("copyService"));
    planRow->addWidget(newPlan);
    planRow->addWidget(rename);
    planRow->addWidget(copy);
    left->addLayout(planRow);
    connect(newPlan, &QPushButton::clicked, this, &MainWindow::createService);
    connect(rename, &QPushButton::clicked, this, &MainWindow::renameService);
    connect(copy, &QPushButton::clicked, this, &MainWindow::duplicateService);
    connect(services_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        currentServiceId_ = services_->currentData().toInt();
        QSettings().setValue(QStringLiteral("service/selected"), currentServiceId_);
        browsingLibrary_ = false;
        browsingMedia_ = false;
        reloadService();
    });
    auto *serviceOrder = new ReorderListWidget(sidebar);
    service_ = serviceOrder;
    serviceOrder->setMediaDropsEnabled(true);
    service_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(serviceOrder, &ReorderListWidget::mediaDropped, this, &MainWindow::appendMedia);
    service_->setObjectName(QStringLiteral("serviceOrder"));
    service_->setDragDropMode(QAbstractItemView::InternalMove);
    service_->setDefaultDropAction(Qt::MoveAction);
    service_->setContextMenuPolicy(Qt::CustomContextMenu);
    service_->setToolTip(QStringLiteral("Select to preview. Double-click to go live. Drag to save a new service order."));
    left->addWidget(service_, 3);
    bulkTransitions_ = new QAction(QStringLiteral("Transitions for Selection…"), this);
    bulkTransitions_->setObjectName(QStringLiteral("bulkTransitions"));
    connect(bulkTransitions_, &QAction::triggered, this, &MainWindow::configureSelectedTransitions);
    undoTransitions_ = new QAction(QStringLiteral("Undo Bulk Transitions"), this);
    undoTransitions_->setObjectName(QStringLiteral("undoBulkTransitions"));
    redoTransitions_ = new QAction(QStringLiteral("Redo Bulk Transitions"), this);
    redoTransitions_->setObjectName(QStringLiteral("redoBulkTransitions"));
    undoTransitions_->setShortcuts(QKeySequence::Undo);
    redoTransitions_->setShortcuts(QKeySequence::Redo);
    for (auto *action : {undoTransitions_, redoTransitions_}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        service_->addAction(action);
        editMenu->addAction(action);
    }
    connect(undoTransitions_, &QAction::triggered, this, [this] { undoTransitions(); });
    connect(redoTransitions_, &QAction::triggered, this, [this] { undoTransitions(true); });
    auto *selectAllService = new QAction(QStringLiteral("Select All Service Items"), this);
    selectAllService->setObjectName(QStringLiteral("selectAllServiceItems"));
    selectAllService->setShortcuts(QKeySequence::SelectAll);
    selectAllService->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    service_->addAction(selectAllService);
    editMenu->addAction(selectAllService);
    editMenu->addAction(bulkTransitions_);
    connect(selectAllService, &QAction::triggered, service_, &QListWidget::selectAll);
    connect(service_, &QListWidget::itemSelectionChanged, this, &MainWindow::updateTransitionActions);
    updateTransitionActions();
    connect(service_, &QListWidget::currentRowChanged, this, [this] { showSelectedService(); });
    connect(service_, &QListWidget::itemClicked, this, [this] {
        if (browsingLibrary_) showSelectedService();
    });
    connect(service_, &QListWidget::itemDoubleClicked, this, [this] { goLive(); });
    connect(serviceOrder, &ReorderListWidget::orderChanged, this, &MainWindow::persistServiceOrder);
    connect(service_, &QListWidget::customContextMenuRequested, this, [this, selectAllService](const QPoint &point) {
        if (auto *item = service_->itemAt(point)) {
            if (item->isSelected()) service_->setCurrentItem(item, QItemSelectionModel::NoUpdate);
            else service_->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
        } else return;
        QMenu menu(this);
        menu.addAction(QStringLiteral("Take Selected Slide Live"), this, &MainWindow::goLive);
        menu.addAction(bulkTransitions_);
        menu.addAction(selectAllService);
        menu.addAction(undoTransitions_);
        menu.addAction(redoTransitions_);
        menu.addAction(QStringLiteral("Timing && Transitions for Current Presentation…"), this, [this] {
            const int songId = service_->currentItem()->data(Qt::UserRole + 1).toInt();
            if (songId) configurePresentation(songId);
            else {
                service_->setCurrentItem(service_->currentItem(), QItemSelectionModel::ClearAndSelect);
                configureSelectedTransitions();
            }
        });
        menu.addAction(QStringLiteral("Assign Background…"), this, [this] {
            assignBackground(service_->currentItem()->data(Qt::UserRole + 1).toInt());
        });
        menu.addAction(QStringLiteral("Duplicate Service Item"), this, &MainWindow::duplicateServiceEntry);
        menu.addAction(QStringLiteral("Move Up"), this, [this] { moveService(-1); });
        menu.addAction(QStringLiteral("Move Down"), this, [this] { moveService(1); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Remove from Service"), this, &MainWindow::removeServiceEntry);
        menu.exec(service_->viewport()->mapToGlobal(point));
    });
    auto *orderRow = new QHBoxLayout;
    auto *up = button(QStringLiteral("↑"), sidebar);
    auto *down = button(QStringLiteral("↓"), sidebar);
    auto *duplicate = button(QStringLiteral("Duplicate"), sidebar, QStringLiteral("duplicateEntry"));
    auto *remove = button(QStringLiteral("Remove"), sidebar, QStringLiteral("removeEntry"));
    orderRow->addWidget(up);
    orderRow->addWidget(down);
    orderRow->addWidget(duplicate);
    orderRow->addWidget(remove);
    left->addLayout(orderRow);
    connect(up, &QPushButton::clicked, this, [this] { moveService(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveService(1); });
    connect(duplicate, &QPushButton::clicked, this, &MainWindow::duplicateServiceEntry);
    connect(remove, &QPushButton::clicked, this, &MainWindow::removeServiceEntry);
    auto *bulkButton = button(QStringLiteral("Transitions for Selection…"), sidebar, QStringLiteral("bulkTransitionsButton"));
    left->addWidget(bulkButton);
    connect(bulkButton, &QPushButton::clicked, bulkTransitions_, &QAction::trigger);
    connect(bulkTransitions_, &QAction::changed, bulkButton, [this, bulkButton] { bulkButton->setEnabled(bulkTransitions_->isEnabled()); });
    bulkButton->setEnabled(bulkTransitions_->isEnabled());
    saveInfo_ = new QLabel(QStringLiteral("Changes save automatically"), sidebar);
    saveInfo_->setObjectName(QStringLiteral("saveStatus"));
    left->addWidget(saveInfo_);

    sidebar = column(sidebarTabs_, &left);
    sidebarTabs_->addTab(sidebar, QStringLiteral("Media Library"));
    mediaSearch_ = new QLineEdit(sidebar);
    mediaSearch_->setObjectName(QStringLiteral("mediaSearch"));
    mediaSearch_->setPlaceholderText(QStringLiteral("Search media and folders"));
    left->addWidget(mediaSearch_);
    mediaFolders_ = new QComboBox(sidebar);
    mediaFolders_->setObjectName(QStringLiteral("mediaFolders"));
    mediaFolders_->addItem(QStringLiteral("All Media"));
    left->addWidget(mediaFolders_);
    mediaAssets_ = new MediaAssetListWidget(sidebar);
    mediaAssets_->setObjectName(QStringLiteral("mediaLibrary"));
    mediaAssets_->setContextMenuPolicy(Qt::CustomContextMenu);
    mediaAssets_->setToolTip(QStringLiteral("Select to inspect. Drag to the Service Playlist tab to add a shared reference."));
    left->addWidget(mediaAssets_, 1);
    mediaDetails_ = new QLabel(sidebar);
    mediaDetails_->setObjectName(QStringLiteral("mediaDetails"));
    mediaDetails_->setWordWrap(true);
    mediaDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    left->addWidget(mediaDetails_);
    mediaImportButton_ = button(QStringLiteral("Import Media…"), sidebar, QStringLiteral("importMediaAssets"));
    left->addWidget(mediaImportButton_);
    connect(mediaImportButton_, &QPushButton::clicked, this, &MainWindow::chooseMediaFiles);
    auto *mediaTools = new QHBoxLayout;
    auto *previewMedia = button(QStringLiteral("Preview…"), sidebar, QStringLiteral("previewMediaAsset"));
    auto *addMedia = button(QStringLiteral("Add to Service"), sidebar, QStringLiteral("appendMediaAsset"));
    mediaTools->addWidget(previewMedia);
    mediaTools->addWidget(addMedia);
    left->addLayout(mediaTools);
    auto *organizeMedia = button(QStringLiteral("Rename / Organize…"), sidebar, QStringLiteral("editMediaAsset"));
    left->addWidget(organizeMedia);
    connect(organizeMedia, &QPushButton::clicked, this, &MainWindow::editMediaAsset);
    connect(previewMedia, &QPushButton::clicked, this, &MainWindow::previewMediaAsset);
    connect(addMedia, &QPushButton::clicked, this, [this] { appendMedia(); });
    connect(mediaSearch_, &QLineEdit::textChanged, this, [this] { reloadMedia(); });
    connect(mediaFolders_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { reloadMedia(); });
    connect(mediaAssets_, &QListWidget::currentRowChanged, this, &MainWindow::showSelectedMedia);
    connect(mediaAssets_, &QListWidget::itemClicked, this, &MainWindow::showSelectedMedia);
    connect(mediaAssets_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        if (auto *item = mediaAssets_->itemAt(point)) mediaAssets_->setCurrentItem(item); else return;
        QMenu menu(this);
        menu.addAction(QStringLiteral("Preview…"), this, &MainWindow::previewMediaAsset);
        menu.addAction(QStringLiteral("Take Live"), this, [this] { showSelectedMedia(); goLive(); });
        menu.addAction(QStringLiteral("Add to Service"), this, [this] { appendMedia(); });
        menu.addAction(QStringLiteral("Rename / Organize…"), this, &MainWindow::editMediaAsset);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Delete from Media Library…"), this, &MainWindow::deleteMediaAsset);
        menu.exec(mediaAssets_->viewport()->mapToGlobal(point));
    });

    QVBoxLayout *center;
    auto *workspace = column(split, &center);
    selectedInfo_ = header(QStringLiteral("SLIDES"), workspace);
    selectedInfo_->setObjectName(QStringLiteral("selectedInfo"));
    center->addWidget(selectedInfo_);
    auto *hint = new QLabel(QStringLiteral("Show: click to go live • Edit: double-click to edit text, drag to arrange"), workspace);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    center->addWidget(hint);
    auto *sectionTools = new QHBoxLayout;
    auto *sections = button(QStringLiteral("Song Sections / Arrangements…"), workspace, QStringLiteral("editSongStructure"));
    songSections_ = sections;
    sectionTools->addWidget(sections);
    auto *looks = button(QStringLiteral("Looks / Reflow…"), workspace, QStringLiteral("configureLooks"));
    looksButton_ = looks;
    sectionTools->addWidget(looks);
    connect(looks, &QPushButton::clicked, this, &MainWindow::configureLooks);
    auto *midiSlide = button(QStringLiteral("Slide MIDI…"), workspace, QStringLiteral("configureSlideMidi"));
    sectionTools->addWidget(midiSlide);
    connect(midiSlide, &QPushButton::clicked, this, &MainWindow::configureMidiSlide);
    center->addLayout(sectionTools);
    connect(sections, &QPushButton::clicked, this, &MainWindow::editSongStructure);
    auto *presentationTools = new QHBoxLayout;
    editSlide_ = button(QStringLiteral("Edit Slide Text…"), workspace, QStringLiteral("editSlideText"));
    editSlide_->setEnabled(false);
    presentationTools->addWidget(editSlide_);
    auto *timingButton = button(QStringLiteral("Timing && Transitions…"), workspace, QStringLiteral("presentationSettings"));
    presentationSettings_ = timingButton;
    presentationTools->addWidget(timingButton);
    center->addLayout(presentationTools);
    auto *viewTools = new QHBoxLayout;
    slideViewMode_ = new QComboBox(workspace);
    slideViewMode_->setObjectName(QStringLiteral("slideViewMode"));
    slideViewMode_->addItems({QStringLiteral("Grid"), QStringLiteral("Text")});
    slideViewMode_->setAccessibleName(QStringLiteral("Slide view"));
    slideViewMode_->setToolTip(QStringLiteral("Grid shows audience thumbnails. Text shows readable lyrics. Changing views keeps live output."));
    viewTools->addWidget(slideViewMode_);
    viewTools->addWidget(new QLabel(QStringLiteral("Slide size"), workspace));
    slideSize_ = new QSlider(Qt::Horizontal, workspace);
    slideSize_->setObjectName(QStringLiteral("slideSize"));
    slideSize_->setRange(160, 320);
    slideSize_->setValue(224);
    slideSize_->setAccessibleName(QStringLiteral("Slide size"));
    slideSize_->setToolTip(QStringLiteral("Adjust thumbnail size in Grid view or lyric size in Text view."));
    viewTools->addWidget(slideSize_, 1);
    center->addLayout(viewTools);
    connect(editSlide_, &QPushButton::clicked, this, &MainWindow::editSelectedSlide);
    connect(timingButton, &QPushButton::clicked, this, [this] {
        configurePresentation(workspaceSongId());
    });
    selectedTimingInfo_ = new QLabel(workspace);
    selectedTimingInfo_->setObjectName(QStringLiteral("selectedTimingInfo"));
    selectedTimingInfo_->setToolTip(QStringLiteral("Saved presentation settings. Take a slide live to apply changes."));
    selectedTimingInfo_->setWordWrap(true);
    center->addWidget(selectedTimingInfo_);
    auto *slideOrder = new ReorderListWidget(workspace);
    slides_ = slideOrder;
    slides_->setObjectName(QStringLiteral("slideWorkspace"));
    gridDelegate_ = new SlideGridDelegate(slides_);
    textDelegate_ = new SlideTextDelegate(slides_);
    slides_->setViewMode(QListView::IconMode);
    slides_->setIconSize(QSize(256, 144));
    slides_->setGridSize(QSize(280, 182));
    slides_->setResizeMode(QListView::Adjust);
    slides_->setMovement(QListView::Snap);
    slides_->setWrapping(true);
    slides_->setWordWrap(false);
    slides_->setSpacing(6);
    slides_->setDragDropMode(QAbstractItemView::NoDragDrop);
    slides_->setDefaultDropAction(Qt::MoveAction);
    slides_->setContextMenuPolicy(Qt::CustomContextMenu);
    center->addWidget(slides_, 1);
    connect(slideViewMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateSlideView);
    connect(slideSize_, &QSlider::valueChanged, this, &MainWindow::updateSlideView);
    connect(slides_, &QListWidget::currentRowChanged, this, &MainWindow::showSelectedSlide);
    connect(slides_, &QListWidget::itemClicked, this, [this] { if (mode_->currentIndex() == 0) goLive(); });
    connect(slides_, &QListWidget::itemDoubleClicked, this, [this] { if (mode_->currentIndex() == 1) editSelectedSlide(); });
    connect(slideOrder, &ReorderListWidget::orderChanged, this, &MainWindow::persistSlideOrder);
    connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateSlideDragMode);
    connect(slides_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        if (auto *item = slides_->itemAt(point)) slides_->setCurrentItem(item); else return;
        QMenu menu(this);
        menu.addAction(QStringLiteral("Edit Slide Text…"), this, &MainWindow::editSelectedSlide);
        menu.addAction(QStringLiteral("Looks / Reflow…"), this, &MainWindow::configureLooks);
        menu.addAction(QStringLiteral("MIDI Actions / Input Trigger…"), this, &MainWindow::configureMidiSlide);
        menu.addAction(QStringLiteral("Take Live"), this, &MainWindow::goLive);
        menu.addAction(QStringLiteral("Move Earlier in Service Arrangement"), this, [this] { moveSlide(-1); })->setEnabled(!browsingLibrary_);
        menu.addAction(QStringLiteral("Move Later in Service Arrangement"), this, [this] { moveSlide(1); })->setEnabled(!browsingLibrary_);
        menu.addAction(QStringLiteral("Reset to Song Order"), this, &MainWindow::resetSlideOrder)->setEnabled(!browsingLibrary_);
        menu.addAction(QStringLiteral("Batch Rename Presentation Media…"), this, [this] {
            renameMediaPresentation(workspaceSongId());
        });
        menu.exec(slides_->viewport()->mapToGlobal(point));
    });
    auto *globalTransitions = new QWidget(workspace);
    globalTransitions->setObjectName(QStringLiteral("globalTransitionsBar"));
    auto *globalRow = new QHBoxLayout(globalTransitions);
    globalRow->setContentsMargins(0, 4, 0, 4);
    globalRow->addWidget(new QLabel(QStringLiteral("Global transition"), globalTransitions));
    globalTransitionType_ = new QComboBox(globalTransitions);
    globalTransitionType_->setObjectName(QStringLiteral("globalTransitionType"));
    globalTransitionType_->addItem(QStringLiteral("Cut"), int(SlideTransitionType::Cut));
    globalTransitionType_->addItem(QStringLiteral("Fade"), int(SlideTransitionType::Fade));
    globalTransitionType_->addItem(QStringLiteral("Slide Left"), int(SlideTransitionType::SlideLeft));
    globalTransitionType_->addItem(QStringLiteral("Slide Right"), int(SlideTransitionType::SlideRight));
    globalRow->addWidget(globalTransitionType_, 1);
    globalRow->addWidget(new QLabel(QStringLiteral("Time"), globalTransitions));
    globalTransitionTime_ = new QDoubleSpinBox(globalTransitions);
    globalTransitionTime_->setObjectName(QStringLiteral("globalTransitionTime"));
    globalTransitionTime_->setRange(0, 10);
    globalTransitionTime_->setDecimals(2);
    globalTransitionTime_->setSingleStep(.1);
    globalTransitionTime_->setSuffix(QStringLiteral(" s"));
    globalTransitionTime_->setKeyboardTracking(false);
    globalRow->addWidget(globalTransitionTime_);
    globalTransitions->setToolTip(QStringLiteral("Default for all presentations. Presentation overrides take priority. Changes apply to the next cue or audience output control, including in a running service."));
    updateProgramTransitionUi();
    connect(globalTransitionType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        programSettings_.transition = SlideTransitionType(globalTransitionType_->currentData().toInt());
        saveProgramTransitions();
    });
    connect(globalTransitionTime_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double seconds) {
        programSettings_.transitionDurationMs = qRound(seconds * 1000);
        saveProgramTransitions();
    });
    center->addWidget(globalTransitions);
    auto *take = button(QStringLiteral("TAKE SELECTED SLIDE LIVE"), workspace, QStringLiteral("takeLive"));
    center->addWidget(take);
    connect(take, &QPushButton::clicked, this, &MainWindow::goLive);
    auto *navigationHint = new QLabel(QStringLiteral("↑ / ← Previous   •   ↓ / → / Space Next   •   Esc Operator focus"), workspace);
    navigationHint->setObjectName(QStringLiteral("hint"));
    navigationHint->setWordWrap(true);
    center->addWidget(navigationHint);

    QVBoxLayout *right;
    auto *monitor = column(split, &right);
    right->setSpacing(4);
    monitor->setMinimumWidth(280);
    liveInfo_ = header(QStringLiteral("LIVE • Ready"), monitor);
    liveInfo_->setObjectName(QStringLiteral("liveInfo"));
    right->addWidget(liveInfo_);
    livePreview_ = new SlidePreview(monitor, false);
    livePreview_->setAudienceSource(&outputs_.audience());
    livePreview_->setObjectName(QStringLiteral("livePreview"));
    right->addWidget(livePreview_, 2);
    timingStatus_ = new QLabel(QStringLiteral("Auto-advance: Off"), monitor);
    timingStatus_->setObjectName(QStringLiteral("autoAdvanceStatus"));
    timingStatus_->setWordWrap(true);
    right->addWidget(timingStatus_);
    pauseAutoAdvance_ = button(QStringLiteral("Pause Auto-Advance"), monitor, QStringLiteral("pauseAutoAdvance"));
    pauseAutoAdvance_->setEnabled(false);
    right->addWidget(pauseAutoAdvance_);
    connect(pauseAutoAdvance_, &QPushButton::clicked, &controller_, &PresentationController::toggleAutoAdvancePaused);
    previewTabs_ = new QTabWidget(monitor);
    previewTabs_->setObjectName(QStringLiteral("previewTabs"));
    auto *nextPane = new QWidget(previewTabs_);
    auto *nextLayout = new QVBoxLayout(nextPane);
    nextLayout->setContentsMargins(0, 0, 0, 0);
    nextInfo_ = header(QStringLiteral("UP NEXT"), nextPane);
    nextInfo_->setObjectName(QStringLiteral("nextInfo"));
    nextLayout->addWidget(nextInfo_);
    nextPreview_ = new SlidePreview(nextPane);
    nextPreview_->setObjectName(QStringLiteral("nextPreview"));
    nextLayout->addWidget(nextPreview_, 1);
    previewTabs_->addTab(nextPane, QStringLiteral("Up Next"));
    selectedPreview_ = new SlidePreview(previewTabs_);
    selectedPreview_->setObjectName(QStringLiteral("selectedPreview"));
    previewTabs_->addTab(selectedPreview_, QStringLiteral("Selected"));
    previewTabs_->setMinimumHeight(nextLayout->minimumSize().height() + previewTabs_->tabBar()->sizeHint().height() + 4);
    right->addWidget(previewTabs_, 2);
    auto *transport = new QHBoxLayout;
    auto *previous = button(QStringLiteral("◀ Previous"), monitor, QStringLiteral("previousSlide"));
    auto *next = button(QStringLiteral("Next ▶"), monitor, QStringLiteral("nextSlide"));
    transport->addWidget(previous);
    transport->addWidget(next);
    right->addLayout(transport);
    connect(previous, &QPushButton::clicked, this, &MainWindow::rewind);
    connect(next, &QPushButton::clicked, this, &MainWindow::advance);
    mediaStatus_ = new QLabel(monitor);
    mediaStatus_->setObjectName(QStringLiteral("mediaStatus"));
    mediaStatus_->setWordWrap(true);
    right->addWidget(mediaStatus_);
    auto *videoTransport = new QHBoxLayout;
    pauseVideo_ = button(QStringLiteral("Pause Video"), monitor, QStringLiteral("pauseVideo"));
    restartVideo_ = button(QStringLiteral("Restart"), monitor, QStringLiteral("restartVideo"));
    muteVideo_ = button(QStringLiteral("Mute"), monitor, QStringLiteral("muteVideo"));
    videoTransport->addWidget(pauseVideo_);
    videoTransport->addWidget(restartVideo_);
    videoTransport->addWidget(muteVideo_);
    right->addLayout(videoTransport);
    connect(pauseVideo_, &QPushButton::clicked, &playback_, &MediaPlayback::togglePause);
    connect(restartVideo_, &QPushButton::clicked, &playback_, &MediaPlayback::restart);
    connect(muteVideo_, &QPushButton::clicked, &playback_, &MediaPlayback::toggleMute);
    auto *safety = new QHBoxLayout;
    blackoutButton_ = button(QStringLiteral("Blackout"), monitor, QStringLiteral("blackout"));
    clearButton_ = button(QStringLiteral("Clear Text"), monitor, QStringLiteral("clearText"));
    blackoutButton_->setCheckable(true);
    clearButton_->setCheckable(true);
    blackoutButton_->setToolTip(QStringLiteral("Hide audience content and mute video audio. The video playhead continues; Stage stays visible."));
    clearButton_->setToolTip(QStringLiteral("Hide audience text while keeping the background, media, and audio. Stage stays visible."));
    safety->addWidget(blackoutButton_);
    safety->addWidget(clearButton_);
    right->addLayout(safety);
    connect(blackoutButton_, &QPushButton::clicked, &controller_, &PresentationController::toggleBlackout);
    connect(clearButton_, &QPushButton::clicked, &controller_, &PresentationController::toggleClear);
    auto *restore = button(QStringLiteral("Restore Audience"), monitor, QStringLiteral("restoreAudience"));
    restore->setToolTip(QStringLiteral("Remove Blackout or Clear Text without changing the live slide."));
    right->addWidget(restore);
    connect(restore, &QPushButton::clicked, &controller_, &PresentationController::restoreOutput);
    // Keep audience controls immediately below Live even on compact screens.
    right->removeItem(safety);
    right->removeWidget(restore);
    right->insertLayout(2, safety);
    right->insertWidget(3, restore);
    split->setSizes({300, 710, 350});
    const QSettings preferences;
    split->restoreState(preferences.value(QStringLiteral("workspace/panels")).toByteArray());
    sidebarTabs_->setCurrentIndex(qBound(0, preferences.value(QStringLiteral("workspace/sidebarView"), 0).toInt(), 2));
    slideViewMode_->setCurrentIndex(qBound(0, preferences.value(QStringLiteral("workspace/slideView"), 0).toInt(), 1));
    slideSize_->setValue(preferences.value(QStringLiteral("workspace/slideSize"), 224).toInt());
    previewTabs_->setCurrentIndex(qBound(0, preferences.value(QStringLiteral("workspace/previewTab"), 0).toInt(), 1));
    updateSlideView();
    statusBar();
}

bool MainWindow::reloadSongs(bool reportFailure) {
    const int selected = selectedSongId();
    const int slide = slides_->currentRow();
    const auto songs = library_.songs(search_->text().trimmed());
    if (!library_.lastError().isEmpty()) {
        if (reportFailure) reportError(library_.lastError());
        return false;
    }
    const QSignalBlocker block(songs_);
    songs_->clear();
    for (const auto &song : songs) {
        auto *item = new QListWidgetItem(song.title, songs_);
        item->setData(Qt::UserRole, song.id);
        if (song.id == selected) songs_->setCurrentItem(item);
    }
    if (!songs_->currentItem() && songs_->count()) songs_->setCurrentRow(0);
    if (browsingLibrary_ && !browsingMedia_) return showSelectedPresentation(selectedSongId() == selected ? slide : 0, reportFailure);
    return true;
}

bool MainWindow::reloadServices(int selectServiceId, int selectEntryId, int selectSlide, bool reportFailure) {
    if (!selectServiceId) selectServiceId = currentServiceId_;
    const auto plans = library_.services();
    if (!library_.lastError().isEmpty()) {
        if (reportFailure) reportError(library_.lastError());
        return false;
    }
    const QSignalBlocker block(services_);
    services_->clear();
    for (const auto &plan : plans) services_->addItem(plan.name, plan.id);
    int index = services_->findData(selectServiceId);
    if (index < 0 && services_->count()) index = 0;
    services_->setCurrentIndex(index);
    currentServiceId_ = services_->currentData().toInt();
    QSettings().setValue(QStringLiteral("service/selected"), currentServiceId_);
    return reloadService(selectEntryId, selectSlide, reportFailure);
}

void MainWindow::refreshExternalChanges() {
    // Keep editor drafts and in-progress drag operations stable until the operator finishes.
    if (rebuilding_ || QApplication::activeModalWidget() || QApplication::activePopupWidget()
        || QApplication::mouseButtons() != Qt::NoButton) return;
    const qint64 version = library_.dataVersion();
    if (version < 0 || version == externalDataVersion_) return;
    const int entryId = selectedEntryId();
    const int slide = slides_->currentRow();
    const int songScroll = songs_->verticalScrollBar()->value();
    const int serviceScroll = service_->verticalScrollBar()->value();
    const int slideScroll = slides_->verticalScrollBar()->value();
    if (!reloadSongs(false) || !reloadMedia(false) || !reloadServices(currentServiceId_, entryId, slide, false)) {
        statusBar()->showMessage(QStringLiteral("External library update could not be loaded. Retrying automatically."), 5000);
        return;
    }
    songs_->verticalScrollBar()->setValue(songScroll);
    service_->verticalScrollBar()->setValue(serviceScroll);
    slides_->verticalScrollBar()->setValue(slideScroll);
    reloadMidiBindings();
    externalDataVersion_ = version;
    statusBar()->showMessage(QStringLiteral("Library and saved services updated by an external connection."), 8000);
}

bool MainWindow::reloadService(int selectEntryId, int selectSlide, bool reportFailure) {
    libraryDestination_->setText(QStringLiteral("Adding to: %1").arg(services_->currentText()));
    QSet<int> selectedIds;
    for (auto *item : service_->selectedItems()) selectedIds.insert(item->data(Qt::UserRole).toInt());
    if (selectEntryId && !selectedIds.contains(selectEntryId)) selectedIds.clear();
    if (!selectEntryId) selectEntryId = selectedEntryId();
    const auto entries = library_.service(currentServiceId_);
    if (!library_.lastError().isEmpty()) {
        if (reportFailure) reportError(library_.lastError());
        return false;
    }
    QVector<Song> songs;
    for (const auto &entry : entries) {
        songs.append(library_.entrySource(entry.id));
        if (!library_.lastError().isEmpty()) {
            if (reportFailure) reportError(library_.lastError());
            return false;
        }
    }
    const QSignalBlocker block(service_);
    rebuilding_ = true;
    service_->clear();
    int row = -1;
    for (int i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        const Song &song = songs.at(i);
        auto *item = new QListWidgetItem(song.title, service_);
        item->setData(Qt::UserRole, entry.id);
        item->setData(Qt::UserRole + 1, entry.songId);
        item->setData(Qt::UserRole + 2, song.title);
        item->setData(Qt::UserRole + 3, entry.mediaAssetId);
        if (entry.mediaAssetId) item->setToolTip(QStringLiteral("Shared Media Library item"));
        if (entry.id == selectEntryId) row = service_->count() - 1;
    }
    if (row < 0 && service_->count()) row = 0;
    service_->setCurrentRow(row, QItemSelectionModel::NoUpdate);
    for (int i = 0; i < service_->count(); ++i)
        service_->item(i)->setSelected(selectedIds.isEmpty() ? i == row : selectedIds.contains(service_->item(i)->data(Qt::UserRole).toInt()));
    if (service_->selectedItems().isEmpty() && row >= 0) service_->item(row)->setSelected(true);
    updateTransitionActions();
    rebuilding_ = false;
    if (!showSelectedPresentation(selectSlide, reportFailure)) return false;
    updateLiveUi();
    return true;
}

int MainWindow::selectedMediaAssetId() const {
    return mediaAssets_->currentItem() ? mediaAssets_->currentItem()->data(Qt::UserRole).toInt() : 0;
}

Song MainWindow::workspaceSource() const {
    if (!browsingLibrary_) return library_.entrySource(selectedEntryId());
    if (!browsingMedia_) return library_.song(selectedSongId());
    const auto asset = library_.mediaAsset(selectedMediaAssetId());
    Song source;
    if (!asset.id) return source;
    source.mediaAssetId = asset.id;
    source.title = asset.media.name;
    source.slides = {QStringLiteral("")};
    source.richText = {QStringLiteral("")};
    source.media = {asset.media};
    source.slideIds = {0};
    source.midiActions = {library_.midiActions(MidiOwnerType::Asset, asset.id)};
    return source;
}

bool MainWindow::reloadMedia(bool reportFailure) {
    const auto assets = library_.mediaAssets(mediaSearch_->text());
    if (!library_.lastError().isEmpty()) {
        if (reportFailure) reportError(library_.lastError());
        return false;
    }
    const int selected = selectedMediaAssetId();
    const QString folder = mediaFolders_->currentData().toString();
    const QSignalBlocker listBlock(mediaAssets_);
    const QSignalBlocker folderBlock(mediaFolders_);
    QStringList folders;
    for (const auto &asset : assets)
        if (!asset.folder.isEmpty() && !folders.contains(asset.folder)) folders.append(asset.folder);
    folders.sort(Qt::CaseInsensitive);
    mediaFolders_->clear();
    mediaFolders_->addItem(QStringLiteral("All Media"), QStringLiteral(""));
    for (const auto &name : folders) mediaFolders_->addItem(name, name);
    mediaFolders_->setCurrentIndex(qMax(0, mediaFolders_->findData(folder)));
    const QString selectedFolder = mediaFolders_->currentData().toString();
    mediaAssets_->clear();
    for (const auto &asset : assets) {
        if (!selectedFolder.isEmpty() && asset.folder != selectedFolder) continue;
        auto *item = new QListWidgetItem(asset.media.name, mediaAssets_);
        item->setData(Qt::UserRole, asset.id);
        item->setToolTip(asset.media.path);
        if (asset.id == selected) mediaAssets_->setCurrentItem(item);
    }
    if (!mediaAssets_->currentItem() && mediaAssets_->count()) mediaAssets_->setCurrentRow(0);
    if (browsingMedia_) showSelectedMedia();
    return true;
}

void MainWindow::showSelectedMedia() {
    if (rebuilding_) return;
    browsingLibrary_ = true;
    browsingMedia_ = true;
    const auto asset = library_.mediaAsset(selectedMediaAssetId());
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    mediaDetails_->clear();
    if (asset.id) {
        const QString path = resolveMedia(asset.media).path;
        const QFileInfo file(path);
        const QString kind = asset.media.kind == MediaKind::Image ? QStringLiteral("Image")
            : asset.media.kind == MediaKind::Video ? QStringLiteral("Video") : QStringLiteral("Audio");
        QStringList details{kind, asset.folder.isEmpty() ? QStringLiteral("Unfiled") : asset.folder};
        if (file.isFile()) {
            details << QStringLiteral("%1 bytes").arg(file.size());
            if (asset.media.kind == MediaKind::Image) {
                QImageReader reader(path);
                const auto size = reader.size();
                if (size.isValid()) details << QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
            }
            details << QStringLiteral("File modified %1").arg(file.lastModified().toString(Qt::ISODate));
        } else details << QStringLiteral("File missing");
        details << (asset.importedAt > 0
            ? QStringLiteral("Imported %1").arg(QDateTime::fromSecsSinceEpoch(asset.importedAt).toString(Qt::ISODate))
            : QStringLiteral("Import date unknown (legacy media)"));
        details << asset.media.path;
        mediaDetails_->setText(details.join(QLatin1Char('\n')));
    }
    showSelectedPresentation();
}

void MainWindow::editMediaAsset() {
    const auto asset = library_.mediaAsset(selectedMediaAssetId());
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!asset.id) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("mediaAssetEditor"));
    dialog.setWindowTitle(QStringLiteral("Rename / Organize Media"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(asset.media.name, &dialog);
    name->setObjectName(QStringLiteral("mediaAssetName"));
    name->setMaxLength(256);
    auto *folder = new QComboBox(&dialog);
    folder->setObjectName(QStringLiteral("mediaAssetFolder"));
    folder->setEditable(true);
    folder->addItem(QStringLiteral(""));
    const auto assets = library_.mediaAssets();
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    for (const auto &item : assets)
        if (!item.folder.isEmpty() && folder->findText(item.folder) < 0) folder->addItem(item.folder);
    folder->setCurrentText(asset.folder);
    folder->lineEdit()->setMaxLength(256);
    auto *loop = new QCheckBox(QStringLiteral("Loop playback"), &dialog);
    loop->setObjectName(QStringLiteral("mediaAssetLoop"));
    loop->setEnabled(asset.media.kind != MediaKind::Image);
    loop->setChecked(asset.media.loop);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Folder"), folder);
    form->addRow(loop);
    layout->addLayout(form);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(actions, &QDialogButtonBox::accepted, &dialog, [&] {
        QString error;
        if (!library_.updateMediaAsset(asset.id, name->text(), folder->currentText(), loop->isChecked(), &error)) {
            QMessageBox::warning(&dialog, QStringLiteral("Cannot save media"), error);
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    reloadMedia();
    reloadService(selectedEntryId(), slides_->currentRow());
    markSaved();
}

void MainWindow::deleteMediaAsset() {
    const auto asset = library_.mediaAsset(selectedMediaAssetId());
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!asset.id) return;
    if (QMessageBox::question(this, QStringLiteral("Delete media item?"),
        QStringLiteral("Remove “%1” from the Media Library? Items referenced by presentations or saved services are protected. "
                       "The managed file is retained for recovery.").arg(asset.media.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    if (!library_.deleteMediaAsset(asset.id, &error)) return reportError(error);
    reloadMedia();
    markSaved();
}

void MainWindow::appendMedia(int id, int beforeEntryId) {
    if (!id) id = selectedMediaAssetId();
    if (!id || !currentServiceId_) return;
    QString error;
    const int selected = library_.appendMediaToService(currentServiceId_, id, &error, beforeEntryId);
    if (!selected) return reportError(error);
    browsingLibrary_ = false;
    browsingMedia_ = false;
    sidebarTabs_->setCurrentIndex(1);
    reloadService(selected);
    markSaved();
}

void MainWindow::previewMediaAsset() {
    const auto asset = library_.mediaAsset(selectedMediaAssetId());
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!asset.id) return;
    const QString previewOwner = cleanupLeaseOwner_ + "-preview";
    CleanupProtection previewProtection; previewProtection.assets.insert(asset.id); previewProtection.paths.insert(asset.media.path);
    QString previewError;
    if (!library_.replaceCleanupLease(previewOwner, previewProtection, &previewError)) return reportError(previewError);
    QTimer previewLease;
    previewLease.setInterval(10000);
    connect(&previewLease, &QTimer::timeout, this, [this, previewOwner, previewProtection] { QString error; library_.replaceCleanupLease(previewOwner, previewProtection, &error); });
    previewLease.start();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("mediaAssetPreview"));
    dialog.setWindowTitle(QStringLiteral("Preview — %1").arg(asset.media.name));
    dialog.resize(720, 510);
    PresentationController previewController;
    MediaPlayback previewPlayback(&previewController);
    auto *layout = new QVBoxLayout(&dialog);
    auto *preview = new SlidePreview(&dialog);
    layout->addWidget(preview, 1);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    const auto media = resolveMedia(asset.media);
    preview->setSlide(previewState(QStringLiteral(""), media), {}, mediaThumbnail(media));
    auto *play = new QPushButton(QStringLiteral("Play Preview"), &dialog);
    play->setObjectName(QStringLiteral("playMediaPreview"));
    play->setVisible(media.kind != MediaKind::Image);
    layout->addWidget(play);
    connect(play, &QPushButton::clicked, &dialog, [&] {
        QString usageError; library_.recordUsage(0, {asset.id}, {asset.media.path}, &usageError);
        if (previewController.state().index < 0)
            previewController.takeLive(PresentationItem{0, asset.media.name, {QStringLiteral("")}, {media}});
        else previewPlayback.togglePause();
    });
    connect(&previewPlayback, &MediaPlayback::frameChanged, &dialog, [&] {
        preview->setSlide(previewController.state(), {}, previewPlayback.frame());
    });
    connect(&previewPlayback, &MediaPlayback::statusChanged, &dialog, [&] {
        status->setText(previewPlayback.status());
        if (previewController.state().index >= 0)
            play->setText(previewPlayback.isPlaying() ? QStringLiteral("Pause Preview") : QStringLiteral("Play Preview"));
    });
    if (media.kind == MediaKind::Image) {
        library_.recordUsage(0, {asset.id}, {asset.media.path}, &previewError);
        previewController.takeLive(PresentationItem{0, asset.media.name, {QStringLiteral("")}, {media}});
    }
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
    library_.clearCleanupLease(previewOwner);
}

int MainWindow::selectedSongId() const {
    return songs_->currentItem() ? songs_->currentItem()->data(Qt::UserRole).toInt() : 0;
}
int MainWindow::workspaceSongId() const {
    if (browsingMedia_) return 0;
    return browsingLibrary_ ? selectedSongId()
        : service_->currentItem() ? service_->currentItem()->data(Qt::UserRole + 1).toInt() : 0;
}
int MainWindow::selectedEntryId() const {
    return service_->currentItem() ? service_->currentItem()->data(Qt::UserRole).toInt() : 0;
}
QStringList MainWindow::selectedSlides() const {
    QStringList texts;
    for (int i = 0; i < slides_->count(); ++i) texts.append(slides_->item(i)->data(Qt::UserRole + 1).toString());
    return texts;
}
QStringList MainWindow::selectedRichText() const {
    QStringList html;
    for (int i = 0; i < slides_->count(); ++i) html.append(slides_->item(i)->data(Qt::UserRole + 4).toString());
    return html;
}
QVector<SlideLook> MainWindow::selectedLooks() const {
    QVector<SlideLook> result;
    for (int i = 0; i < slides_->count(); ++i) result.append(slides_->item(i)->data(Qt::UserRole + 6).value<SlideLook>());
    return result;
}
QVector<SlideLook> MainWindow::resolveLooks(QVector<SlideLook> looks) const {
    for (auto &look : looks)
        if (look.id > 0 && look.settings.background == PresentationLook::Background::Image)
            look.backgroundPath = resolveBackground(look.settings.backgroundPath);
    return looks;
}

QVector<int> MainWindow::selectedSlideOrder() const {
    QVector<int> order;
    for (int i = 0; i < slides_->count(); ++i) order.append(slides_->item(i)->data(Qt::UserRole).toInt());
    return order;
}
QVector<SlideMedia> MainWindow::selectedMedia() const {
    QVector<SlideMedia> result;
    for (int i = 0; i < slides_->count(); ++i)
        result.append(resolveMedia(slides_->item(i)->data(Qt::UserRole + 3).value<SlideMedia>()));
    return result;
}
SlideMedia MainWindow::resolveMedia(SlideMedia media) const {
    if (media.kind != MediaKind::None && mediaManager_ && !QFileInfo(media.path).isAbsolute())
        media.path = mediaManager_->absolutePath(media.path);
    return media;
}
QPixmap MainWindow::mediaThumbnail(const SlideMedia &media) const {
    if (!mediaManager_ || media.kind != MediaKind::Image) return {};
    const QString relative = QFileInfo(media.path).isAbsolute()
        ? QDir(library_.databaseDirectory()).relativeFilePath(media.path) : media.path;
    return mediaManager_->thumbnail(relative);
}

QString MainWindow::resolveBackground(const QString &path) const {
    const QString resource = BuiltInBackgrounds::resourcePath(path);
    if (!resource.isEmpty()) return resource;
    return path.isEmpty() ? QString() : QDir(library_.databaseDirectory()).absoluteFilePath(path);
}

QPixmap MainWindow::backgroundPixmap(const QString &absolutePath) const {
    if (absolutePath.isEmpty()) return {};
    const QFileInfo info(absolutePath);
    const QString key = QStringLiteral("worshipcue-background:%1:%2:%3")
        .arg(absolutePath).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    QPixmap result;
    if (QPixmapCache::find(key, &result)) return result;
    QImageReader reader(absolutePath);
    reader.setAutoTransform(true);
    if (reader.size().isValid()) reader.setScaledSize(reader.size().scaled(QSize(1920, 1080), Qt::KeepAspectRatio));
    result = QPixmap::fromImage(reader.read());
    if (!result.isNull()) QPixmapCache::insert(key, result);
    return result;
}

bool MainWindow::showSelectedService(int selectSlide, bool reportFailure) {
    if (rebuilding_) return true;
    browsingLibrary_ = false;
    browsingMedia_ = false;
    return showSelectedPresentation(selectSlide, reportFailure);
}

bool MainWindow::showSelectedLibrary(int selectSlide, bool reportFailure) {
    if (rebuilding_) return true;
    browsingLibrary_ = true;
    browsingMedia_ = false;
    return showSelectedPresentation(selectSlide, reportFailure);
}

void MainWindow::updateSlideDragMode() {
    slides_->setDragDropMode(!browsingLibrary_ && mode_->currentIndex() == 1
        ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

bool MainWindow::showSelectedPresentation(int selectSlide, bool reportFailure) {
    const Song song = workspaceSource();
    QVector<int> order;
    if (!library_.lastError().isEmpty()) {
        if (reportFailure) reportError(library_.lastError());
        return false;
    }
    if (song.id && !browsingLibrary_) {
        const auto entries = library_.service(currentServiceId_);
        if (!library_.lastError().isEmpty()) {
            if (reportFailure) reportError(library_.lastError());
            return false;
        }
        for (const auto &entry : entries) {
            if (entry.id == selectedEntryId()) { order = entry.slideOrder; break; }
        }
        for (int index : order) {
            if (index < 0 || index >= song.slides.size()) {
                if (reportFailure) reportError(QStringLiteral("This service arrangement references a missing slide. Reset its order from the slide context menu."));
                return false;
            }
        }
    }
    songSections_->setEnabled(song.id > 0 && std::all_of(song.media.cbegin(), song.media.cend(),
        [](const SlideMedia &media) { return media.kind == MediaKind::None; }));
    presentationSettings_->setEnabled(song.id > 0);
    looksButton_->setEnabled(song.id > 0);
    selectedBackgroundPath_ = resolveBackground(song.backgroundPath);
    selectedBackground_ = backgroundPixmap(selectedBackgroundPath_);
    const auto settings = song.settings.resolvedWithDefaults(programSettings_);
    const QString timing = settings.autoAdvanceMs > 0
        ? QStringLiteral("Auto: %1 s • %2").arg(settings.autoAdvanceMs / 1000.0, 0, 'f', 1)
            .arg(settings.loop ? QStringLiteral("Loop presentation") : QStringLiteral("Hold at end"))
        : QStringLiteral("Manual advance");
    const QString transition = settings.transition == SlideTransitionType::Cut || settings.transitionDurationMs == 0
        ? QStringLiteral("Cut") : QStringLiteral("%1 • %2 s").arg(transitionName(settings.transition))
            .arg(settings.transitionDurationMs / 1000.0, 0, 'f', 2);
    const QString includedTitle = BuiltInBackgrounds::title(song.backgroundPath);
    const QString backgroundName = song.backgroundPath.isEmpty() ? QStringLiteral("Default background")
        : QStringLiteral("Background: %1%2").arg(includedTitle.isEmpty() ? QFileInfo(song.backgroundPath).completeBaseName() : includedTitle,
            selectedBackground_.isNull() ? QStringLiteral(" (missing)") : QString());
    selectedTimingSummary_ = song.id ? QStringLiteral("%1 • %2 (%3)\n%4").arg(timing, transition,
        song.settings.overrideTransition ? QStringLiteral("presentation override") : QStringLiteral("global default"), backgroundName) : QString();
    selectedTimingInfo_->setText(selectedTimingSummary_);
    const QSignalBlocker block(slides_);
    rebuilding_ = true;
    slides_->clear();
    if (song.id || song.mediaAssetId) {
        if (order.isEmpty()) order = song.playbackOrder();
        for (const int sourceIndex : order) {
            if (sourceIndex < 0 || sourceIndex >= song.slides.size()) continue;
            auto *item = new QListWidgetItem(slides_);
            item->setData(Qt::UserRole, sourceIndex);
            item->setData(Qt::UserRole + 1, song.slides.at(sourceIndex));
            item->setData(Qt::UserRole + 4, song.richText.value(sourceIndex));
            const SlideMedia media = song.media.value(sourceIndex);
            item->setData(Qt::UserRole + 3, QVariant::fromValue(media));
            item->setData(Qt::UserRole + 5, song.structure.slideLabel(sourceIndex));
            item->setData(Qt::UserRole + 6, QVariant::fromValue(resolveLooks({song.looks.value(sourceIndex)}).first()));
            item->setToolTip(media.kind == MediaKind::None ? song.structure.slideLabel(sourceIndex) + QLatin1Char('\n') + song.slides.at(sourceIndex)
                : QStringLiteral("%1\n%2").arg(media.name, resolveMedia(media).path));
        }
        selectedInfo_->setText(QStringLiteral("%1 • %2 %3").arg(song.title).arg(slides_->count())
            .arg(slides_->count() == 1 ? QStringLiteral("slide") : QStringLiteral("slides")));
        if (slides_->count()) slides_->setCurrentRow(qBound(0, selectSlide, slides_->count() - 1));
    } else {
        selectedInfo_->setText(browsingLibrary_ ? QStringLiteral("Select a library item to view its slides")
                                              : QStringLiteral("Add songs to build this service"));
    }
    rebuilding_ = false;
    updateSlideDragMode();
    refreshThumbnails();
    showSelectedSlide();
    return true;
}

void MainWindow::showSelectedSlide() {
    const auto *item = slides_->currentItem();
    editSlide_->setEnabled(item && workspaceSongId());
    const SlideMedia media = item ? item->data(Qt::UserRole + 3).value<SlideMedia>() : SlideMedia{};
    const auto style = item ? item->data(Qt::UserRole + 6).value<SlideLook>() : SlideLook{};
    const QString text = item ? item->data(Qt::UserRole + 1).toString() : QString();
    const QString html = item ? item->data(Qt::UserRole + 4).toString() : QString();
    const QPixmap background = style.id > 0 && style.settings.background == PresentationLook::Background::Image
        ? backgroundPixmap(style.backgroundPath) : selectedBackground_;
    selectedPreview_->setSlide(previewState(text, resolveMedia(media), html, style), background, mediaThumbnail(media));
    selectedTimingInfo_->setText(selectedTimingSummary_ + (style.id > 0 && !style.fitText && RichTextSupport::overflows(text, html, style)
        ? QStringLiteral("\nText overflow • output uses safety fitting. Open Looks / Reflow to review.") : QString()));
}

void MainWindow::createService() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New Service"), QStringLiteral("Service name:"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QString error;
    const int id = library_.createService(name, &error);
    if (!id) return reportError(error);
    reloadServices(id);
    markSaved();
}
void MainWindow::renameService() {
    if (!currentServiceId_) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename Service"), QStringLiteral("Service name:"), QLineEdit::Normal, services_->currentText(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QString error;
    if (!library_.renameService(currentServiceId_, name, &error)) return reportError(error);
    reloadServices(currentServiceId_);
    markSaved();
}
void MainWindow::duplicateService() {
    if (!currentServiceId_) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Copy Service"), QStringLiteral("New service name:"), QLineEdit::Normal, services_->currentText() + QStringLiteral(" (Copy)"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QString error;
    const int id = library_.duplicateService(currentServiceId_, name, &error);
    if (!id) return reportError(error);
    reloadServices(id);
    markSaved();
}
void MainWindow::chooseMediaFiles() {
    QStringList patterns;
    for (const auto &ext : MediaManager::supportedImageExtensions()) patterns.append(QStringLiteral("*.") + ext);
    for (const auto &ext : MediaManager::supportedVideoExtensions()) patterns.append(QStringLiteral("*.") + ext);
    for (const auto &ext : MediaManager::supportedAudioExtensions()) patterns.append(QStringLiteral("*.") + ext);
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import media"), {},
        QStringLiteral("Media (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' '))));
    importMediaFiles(files);
}

bool MainWindow::importMediaFiles(const QStringList &paths) {
    if (paths.isEmpty() || !mediaManager_ || mediaManager_->busy()) return false;
    QStringList sorted = paths;
    QCollator collator;
    collator.setNumericMode(true);
    std::stable_sort(sorted.begin(), sorted.end(), [&](const QString &a, const QString &b) {
        const int result = collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName());
        return result ? result < 0 : a < b;
    });
    MediaImportDialog dialog(sorted, this);
    if (sorted.size() == 1) dialog.setPresentationTitle(QFileInfo(sorted.first()).completeBaseName());
    if (dialog.exec() != QDialog::Accepted) return false;
    importTitle_ = dialog.title();
    importAsPresentation_ = dialog.createPresentation();
    importServiceId_ = dialog.addToService() ? currentServiceId_ : 0;
    importProgress_ = new QProgressDialog(QStringLiteral("Copying media into WorshipCue…"), QStringLiteral("Cancel"), 0, sorted.size(), this);
    importProgress_->setWindowTitle(QStringLiteral("Importing Media"));
    importProgress_->setWindowModality(Qt::NonModal);
    importProgress_->setAutoClose(false);
    importProgress_->setMinimumDuration(300);
    connect(importProgress_, &QProgressDialog::canceled, mediaManager_.get(), &MediaManager::cancelImport);
    if (!mediaManager_->importFiles(sorted, dialog.names(), dialog.loopVideos())) {
        importProgress_->deleteLater();
        importProgress_ = nullptr;
        return false;
    }
    importButton_->setEnabled(false);
    mediaImportButton_->setEnabled(false);
    importAction_->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("Importing media. Presentation controls remain available."));
    return true;
}

void MainWindow::renameMedia() { renameMediaPresentation(selectedSongId()); }

void MainWindow::renameMediaPresentation(int id) {
    if (!id) return;
    const Song song = library_.song(id);
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    QStringList names;
    QVector<int> indices;
    for (int i = 0; i < song.media.size(); ++i) {
        if (song.media.at(i).kind == MediaKind::None) continue;
        names.append(song.media.at(i).name);
        indices.append(i);
    }
    if (names.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Choose a picture or video presentation to rename its media."), 6000);
        return;
    }
    MediaImportDialog dialog(names, this, true);
    dialog.setPresentationTitle(song.title);
    if (dialog.exec() != QDialog::Accepted) return;
    QStringList allNames;
    for (int i = 0; i < song.slides.size(); ++i) allNames.append(QString());
    const auto renamed = dialog.names();
    for (int i = 0; i < indices.size(); ++i) allNames[indices.at(i)] = renamed.at(i);
    QString error;
    if (!library_.updatePresentationMediaNames(id, allNames, &error)) return reportError(error);
    const int slide = slides_->currentRow();
    reloadSongs();
    reloadService(selectedEntryId(), slide);
    markSaved();
    statusBar()->showMessage(QStringLiteral("Media names saved. Live playback and original files are unchanged."), 8000);
}

void MainWindow::updatePlaybackUi() {
    if (!mediaStatus_) return;
    const bool video = playback_.isPlayable();
    mediaStatus_->setVisible(controller_.state().currentMedia().kind != MediaKind::None);
    pauseVideo_->setVisible(video);
    restartVideo_->setVisible(video);
    muteVideo_->setVisible(video);
    pauseVideo_->setEnabled(video && playback_.errorString().isEmpty());
    restartVideo_->setEnabled(video && playback_.errorString().isEmpty());
    muteVideo_->setText(playback_.muted() ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
    pauseVideo_->setText(playback_.isAudio()
        ? (playback_.isPaused() ? QStringLiteral("Play Audio") : QStringLiteral("Pause Audio"))
        : (playback_.isPaused() ? QStringLiteral("Play Video") : QStringLiteral("Pause Video")));
    mediaStatus_->setText(playback_.errorString().isEmpty() ? playback_.status() : playback_.errorString());
    mediaStatus_->setStyleSheet(playback_.errorString().isEmpty() ? QStringLiteral("color: #a5b2c5;") : QStringLiteral("color: #ff9d9d;"));
}

void MainWindow::createSong() {
    SongStructureDialog dialog({}, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    const Song song = dialog.draft();
    if (!library_.createSong(song.title, song.slides, &error, {}, {}, song.richText, song.structure)) return reportError(error);
    reloadSongs();
    markSaved();
}
void MainWindow::importLyrics() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import Song Lyrics"), {},
        QStringLiteral("Lyrics (*.txt *.text *.pro *.chopro *.cho *.chordpro);;All Files (*)"));
    if (path.isEmpty()) return;
    SongEditorDialog dialog(this);
    QString error;
    if (!dialog.importLyricsFile(path, &error)) return reportError(error);
    if (dialog.exec() != QDialog::Accepted) return;
    QStringList sources;
    const auto structure = dialog.songStructure(&sources);
    const int imported = library_.createTextPresentation(dialog.title(), sources, &error, {}, {}, {}, structure);
    if (!imported || !library_.recordPresentationImport(imported, &error)) return reportError(error);
    search_->clear();
    reloadSongs();
    for (int i = 0; i < songs_->count(); ++i)
        if (songs_->item(i)->text() == dialog.title()) songs_->setCurrentRow(i);
    markSaved();
    statusBar()->showMessage(QStringLiteral("Lyrics imported into the library. Use Add → to include the song in this service."), 8000);
}
void MainWindow::editSong() {
    editPresentation(selectedSongId());
}
void MainWindow::editSongStructure() {
    const Song song = workspaceSource();
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!song.id) return;
    for (const auto &media : song.media) if (media.kind != MediaKind::None) {
        statusBar()->showMessage(QStringLiteral("Choose a text song to edit its sections and arrangements."), 6000);
        return;
    }
    SongStructureDialog dialog(song, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!library_.updateSongStructure(dialog.original(), dialog.draft(), &error)) return reportError(error);
    refreshStructuredSong(song.id);
    reloadSongs();
    reloadService(selectedEntryId(), slides_->currentRow());
    markSaved();
}

void MainWindow::refreshStructuredSong(int songId) {
    const auto song = library_.song(songId);
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!song.structure.isEmpty())
        controller_.refreshSongText(songId, song.structure.slideKeys(), song.slides, song.richText, resolveLooks(song.looks));
}

void MainWindow::configureLooks() {
    const auto original = workspaceSource();
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!original.id || !slides_->currentItem()) return;
    const int source = slides_->currentItem()->data(Qt::UserRole).toInt();
    PresentationLookDialog dialog(&library_, original, source, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!library_.updatePresentationDraft(original, dialog.draft(), dialog.sourceIndices(), &error)) return reportError(error);
    const int row = slides_->currentRow();
    reloadSongs();
    reloadService(selectedEntryId(), row);
    markSaved();
    statusBar()->showMessage(QStringLiteral("Look and reflow saved. Take a slide live to present this draft."), 8000);
}

void MainWindow::editSelectedSlide() {
    if (!slides_->currentItem()) return;
    editPresentation(workspaceSongId(), slides_->currentItem()->data(Qt::UserRole).toInt());
}
void MainWindow::editPresentation(int songId, int sourceSlide) {
    if (!songId) return;
    Song song = library_.song(songId);
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!song.id) return;
    QVector<QPixmap> frames;
    for (auto &media : song.media) {
        frames.append(mediaThumbnail(media));
        media = resolveMedia(media);
    }
    song.looks = resolveLooks(song.looks);
    SlideEditorDialog dialog(song, sourceSlide, this, backgroundPixmap(resolveBackground(song.backgroundPath)), frames);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    auto overrides = song.appearance.richTextOverrides;
    const auto html = dialog.richText();
    for (int i = 0; i < html.size(); ++i)
        if (song.looks.value(i).id > 0 && html.at(i) != song.richText.value(i)) overrides[i] = true;
    if (!library_.updatePresentationText(songId, dialog.title(), dialog.slides(), html, &error, overrides)) return reportError(error);
    const int row = slides_->currentRow();
    refreshStructuredSong(songId);
    reloadSongs();
    reloadService(selectedEntryId(), row);
    markSaved();
    statusBar()->showMessage(song.structure.isEmpty()
        ? QStringLiteral("Slide text and formatting saved. Take a slide live to present your changes.")
        : QStringLiteral("Shared song text saved and updated across all arranged occurrences."), 8000);
}
void MainWindow::duplicateSong() {
    const Song song = library_.song(selectedSongId());
    if (!song.id) return;
    QString error;
    const QString title = song.title + QStringLiteral(" (Copy)");
    const bool hasMedia = std::any_of(song.media.cbegin(), song.media.cend(), [](const SlideMedia &m) { return m.kind != MediaKind::None; });
    if (hasMedia ? !library_.createPresentation(title, song.slides, song.media, &error, song.settings, song.backgroundPath, song.richText, song.appearance)
                 : !library_.createSong(title, song.slides, &error, song.settings, song.backgroundPath, song.richText, song.structure, song.appearance)) return reportError(error);
    reloadSongs();
    markSaved();
}
void MainWindow::deleteSong() {
    const int id = selectedSongId();
    if (!id) return;
    if (QMessageBox::question(this, QStringLiteral("Delete song?"),
        QStringLiteral("Delete “%1” from the library and every saved service? The currently live service stays on screen.").arg(library_.song(id).title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    if (!library_.deleteSong(id, &error)) return reportError(error);
    reloadSongs();
    reloadService();
    markSaved();
}
void MainWindow::appendSong() {
    const int id = selectedSongId();
    if (!id || !currentServiceId_) return;
    QString error;
    if (!library_.appendToService(currentServiceId_, id, &error)) return reportError(error);
    const auto entries = library_.service(currentServiceId_);
    browsingLibrary_ = false;
    browsingMedia_ = false;
    reloadService(entries.isEmpty() ? 0 : entries.last().id);
    markSaved();
}
void MainWindow::removeServiceEntry() {
    const int id = selectedEntryId();
    if (!id) return;
    QString error;
    if (!library_.removeFromService(id, &error)) return reportError(error);
    reloadService();
    markSaved();
}
void MainWindow::duplicateServiceEntry() {
    const int id = selectedEntryId();
    if (!id) return;
    QString error;
    if (!library_.duplicateServiceEntry(id, &error)) return reportError(error);
    reloadService(id);
    markSaved();
}
void MainWindow::moveService(int delta) {
    const int row = service_->currentRow();
    if (row < 0 || row + delta < 0 || row + delta >= service_->count()) return;
    const int id = selectedEntryId();
    QString error;
    if (!library_.moveServiceEntry(currentServiceId_, row, row + delta, &error)) return reportError(error);
    reloadService(id);
    markSaved();
}
void MainWindow::persistServiceOrder() {
    if (rebuilding_) return;
    QVector<int> ids;
    for (int i = 0; i < service_->count(); ++i) ids.append(service_->item(i)->data(Qt::UserRole).toInt());
    const int id = selectedEntryId();
    QString error;
    if (!library_.reorderServiceEntries(currentServiceId_, ids, &error)) {
        reloadService(id);
        return reportError(error);
    }
    reloadService(id);
    markSaved();
}
void MainWindow::persistSlideOrder() {
    if (browsingLibrary_) {
        showSelectedPresentation(slides_->currentRow());
        return;
    }
    if (rebuilding_ || !selectedEntryId()) return;
    QString error;
    if (!library_.setEntrySlideOrder(selectedEntryId(), selectedSlideOrder(), &error)) {
        showSelectedService();
        return reportError(error);
    }
    refreshThumbnails();
    showSelectedSlide();
    markSaved();
}
void MainWindow::moveSlide(int delta) {
    if (browsingLibrary_) return;
    const int row = slides_->currentRow();
    if (row < 0 || row + delta < 0 || row + delta >= slides_->count()) return;
    auto order = selectedSlideOrder();
    order.move(row, row + delta);
    QString error;
    if (!library_.setEntrySlideOrder(selectedEntryId(), order, &error)) return reportError(error);
    showSelectedService(row + delta);
    markSaved();
}
void MainWindow::resetSlideOrder() {
    if (browsingLibrary_ || !selectedEntryId()) return;
    QString error;
    if (!library_.setEntrySlideOrder(selectedEntryId(), {}, &error)) return reportError(error);
    showSelectedService();
    markSaved();
}

PresentationItem MainWindow::midiSnapshot(const Song &song, const QVector<int> &order, int entryId) const {
    PresentationItem item;
    item.entryId = entryId; item.title = song.title; item.songId = song.id; item.mediaAssetId = song.mediaAssetId;
    item.settings = song.settings.resolvedWithDefaults(programSettings_);
    item.backgroundPath = resolveBackground(song.backgroundPath);
    item.sourceSlideKeys = arrangedSourceKeys(song, order);
    for (int source : order) {
        item.slides.append(song.slides.value(source)); item.richText.append(song.richText.value(source));
        item.media.append(resolveMedia(song.media.value(source)));
        item.looks.append(song.looks.value(source)); item.sourceSlideIds.append(song.slideIds.value(source));
        item.midiActions.append(song.midiActions.value(source));
    }
    item.looks = resolveLooks(item.looks);
    return item;
}
bool MainWindow::takeSavedService(int serviceId, int entryId, int row, QString *error) {
    QVector<PresentationItem> items;
    const auto entries = library_.service(serviceId);
    if (!library_.lastError().isEmpty()) { *error = library_.lastError(); return false; }
    bool found = false;
    for (const auto &entry : entries) {
        const auto song = library_.entrySource(entry.id);
        if (!library_.lastError().isEmpty()) { *error = library_.lastError(); return false; }
        auto item = midiSnapshot(song, entry.slideOrder.isEmpty() ? song.playbackOrder() : entry.slideOrder, entry.id);
        if (entry.id == entryId) found = row >= 0 && row < item.slides.size();
        items.append(item);
    }
    if (!found) { *error = QStringLiteral("The MIDI target is no longer in this saved service arrangement."); return false; }
    if (!prepareCleanupSnapshot(items, error)) return false;
    liveLibrarySongId_ = 0; liveLibraryMediaId_ = 0;
    controller_.takeServiceLive(serviceId, items, entryId, row);
    return true;
}
void MainWindow::goLive() {
    if (slides_->currentRow() < 0) return;
    if (browsingLibrary_) {
        const auto song = workspaceSource();
        if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
        liveLibrarySongId_ = song.id; liveLibraryMediaId_ = song.mediaAssetId;
        const auto item = midiSnapshot(song, selectedSlideOrder()); QString error;
        if (!prepareCleanupSnapshot({item}, &error)) return reportError(error);
        controller_.takeLive(item, slides_->currentRow());
    } else {
        QString error;
        if (!takeSavedService(currentServiceId_, selectedEntryId(), slides_->currentRow(), &error)) return reportError(error);
    }
    slides_->setFocus();
}
void MainWindow::reloadMidiBindings() {
    const auto bindings = library_.midiBindings();
    QString error = library_.lastError();
    if (!error.isEmpty()) { midi_.setBindings({}); midi_.record(error); return; }
    if (!midi_.setBindings(bindings, &error)) midi_.record(error);
}
void MainWindow::configureMidiSlide() {
    const auto source = workspaceSource();
    const auto order = selectedSlideOrder();
    const int row = slides_->currentRow();
    if (row < 0 || row >= order.size() || !library_.lastError().isEmpty()) return;
    const int canonical = order.at(row);
    MidiBinding target; target.name = source.title + QStringLiteral(" — slide %1").arg(row + 1);
    target.command = MidiCommand::TakeSlide; target.songId = source.id; target.assetId = source.mediaAssetId;
    target.slideId = source.slideIds.value(canonical); target.entryId = browsingLibrary_ ? 0 : selectedEntryId();
    for (int i = 0; i < row; ++i) if (order.at(i) == canonical) ++target.occurrence;
    const auto type = source.mediaAssetId ? (target.entryId ? MidiOwnerType::Entry : MidiOwnerType::Asset) : MidiOwnerType::Slide;
    const qint64 owner = type == MidiOwnerType::Slide ? target.slideId : type == MidiOwnerType::Entry ? target.entryId : target.assetId;
    editMidiSlide(this, midi_, library_, type, owner, target);
    reloadMidiBindings();
}
void MainWindow::dispatchMidi(const MidiBinding &binding) {
    if (mode_->currentIndex() != 0 || QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
    const auto current = library_.midiBinding(binding.id);
    if (!current.id || !current.enabled || current.toJson() != binding.toJson()) {
        midi_.record(QStringLiteral("Skipped stale MIDI mapping; reloading saved mappings.")); reloadMidiBindings(); return;
    }
    switch (binding.command) {
    case MidiCommand::Next: advance(); return;
    case MidiCommand::Previous: rewind(); return;
    case MidiCommand::Blackout: controller_.toggleBlackout(); return;
    case MidiCommand::ClearText: controller_.toggleClear(); return;
    case MidiCommand::Restore: controller_.restoreOutput(); return;
    case MidiCommand::PauseAutoAdvance: controller_.toggleAutoAdvancePaused(); return;
    case MidiCommand::PauseMedia: playback_.togglePause(); return;
    case MidiCommand::RestartMedia: playback_.restart(); return;
    case MidiCommand::MuteMedia: playback_.toggleMute(); return;
    case MidiCommand::ReleaseNotes: midi_.releaseNotes(); return;
    default: break;
    }
    QString error;
    Song song;
    int serviceId = 0;
    QVector<int> order;
    if (binding.entryId) {
        serviceId = library_.entryServiceId(binding.entryId);
        song = library_.entrySource(binding.entryId);
        for (const auto &entry : library_.service(serviceId)) if (entry.id == binding.entryId) order = entry.slideOrder;
    } else if (binding.assetId) {
        const auto asset = library_.mediaAsset(binding.assetId);
        if (asset.id) { song.title = asset.media.name; song.mediaAssetId = asset.id; song.slides = {QStringLiteral("")}; song.media = {asset.media}; song.slideIds = {0}; song.midiActions = {library_.midiActions(MidiOwnerType::Asset, asset.id)}; }
    } else song = library_.song(binding.songId);
    if (!library_.lastError().isEmpty()) error = library_.lastError();
    if ((binding.songId && song.id != binding.songId) || (binding.assetId && song.mediaAssetId != binding.assetId) || (binding.entryId && !serviceId)) error = QStringLiteral("The saved MIDI target no longer exists.");
    if (order.isEmpty()) order = song.playbackOrder();
    int row = 0;
    if (binding.command == MidiCommand::TakeSlide && binding.slideId) {
        row = -1; int occurrence = 0;
        for (int i = 0; i < order.size(); ++i) if (song.slideIds.value(order.at(i)) == binding.slideId && occurrence++ == binding.occurrence) { row = i; break; }
    }
    if (row < 0 || row >= order.size()) error = QStringLiteral("The MIDI slide occurrence is no longer in the saved arrangement.");
    if (!error.isEmpty()) { midi_.record(error); statusBar()->showMessage(error, 5000); return; }
    if (binding.command == MidiCommand::SelectPresentation) {
        if (serviceId) { browsingLibrary_ = false; browsingMedia_ = false; sidebarTabs_->setCurrentIndex(1); reloadServices(serviceId, binding.entryId, row, false); }
        else { sidebarTabs_->setCurrentIndex(0); search_->clear(); reloadSongs(false); for (int i=0;i<songs_->count();++i) if(songs_->item(i)->data(Qt::UserRole).toInt()==song.id) { songs_->setCurrentRow(i); showSelectedLibrary(row, false); break; } }
        return;
    }
    if (serviceId) {
        if (!takeSavedService(serviceId, binding.entryId, row, &error)) { midi_.record(error); return; }
    } else {
        liveLibrarySongId_ = song.id; liveLibraryMediaId_ = song.mediaAssetId;
        const auto item = midiSnapshot(song, order);
        if (!prepareCleanupSnapshot({item}, &error)) { midi_.record(error); return; }
        controller_.takeLive(item, row);
    }
    revealLiveCue(); syncLiveSelection();
}
void MainWindow::advance() {
    if (controller_.state().index < 0) goLive();
    else if (!controller_.next()) statusBar()->showMessage(QStringLiteral("End of the live service. Output held on the last slide."), 4000);
    syncLiveSelection();
}
void MainWindow::rewind() {
    if (controller_.state().index < 0) return;
    if (!controller_.previous()) statusBar()->showMessage(QStringLiteral("Beginning of the live service. Output held on the first slide."), 4000);
    syncLiveSelection();
}
void MainWindow::syncLiveSelection() {
    const auto &state = controller_.state();
    if (browsingLibrary_) {
        if (state.serviceId == 0 && (browsingMedia_ ? liveLibraryMediaId_ == selectedMediaAssetId() : liveLibrarySongId_ == selectedSongId())
            && selectedSlides() == state.slides && selectedRichText() == state.richText && selectedLooks() == state.looks
            && sameMediaContent(selectedMedia(), state.media) && selectedBackgroundPath_ == state.backgroundPath)
            slides_->setCurrentRow(state.index);
        return;
    }
    if (state.serviceId != currentServiceId_) return;
    for (int i = 0; i < service_->count(); ++i) {
        if (service_->item(i)->data(Qt::UserRole).toInt() != state.entryId) continue;
        if (service_->currentRow() != i) service_->setCurrentRow(i);
        // Edits may have changed the saved arrangement; never imply it is the live snapshot.
        if (selectedSlides() == state.slides && selectedRichText() == state.richText && selectedLooks() == state.looks && sameMediaContent(selectedMedia(), state.media)
            && selectedBackgroundPath_ == state.backgroundPath) {
            slides_->setCurrentRow(state.index);
            if (slides_->currentItem()) slides_->scrollToItem(slides_->currentItem());
        }
        return;
    }
}
void MainWindow::refreshThumbnails() {
    const auto &state = controller_.state();
    const auto &background = selectedBackground_;
    const bool backgroundChanged = thumbnailBackgroundKey_ != background.cacheKey();
    thumbnailBackgroundKey_ = background.cacheKey();
    const bool sameSource = browsingLibrary_ ? state.serviceId == 0 && (browsingMedia_ ? liveLibraryMediaId_ == selectedMediaAssetId() : liveLibrarySongId_ == selectedSongId())
        : state.serviceId == currentServiceId_ && state.entryId == selectedEntryId();
    const bool same = sameSource
                      && selectedSlides() == state.slides && selectedRichText() == state.richText && selectedLooks() == state.looks && sameMediaContent(selectedMedia(), state.media)
                      && selectedBackgroundPath_ == state.backgroundPath;
    for (int i = 0; i < slides_->count(); ++i) {
        auto *item = slides_->item(i);
        const auto media = item->data(Qt::UserRole + 3).value<SlideMedia>();
        const auto style = item->data(Qt::UserRole + 6).value<SlideLook>();
        const QPixmap slideBackground = style.id > 0 && style.settings.background == PresentationLook::Background::Image
            ? backgroundPixmap(style.backgroundPath) : background;
        const bool missing = media.kind != MediaKind::None && !QFileInfo::exists(resolveMedia(media).path);
        const bool live = same && state.index == i;
        QPixmap image = item->data(Qt::UserRole + 2).value<QPixmap>();
        if (image.isNull() || backgroundChanged) {
            if (media.kind == MediaKind::None) {
                image = SlideRenderer::thumbnail(item->data(Qt::UserRole + 1).toString(), QSize(320, 180), slideBackground,
                                                 item->data(Qt::UserRole + 4).toString(), style);
            } else {
                image = QPixmap(320, 180);
                image.fill(Qt::black);
                QPainter painter(&image);
                const QPixmap frame = mediaThumbnail(media);
                SlideRenderer::paintAudience(painter, image.rect(), previewState(item->data(Qt::UserRole + 1).toString(), resolveMedia(media),
                                                                               item->data(Qt::UserRole + 4).toString(), style), slideBackground, frame);
                if (media.kind == MediaKind::Video || missing || frame.isNull()) {
                    painter.setPen(missing ? QColor(QStringLiteral("#ff9d9d")) : Qt::white);
                    painter.drawText(image.rect().adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap,
                        missing ? QStringLiteral("MISSING MEDIA\n%1").arg(media.name)
                        : media.kind == MediaKind::Audio ? QStringLiteral("AUDIO\n%1").arg(media.name)
                        : media.kind == MediaKind::Video ? QStringLiteral("▶ VIDEO\n%1").arg(media.name)
                                                       : QStringLiteral("Loading image…"));
                }
            }
            item->setData(Qt::UserRole + 2, image);
        }
        if (live) {
            QPainter p(&image);
            p.setPen(QPen(QColor(QStringLiteral("#54e5a0")), 6));
            p.drawRect(image.rect().adjusted(3, 3, -3, -3));
        }
        item->setIcon(QIcon(image));
        const auto sectionName = item->data(Qt::UserRole + 5).toString();
        const QString label = media.kind == MediaKind::None ? (sectionName.isEmpty() ? QString::number(i + 1)
            : QStringLiteral("%1 • %2").arg(i + 1).arg(sectionName)) : QStringLiteral("%1 • %2").arg(i + 1).arg(media.name);
        item->setText(label + (live ? QStringLiteral("  • LIVE") : QString()) + (missing ? QStringLiteral(" • MISSING") : QString()));
        item->setForeground(missing ? QColor(QStringLiteral("#ff9d9d")) : live ? QColor(QStringLiteral("#62e8ac")) : QColor(QStringLiteral("#eaf0f8")));
    }
}
void MainWindow::updateLiveUi() {
    const auto &state = controller_.state();
    returnToLive_->setEnabled(state.index >= 0 && (liveLibrarySongId_ > 0 || liveLibraryMediaId_ > 0 || (state.serviceId > 0 && state.entryId > 0)));
    const QString status = state.blackout ? QStringLiteral("BLACKOUT") : state.clear ? QStringLiteral("BACKGROUND ONLY") : QStringLiteral("LIVE");
    liveInfo_->setText(state.index < 0 ? QStringLiteral("%1 • Ready").arg(status)
        : QStringLiteral("%1 • %2 • %3/%4").arg(status, state.title).arg(state.index + 1).arg(state.slides.size()));
    const auto look = state.currentLook();
    if (state.index >= 0 && look.id > 0 && !look.fitText && RichTextSupport::overflows(state.currentText(), state.currentRichText(), look))
        liveInfo_->setText(liveInfo_->text() + QStringLiteral(" • Text overflow • safety fit"));
    livePreview_->setSlide(state, outputs_.audience().backgroundPixmap(), playback_.frame());
    const QString next = state.nextText();
    const auto media = state.nextMedia();
    nextInfo_->setText(state.nextTitle().isEmpty() ? QStringLiteral("UP NEXT • End of service")
                                    : QStringLiteral("UP NEXT • %1").arg(state.nextTitle()));
    nextPreview_->setSlide(previewState(next, media, state.nextRichText(), state.nextLook()), backgroundPixmap(state.nextBackgroundPath()), mediaThumbnail(media));
    blackoutButton_->setChecked(state.blackout);
    clearButton_->setChecked(state.clear);
    for (int i = 0; i < service_->count(); ++i) {
        auto *item = service_->item(i);
        const bool live = state.serviceId == currentServiceId_ && item->data(Qt::UserRole).toInt() == state.entryId;
        item->setText(QStringLiteral("%1. %2%3").arg(i + 1).arg(item->data(Qt::UserRole + 2).toString(), live ? QStringLiteral("  • LIVE") : QString()));
        item->setForeground(live ? QColor(QStringLiteral("#62e8ac")) : QColor(QStringLiteral("#eaf0f8")));
    }
    refreshThumbnails();
    updatePlaybackUi();
    updateTimingUi();
}

void MainWindow::configurePresentation(int songId) {
    if (!songId) return;
    const Song song = library_.song(songId);
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    if (!song.id) return;
    PresentationSettingsDialog dialog(song.title, song.settings, this, programSettings_);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!library_.updatePresentationSettings(songId, dialog.settings(), &error)) return reportError(error);
    const int row = slides_->currentRow();
    showSelectedPresentation(row);
    markSaved();
    statusBar()->showMessage(QStringLiteral("Timing and transitions saved. Take a slide live to use the updated settings."), 8000);
}

void MainWindow::updateTransitionActions() {
    if (!bulkTransitions_) return;
    bulkTransitions_->setEnabled(!service_->selectedItems().isEmpty());
    undoTransitions_->setEnabled(!transitionUndo_.isEmpty());
    redoTransitions_->setEnabled(!transitionRedo_.isEmpty());
}

void MainWindow::configureSelectedTransitions() {
    QVector<TransitionChange> changes;
    QSet<QString> targets;
    int count = 0;
    for (int row = 0; row < service_->count(); ++row) {
        auto *item = service_->item(row);
        if (!item->isSelected()) continue;
        ++count;
        const int entryId = item->data(Qt::UserRole).toInt();
        const Song source = library_.entrySource(entryId);
        if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
        if (!source.id && !source.mediaAssetId) return reportError(QStringLiteral("A selected service item no longer exists."));
        const bool media = source.mediaAssetId > 0;
        const int id = media ? entryId : source.id;
        const QString key = QString::number(media) + QLatin1Char(':') + QString::number(id);
        if (targets.contains(key)) continue;
        targets.insert(key);
        changes.append({media ? TransitionChange::Target::MediaEntry : TransitionChange::Target::Presentation, id, source.settings, {}});
    }
    if (changes.isEmpty()) return;
    PresentationSettings initial = changes.first().before;
    const bool mixed = std::any_of(changes.cbegin(), changes.cend(), [&](const TransitionChange &change) {
        return !change.before.sameTransition(initial);
    });
    PresentationSettingsDialog dialog(QStringLiteral("%1 selected service item%2%3").arg(count)
        .arg(count == 1 ? QString() : QStringLiteral("s"))
        .arg(mixed ? QStringLiteral(" • Mixed transitions") : QString()), initial, this, programSettings_, false, true);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto selected = dialog.settings();
    for (auto &change : changes) {
        change.after = change.before;
        change.after.transition = selected.transition;
        change.after.transitionDurationMs = selected.transitionDurationMs;
        change.after.overrideTransition = selected.overrideTransition;
    }
    changes.erase(std::remove_if(changes.begin(), changes.end(), [](const TransitionChange &change) {
        return change.before.sameTransition(change.after);
    }), changes.end());
    if (changes.isEmpty()) return;
    QString error;
    if (!library_.updateTransitions(changes, &error)) return reportError(error);
    transitionUndo_.append(changes);
    transitionRedo_.clear();
    reloadService(selectedEntryId(), slides_->currentRow());
    updateTransitionActions();
    markSaved();
    statusBar()->showMessage(QStringLiteral("Transitions saved for %1 selected items. Take a slide live to use them. Undo is available in the service playlist.").arg(count), 8000);
}

void MainWindow::undoTransitions(bool redo) {
    auto &source = redo ? transitionRedo_ : transitionUndo_;
    auto &destination = redo ? transitionUndo_ : transitionRedo_;
    if (source.isEmpty()) return;
    auto changes = source.last();
    if (!redo) for (auto &change : changes) std::swap(change.before, change.after);
    QString error;
    if (!library_.updateTransitions(changes, &error)) return reportError(error);
    destination.append(source.takeLast());
    reloadService(selectedEntryId(), slides_->currentRow());
    updateTransitionActions();
    markSaved();
}

void MainWindow::updateTimingUi() {
    const auto &state = controller_.state();
    const bool active = controller_.autoAdvanceActive();
    const bool hidden = state.blackout || state.clear;
    const bool paused = controller_.autoAdvancePaused();
    const bool timed = state.index >= 0 && state.settings.autoAdvanceMs > 0;
    timingStatus_->setVisible(timed);
    pauseAutoAdvance_->setVisible(timed);
    pauseAutoAdvance_->setEnabled(active && !hidden);
    pauseAutoAdvance_->setText(paused && !hidden ? QStringLiteral("Resume Auto-Advance") : QStringLiteral("Pause Auto-Advance"));
    if (state.index < 0 || state.settings.autoAdvanceMs <= 0) {
        timingStatus_->setText(QStringLiteral("Auto-advance: Off"));
    } else if (!active) {
        timingStatus_->setText(QStringLiteral("Auto-advance: Complete • Last slide held"));
    } else {
        const QString remaining = QString::number(controller_.autoAdvanceRemainingMs() / 1000.0, 'f', 1);
        const QString action = state.index + 1 < state.slides.size() ? QStringLiteral("Next slide")
            : state.settings.loop ? QStringLiteral("Loop to first slide") : QStringLiteral("Hold last slide");
        timingStatus_->setText(hidden ? QStringLiteral("Audience hidden • %1 s left").arg(remaining)
            : paused ? QStringLiteral("Auto-advance paused • %1 s left").arg(remaining)
            : QStringLiteral("%1 in %2 s").arg(action, remaining));
    }
}

void MainWindow::configureProgramTransitions() {
    PresentationSettingsDialog dialog(QStringLiteral("Default for all presentations"), programSettings_, this, programSettings_, true);
    if (dialog.exec() != QDialog::Accepted) return;
    programSettings_ = dialog.settings();
    saveProgramTransitions();
}

void MainWindow::updateProgramTransitionUi() {
    const QSignalBlocker typeBlock(globalTransitionType_);
    const QSignalBlocker timeBlock(globalTransitionTime_);
    globalTransitionType_->setCurrentIndex(globalTransitionType_->findData(int(programSettings_.transition)));
    globalTransitionTime_->setValue(programSettings_.transitionDurationMs / 1000.0);
    globalTransitionTime_->setEnabled(programSettings_.transition != SlideTransitionType::Cut);
}

void MainWindow::saveProgramTransitions() {
    if (!programSettings_.isValid()) return;
    QSettings settings;
    settings.setValue(QStringLiteral("presentation/transitionType"), int(programSettings_.transition));
    settings.setValue(QStringLiteral("presentation/transitionDurationMs"), programSettings_.transitionDurationMs);
    controller_.setGlobalTransitionDefaults(programSettings_);
    updateProgramTransitionUi();
    showSelectedPresentation(slides_->currentRow());
    statusBar()->showMessage(QStringLiteral("Global transition saved. Applies to the next cue or audience output control; presentation overrides take priority."), 8000);
}

void MainWindow::updateSlideView() {
    if (!slides_ || !gridDelegate_ || !textDelegate_) return;
    const bool text = slideViewMode_->currentIndex() == 1;
    slides_->setUpdatesEnabled(false);
    slides_->setItemDelegate(text ? textDelegate_ : gridDelegate_);
    slides_->setViewMode(text ? QListView::ListMode : QListView::IconMode);
    slides_->setWrapping(!text);
    slides_->setFlow(text ? QListView::TopToBottom : QListView::LeftToRight);
    slides_->setWordWrap(text);
    if (text) {
        slides_->setGridSize(QSize());
        textDelegate_->setTextPointSize(12 + (slideSize_->value() - 160) / 20);
    } else {
        const int width = slideSize_->value();
        slides_->setIconSize(QSize(width, width * 9 / 16));
        slides_->setGridSize(QSize(width + 24, width * 9 / 16 + 38));
    }
    slides_->setUpdatesEnabled(true);
    slides_->doItemsLayout();
    slides_->viewport()->update();
}

void MainWindow::revealLiveCue() {
    const auto &state = controller_.state();
    if (state.serviceId == 0 && liveLibraryMediaId_) {
        mediaSearch_->clear();
        mediaFolders_->setCurrentIndex(0);
        for (int i = 0; i < mediaAssets_->count(); ++i) {
            if (mediaAssets_->item(i)->data(Qt::UserRole).toInt() != liveLibraryMediaId_) continue;
            sidebarTabs_->setCurrentIndex(2);
            mediaAssets_->setCurrentRow(i);
            showSelectedMedia();
            slides_->setFocus();
            return;
        }
        statusBar()->showMessage(QStringLiteral("The live media item was removed from the Media Library."), 6000);
        return;
    }
    if (state.serviceId == 0 && liveLibrarySongId_) {
        search_->clear();
        for (int i = 0; i < songs_->count(); ++i) {
            if (songs_->item(i)->data(Qt::UserRole).toInt() != liveLibrarySongId_) continue;
            sidebarTabs_->setCurrentIndex(0);
            songs_->setCurrentRow(i);
            showSelectedLibrary(state.index);
            slides_->setFocus();
            return;
        }
        statusBar()->showMessage(QStringLiteral("The live presentation is no longer saved in the library."), 6000);
        return;
    }
    const int serviceIndex = services_->findData(state.serviceId);
    if (serviceIndex < 0) {
        statusBar()->showMessage(QStringLiteral("The live service is no longer saved in the library."), 6000);
        return;
    }
    sidebarTabs_->setCurrentIndex(1);
    services_->setCurrentIndex(serviceIndex);
    if (browsingLibrary_) showSelectedService();
    syncLiveSelection();
    if (selectedEntryId() != state.entryId) {
        statusBar()->showMessage(QStringLiteral("The live entry was removed from the saved service. Live output is still running."), 6000);
        return;
    }
    if (selectedSlides() != state.slides || selectedRichText() != state.richText || selectedLooks() != state.looks || !sameMediaContent(selectedMedia(), state.media)
        || selectedBackgroundPath_ != state.backgroundPath)
        statusBar()->showMessage(QStringLiteral("Saved slides have changed since going live. Live output keeps its original copy."), 6000);
    slides_->setFocus();
}

void MainWindow::assignBackground(int songId) {
    const Song song = library_.song(songId);
    if (!library_.lastError().isEmpty()) return reportError(library_.lastError());
    BackgroundLibraryDialog dialog(library_.databaseDirectory(), song.backgroundPath, this);
    connect(&dialog, &BackgroundLibraryDialog::fileImported, this, [this](const QString &path) { QString error; if (!library_.recordManagedImport(path, &error)) statusBar()->showMessage(error, 5000); });
    dialog.setAssignmentEnabled(song.id > 0);
    if (song.id) dialog.setWindowTitle(QStringLiteral("Background Library — Assign to %1").arg(song.title));
    if (dialog.exec() != QDialog::Accepted) return;
    if (!song.id) return;
    QString error;
    if (!library_.updatePresentationBackground(song.id, dialog.selectedPath(), &error)) return reportError(error);
    showSelectedPresentation(slides_->currentRow());
    markSaved();
    statusBar()->showMessage(QStringLiteral("Background assigned to “%1” and all its service entries. Take a slide live to apply it.").arg(song.title), 8000);
}
void MainWindow::refreshOutputOptions() {
    const auto choices = outputs_.availableTargets();
    auto refresh = [&](QComboBox *combo, DisplayWindow::Kind kind) {
        const QSignalBlocker block(combo);
        combo->clear();
        for (const auto &choice : choices) combo->addItem(choice.label, choice.id);
        combo->setCurrentIndex(combo->findData(outputs_.target(kind)));
    };
    refresh(audienceTarget_, DisplayWindow::Kind::Audience);
    refresh(stageTarget_, DisplayWindow::Kind::Stage);
    updateOutputStatus();
}
void MainWindow::updateOutputStatus() {
    QStringList statuses;
    for (auto id : outputIds) statuses.append(QStringLiteral("%1: %2").arg(outputName(id), outputs_.status(id)));
    outputInfo_->setText(statuses.join(QStringLiteral("   •   ")));
}
void MainWindow::focusOperator() {
    show();
    raise();
    activateWindow();
    slides_->setFocus(Qt::ShortcutFocusReason);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress || QApplication::activeModalWidget()) return QMainWindow::eventFilter(watched, event);
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget) return false;
    QWidget *window = widget->window();
    if (window != this && !outputs_.ownsWindow(window)) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
        focusOperator();
        return true;
    }
    auto isTyping = [](QWidget *receiver) {
        for (QWidget *w = receiver; w; w = w->parentWidget()) {
            if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w)
                || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w)) return true;
        }
        return false;
    };
    if (isTyping(widget) || isTyping(QApplication::focusWidget())) return false;
    const QKeySequence pressed(key->keyCombination());
    if (pressed == blackoutShortcut_) { if (!key->isAutoRepeat()) controller_.toggleBlackout(); return true; }
    if (pressed == clearShortcut_) { if (!key->isAutoRepeat()) controller_.toggleClear(); return true; }
    if (key->modifiers() != Qt::NoModifier || mode_->currentIndex() != 0) return false;
    switch (key->key()) {
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Space: case Qt::Key_PageDown: advance(); return true;
    case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: rewind(); return true;
    default: return false;
    }
}
void MainWindow::configureShortcuts() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Keyboard Shortcuts"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *blackout = new QKeySequenceEdit(blackoutShortcut_, &dialog);
    auto *clear = new QKeySequenceEdit(clearShortcut_, &dialog);
    form->addRow(QStringLiteral("Audience blackout"), blackout);
    form->addRow(QStringLiteral("Audience clear text"), clear);
    layout->addLayout(form);
    auto *info = new QLabel(QStringLiteral("Choose one modified key combination for each action. Navigation keys are reserved. Shortcuts are ignored while typing."), &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(actions, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto a = blackout->keySequence();
        const auto b = clear->keySequence();
        if (a.count() != 1 || b.count() != 1 || a == b || a[0].keyboardModifiers() == Qt::NoModifier || b[0].keyboardModifiers() == Qt::NoModifier) {
            QMessageBox::warning(&dialog, QStringLiteral("Choose distinct shortcuts"), QStringLiteral("Each action needs a different single key combination with Ctrl, Alt, Shift, or Command."));
            return;
        }
        blackoutShortcut_ = a;
        clearShortcut_ = b;
        QSettings settings;
        settings.setValue(QStringLiteral("shortcuts/blackout"), a.toString(QKeySequence::PortableText));
        settings.setValue(QStringLiteral("shortcuts/clear"), b.toString(QKeySequence::PortableText));
        dialog.accept();
    });
    dialog.exec();
}
void MainWindow::configureMcp() {
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("mcpSetupDialog"));
    dialog.setWindowTitle(QStringLiteral("AI / MCP Setup"));
    dialog.resize(760, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(QStringLiteral(
        "Connect a desktop AI client that supports local stdio MCP to create saved service orders. "
        "The AI can create song and sermon presentations, search the library, and import Planning Center plans into saved services. Your AI client receives the library content it requests "
        "and provides the model; WorshipCue does not need an API key."), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *planningCenterHelp = new QLabel(QStringLiteral(
        "For Planning Center, add PLANNING_CENTER_APPLICATION_ID and PLANNING_CENTER_SECRET "
        "to this server's environment in your AI client. Get a "
        "<a style=\"color: #8dc6ff;\" href=\"https://api.planningcenteronline.com/personal_access_tokens\">Personal Access Token</a> "
        "for an account with Services access. Other MCP tools work without these credentials."), &dialog);
    planningCenterHelp->setObjectName(QStringLiteral("planningCenterSetupHelp"));
    planningCenterHelp->setWordWrap(true);
    planningCenterHelp->setOpenExternalLinks(true);
    layout->addWidget(planningCenterHelp);

    QString executable = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WorshipCueMcp"));
#ifdef Q_OS_WIN
    executable += QStringLiteral(".exe");
#endif
    const bool persistent = !databasePath_.isEmpty() && databasePath_ != QStringLiteral(":memory:");
    const bool available = QFileInfo(executable).isExecutable();
    auto *status = new QLabel(!persistent
        ? QStringLiteral("This library has no persistent database file. Open a saved library before connecting an AI client.")
        : available ? QStringLiteral("Ready. Add the configuration below to your AI client's MCP server settings, then reconnect the client.")
        : QStringLiteral("The MCP server executable is missing. Rebuild WorshipCue to install its companion server before connecting."), &dialog);
    status->setObjectName(QStringLiteral("mcpSetupStatus"));
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *paths = new QLabel(QStringLiteral("Server: %1\nLibrary: %2").arg(executable, databasePath_), &dialog);
    paths->setObjectName(QStringLiteral("mcpSetupPaths"));
    paths->setTextFormat(Qt::PlainText);
    paths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    paths->setWordWrap(true);
    layout->addWidget(paths);

    const QJsonObject server{{QStringLiteral("command"), executable},
                             {QStringLiteral("args"), QJsonArray{QStringLiteral("--database"), databasePath_}}};
    const QJsonObject config{{QStringLiteral("mcpServers"), QJsonObject{{QStringLiteral("worshipcue"), server}}}};
    const QString configuration = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Indented));
    auto *editor = new QPlainTextEdit(&dialog);
    editor->setObjectName(QStringLiteral("mcpConfiguration"));
    editor->setReadOnly(true);
    editor->setPlainText(configuration);
    layout->addWidget(editor, 1);
    auto *copy = new QPushButton(QStringLiteral("Copy Configuration"), &dialog);
    copy->setObjectName(QStringLiteral("copyMcpConfiguration"));
    copy->setEnabled(persistent && available);
    connect(copy, &QPushButton::clicked, &dialog, [configuration, copy] {
        QApplication::clipboard()->setText(configuration);
        copy->setText(QStringLiteral("Copied"));
    });
    layout->addWidget(copy);
    auto *example = new QLabel(QStringLiteral(
        "Try: “Create a service named Sunday Morning with a welcome slide, two songs from my library, "
        "a message title slide, and a closing song. Show me a preview before saving.”\n\n"
        "New services appear in the saved service selector automatically. Taking a service live remains an operator action."), &dialog);
    example->setWordWrap(true);
    layout->addWidget(example);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(actions);
    dialog.exec();
}

void MainWindow::markSaved() {
    reloadMidiBindings();
    saveInfo_->setText(QStringLiteral("✓ All service changes saved"));
}
void MainWindow::reportError(const QString &error) {
    saveInfo_->setText(QStringLiteral("Operation failed • review error"));
    QMessageBox::critical(this, QStringLiteral("WorshipCue"), error.isEmpty() ? QStringLiteral("The operation failed.") : error);
}
void MainWindow::closeEvent(QCloseEvent *event) {
    QSettings().setValue(QStringLiteral("control/geometry"), saveGeometry());
    QSettings settings;
    settings.setValue(QStringLiteral("workspace/panels"), operatorSplitter_->saveState());
    settings.setValue(QStringLiteral("workspace/sidebarView"), sidebarTabs_->currentIndex());
    settings.setValue(QStringLiteral("workspace/slideView"), slideViewMode_->currentIndex());
    settings.setValue(QStringLiteral("workspace/slideSize"), slideSize_->value());
    settings.setValue(QStringLiteral("workspace/previewTab"), previewTabs_->currentIndex());
    midi_.shutdown();
    outputs_.shutdown();
    if (cleanupLeaseTimer_) cleanupLeaseTimer_->stop();
    library_.clearCleanupLease(cleanupLeaseOwner_);
    QMainWindow::closeEvent(event);
}


QString MainWindow::managedCleanupPath(const QString &path) const {
    if (path.isEmpty() || path.startsWith(QLatin1Char(':')) || library_.databaseDirectory().isEmpty()) return {};
    const auto relative = QDir::isAbsolutePath(path) ? QDir(library_.databaseDirectory()).relativeFilePath(path) : path;
    return (relative.startsWith("media/") || relative.startsWith("backgrounds/")) && QDir::cleanPath(relative) == relative ? relative : QString();
}
CleanupProtection MainWindow::cleanupProtection(const QVector<PresentationItem> &items) const {
    CleanupProtection result;
    const auto add = [this,&result](const QString &path) { const auto relative=managedCleanupPath(path); if (!relative.isEmpty()) result.paths.insert(relative); };
    for (const auto &item:items) {
        if (item.songId) result.songs.insert(item.songId);
        if (item.mediaAssetId) result.assets.insert(item.mediaAssetId);
        for (const auto &media:item.media) add(media.path);
        add(item.backgroundPath);
        for (const auto &look:item.looks) add(look.backgroundPath);
    }
    return result;
}
bool MainWindow::prepareCleanupSnapshot(const QVector<PresentationItem> &items, QString *error) {
    auto next=cleanupProtection(items);const auto current=cleanupProtection(controller_.snapshotItems());
    next.songs.unite(current.songs);next.assets.unite(current.assets);next.paths.unite(current.paths);
    return library_.replaceCleanupLease(cleanupLeaseOwner_,next,error);
}
void MainWindow::recordLiveUsage() {
    const auto &state=controller_.state();if(state.index<0){library_.clearCleanupLease(cleanupLeaseOwner_);usageRevision_=0;return;}
    QString error;
    if(!library_.replaceCleanupLease(cleanupLeaseOwner_,cleanupProtection(controller_.snapshotItems()),&error)) { statusBar()->showMessage(error,5000); return; }
    if(usageRevision_==state.cueRevision)return;
    QSet<QString> paths;const auto media=managedCleanupPath(state.currentMedia().path),background=managedCleanupPath(state.currentBackgroundPath());
    if(!media.isEmpty())paths.insert(media);if(!background.isEmpty())paths.insert(background);
    const auto *item=controller_.liveItem();QSet<int> assets;if(item&&item->mediaAssetId)assets.insert(item->mediaAssetId);
    if(!library_.recordUsage(item?item->songId:0,assets,paths,&error)){statusBar()->showMessage(error,5000);return;}
    usageRevision_=state.cueRevision;
}
void MainWindow::configureCleanup() {
    if(mediaManager_&&mediaManager_->busy()){statusBar()->showMessage(QStringLiteral("Wait for the current import to finish before cleaning up the library."),5000);return;}
    editLibraryCleanup(this,library_,[this]{return cleanupProtection(controller_.snapshotItems());});
    reloadSongs();reloadMedia();reloadServices(currentServiceId_,selectedEntryId(),slides_->currentRow());reloadMidiBindings();
}
