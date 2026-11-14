#pragma once

#include <QStringList>
#include <QVector>

// Lyrics and HTML live once in the presentation's canonical slide arrays.
// Sections describe contiguous groups in those arrays. Arrangements reference IDs.
struct SongSection {
    QString id;
    QString name;
    int slideCount = 1;
    // Empty in legacy songs; numeric fallback IDs retain existing live/page keys.
    QStringList slideIds;
    QString slideId(int index) const { return slideIds.isEmpty() ? QString::number(index) : slideIds.value(index); }
    void reflow(const QVector<int> &counts);
};

struct SongArrangement {
    QString id;
    QString name;
    QStringList sectionIds;
};

struct SongStructure {
    QVector<SongSection> sections;
    QVector<SongArrangement> arrangements;
    QString activeArrangementId;

    bool isEmpty() const { return sections.isEmpty() && arrangements.isEmpty() && activeArrangementId.isEmpty(); }
    bool validate(int sourceSlideCount, QString *error = nullptr) const;
    QVector<int> slideOrder() const;
    int sectionStart(const QString &id) const;
    QString sectionName(int sourceSlide) const;
    QString slideLabel(int sourceSlide) const;
    QStringList slideKeys() const;
    QString toJson() const;
    static bool fromJson(const QString &json, int sourceSlideCount, SongStructure *result, QString *error);
    static SongStructure fromSlides(int count);
};
