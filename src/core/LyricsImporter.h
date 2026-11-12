#pragma once

#include <QString>
#include <QStringList>

struct ImportedLyrics {
    QString title;
    QStringList slides;
};

// Import produces ordinary editable song slides; it does not write the library.
class LyricsImporter {
public:
    static ImportedLyrics parse(const QString &text, const QString &suggestedTitle = {},
                                bool chordPro = false);
};
