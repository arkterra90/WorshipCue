#include "McpServer.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
#include <limits>

namespace {
QJsonObject result(const QJsonObject &data, bool failed = false) {
    return {{"content", QJsonArray{QJsonObject{{"type", "text"},
        {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}}}},
        {"structuredContent", data}, {"isError", failed}};
}
QJsonObject failure(const QString &error) { return result({{"error", error}}, true); }
bool keys(const QJsonObject &args, const QStringList &allowed) {
    for (auto it = args.begin(); it != args.end(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}
bool text(const QJsonValue &value, int maximum, bool nonblank = true) {
    return value.isString() && value.toString().size() <= maximum && !value.toString().contains(QChar::Null)
        && (!nonblank || !value.toString().trimmed().isEmpty());
}
bool integer(const QJsonValue &value, int minimum, int maximum, int *out) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < minimum
        || value.toDouble() > maximum || std::floor(value.toDouble()) != value.toDouble()) return false;
    *out = value.toInt();
    return true;
}
QJsonObject assetData(const MediaAsset &asset) {
    return {{"media_asset_id", asset.id}, {"name", asset.media.name}, {"folder", asset.folder.left(1024)},
        {"folder_truncated", asset.folder.size() > 1024},
        {"media_kind", asset.media.kind == MediaKind::Image ? "image" : asset.media.kind == MediaKind::Video ? "video" : "audio"},
        {"loop", asset.media.loop}, {"imported_at", asset.importedAt ? QJsonValue(asset.importedAt) : QJsonValue(QJsonValue::Null)}};
}
}

QString McpServer::presentationRevision(const Song &song) {
    // Hash edit-relevant state, including rich formatting that an AI must not
    // accidentally replace after an operator has changed it. Length framing
    // prevents ambiguous concatenation. No timestamps or credentials are used.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto add = [&](const QString &value) {
        const auto bytes = value.toUtf8();
        hash.addData(QByteArray::number(bytes.size()) + ':');
        hash.addData(bytes);
    };
    add(QString::number(song.id));
    add(song.title);
    add(song.structure.toJson());
    for (int i = 0; i < song.slides.size(); ++i) {
        add(QString::number(song.slideIds.value(i)));
        add(song.slides.at(i));
        add(song.richText.value(i));
        add(QString::number(song.appearance.slideLookIds.value(i)));
        add(QString::number(song.appearance.richTextOverrides.value(i)));
        add(QString::number(song.appearance.fitText.value(i)));
    }
    add(QString::number(song.appearance.lookId));
    return QString::fromLatin1(hash.result().toHex());
}

QJsonArray McpServer::libraryToolDefinitions() {
    auto definitions = QJsonDocument::fromJson(R"json([
      {"name":"list_media_assets","description":"Find standalone Media Library assets by name or folder. Returns IDs and metadata only, never file contents. Recovery items are excluded. Use media_asset_id in create_service_order to reuse the asset without copying or creating slides.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"query":{"type":"string","maxLength":256},"offset":{"type":"integer","minimum":0,"maximum":2147483647},"limit":{"type":"integer","minimum":1,"maximum":100}}}},
      {"name":"get_media_asset","description":"Inspect one active standalone media asset's name, type, folder, loop and import date. Unknown dates are null. Does not read or expose file contents.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"media_asset_id":{"type":"integer","minimum":1,"maximum":2147483647}},"required":["media_asset_id"]}},
      {"name":"update_song_section","description":"Edit the authoritative plain lyrics of one existing structured song section, affecting every repeated occurrence. First get_presentation for revision, section ID and slide_count. Supply exactly that many source slides; IDs, other sections, Looks, service orders and MIDI assignments survive. Changed pages lose their old rich text overrides; unchanged pages retain formatting. Defaults dry_run=true. Review the preview before saving; stale revisions fail without changes. Live output retains its captured text until the operator takes it again.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"song_id":{"type":"integer","minimum":1,"maximum":2147483647},"expected_revision":{"type":"string","pattern":"^[a-f0-9]{64}$"},"section_id":{"type":"string","minLength":1,"maxLength":64},"slides":{"type":"array","minItems":1,"maxItems":1000,"items":{"type":"string","minLength":1,"maxLength":65536}},"dry_run":{"type":"boolean","default":true}},"required":["song_id","expected_revision","section_id","slides"]}},
      {"name":"save_song_arrangement","description":"Create a named arrangement (omit arrangement_id) or replace an existing arrangement's name and ordered section references. Repeats reuse the same lyrics. First get_presentation for revision and section IDs. make_active=true selects it for natural-order services; custom service orders remain unchanged. Defaults make_active=false and dry_run=true. Returns arrangement_id; inspect after a lost save response before retrying. Does not take anything live.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"song_id":{"type":"integer","minimum":1,"maximum":2147483647},"expected_revision":{"type":"string","pattern":"^[a-f0-9]{64}$"},"arrangement_id":{"type":"string","minLength":1,"maxLength":64},"name":{"type":"string","minLength":1,"maxLength":100},"section_ids":{"type":"array","minItems":1,"maxItems":10000,"items":{"type":"string","minLength":1,"maxLength":64}},"make_active":{"type":"boolean","default":false},"dry_run":{"type":"boolean","default":true}},"required":["song_id","expected_revision","name","section_ids"]}}
    ])json").array();
    for (int i = 0; i < definitions.size(); ++i) {
        auto entry = definitions.at(i).toObject();
        const bool readOnly = i < 2;
        entry.insert("annotations", QJsonObject{{"readOnlyHint", readOnly}, {"destructiveHint", !readOnly},
            {"idempotentHint", readOnly}, {"openWorldHint", false}});
        definitions[i] = entry;
    }
    return definitions;
}

QJsonObject McpServer::callLibraryTool(const QString &name, const QJsonObject &args) {
    QString error;
    if (name == "list_media_assets") {
        int offset = 0, limit = 50;
        if (!keys(args, {"query", "offset", "limit"})
            || (args.contains("query") && !text(args["query"], 256, false))
            || (args.contains("offset") && !integer(args["offset"], 0, std::numeric_limits<int>::max(), &offset))
            || (args.contains("limit") && !integer(args["limit"], 1, 100, &limit)))
            return failure("Use query (at most 256 characters), nonnegative offset and limit 1–100, and only documented fields.");
        const auto assets = library_.mediaAssets(args["query"].toString());
        if (!library_.lastError().isEmpty()) return failure(library_.lastError());
        QJsonArray items;
        for (qsizetype i = offset; i < assets.size() && i - offset < limit; ++i) items.append(assetData(assets.at(i)));
        QJsonObject output{{"media_assets", items}, {"total", assets.size()}};
        if (qint64(offset) + limit < assets.size()) output.insert("next_offset", offset + limit);
        return result(output);
    }
    int id = 0;
    if (name == "get_media_asset") {
        if (!keys(args, {"media_asset_id"}) || !integer(args["media_asset_id"], 1, std::numeric_limits<int>::max(), &id))
            return failure("Supply only a positive integer media_asset_id.");
        const auto asset = library_.mediaAsset(id);
        if (!library_.lastError().isEmpty()) return failure(library_.lastError());
        return asset.id ? result(assetData(asset)) : failure("This media asset does not exist or is in Recovery.");
    }
    const bool sectionEdit = name == "update_song_section";
    if (!keys(args, sectionEdit ? QStringList{"song_id", "expected_revision", "section_id", "slides", "dry_run"}
                              : QStringList{"song_id", "expected_revision", "arrangement_id", "name", "section_ids", "make_active", "dry_run"})
        || !integer(args["song_id"], 1, std::numeric_limits<int>::max(), &id)
        || !text(args["expected_revision"], 64) || args["expected_revision"].toString().size() != 64
        || (args.contains("dry_run") && !args["dry_run"].isBool()))
        return failure("Supply song_id, expected_revision from get_presentation, a boolean dry_run, and only documented fields.");
    const auto original = library_.song(id);
    if (!library_.lastError().isEmpty()) return failure(library_.lastError());
    if (!original.id || original.structure.isEmpty()) return failure("Choose an existing structured song. Group legacy slides in Song Sections / Arrangements first.");
    for (const auto &media : original.media) if (media.kind != MediaKind::None) return failure("Choose a text song, not a media presentation.");
    if (args["expected_revision"].toString() != presentationRevision(original))
        return failure("This presentation changed. Call get_presentation again, review the changes, and use its new revision.");
    Song draft = original;
    QJsonObject preview{{"song_id", id}, {"expected_revision", args["expected_revision"]}};
    if (sectionEdit) {
        if (!text(args["section_id"], 64) || !args["slides"].isArray()) return failure("Supply an existing section_id and an array of plain-text slides.");
        const QString sectionId = args["section_id"].toString();
        const int start = draft.structure.sectionStart(sectionId);
        int count = 0;
        for (const auto &section : draft.structure.sections) if (section.id == sectionId) count = section.slideCount;
        const auto slides = args["slides"].toArray();
        if (start < 0 || slides.size() != count || slides.size() > 1000)
            return failure("Use an existing section ID and exactly its current slide_count. Structural changes belong in the song editor.");
        qsizetype characters = 0;
        for (int i = 0; i < slides.size(); ++i) {
            if (!text(slides.at(i), 65536)) return failure("Each slide needs nonblank text of at most 65536 characters without NUL.");
            characters += slides.at(i).toString().size();
            if (characters > 256 * 1024) return failure("A section edit is limited to 256 Ki characters; edit smaller sections in separate calls.");
            if (draft.slides.at(start + i) != slides.at(i).toString()) {
                draft.slides[start + i] = slides.at(i).toString();
                draft.richText[start + i].clear();
            }
        }
        preview.insert("section_id", sectionId);
        preview.insert("slides", slides);
    } else {
        if (!text(args["name"], 100) || !args["section_ids"].isArray()
            || args["section_ids"].toArray().isEmpty() || args["section_ids"].toArray().size() > 10000
            || (args.contains("arrangement_id") && !text(args["arrangement_id"], 64))
            || (args.contains("make_active") && !args["make_active"].isBool()))
            return failure("Supply a name (1–100 characters), 1–10000 section_ids, optional existing arrangement_id and boolean make_active.");
        SongArrangement arrangement;
        arrangement.name = args["name"].toString().trimmed();
        arrangement.id = args.contains("arrangement_id") ? args["arrangement_id"].toString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (const auto &section : args["section_ids"].toArray()) {
            if (!text(section, 64)) return failure("section_ids must contain existing section ID strings.");
            arrangement.sectionIds.append(section.toString());
        }
        int position = -1;
        for (int i = 0; i < draft.structure.arrangements.size(); ++i)
            if (draft.structure.arrangements.at(i).id == arrangement.id) position = i;
        if (args.contains("arrangement_id") && position < 0) return failure("The requested arrangement does not exist. Omit arrangement_id to create one.");
        if (position < 0) draft.structure.arrangements.append(arrangement);
        else draft.structure.arrangements[position] = arrangement;
        if (args["make_active"].toBool()) draft.structure.activeArrangementId = arrangement.id;
        preview.insert("arrangement_id", arrangement.id);
        preview.insert("song_structure", QJsonDocument::fromJson(draft.structure.toJson().toUtf8()).object());
    }
    if (!draft.structure.validate(int(draft.slides.size()), &error)) return failure(error);
    const bool dryRun = args.value("dry_run").toBool(true);
    if (!dryRun && !library_.updateSongStructure(original, draft, &error)) return failure(error);
    preview.insert("dry_run", dryRun);
    preview.insert("saved", !dryRun);
    // The next edit reads a fresh persisted revision. A new arrangement ID in
    // a dry run is only a preview; it does not identify any saved arrangement.
    return result(preview);
}
