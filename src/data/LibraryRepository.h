#pragma once

#include "../core/MediaSlide.h"
#include "LibraryCleanup.h"
#include "../core/MidiTypes.h"
#include "../core/PresentationLook.h"
#include "../core/SongStructure.h"
#include "../core/PresentationSettings.h"
#include <QSqlDatabase>
#include <QStringList>
#include <QVector>

struct MediaAsset {
    int id = 0;
    SlideMedia media;
    QString folder;
    qint64 importedAt = 0;
};

struct Song {
    int id = 0;
    QString title;
    QStringList slides;
    QVector<SlideMedia> media;
    PresentationSettings settings;
    QString backgroundPath;
    // Canonical editor HTML, aligned with slides; empty entries use plain text.
    QStringList richText;
    // Set only for a standalone media entry resolved for preview/playback.
    int mediaAssetId = 0;
    SongStructure structure;
    PresentationAppearance appearance;
    QVector<SlideLook> looks;
    QVector<qint64> slideIds;
    QVector<QVector<MidiOutputAction>> midiActions;
    QVector<int> playbackOrder() const {
        if (!structure.isEmpty()) return structure.slideOrder();
        QVector<int> order;
        for (int i = 0; i < slides.size(); ++i) order.append(i);
        return order;
    }
};

struct ServicePlan {
    int id = 0;
    QString name;
};

struct TransitionChange {
    enum class Target { Presentation, MediaEntry };
    Target target = Target::Presentation;
    int id = 0;
    PresentationSettings before;
    PresentationSettings after;
};

struct ServiceEntry {
    int id = 0;
    int songId = 0;
    // Source-slide indices; empty means the song's natural order.
    QVector<int> slideOrder;
    int mediaAssetId = 0;
};

// One occurrence in a new service. Use an existing songId, or leave it zero
// and provide title/slides to create a reusable text presentation.
struct ServiceOrderItem {
    int songId = 0;
    QString title;
    QStringList slides;
    QVector<int> slideOrder;
};

enum class MidiOwnerType { Slide, Entry, Asset };

class LibraryRepository {
public:
    LibraryRepository() = default;
    ~LibraryRepository();
    LibraryRepository(const LibraryRepository &) = delete;
    LibraryRepository &operator=(const LibraryRepository &) = delete;

    bool open(QString *error, const QString &databasePath = {});
    QString lastError() const { return lastError_; }
    QString databaseDirectory() const;
    QString databasePath() const { return db_.databaseName(); }
    // Changes when another connection commits; useful for refreshing the operator UI.
    qint64 dataVersion() const;
    QVector<Song> songs(const QString &search = {}) const;
    Song song(int id) const;
    bool createSong(const QString &title, const QStringList &slides, QString *error,
                    const PresentationSettings &settings = {}, const QString &backgroundPath = {},
                    const QStringList &richText = {}, const SongStructure &structure = {}, const PresentationAppearance &appearance = {});
    int createTextPresentation(const QString &title, const QStringList &slides, QString *error,
                    const PresentationSettings &settings = {}, const QString &backgroundPath = {},
                    const QStringList &richText = {}, const SongStructure &structure = {}, const PresentationAppearance &appearance = {});
    int createPresentation(const QString &title, const QStringList &texts,
                           const QVector<SlideMedia> &media, QString *error,
                           const PresentationSettings &settings = {}, const QString &backgroundPath = {},
                           const QStringList &richText = {}, const PresentationAppearance &appearance = {});
    bool updatePresentationMediaNames(int songId, const QStringList &names, QString *error);
    bool renamePresentation(int songId, const QString &title, QString *error);
    bool updatePresentationSettings(int songId, const PresentationSettings &settings, QString *error);
    // Atomic transition-only changes. Reject stale transitions without touching timing or appearance.
    bool updateTransitions(const QVector<TransitionChange> &changes, QString *error);
    bool updatePresentationBackground(int songId, const QString &path, QString *error);
    bool updateSong(int id, const QString &title, const QStringList &slides, QString *error);
    // Edits slide text in place, preserving slide identities, media, and arrangements.
    bool updatePresentationText(int songId, const QString &title, const QStringList &texts,
                                const QStringList &richText, QString *error, const QVector<bool> &lookOverrides = {});
    bool deleteSong(int id, QString *error);
    bool updateSongStructure(const Song &original, const Song &draft, QString *error);

    QVector<SavedLook> looks() const;
    SavedLook look(int id) const;
    int createLook(const QString &name, const PresentationLook &settings, QString *error);
    // Each new page maps to an original source page. Repeated indices are split pages.
    bool updatePresentationDraft(const Song &original, const Song &draft,
                                 const QVector<int> &sourceIndices, QString *error);

    QVector<MediaAsset> mediaAssets(const QString &search = {}) const;
    MediaAsset mediaAsset(int id) const;
    // Registers the batch atomically, sharing any existing managed paths.
    QVector<int> registerMediaAssets(const QVector<SlideMedia> &media, QString *error);
    bool updateMediaAsset(int id, const QString &name, const QString &folder, bool loop, QString *error);
    // Referenced assets are protected. Managed files remain available for recovery.
    bool deleteMediaAsset(int id, QString *error);
    int appendMediaToService(int serviceId, int mediaAssetId, QString *error, int beforeEntryId = 0);
    Song entrySource(int entryId) const;

    QVector<CleanupItem> cleanupCandidates(int days, const CleanupProtection &protection = {}) const;
    CleanupPlan estimateCleanup(const QVector<CleanupItem> &, const CleanupProtection & = {}) const;
    CleanupPlan previewCleanup(const QVector<CleanupKey> &, int days, const CleanupProtection & = {}) const;
    QString archiveCleanup(const QVector<CleanupKey> &, int days, const CleanupProtection &, QString *error);
    QVector<CleanupBatch> cleanupBatches() const;
    bool restoreCleanup(const QString &batch, QString *error);
    CleanupPlan previewCleanupPurge(const QString &batch, const CleanupProtection & = {}) const;
    bool purgeCleanup(const QString &batch, const CleanupPlan &reviewed, const CleanupProtection &, QString *error);
    bool recoverCleanup(QString *error);
    bool recordUsage(int songId, const QSet<int> &assets, const QSet<QString> &paths, QString *error);
    bool replaceCleanupLease(const QString &owner, const CleanupProtection &, QString *error);
    void clearCleanupLease(const QString &owner);
    UsageDates usageDates(CleanupKind kind, int id = 0, const QString &path = {}) const;
    bool recordPresentationImport(int songId, QString *error);
    bool recordManagedImport(const QString &path, QString *error);

    QVector<MidiOutputAction> midiActions(MidiOwnerType type, qint64 id) const;
    bool setMidiActions(MidiOwnerType type, qint64 id, const QVector<MidiOutputAction> &original,
                        const QVector<MidiOutputAction> &actions, QString *error);
    QVector<MidiBinding> midiBindings() const;
    MidiBinding midiBinding(qint64 id) const;
    bool saveMidiBinding(MidiBinding binding, QString *error, const MidiBinding *original = nullptr);
    bool deleteMidiBinding(qint64 id, QString *error);
    int entryServiceId(int entryId) const;

    QVector<ServicePlan> services() const;
    int createService(const QString &name, QString *error);
    // Saves the complete service and any new text presentations in one transaction.
    // Returns zero on failure; no part of the order is saved in that case.
    int createServiceOrder(const QString &name, const QVector<ServiceOrderItem> &items, QString *error);
    bool renameService(int id, const QString &name, QString *error);
    int duplicateService(int id, const QString &name, QString *error);
    QVector<ServiceEntry> service(int serviceId) const;
    bool appendToService(int serviceId, int songId, QString *error);
    bool removeFromService(int entryId, QString *error);
    bool moveServiceEntry(int serviceId, int from, int to, QString *error);
    bool reorderServiceEntries(int serviceId, const QVector<int> &ids, QString *error);
    bool duplicateServiceEntry(int entryId, QString *error);
    QStringList entrySlides(int entryId) const;
    QStringList entryRichText(int entryId) const;
    QVector<SlideMedia> entryMedia(int entryId) const;
    QVector<SlideLook> entryLooks(int entryId) const;
    bool setEntrySlideOrder(int entryId, const QVector<int> &indices, QString *error);

private:
    QSqlDatabase db_;
    mutable QString lastError_;
    void close();
    void clearError(QString *error = nullptr) const;
    bool execute(const QString &sql, QString *error = nullptr) const;
    bool storeSlides(int songId, const QStringList &slides, QString *error,
                     const QStringList &richText = {}, const PresentationAppearance &appearance = {});
    bool exists(const QString &table, int id, QString *error) const;
    int registerMediaAsset(const SlideMedia &media, QString *error);
    Song entryContent(int entryId) const;
    bool validateMidiTarget(const MidiBinding &, QString *error) const;
    bool copyEntryMidiActions(int source, int target, QString *error);
    bool migrateCleanup(QString *error);
    CleanupProtection cleanupLeases(const CleanupProtection &) const;
    QStringList cleanupPaths(CleanupKind kind, int id) const;
    QString cleanupReferenceReason(const QString &path, const QSet<int> &excludeSongs = {}, const QSet<int> &excludeAssets = {}) const;
    CleanupPlan cleanupPlan(const QVector<CleanupItem> &, const CleanupProtection &) const;
    bool fail(const QString &message, QString *error = nullptr) const;
};
