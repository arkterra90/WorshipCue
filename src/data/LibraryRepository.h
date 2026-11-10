#pragma once

#include "../core/MediaSlide.h"
#include "../core/PresentationSettings.h"
#include <QSqlDatabase>
#include <QStringList>
#include <QVector>

struct Song {
    int id = 0;
    QString title;
    QStringList slides;
    QVector<SlideMedia> media;
    PresentationSettings settings;
};

struct ServicePlan {
    int id = 0;
    QString name;
};

struct ServiceEntry {
    int id = 0;
    int songId = 0;
    // Source-slide indices; empty means the song's natural order.
    QVector<int> slideOrder;
};

class LibraryRepository {
public:
    LibraryRepository() = default;
    ~LibraryRepository();
    LibraryRepository(const LibraryRepository &) = delete;
    LibraryRepository &operator=(const LibraryRepository &) = delete;

    bool open(QString *error, const QString &databasePath = {});
    QString lastError() const { return lastError_; }
    QString databaseDirectory() const;
    QVector<Song> songs(const QString &search = {}) const;
    Song song(int id) const;
    bool createSong(const QString &title, const QStringList &slides, QString *error,
                    const PresentationSettings &settings = {});
    int createPresentation(const QString &title, const QStringList &texts,
                           const QVector<SlideMedia> &media, QString *error,
                           const PresentationSettings &settings = {});
    bool updatePresentationMediaNames(int songId, const QStringList &names, QString *error);
    bool renamePresentation(int songId, const QString &title, QString *error);
    bool updatePresentationSettings(int songId, const PresentationSettings &settings, QString *error);
    bool updateSong(int id, const QString &title, const QStringList &slides, QString *error);
    bool deleteSong(int id, QString *error);

    QVector<ServicePlan> services() const;
    int createService(const QString &name, QString *error);
    bool renameService(int id, const QString &name, QString *error);
    int duplicateService(int id, const QString &name, QString *error);
    QVector<ServiceEntry> service(int serviceId) const;
    bool appendToService(int serviceId, int songId, QString *error);
    bool removeFromService(int entryId, QString *error);
    bool moveServiceEntry(int serviceId, int from, int to, QString *error);
    bool reorderServiceEntries(int serviceId, const QVector<int> &ids, QString *error);
    bool duplicateServiceEntry(int entryId, QString *error);
    QStringList entrySlides(int entryId) const;
    QVector<SlideMedia> entryMedia(int entryId) const;
    bool setEntrySlideOrder(int entryId, const QVector<int> &indices, QString *error);

private:
    QSqlDatabase db_;
    mutable QString lastError_;
    void close();
    void clearError(QString *error = nullptr) const;
    bool execute(const QString &sql, QString *error = nullptr) const;
    bool storeSlides(int songId, const QStringList &slides, QString *error);
    bool exists(const QString &table, int id, QString *error) const;
    bool fail(const QString &message, QString *error = nullptr) const;
};
