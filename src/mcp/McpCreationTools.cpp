#include "McpServer.h"
#include "../core/LyricsImporter.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <limits>

namespace {
QJsonObject result(const QJsonObject &data, bool failed = false) {
    return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}}}},
        {"structuredContent", data}, {"isError", failed}};
}
QJsonObject failure(const QString &error) { return result({{"error", error}}, true); }
bool keys(const QJsonObject &data, const QStringList &allowed) {
    for (auto it = data.begin(); it != data.end(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}
bool text(const QJsonValue &value, int maximum, bool required = true) {
    return value.isString() && value.toString().size() <= maximum && !value.toString().contains(QChar::Null)
        && (!required || !value.toString().trimmed().isEmpty());
}
bool page(const QJsonObject &args, const QString &field, int min, int max, int *value) {
    if (!args.contains(field)) return true;
    const auto number = args.value(field);
    if (!number.isDouble() || number.toDouble() < min || number.toDouble() > max
        || std::floor(number.toDouble()) != number.toDouble()) return false;
    *value = number.toInt();
    return true;
}
QString relationshipId(const QJsonObject &item, const QString &relationship) {
    return item.value("relationships").toObject().value(relationship).toObject().value("data").toObject().value("id").toString();
}
QString sectionKey(QString label) {
    label = label.toCaseFolded();
    label.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    if (!label.isEmpty() && !label.back().isDigit()) label += QLatin1Char('1');
    return label;
}
struct Lyrics {
    QStringList slides;
    QMap<QString, QVector<int>> sections;
};
Lyrics splitLyrics(QString raw) {
    raw.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    raw.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    static const QRegularExpression heading(QStringLiteral(
        "^\\[?((?:verse|chorus|pre[- ]?chorus|bridge|refrain|intro|outro|tag|ending|instrumental)(?:[ \\t]*[0-9]+)?)\\]?:?$"),
        QRegularExpression::CaseInsensitiveOption);
    Lyrics output;
    QString current;
    QStringList lines;
    const auto flush = [&] {
        const auto slides = LyricsImporter::parse(lines.join(QLatin1Char('\n'))).slides;
        for (const auto &slide : slides) {
            if (!current.isEmpty()) output.sections[current].append(int(output.slides.size()));
            output.slides.append(slide);
        }
        lines.clear();
    };
    for (const auto &line : raw.split(QLatin1Char('\n'))) {
        const auto match = heading.match(line.trimmed());
        if (match.hasMatch()) { flush(); current = sectionKey(match.captured(1)); }
        else lines.append(line);
    }
    flush();
    return output;
}
QJsonArray asArray(const QStringList &slides) {
    QJsonArray array;
    for (const auto &slide : slides) array.append(slide);
    return array;
}
}

QJsonArray McpServer::extraToolDefinitions() {
    // Schemas are also checked at runtime; credentials are never tool arguments.
    const auto definitions = QJsonDocument::fromJson(R"json([
      {"name":"create_song_presentation","description":"Create a reusable, editable song presentation in the WorshipCue library. Supply title and either explicit slides OR lyrics (blank lines, section headings and --- split slides; bracketed chords are stripped). dry_run previews without saving. Returns song_id for use in create_service_order.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"title":{"type":"string","minLength":1,"maxLength":256},"lyrics":{"type":"string","minLength":1,"maxLength":1048576},"slides":{"type":"array","minItems":1,"maxItems":1000,"items":{"type":"string","minLength":1,"maxLength":65536}},"dry_run":{"type":"boolean","default":false}},"required":["title"],"oneOf":[{"required":["lyrics"]},{"required":["slides"]}]}},
      {"name":"create_sermon_presentation","description":"Create a reusable, editable sermon presentation: a title slide followed by your explicit content slides. Optional speaker and scripture appear on the title slide. Supply actual sermon/scripture content, never invent attributed quotations. Stored as an ordinary WorshipCue text presentation. dry_run previews without saving; returns song_id for service orders.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"title":{"type":"string","minLength":1,"maxLength":256},"speaker":{"type":"string","maxLength":256},"scripture":{"type":"string","maxLength":1024},"slides":{"type":"array","minItems":1,"maxItems":999,"items":{"type":"string","minLength":1,"maxLength":65536}},"dry_run":{"type":"boolean","default":false}},"required":["title","slides"]}},
      {"name":"list_planning_center_service_types","description":"Read Planning Center Services service type IDs/names. Requires PLANNING_CENTER_APPLICATION_ID and PLANNING_CENTER_SECRET in the server environment (Personal Access Token).","inputSchema":{"type":"object","additionalProperties":false,"properties":{"offset":{"type":"integer","minimum":0,"maximum":2147483647},"limit":{"type":"integer","minimum":1,"maximum":100}}}},
      {"name":"list_planning_center_plans","description":"Read saved Planning Center plans for a service_type_id, newest dates first. Returns API data with plan IDs/dates/titles and pagination links. Does not modify Planning Center.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"service_type_id":{"type":"string","pattern":"^[1-9][0-9]{0,19}$"},"offset":{"type":"integer","minimum":0,"maximum":2147483647},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["service_type_id"]}},
      {"name":"get_planning_center_plan","description":"Read a complete Planning Center plan and ordered items with included songs/arrangements and available lyrics. Details/notes are planning data, not automatically audience slide content. IDs are strings. Does not modify either app.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"service_type_id":{"type":"string","pattern":"^[1-9][0-9]{0,19}$"},"plan_id":{"type":"string","pattern":"^[1-9][0-9]{0,19}$"}},"required":["service_type_id","plan_id"]}},
      {"name":"import_planning_center_service","description":"Preview or atomically create a WorshipCue service from a Planning Center plan. Defaults dry_run=true. Preserves pre/during/post and item sequence. Reuses unique matching library songs; imports available arrangement lyrics and labelled sequences. Headers are skipped explicitly; ordinary items become title slides. Media/missing lyrics/ambiguous matches/unmappable sequences block saving until item_overrides supplies song_id/slide_order, title/slides, or skip:true. allow_placeholders=true explicitly permits title-only placeholders. No files are downloaded, no Planning Center writes, and private item notes are not projected. Each save creates a NEW service.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"service_type_id":{"type":"string","pattern":"^[1-9][0-9]{0,19}$"},"plan_id":{"type":"string","pattern":"^[1-9][0-9]{0,19}$"},"name":{"type":"string","minLength":1,"maxLength":256},"dry_run":{"type":"boolean","default":true},"allow_placeholders":{"type":"boolean","default":false},"item_overrides":{"type":"object","maxProperties":500,"additionalProperties":{"oneOf":[{"type":"object","additionalProperties":false,"properties":{"skip":{"const":true}},"required":["skip"]},{"type":"object","additionalProperties":false,"properties":{"song_id":{"type":"integer","minimum":1,"maximum":2147483647},"slide_order":{"type":"array","maxItems":10000,"items":{"type":"integer","minimum":0}}},"required":["song_id"]},{"type":"object","additionalProperties":false,"properties":{"title":{"type":"string","minLength":1,"maxLength":256},"slides":{"type":"array","minItems":1,"maxItems":1000,"items":{"type":"string","minLength":1,"maxLength":65536}},"slide_order":{"type":"array","maxItems":10000,"items":{"type":"integer","minimum":0}}},"required":["title","slides"]}]}}},"required":["service_type_id","plan_id"]}}
    ])json").array();
    QJsonArray tools;
    for (const auto &definition : definitions) {
        auto entry = definition.toObject();
        const QString name = entry.value("name").toString();
        const bool readOnly = name.startsWith(QStringLiteral("list_")) || name.startsWith(QStringLiteral("get_"));
        if (name == QStringLiteral("get_planning_center_plan") || name == QStringLiteral("import_planning_center_service")) {
            auto schema = entry.value("inputSchema").toObject();
            auto properties = schema.value("properties").toObject();
            properties.insert("plan_url", QJsonObject{{"type", "string"}, {"maxLength", 512},
                {"description", "Planning Center Services plan URL, e.g. https://services.planningcenteronline.com/plans/12345. Use this OR plan_id (optional service_type_id)."}});
            schema.insert("properties", properties);
            schema.remove("required");
            schema.insert("oneOf", QJsonArray{QJsonObject{{"required", QJsonArray{"plan_url"}}}, QJsonObject{{"required", QJsonArray{"plan_id"}}}});
            entry.insert("inputSchema", schema);
        }
        entry.insert("annotations", QJsonObject{{"readOnlyHint", readOnly}, {"destructiveHint", false},
            {"idempotentHint", readOnly}, {"openWorldHint", name.contains(QStringLiteral("planning_center"))}});
        tools.append(entry);
    }
    return tools;
}

QJsonObject McpServer::callCreationTool(const QString &name, const QJsonObject &args) {
    QString error;
    if (name == QStringLiteral("create_song_presentation") || name == QStringLiteral("create_sermon_presentation")) {
        const bool sermon = name == QStringLiteral("create_sermon_presentation");
        if (!keys(args, sermon ? QStringList{"title", "speaker", "scripture", "slides", "dry_run"}
                                : QStringList{"title", "lyrics", "slides", "dry_run"})
            || !text(args.value("title"), 256)
            || (args.contains("dry_run") && !args.value("dry_run").isBool()))
            return failure(QStringLiteral("Supply a nonblank title of at most 256 characters, a boolean dry_run, and only documented fields."));
        QStringList slides;
        SongStructure structure;
        if (!sermon && args.contains("lyrics")) {
            if (args.contains("slides") || !text(args.value("lyrics"), 1024 * 1024)) return failure(QStringLiteral("Supply either lyrics (at most 1 Mi characters) or slides."));
            const auto imported = LyricsImporter::parse(args.value("lyrics").toString(), args.value("title").toString());
            structure = imported.songStructure(&slides);
        } else {
            if (!args.value("slides").isArray()) return failure(QStringLiteral("slides must be an array of text strings."));
            for (const auto &slide : args.value("slides").toArray()) {
                if (!text(slide, 65536)) return failure(QStringLiteral("Each slide needs nonblank text of at most 65536 characters."));
                slides.append(slide.toString());
            }
            if (slides.isEmpty()) return failure(QStringLiteral("Supply at least one content slide."));
        }
        if (!sermon && structure.isEmpty()) {
            ImportedLyrics imported;
            imported.slides = slides;
            structure = imported.songStructure(&slides);
        }
        if (sermon) {
            QStringList title{args.value("title").toString().trimmed()};
            for (const auto &field : {QStringLiteral("speaker"), QStringLiteral("scripture")}) {
                if (args.contains(field)) {
                    if (!text(args.value(field), field == QStringLiteral("speaker") ? 256 : 1024, false)) return failure(QStringLiteral("Invalid speaker or scripture text."));
                    const auto line = args.value(field).toString().trimmed();
                    if (!line.isEmpty()) title.append(line);
                }
            }
            slides.prepend(title.join(QLatin1Char('\n')));
        }
        if (!sermon && !structure.validate(int(slides.size()), &error)) return failure(error);
        const QJsonObject item{{"title", args.value("title")}, {"slides", asArray(slides)}};
        const auto validation = callTool(QStringLiteral("create_service_order"), {{"name", args.value("title")}, {"items", QJsonArray{item}}, {"dry_run", true}});
        if (validation.value("isError").toBool()) return validation;
        const bool dryRun = args.value("dry_run").toBool();
        int id = 0;
        if (!dryRun) {
            id = library_.createTextPresentation(args.value("title").toString(), slides, &error, {}, {}, {}, structure);
            if (!id) return failure(error);
        }
        QJsonObject data{{"presentation_type", sermon ? "sermon" : "song"}, {"title", args.value("title").toString().trimmed()},
            {"slide_count", slides.size()}, {"saved", !dryRun}, {"dry_run", dryRun}};
        if (!sermon) data.insert("arranged_slide_count", structure.slideOrder().size());
        if (id) data.insert("song_id", id);
        if (dryRun) {
            QJsonArray preview;
            qsizetype budget = 256 * 1024;
            bool truncated = slides.size() > 20;
            for (int i = 0; i < slides.size() && i < 20; ++i) {
                const auto visible = slides.at(i).left(budget);
                truncated = truncated || visible.size() < slides.at(i).size();
                budget -= visible.size();
                preview.append(visible);
            }
            data.insert("slides", preview);
            data.insert("slides_truncated", truncated);
        }
        return result(data);
    }
    if (name == QStringLiteral("list_planning_center_service_types") || name == QStringLiteral("list_planning_center_plans")) {
        const bool types = name == QStringLiteral("list_planning_center_service_types");
        int offset = 0, limit = 50;
        if (!keys(args, types ? QStringList{"offset", "limit"} : QStringList{"service_type_id", "offset", "limit"})
            || !page(args, "offset", 0, std::numeric_limits<int>::max(), &offset) || !page(args, "limit", 1, 100, &limit)
            || (!types && (!args.value("service_type_id").isString() || !PlanningCenterClient::validId(args.value("service_type_id").toString()))))
            return failure(QStringLiteral("Use a numeric string service_type_id, nonnegative offset and limit from 1 to 100; only documented fields are accepted."));
        const auto data = types ? planningCenter_.serviceTypes(offset, limit, &error)
            : planningCenter_.plans(args.value("service_type_id").toString(), offset, limit, &error);
        if (!error.isEmpty()) return failure(error);
        if (!data.value("data").isArray()) return failure(QStringLiteral("Planning Center collection data is missing."));
        return result(data);
    }
    const bool inspect = name == QStringLiteral("get_planning_center_plan");
    if (!keys(args, inspect ? QStringList{"service_type_id", "plan_id", "plan_url"}
            : QStringList{"service_type_id", "plan_id", "plan_url", "name", "dry_run", "allow_placeholders", "item_overrides"})
        || (args.contains("service_type_id") && (!args.value("service_type_id").isString() || !PlanningCenterClient::validId(args.value("service_type_id").toString())))
        || (args.contains("name") && !text(args.value("name"), 256))
        || (args.contains("dry_run") && !args.value("dry_run").isBool())
        || (args.contains("allow_placeholders") && !args.value("allow_placeholders").isBool())
        || (args.contains("item_overrides") && !args.value("item_overrides").isObject()))
        return failure(QStringLiteral("Supply numeric string service_type_id and plan_id, valid optional fields, and only documented parameters."));
    QString serviceType = args.value("service_type_id").toString();
    QString planId = args.value("plan_id").toString();
    if (args.contains("plan_url")) {
        if (args.contains("plan_id") || args.contains("service_type_id") || !text(args.value("plan_url"), 512))
            return failure(QStringLiteral("Supply plan_url OR plan_id with optional service_type_id, not both."));
        const QUrl url(args.value("plan_url").toString());
        static const QRegularExpression path(QStringLiteral("^/(?:service_types/([1-9][0-9]{0,19})/)?plans/([1-9][0-9]{0,19})(?:/order)?/?$"));
        const auto match = path.match(url.path());
        if (!url.isValid() || url.scheme() != QStringLiteral("https") || !url.userInfo().isEmpty() || url.port(443) != 443
            || url.host() != QStringLiteral("services.planningcenteronline.com") || !match.hasMatch())
            return failure(QStringLiteral("Use a valid https://services.planningcenteronline.com/plans/<id> URL."));
        serviceType = match.captured(1);
        planId = match.captured(2);
    }
    if (!PlanningCenterClient::validId(planId) || (args.contains("plan_id") && !args.value("plan_id").isString()))
        return failure(QStringLiteral("Supply a Planning Center plan_url or numeric string plan_id."));
    const auto source = planningCenter_.plan(serviceType, planId, &error);
    if (!error.isEmpty()) return failure(error);
    serviceType = source.value("service_type_id").toString();
    if (inspect) {
        if (QJsonDocument(source).toJson(QJsonDocument::Compact).size() > 1024 * 1024)
            return failure(QStringLiteral("This plan exceeds the inspection response limit. Use import_planning_center_service for a compact preview."));
        return result(source);
    }
    const auto plan = source.value("plan").toObject();
    const auto attributes = plan.value("attributes").toObject();
    QString serviceName = args.value("name").toString().trimmed();
    if (serviceName.isEmpty()) {
        serviceName = attributes.value("title").toString().trimmed();
        const auto dates = attributes.value("dates").toString().trimmed();
        if (!dates.isEmpty()) serviceName = serviceName.isEmpty() ? dates : serviceName + QStringLiteral(" — ") + dates;
        if (serviceName.isEmpty()) serviceName = QStringLiteral("Planning Center %1").arg(planId);
    }
    QMap<QString, QJsonObject> included;
    for (const auto &entry : source.value("included").toArray()) {
        const auto object = entry.toObject();
        included[object.value("type").toString() + QLatin1Char(':') + object.value("id").toString()] = object;
    }
    const auto local = library_.songs();
    if (!library_.lastError().isEmpty()) return failure(library_.lastError());
    const auto overrides = args.value("item_overrides").toObject();
    QSet<QString> sourceIds;
    QJsonArray items, preview, issues, warnings;
    for (const auto &value : source.value("items").toArray()) {
        if (!value.isObject()) return failure(QStringLiteral("Planning Center contains an invalid item."));
        const auto sourceItem = value.toObject();
        const auto id = sourceItem.value("id").toString();
        if (!PlanningCenterClient::validId(id) || sourceIds.contains(id)) return failure(QStringLiteral("Planning Center item IDs are invalid or duplicated."));
        sourceIds.insert(id);
        const auto attr = sourceItem.value("attributes").toObject();
        const QString kind = attr.value("item_type").toString();
        const QString title = attr.value("title").toString().trimmed();
        QJsonObject item;
        QString resolution;
        QString issue;
        bool skip = false;
        if (overrides.contains(id)) {
            if (!overrides.value(id).isObject()) return failure(QStringLiteral("Each item override must be an object."));
            item = overrides.value(id).toObject();
            if (item.contains("skip")) {
                if (!keys(item, {"skip"}) || !item.value("skip").isBool() || !item.value("skip").toBool())
                    return failure(QStringLiteral("A skip override must be exactly {skip:true}."));
                skip = true;
                item = {};
            }
            resolution = skip ? QStringLiteral("skipped_by_override") : QStringLiteral("overridden");
        } else if (kind == QStringLiteral("header")) {
            skip = true;
            resolution = QStringLiteral("section_header_skipped");
        } else if (kind == QStringLiteral("song")) {
            const auto song = included.value(QStringLiteral("Song:") + relationshipId(sourceItem, "song")).value("attributes").toObject();
            const auto arrangement = included.value(QStringLiteral("Arrangement:") + relationshipId(sourceItem, "arrangement")).value("attributes").toObject();
            const QString songTitle = song.value("title").toString().trimmed().isEmpty() ? title : song.value("title").toString().trimmed();
            const auto lyrics = splitLyrics(arrangement.value("lyrics").toString());
            const auto custom = attr.value("custom_arrangement_sequence");
            const auto sequence = custom.isArray() && !custom.toArray().isEmpty() ? custom.toArray() : arrangement.value("sequence").toArray();
            QJsonArray order;
            bool sequenceKnown = true;
            for (const auto &section : sequence) {
                const auto indices = lyrics.sections.value(sectionKey(section.toString()));
                if (!section.isString() || indices.isEmpty()) { sequenceKnown = false; break; }
                for (int index : indices) order.append(index);
            }
            QVector<Song> matches;
            for (const auto &candidate : local)
                if (candidate.title.trimmed().compare(songTitle, Qt::CaseInsensitive) == 0
                    && std::all_of(candidate.media.begin(), candidate.media.end(), [](const auto &media) { return media.kind == MediaKind::None; }))
                    matches.append(candidate);
            if (matches.size() > 1) issue = QStringLiteral("Multiple library presentations match this song title; choose a song_id override.");
            else if (matches.size() == 1 && (sequence.isEmpty() || (sequenceKnown && matches.first().slides == lyrics.slides))) {
                item = {{"song_id", matches.first().id}};
                if (!sequence.isEmpty()) item.insert("slide_order", order);
                resolution = QStringLiteral("reused_library_song");
            } else if (!lyrics.slides.isEmpty() && (sequence.isEmpty() || sequenceKnown)) {
                item = {{"title", songTitle}, {"slides", asArray(lyrics.slides)}};
                if (!sequence.isEmpty()) item.insert("slide_order", order);
                resolution = QStringLiteral("imported_arrangement_lyrics");
            } else issue = lyrics.slides.isEmpty() ? QStringLiteral("No usable arrangement lyrics or matching library song; supply slides or song_id.")
                : QStringLiteral("Song sequence cannot be mapped to labelled lyrics; supply explicit slides/order.");
        } else if (kind == QStringLiteral("item")) {
            item = {{"title", title}, {"slides", QJsonArray{title}}};
            resolution = QStringLiteral("title_slide");
            if (!attr.value("html_details").toString().trimmed().isEmpty() || !attr.value("description").toString().trimmed().isEmpty())
                warnings.append(QJsonObject{{"item_id", id}, {"message", "Planning details were not projected. Supply an override with audience-ready sermon/announcement slides if needed."}});
        } else issue = QStringLiteral("Media or unsupported item requires a library song_id, explicit text slides, or skip:true override.");
        if (!issue.isEmpty() && args.value("allow_placeholders").toBool()) {
            item = {{"title", title}, {"slides", QJsonArray{title}}};
            warnings.append(QJsonObject{{"item_id", id}, {"message", issue + QStringLiteral(" A title-only placeholder was explicitly allowed.")}});
            resolution = QStringLiteral("title_only_placeholder");
            issue.clear();
        }
        if (!issue.isEmpty()) issues.append(QJsonObject{{"item_id", id}, {"title", title}, {"reason", issue}});
        if (!skip && issue.isEmpty()) items.append(item);
        preview.append(QJsonObject{{"item_id", id}, {"title", title}, {"item_type", kind}, {"resolution", resolution}, {"skipped", skip}});
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        if (!sourceIds.contains(it.key())) return failure(QStringLiteral("An item override references an ID outside this plan: %1").arg(it.key()));
    const bool dryRun = args.value("dry_run").toBool(true);
    QJsonObject metadata{{"service_type_id", serviceType}, {"plan_id", planId},
        {"name", serviceName}, {"source_items", preview}, {"warnings", warnings}, {"unresolved_items", issues}, {"ready_to_save", issues.isEmpty() && !items.isEmpty()}};
    if (!issues.isEmpty() || items.isEmpty()) {
        metadata.insert("saved", false);
        metadata.insert("dry_run", dryRun);
        if (items.isEmpty() && issues.isEmpty()) metadata.insert("error", "This plan has no importable items.");
        return result(metadata, !dryRun || items.isEmpty());
    }
    auto response = callTool(QStringLiteral("create_service_order"), {{"name", serviceName}, {"items", items}, {"dry_run", dryRun}});
    auto data = response.value("structuredContent").toObject();
    if (response.value("isError").toBool()) metadata.insert("ready_to_save", false);
    for (auto it = metadata.begin(); it != metadata.end(); ++it) data.insert(it.key(), it.value());
    return result(data, response.value("isError").toBool());
}
