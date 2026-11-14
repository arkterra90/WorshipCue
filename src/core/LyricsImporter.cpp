#include "LyricsImporter.h"

#include <QRegularExpression>
#include <QSet>
#include <QUuid>

ImportedLyrics LyricsImporter::parse(const QString &text, const QString &suggestedTitle,
                                     bool chordPro) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (normalized.startsWith(QChar(0xfeff))) normalized.remove(0, 1);

    static const QRegularExpression section(QStringLiteral(
        "^(?:verse(?:[ \\t]+\\d+)?|chorus(?:[ \\t]+\\d+)?|pre[- ]?chorus(?:[ \\t]+\\d+)?|"
        "bridge(?:[ \\t]+\\d+)?|refrain|intro|outro|tag|ending|instrumental)(?:[ \\t]*:)?$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression directive(QStringLiteral("^\\{([a-z_]+)(?::[ \\t]*(.*))?\\}$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression chord(QStringLiteral(
        "\\[(?:[A-G](?:#|b)?(?:(?:maj|min|dim|aug|sus|add|no|m|M)|[0-9]+|[#b][0-9]+|"
        "\\((?:add|sus|#|b)?[0-9]+\\))*"
        "(?:/[A-G](?:#|b)?)?|N\\.C\\.)\\]"));

    ImportedLyrics result;
    result.title = suggestedTitle.trimmed();
    QStringList lines;
    QString sectionName;
    int sectionIndex = -1;
    const auto flush = [&] {
        const QString slide = lines.join(QLatin1Char('\n')).trimmed();
        if (!slide.isEmpty()) {
            result.slides.append(slide);
            if (sectionName.isEmpty()) result.sections.append({QStringLiteral("Section %1").arg(result.sections.size() + 1), {slide}});
            else {
                if (sectionIndex < 0) {
                    sectionIndex = int(result.sections.size());
                    result.sections.append({sectionName, {}});
                }
                result.sections[sectionIndex].slides.append(slide);
            }
        }
        lines.clear();
    };
    for (QString line : normalized.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (chordPro && trimmed.startsWith(QLatin1Char('#'))) continue;
        const auto tag = directive.match(trimmed);
        if (tag.hasMatch()) {
            const QString key = tag.captured(1).toLower();
            const QString value = tag.captured(2).trimmed();
            if (key == QStringLiteral("title") || key == QStringLiteral("t")) {
                if (!value.isEmpty()) result.title = value;
                chordPro = true;
                continue;
            }
            if (key == QStringLiteral("start_of_chorus") || key == QStringLiteral("soc")
                || key == QStringLiteral("end_of_chorus") || key == QStringLiteral("eoc")
                || key == QStringLiteral("start_of_verse") || key == QStringLiteral("sov")
                || key == QStringLiteral("end_of_verse") || key == QStringLiteral("eov")
                || key == QStringLiteral("comment") || key == QStringLiteral("c")
                || key == QStringLiteral("comment_italic") || key == QStringLiteral("ci")) {
                flush();
                sectionIndex = -1;
                sectionName.clear();
                if (key == QStringLiteral("start_of_chorus") || key == QStringLiteral("soc")) sectionName = QStringLiteral("Chorus");
                if (key == QStringLiteral("start_of_verse") || key == QStringLiteral("sov")) sectionName = QStringLiteral("Verse");
                if (key == QStringLiteral("comment") || key == QStringLiteral("c")
                    || key == QStringLiteral("comment_italic") || key == QStringLiteral("ci")) sectionName = value.left(100);
                chordPro = true;
                continue;
            }
            if (key == QStringLiteral("subtitle") || key == QStringLiteral("st")
                || key == QStringLiteral("artist") || key == QStringLiteral("composer")
                || key == QStringLiteral("lyricist") || key == QStringLiteral("copyright")
                || key == QStringLiteral("key") || key == QStringLiteral("tempo")
                || key == QStringLiteral("capo") || key == QStringLiteral("time")
                || key == QStringLiteral("define") || key == QStringLiteral("meta")) {
                chordPro = true;
                continue;
            }
        }
        QString heading = trimmed;
        if (heading.startsWith(QLatin1Char('[')) && heading.endsWith(QLatin1Char(']')))
            heading = heading.mid(1, heading.size() - 2).trimmed();
        if (section.match(heading).hasMatch()) {
            flush();
            if (heading.endsWith(QLatin1Char(':'))) heading.chop(1);
            sectionName = heading.trimmed();
            sectionIndex = -1;
            continue;
        }
        if (trimmed.isEmpty() || trimmed == QStringLiteral("---")) {
            flush();
            continue;
        }
        // Chord tokens are also recognized in clipboard text, where there is no
        // filename extension to identify ChordPro. Other bracketed words survive.
        if (chordPro || chord.match(line).hasMatch()) line.remove(chord);
        if (!line.trimmed().isEmpty()) lines.append(line.trimmed());
    }
    flush();
    if (result.title.isEmpty() && !result.slides.isEmpty())
        result.title = result.slides.first().section(QLatin1Char('\n'), 0, 0).left(100);
    return result;
}

SongStructure ImportedLyrics::songStructure(QStringList *sourceSlides) const {
    sourceSlides->clear();
    SongStructure structure;
    SongArrangement arrangement{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("Default"), {}};
    auto groups = sections;
    if (groups.isEmpty()) for (int i = 0; i < slides.size(); ++i)
        groups.append({QStringLiteral("Section %1").arg(i + 1), {slides.at(i)}});
    QVector<QStringList> sectionSlides;
    QStringList sectionBaseNames;
    QSet<QString> names;
    for (const auto &group : groups) {
        int existing = -1;
        const QString base = group.name.trimmed().left(90);
        for (int i = 0; i < structure.sections.size(); ++i)
            if (sectionBaseNames.at(i).compare(base, Qt::CaseInsensitive) == 0
                && sectionSlides.at(i) == group.slides) { existing = i; break; }
        if (existing >= 0) {
            arrangement.sectionIds.append(structure.sections.at(existing).id);
            continue;
        }
        QString name = base.isEmpty() ? QStringLiteral("Section") : base;
        int suffix = 2;
        while (names.contains(name.toCaseFolded())) name = base + QStringLiteral(" %1").arg(suffix++);
        names.insert(name.toCaseFolded());
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        structure.sections.append({id, name, int(group.slides.size())});
        sourceSlides->append(group.slides);
        sectionSlides.append(group.slides);
        sectionBaseNames.append(base);
        arrangement.sectionIds.append(id);
    }
    structure.arrangements.append(arrangement);
    structure.activeArrangementId = arrangement.id;
    return structure;
}
