#pragma once

#include "SongStructure.h"
#include <QString>
#include <QStringList>

struct ImportedLyricSection {
    QString name;
    QStringList slides;
};

struct ImportedLyrics {
    QString title;
    QStringList slides;
    QVector<ImportedLyricSection> sections;
    SongStructure songStructure(QStringList *sourceSlides) const;
};

// Import produces ordinary editable song slides; it does not write the library.
class LyricsImporter {
public:
    static ImportedLyrics parse(const QString &text, const QString &suggestedTitle = {},
                                bool chordPro = false);
};
