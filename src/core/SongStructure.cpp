#include "SongStructure.h"

#include <QHash>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validLabel(const QString &value, int limit) {
    return !value.trimmed().isEmpty() && value.size() <= limit && !value.contains(QChar::Null);
}
}

void SongSection::reflow(const QVector<int> &counts) {
    if (!slideIds.isEmpty()) {
        QStringList expanded;
        QSet<QString> used(slideIds.cbegin(), slideIds.cend());
        for (int i = 0; i < counts.size(); ++i) {
            const auto source = slideId(i);
            expanded.append(source);
            for (int part = 1; part < counts.at(i); ++part) {
                int salt = part;
                QString generated;
                do {
                    generated = QString::fromLatin1(QCryptographicHash::hash(
                        (source + QLatin1Char(':') + QString::number(salt++)).toUtf8(), QCryptographicHash::Sha256).toHex());
                } while (used.contains(generated));
                used.insert(generated);
                expanded.append(generated);
            }
        }
        slideIds = expanded;
    }
    slideCount = 0;
    for (int count : counts) slideCount += count;
}

bool SongStructure::validate(int sourceSlideCount, QString *error) const {
    if (error) error->clear();
    const auto reject = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (isEmpty()) return true;
    if (sections.isEmpty() || sections.size() > 1000 || arrangements.isEmpty() || arrangements.size() > 100)
        return reject(QStringLiteral("A structured song needs 1–1000 sections and 1–100 arrangements."));
    QSet<QString> sectionIds, sectionNames;
    QHash<QString, int> sectionCounts;
    qint64 count = 0;
    for (const auto &section : sections) {
        const auto name = section.name.trimmed().toCaseFolded();
        if (!validLabel(section.id, 64) || !validLabel(section.name, 100)
            || sectionIds.contains(section.id) || sectionNames.contains(name)
            || section.slideCount < 1 || section.slideCount > 1000)
            return reject(QStringLiteral("Sections need unique IDs, unique names, and 1–1000 slides each."));
        if (!section.slideIds.isEmpty()) {
            if (section.slideIds.size() != section.slideCount)
                return reject(QStringLiteral("Each section slide needs one stable slide ID."));
            QSet<QString> pageIds;
            for (const auto &id : section.slideIds) {
                if (!validLabel(id, 64) || id.contains(QLatin1Char('/')) || pageIds.contains(id))
                    return reject(QStringLiteral("Section slide IDs must be valid and unique within their section."));
                pageIds.insert(id);
            }
        }
        sectionIds.insert(section.id);
        sectionCounts.insert(section.id, section.slideCount);
        sectionNames.insert(name);
        count += section.slideCount;
    }
    if (count != sourceSlideCount || count > 10000)
        return reject(QStringLiteral("Section slide counts must match the song's source slides (at most 10000)."));
    QSet<QString> arrangementIds, arrangementNames;
    bool activeFound = false;
    qsizetype totalReferences = 0;
    for (const auto &arrangement : arrangements) {
        const auto name = arrangement.name.trimmed().toCaseFolded();
        if (!validLabel(arrangement.id, 64) || !validLabel(arrangement.name, 100)
            || arrangementIds.contains(arrangement.id) || arrangementNames.contains(name)
            || arrangement.sectionIds.isEmpty() || arrangement.sectionIds.size() > 10000)
            return reject(QStringLiteral("Arrangements need unique IDs, unique names, and 1–10000 section references."));
        arrangementIds.insert(arrangement.id);
        arrangementNames.insert(name);
        activeFound = activeFound || arrangement.id == activeArrangementId;
        qint64 slideCount = 0;
        for (const auto &id : arrangement.sectionIds) {
            if (!sectionIds.contains(id)) return reject(QStringLiteral("An arrangement references a missing section."));
            slideCount += sectionCounts.value(id);
        }
        if (slideCount > 10000) return reject(QStringLiteral("An arrangement may contain at most 10000 slides."));
        totalReferences += arrangement.sectionIds.size();
    }
    if (totalReferences > 50000) return reject(QStringLiteral("A song may contain at most 50000 section references across arrangements."));
    if (!activeFound) return reject(QStringLiteral("Choose a saved arrangement for this song."));
    return true;
}

int SongStructure::sectionStart(const QString &id) const {
    int start = 0;
    for (const auto &section : sections) {
        if (section.id == id) return start;
        start += section.slideCount;
    }
    return -1;
}

QString SongStructure::sectionName(int sourceSlide) const {
    for (const auto &section : sections) {
        if (sourceSlide >= 0 && sourceSlide < section.slideCount) return section.name;
        sourceSlide -= section.slideCount;
    }
    return {};
}

QString SongStructure::slideLabel(int sourceSlide) const {
    for (const auto &section : sections) {
        if (sourceSlide >= 0 && sourceSlide < section.slideCount)
            return QStringLiteral("%1 • Slide %2").arg(section.name).arg(sourceSlide + 1);
        sourceSlide -= section.slideCount;
    }
    return {};
}

QVector<int> SongStructure::slideOrder() const {
    QVector<int> order;
    QHash<QString, QPair<int, int>> ranges;
    int start = 0;
    for (const auto &section : sections) {
        ranges.insert(section.id, {start, section.slideCount});
        start += section.slideCount;
    }
    for (const auto &arrangement : arrangements) {
        if (arrangement.id != activeArrangementId) continue;
        for (const auto &id : arrangement.sectionIds) {
            const auto range = ranges.value(id);
            for (int i = 0; i < range.second; ++i) order.append(range.first + i);
        }
    }
    return order;
}

QStringList SongStructure::slideKeys() const {
    QStringList keys;
    for (const auto &section : sections)
        for (int i = 0; i < section.slideCount; ++i)
            keys.append(section.id + QLatin1Char('/') + section.slideId(i));
    return keys;
}

QString SongStructure::toJson() const {
    if (isEmpty()) return QStringLiteral("");
    QJsonArray storedSections, storedArrangements;
    for (const auto &section : sections) {
        QJsonObject object{{QStringLiteral("id"), section.id}, {QStringLiteral("name"), section.name},
                           {QStringLiteral("slide_count"), section.slideCount}};
        if (!section.slideIds.isEmpty()) object.insert(QStringLiteral("slide_ids"), QJsonArray::fromStringList(section.slideIds));
        storedSections.append(object);
    }
    for (const auto &arrangement : arrangements) {
        QJsonArray ids;
        for (const auto &id : arrangement.sectionIds) ids.append(id);
        storedArrangements.append(QJsonObject{{QStringLiteral("id"), arrangement.id}, {QStringLiteral("name"), arrangement.name},
                                             {QStringLiteral("sections"), ids}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("sections"), storedSections},
        {QStringLiteral("arrangements"), storedArrangements}, {QStringLiteral("active"), activeArrangementId}}).toJson(QJsonDocument::Compact));
}

bool SongStructure::fromJson(const QString &json, int sourceSlideCount, SongStructure *result, QString *error) {
    *result = {};
    if (json.isEmpty()) return true;
    if (json.size() > 8 * 1024 * 1024) {
        if (error) *error = QStringLiteral("Song section metadata exceeds the 8 MB limit.");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    const auto object = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !object.value(QStringLiteral("sections")).isArray() || !object.value(QStringLiteral("arrangements")).isArray()
        || !object.value(QStringLiteral("active")).isString()) {
        if (error) *error = QStringLiteral("This song contains invalid section or arrangement metadata.");
        return false;
    }
    for (const auto &value : object.value(QStringLiteral("sections")).toArray()) {
        const auto section = value.toObject();
        const auto count = section.value(QStringLiteral("slide_count"));
        if (!section.value(QStringLiteral("id")).isString() || !section.value(QStringLiteral("name")).isString()
            || !count.isDouble() || count.toInt(-1) != count.toDouble()) {
            if (error) *error = QStringLiteral("This song contains invalid section metadata.");
            return false;
        }
        SongSection parsed{section.value(QStringLiteral("id")).toString(), section.value(QStringLiteral("name")).toString(), count.toInt(), {}};
        if (section.contains(QStringLiteral("slide_ids"))) {
            const auto ids = section.value(QStringLiteral("slide_ids"));
            if (!ids.isArray() || ids.toArray().size() != parsed.slideCount) {
                if (error) *error = QStringLiteral("This section contains invalid slide IDs.");
                return false;
            }
            for (const auto &id : ids.toArray()) {
                if (!id.isString()) { if (error) *error = QStringLiteral("Invalid section slide ID."); return false; }
                parsed.slideIds.append(id.toString());
            }
        }
        result->sections.append(parsed);
    }
    for (const auto &value : object.value(QStringLiteral("arrangements")).toArray()) {
        const auto arrangement = value.toObject();
        if (!arrangement.value(QStringLiteral("id")).isString() || !arrangement.value(QStringLiteral("name")).isString()
            || !arrangement.value(QStringLiteral("sections")).isArray()) {
            if (error) *error = QStringLiteral("This song contains invalid arrangement metadata.");
            return false;
        }
        SongArrangement parsed{arrangement.value(QStringLiteral("id")).toString(), arrangement.value(QStringLiteral("name")).toString(), {}};
        for (const auto &id : arrangement.value(QStringLiteral("sections")).toArray()) {
            if (!id.isString()) { if (error) *error = QStringLiteral("An arrangement contains an invalid section ID."); return false; }
            parsed.sectionIds.append(id.toString());
        }
        result->arrangements.append(parsed);
    }
    result->activeArrangementId = object.value(QStringLiteral("active")).toString();
    return result->validate(sourceSlideCount, error);
}

SongStructure SongStructure::fromSlides(int count) {
    SongStructure structure;
    SongArrangement arrangement{newId(), QStringLiteral("Default"), {}};
    for (int i = 0; i < count; ++i) {
        const QString id = newId();
        structure.sections.append({id, QStringLiteral("Section %1").arg(i + 1), 1});
        arrangement.sectionIds.append(id);
    }
    structure.arrangements.append(arrangement);
    structure.activeArrangementId = arrangement.id;
    return structure;
}
