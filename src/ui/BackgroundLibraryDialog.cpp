#include "BackgroundLibraryDialog.h"
#include "SlideRenderer.h"
#include "../core/BuiltInBackgrounds.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QPixmap imagePreview(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid()) reader.setScaledSize(size.scaled(QSize(384, 216), Qt::KeepAspectRatio));
    return QPixmap::fromImage(reader.read());
}

QString libraryHint(bool assignmentEnabled) {
    const QString choices = QStringLiteral("%1 ready-to-use backgrounds are included. ")
        .arg(BuiltInBackgrounds::catalog().size());
    return choices + (assignmentEnabled ? QStringLiteral(
        "Choose one for this presentation, or import your own images. Lyrics and pictures appear above this layer. "
        "Imported images are copied into your library for reuse.") : QStringLiteral(
        "Browse the collection or import your own images. Select a presentation, then open this library "
        "again to assign its background. Imported images are copied and your originals are preserved."));
}
}

BackgroundLibraryDialog::BackgroundLibraryDialog(const QString &databaseDirectory,
    const QString &currentPath, QWidget *parent) : QDialog(parent), directory_(databaseDirectory) {
    setObjectName(QStringLiteral("backgroundLibraryDialog"));
    setWindowTitle(QStringLiteral("Background Library"));
    resize(760, 620);
    auto *layout = new QVBoxLayout(this);
    hint_ = new QLabel(libraryHint(true), this);
    hint_->setWordWrap(true);
    layout->addWidget(hint_);
    auto *import = new QPushButton(QStringLiteral("Import Backgrounds…"), this);
    import->setObjectName(QStringLiteral("importBackgrounds"));
    connect(import, &QPushButton::clicked, this, &BackgroundLibraryDialog::chooseFiles);
    layout->addWidget(import, 0, Qt::AlignLeft);
    backgrounds_ = new QListWidget(this);
    backgrounds_->setObjectName(QStringLiteral("backgroundLibraryItems"));
    backgrounds_->setViewMode(QListView::IconMode);
    backgrounds_->setResizeMode(QListView::Adjust);
    backgrounds_->setMovement(QListView::Static);
    backgrounds_->setIconSize(QSize(192, 108));
    backgrounds_->setGridSize(QSize(216, 150));
    backgrounds_->setWordWrap(true);
    backgrounds_->setSpacing(4);
    layout->addWidget(backgrounds_, 1);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    assign_ = actions->addButton(QStringLiteral("Assign Background"), QDialogButtonBox::AcceptRole);
    assign_->setObjectName(QStringLiteral("assignBackground"));
    assign_->setDefault(true);
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(backgrounds_, &QListWidget::itemDoubleClicked, this, [this] {
        if (assignmentEnabled_) accept();
    });
    layout->addWidget(actions);
    reload(currentPath);
}

void BackgroundLibraryDialog::setAssignmentEnabled(bool enabled) {
    assignmentEnabled_ = enabled;
    assign_->setEnabled(enabled);
    hint_->setText(libraryHint(enabled));
}

QString BackgroundLibraryDialog::selectedPath() const {
    const auto *item = backgrounds_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

void BackgroundLibraryDialog::reload(const QString &selectPath) {
    backgrounds_->clear();
    auto *builtIn = new QListWidgetItem(
        QIcon(SlideRenderer::thumbnail(QString(), QSize(192, 108))),
        QStringLiteral("Default background"), backgrounds_);
    builtIn->setData(Qt::UserRole, QStringLiteral(""));
    builtIn->setToolTip(QStringLiteral("Use the built-in background for this presentation."));
    backgrounds_->setCurrentItem(builtIn);
    for (const auto &background : BuiltInBackgrounds::catalog()) {
        const QPixmap preview = imagePreview(BuiltInBackgrounds::resourcePath(background.path));
        auto *item = new QListWidgetItem(
            QIcon(SlideRenderer::thumbnail(QString(), QSize(192, 108), preview)), background.title, backgrounds_);
        item->setData(Qt::UserRole, background.path);
        item->setToolTip(QStringLiteral("%1 — included with WorshipCue. Available offline in every library.")
            .arg(background.title));
        if (background.path == selectPath) backgrounds_->setCurrentItem(item);
    }
    const QDir backgrounds(QDir(directory_).filePath(QStringLiteral("backgrounds")));
    const auto files = backgrounds.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    for (const auto &file : files) {
        QImageReader reader(file.absoluteFilePath());
        if (!reader.canRead()) continue;
        const QString relative = QStringLiteral("backgrounds/") + file.fileName();
        const QPixmap preview = imagePreview(file.absoluteFilePath());
        if (preview.isNull()) continue;
        auto *item = new QListWidgetItem(QIcon(SlideRenderer::thumbnail(QString(), QSize(192, 108), preview)),
                                         file.completeBaseName(), backgrounds_);
        item->setData(Qt::UserRole, relative);
        item->setToolTip(QStringLiteral("Imported background: %1").arg(file.fileName()));
        if (relative == selectPath) backgrounds_->setCurrentItem(item);
    }
    if (!selectPath.isEmpty() && selectedPath() != selectPath) {
        auto *missing = new QListWidgetItem(QStringLiteral("Missing background\n%1").arg(QFileInfo(selectPath).fileName()), backgrounds_);
        missing->setData(Qt::UserRole, selectPath);
        missing->setToolTip(QStringLiteral("The assigned file is missing. Choose another background to replace it."));
        backgrounds_->setCurrentItem(missing);
    }
    backgrounds_->scrollToItem(backgrounds_->currentItem());
}

bool BackgroundLibraryDialog::importFiles(const QStringList &paths, QString *error) {
    if (error) error->clear();
    if (paths.isEmpty()) return true;
    const QString backgroundDirectory = QDir(directory_).filePath(QStringLiteral("backgrounds"));
    if (!QDir().mkpath(backgroundDirectory)) {
        if (error) *error = QStringLiteral("Could not create the background library folder.");
        return false;
    }
    QStringList failures;
    QString selection = selectedPath();
    const QDir target(backgroundDirectory);
    for (const auto &path : paths) {
        const QFileInfo source(path);
        // Decode before copying so unsupported/corrupt content never enters the library.
        if (!source.isFile() || imagePreview(path).isNull()) {
            failures.append(QStringLiteral("%1: this file could not be read as an image.").arg(source.fileName()));
            continue;
        }
        QString base = source.completeBaseName();
        base.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
        base = base.trimmed();
        while (base.endsWith(QLatin1Char('.'))) base.chop(1);
        if (base.isEmpty()) base = QStringLiteral("Background");
        const QString suffix = source.suffix().isEmpty() ? QStringLiteral("img") : source.suffix().toLower();
        QString name = base + QLatin1Char('.') + suffix;
        for (int serial = 2; QFileInfo::exists(target.filePath(name)); ++serial)
            name = QStringLiteral("%1 (%2).%3").arg(base).arg(serial).arg(suffix);
        if (!QFile::copy(path, target.filePath(name))) {
            failures.append(QStringLiteral("%1: the file could not be copied into the library.").arg(source.fileName()));
            continue;
        }
        selection = QStringLiteral("backgrounds/") + name;
    }
    reload(selection);
    if (error) *error = failures.join(QLatin1Char('\n'));
    return failures.isEmpty();
}

void BackgroundLibraryDialog::chooseFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Import Backgrounds"), {},
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;All files (*)"));
    if (paths.isEmpty()) return;
    QString error;
    if (!importFiles(paths, &error))
        QMessageBox::warning(this, QStringLiteral("Background import"), error);
}
