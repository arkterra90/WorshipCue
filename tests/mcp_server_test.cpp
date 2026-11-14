#include "../src/mcp/McpServer.h"
#include "../src/mcp/McpConnectionProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QUuid>
#include <QtTest/QtTest>

namespace {
QJsonObject modernMeta(const QString &version = QStringLiteral("2026-07-28")) {
    return {{QStringLiteral("io.modelcontextprotocol/protocolVersion"), version},
        {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject{}}};
}
QJsonObject request(const QString &method, QJsonObject params = {}, bool modern = true, int id = 1) {
    if (modern) params.insert(QStringLiteral("_meta"), modernMeta());
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
        {QStringLiteral("method"), method}, {QStringLiteral("params"), params}};
}
QJsonObject send(McpServer &server, const QJsonObject &message) {
    return QJsonDocument::fromJson(server.handleMessage(QJsonDocument(message).toJson(QJsonDocument::Compact))).object();
}
QJsonObject call(McpServer &server, const QString &name, const QJsonObject &arguments = {}, bool modern = true) {
    return send(server, request(QStringLiteral("tools/call"), {
        {QStringLiteral("name"), name}, {QStringLiteral("arguments"), arguments}}, modern));
}
QJsonObject data(const QJsonObject &response) {
    return response.value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent")).toObject();
}
bool toolFailed(const QJsonObject &response) {
    return response.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool();
}
QJsonObject newItem(const QString &title, const QJsonArray &slides) {
    return {{QStringLiteral("title"), title}, {QStringLiteral("slides"), slides}};
}
QString executable() {
#ifdef WORSHIPCUE_MCP_EXECUTABLE
    return QString::fromUtf8(WORSHIPCUE_MCP_EXECUTABLE);
#else
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WorshipCueMcp.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WorshipCueMcp"));
#endif
#endif
}
QByteArray encoded(const QJsonObject &value) {
    return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
}
class PlanningEnvironment {
    QMap<QByteArray, QByteArray> saved_;
    QSet<QByteArray> present_;
public:
    PlanningEnvironment() {
        for (const auto *key : {"PLANNING_CENTER_APPLICATION_ID", "PLANNING_CENTER_SECRET", "PLANNING_CENTER_ACCESS_TOKEN"}) {
            saved_.insert(key, qgetenv(key));
            if (qEnvironmentVariableIsSet(key)) present_.insert(key);
            qunsetenv(key);
        }
    }
    ~PlanningEnvironment() {
        for (auto it = saved_.begin(); it != saved_.end(); ++it)
            if (present_.contains(it.key())) qputenv(it.key().constData(), it.value()); else qunsetenv(it.key().constData());
    }
};
}

class McpServerTest : public QObject {
    Q_OBJECT
private slots:
    void planningCenterRequestsUsePatOrOAuthWithoutFollowingRedirects() {
        PlanningEnvironment environment;
        const QUrl url("https://api.planningcenteronline.com/services/v2/service_types");
        QString error;
        QVERIFY(!PlanningCenterClient::requestFor(url, &error).url().isValid());
        QVERIFY(error.contains("server environment"));
        qputenv("PLANNING_CENTER_APPLICATION_ID", "fixture-id");
        qputenv("PLANNING_CENTER_SECRET", "fixture-secret");
        const auto request = PlanningCenterClient::requestFor(url, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Basic ") + QByteArray("fixture-id:fixture-secret").toBase64());
        QCOMPARE(request.rawHeader("X-PCO-API-Version"), QByteArray("2018-11-01"));
        QVERIFY(request.rawHeader("User-Agent").startsWith("WorshipCue/"));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        qputenv("PLANNING_CENTER_ACCESS_TOKEN", "fixture-access-token");
        QCOMPARE(PlanningCenterClient::requestFor(url, &error).rawHeader("Authorization"), QByteArray("Bearer fixture-access-token"));
        QVERIFY(error.isEmpty());
        QVERIFY(!PlanningCenterClient::requestFor(QUrl("https://example.org/services/v2/"), &error).hasRawHeader("Authorization"));
        QVERIFY(!error.isEmpty());
        qputenv("PLANNING_CENTER_ACCESS_TOKEN", "invalid\r\nheader");
        QVERIFY(!PlanningCenterClient::requestFor(url, &error).hasRawHeader("Authorization"));
        QVERIFY(error.contains("invalid format"));
    }

    void planningCenterBrowserLinksResolveThroughDocumentedScopedEndpoints() {
        QString error;
        QStringList requests;
        PlanningCenterClient client([&](const QUrl &url, QString *failure) {
            requests.append(url.path());
            if (url.path() == "/services/v2/service_types") return QJsonObject{{"data", QJsonArray{
                QJsonObject{{"type", "ServiceType"}, {"id", "9"}}, QJsonObject{{"type", "ServiceType"}, {"id", "10"}}}}};
            if (url.path().endsWith("/plans")) {
                if (QUrlQuery(url).queryItemValue("where[id]") != "20") *failure = "Missing plan filter";
                return QJsonObject{{"data", url.path().contains("/10/") ? QJsonArray{QJsonObject{{"id", "20"}}} : QJsonArray{}}};
            }
            if (url.path() == "/services/v2/service_types/10/plans/20") return QJsonObject{{"data", QJsonObject{{"type", "Plan"}, {"id", "20"}}}};
            if (url.path().endsWith("/items")) {
                if (QUrlQuery(url).queryItemValue("include") != "song,arrangement") *failure = "Missing song/arrangement includes";
                return QJsonObject{{"data", QJsonArray{}}};
            }
            *failure = "Undocumented endpoint requested";
            return QJsonObject{};
        });
        const auto plan = client.plan({}, "20", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(plan["service_type_id"].toString(), QString("10"));
        QCOMPARE(requests.size(), 5);
        QVERIFY(client.plan({}, "99", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        requests.clear();
        QVERIFY(client.plan("../invalid", "20", &error).isEmpty());
        QVERIFY(requests.isEmpty());
    }

    void discoversMediaAndCreatesMixedOrdersAtomically() {
        QTemporaryDir directory;
        QString error;
        LibraryRepository library;
        QVERIFY(library.open(&error, directory.filePath("mixed.sqlite")));
        const auto assets = library.registerMediaAssets({{MediaKind::Audio, "media/prelude.wav", "Prelude", true},
                                                        {MediaKind::Image, "media/welcome.png", "Welcome", false}}, &error);
        QCOMPARE(assets.size(), 2);
        QVERIFY(library.updateMediaAsset(assets.first(), "Prelude", "Sunday", true, &error));
        McpServer server(library);
        auto listed = data(call(server, "list_media_assets", {{"limit", 1}}));
        QCOMPARE(listed["total"].toInt(), 2);
        QCOMPARE(listed["media_assets"].toArray().size(), 1);
        QCOMPARE(listed["next_offset"].toInt(), 1);
        QCOMPARE(data(call(server, "list_media_assets", {{"query", "Sunday"}}))["total"].toInt(), 1);
        QCOMPARE(data(call(server, "list_media_assets", {{"offset", 2}}))["media_assets"].toArray().size(), 0);
        const auto asset = data(call(server, "get_media_asset", {{"media_asset_id", assets.first()}}));
        QCOMPARE(asset["name"].toString(), QString("Prelude"));
        QCOMPARE(asset["media_kind"].toString(), QString("audio"));
        QVERIFY(asset["loop"].toBool());
        QVERIFY(!asset.contains("path"));
        for (const auto &args : {QJsonObject{{"limit", 101}}, QJsonObject{{"offset", -1}}, QJsonObject{{"offset", 0.5}},
                                 QJsonObject{{"query", 1}}, QJsonObject{{"query", QString(QChar::Null)}}, QJsonObject{{"sql", "SELECT 1"}}})
            QVERIFY(toolFailed(call(server, "list_media_assets", args)));
        QVERIFY(toolFailed(call(server, "get_media_asset", {{"media_asset_id", 0}})));
        QVERIFY(toolFailed(call(server, "get_media_asset", {{"media_asset_id", 99999}})));

        const auto songs = library.songs().size(), services = library.services().size();
        QJsonObject args{{"name", "Mixed service"}, {"dry_run", true}, {"items", QJsonArray{
            newItem("Welcome", {"Hello"}), QJsonObject{{"media_asset_id", assets.first()}},
            QJsonObject{{"media_asset_id", assets.first()}}, QJsonObject{{"media_asset_id", assets.last()}}}}};
        auto preview = call(server, "create_service_order", args);
        QVERIFY(!toolFailed(preview));
        const auto media = data(preview)["items"].toArray().at(1).toObject();
        QCOMPARE(media["kind"].toString(), QString("media"));
        QVERIFY(!media["new_presentation"].toBool());
        QCOMPARE(media["slide_count"].toInt(), 1);
        QCOMPARE(library.services().size(), services);
        args["dry_run"] = false;
        auto saved = call(server, "create_service_order", args);
        QVERIFY(!toolFailed(saved));
        const auto entries = library.service(data(saved)["service_id"].toInt());
        QCOMPARE(entries.size(), 4);
        QCOMPARE(entries[1].mediaAssetId, assets.first());
        QCOMPARE(entries[2].mediaAssetId, assets.first());
        QCOMPARE(entries[1].songId, 0);
        QCOMPARE(library.songs().size(), songs + 1);
        for (const auto &item : {QJsonObject{{"media_asset_id", assets.first()}, {"song_id", entries.first().songId}},
                                 QJsonObject{{"media_asset_id", assets.first()}, {"slide_order", QJsonArray{0}}},
                                 QJsonObject{{"media_asset_id", 99999}}, QJsonObject{{"media_asset_id", 1.5}}}) {
            QVERIFY(toolFailed(call(server, "create_service_order", {{"name", "Invalid"}, {"items", QJsonArray{newItem("Orphan", {"Text"}), item}}})));
        }
        // Fail after the text presentation is inserted: all service and content
        // rows must roll back, including a later direct-media insert.
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(library.databasePath()); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER fail_media BEFORE INSERT ON service_entries WHEN NEW.media_asset_id IS NOT NULL BEGIN SELECT RAISE(ABORT,'Injected media failure'); END"));
            QVERIFY(toolFailed(call(server, "create_service_order", args)));
            QCOMPARE(library.services().size(), services + 1);
            QCOMPARE(library.songs().size(), songs + 1);
            QVERIFY(query.exec("DROP TRIGGER fail_media"));
            QVERIFY(query.exec("INSERT INTO cleanup_batches VALUES('test',1,1,'archived')"));
            QVERIFY(query.exec(QString("UPDATE content_usage SET archived_batch='test' WHERE asset_id=%1").arg(assets.last())));
            QVERIFY(toolFailed(call(server, "get_media_asset", {{"media_asset_id", assets.last()}})));
            QCOMPARE(data(call(server, "list_media_assets"))["total"].toInt(), 1);
            ServiceOrderItem hidden; hidden.mediaAssetId = assets.last();
            QVERIFY(!library.createServiceOrder("Hidden", {hidden}, &error));
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void sharedSectionAndArrangementEditsValidateRevisionsAndPreserveIdentity() {
        QTemporaryDir directory;
        QString error;
        LibraryRepository library;
        QVERIFY(library.open(&error, directory.filePath("song.sqlite")));
        SongStructure structure;
        structure.sections = {{"v", "Verse", 1}, {"c", "Chorus", 2}};
        structure.arrangements = {{"full", "Full", {"v", "c", "c"}}};
        structure.activeArrangementId = "full";
        PresentationAppearance appearance;
        appearance.lookId = library.createLook("Song Look", {}, &error);
        QVERIFY(appearance.lookId > 0);
        appearance.slideLookIds = {0, 0, 0};
        appearance.richTextOverrides = {false, true, true};
        appearance.fitText = {false, false, false};
        const int id = library.createTextPresentation("Shared song", {"Verse", "Chorus A", "Chorus B"}, &error,
            {}, {}, {"<p>Verse</p>", "<p>Chorus A</p>", "<p>Chorus B</p>"}, structure, appearance);
        QVERIFY(id > 0);
        QVERIFY(library.appendToService(1, id, &error));
        const auto entry = library.service(1).last().id;
        QVERIFY(library.setEntrySlideOrder(entry, {1, 2, 0, 1, 2}, &error));
        MidiOutputAction action;
        action.destination = "Test destination";
        QVERIFY(library.setMidiActions(MidiOwnerType::Slide, library.song(id).slideIds[1], {}, {action}, &error));
        const auto original = library.song(id);
        McpServer server(library);
        const auto revision = [&] { return data(call(server, "get_presentation", {{"song_id", id}})).value("revision"); };
        QJsonObject edit{{"song_id", id}, {"expected_revision", revision()}, {"section_id", "c"}, {"slides", QJsonArray{"New chorus", "Chorus B"}}};
        auto preview = call(server, "update_song_section", edit);
        QVERIFY(!toolFailed(preview)); QVERIFY(data(preview)["dry_run"].toBool());
        QCOMPARE(library.song(id).slides, original.slides);
        edit["dry_run"] = false;
        auto saved = call(server, "update_song_section", edit);
        QVERIFY2(!toolFailed(saved), qPrintable(QString::fromUtf8(QJsonDocument(saved).toJson())));
        QCOMPARE(library.song(id).slideIds, original.slideIds);
        QCOMPARE(library.song(id).appearance.lookId, appearance.lookId);
        QCOMPARE(library.song(id).appearance.richTextOverrides, QVector<bool>({false, false, true}));
        QCOMPARE(library.song(id).midiActions[1], QVector<MidiOutputAction>({action}));
        QCOMPARE(library.song(id).richText, QStringList({"<p>Verse</p>", "", "<p>Chorus B</p>"}));
        QCOMPARE(library.entrySlides(entry), QStringList({"New chorus", "Chorus B", "Verse", "New chorus", "Chorus B"}));
        QVERIFY(toolFailed(call(server, "update_song_section", edit))); // stale
        edit["expected_revision"] = revision();
        for (const auto &replacement : {QJsonObject{{"section_id", "missing"}}, QJsonObject{{"slides", QJsonArray{"Too few"}}},
                                        QJsonObject{{"slides", QJsonArray{"", "Text"}}}, QJsonObject{{"dry_run", "false"}},
                                        QJsonObject{{"slides", QJsonArray{QString(65537, 'x'), "Text"}}}, QJsonObject{{"unknown", true}}}) {
            auto invalid = edit;
            for (auto it = replacement.begin(); it != replacement.end(); ++it) invalid[it.key()] = it.value();
            QVERIFY(toolFailed(call(server, "update_song_section", invalid)));
        }
        const auto unchanged = library.song(id);
        QJsonObject arrangement{{"song_id", id}, {"expected_revision", revision()}, {"name", "Short"},
            {"section_ids", QJsonArray{"c", "v", "c"}}, {"make_active", true}};
        QVERIFY(!toolFailed(call(server, "save_song_arrangement", arrangement)));
        QCOMPARE(library.song(id).structure.arrangements.size(), 1);
        arrangement["dry_run"] = false;
        auto arranged = call(server, "save_song_arrangement", arrangement);
        QVERIFY(!toolFailed(arranged));
        const auto arrangementId = data(arranged)["arrangement_id"].toString();
        QCOMPARE(library.song(id).structure.activeArrangementId, arrangementId);
        QCOMPARE(library.song(id).playbackOrder(), QVector<int>({1, 2, 0, 1, 2}));
        QCOMPARE(library.song(id).slideIds, original.slideIds);
        QCOMPARE(library.service(1).last().slideOrder, QVector<int>({1, 2, 0, 1, 2}));
        QVERIFY(toolFailed(call(server, "save_song_arrangement", arrangement))); // stale
        arrangement["expected_revision"] = revision();
        arrangement["arrangement_id"] = arrangementId;
        arrangement["name"] = "Short revised";
        arrangement["section_ids"] = QJsonArray{"v", "c"};
        QVERIFY(!toolFailed(call(server, "save_song_arrangement", arrangement)));
        QCOMPARE(library.song(id).structure.arrangements.size(), 2);
        arrangement["expected_revision"] = revision();
        arrangement["section_ids"] = QJsonArray{"missing"};
        QVERIFY(toolFailed(call(server, "save_song_arrangement", arrangement)));
        arrangement["section_ids"] = QJsonArray{"v"};
        arrangement["name"] = "Full"; // duplicate name
        QVERIFY(toolFailed(call(server, "save_song_arrangement", arrangement)));
        arrangement["name"] = "Unique"; arrangement["arrangement_id"] = "unknown";
        QVERIFY(toolFailed(call(server, "save_song_arrangement", arrangement)));

        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(library.databasePath()); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER fail_song BEFORE UPDATE ON songs BEGIN SELECT RAISE(ABORT,'Injected song failure'); END"));
            edit["expected_revision"] = revision(); edit["slides"] = QJsonArray{"Must roll back", "Too"};
            QVERIFY(toolFailed(call(server, "update_song_section", edit)));
            QCOMPARE(library.song(id).slides, unchanged.slides);
            QCOMPARE(library.song(id).richText, unchanged.richText);
            QVERIFY(query.exec("DROP TRIGGER fail_song"));
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(library.renamePresentation(id, "Operator changed title", &error));
        QVERIFY(toolFailed(call(server, "update_song_section", edit)));
    }

    void connectionProbeUsesTheRealCompanionAndReportsFailures() {
        QTemporaryDir directory;
        QString error;
        LibraryRepository library;
        const auto path = directory.filePath("probe library.sqlite");
        QVERIFY(library.open(&error, path));
        McpConnectionProbe probe;
        QSignalSpy done(&probe, &McpConnectionProbe::finished);
        probe.start(executable(), path);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done.last().at(0).toBool(), qPrintable(done.last().at(1).toString()));
        QVERIFY(!probe.isRunning());
        probe.start(directory.filePath("missing-server"), path);
        QCOMPARE(done.size(), 2); QVERIFY(!done.last().at(0).toBool());
        probe.start(executable(), directory.filePath("missing-library"));
        QCOMPARE(done.size(), 3); QVERIFY(!done.last().at(0).toBool());
        QVERIFY(!QFileInfo::exists(directory.filePath("missing-library")));
        QFile corrupt(directory.filePath("corrupt.sqlite")); QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("not a database"); corrupt.close();
        probe.start(executable(), corrupt.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 4, 15000);
        QVERIFY(!done.last().at(0).toBool());
        QVERIFY(done.last().at(1).toString().contains("library path"));
        probe.start(executable(), path);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 5, 15000);
        QVERIFY(done.last().at(0).toBool());
    }

    void inspectsSharedSongArrangementsAndPreviewsTheirExpandedCount() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        SongStructure structure;
        structure.sections = {{"v", "Verse", 1}, {"c", "Chorus", 2}};
        structure.arrangements = {{"full", "Full", {"v", "c", "c"}}};
        structure.activeArrangementId = QStringLiteral("full");
        const int id = library.createTextPresentation(QStringLiteral("Arranged song"), {"Verse", "Chorus A", "Chorus B"},
                                                       &error, {}, {}, {"", "", ""}, structure);
        QVERIFY(id > 0);
        QVERIFY(library.appendToService(1, id, &error));
        McpServer server(library);
        const auto service = data(call(server, QStringLiteral("get_service"), {{QStringLiteral("service_id"), 1}}));
        const auto item = service.value(QStringLiteral("items")).toArray().last().toObject();
        QCOMPARE(item.value(QStringLiteral("song_arrangement_id")).toString(), QStringLiteral("full"));
        QCOMPARE(item.value(QStringLiteral("slide_count")).toInt(), 5);
        const auto slides = item.value(QStringLiteral("slides")).toArray();
        QCOMPARE(slides.at(1).toObject().value(QStringLiteral("text")), slides.at(3).toObject().value(QStringLiteral("text")));
        const auto presentation = data(call(server, QStringLiteral("get_presentation"), {{QStringLiteral("song_id"), id}}));
        QCOMPARE(presentation.value(QStringLiteral("slide_count")).toInt(), 3);
        QCOMPARE(presentation.value(QStringLiteral("song_structure")).toObject().value(QStringLiteral("sections")).toArray().size(), 2);
        const auto preview = data(call(server, QStringLiteral("create_service_order"), {
            {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("dry_run"), true},
            {QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("song_id"), id}}}}}));
        QCOMPARE(preview.value(QStringLiteral("items")).toArray().first().toObject().value(QStringLiteral("slide_count")).toInt(), 5);
    }

    void inspectsMixedMediaServicesWithoutInventingPresentations() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        const auto count = library.songs().size();
        const auto ids = library.registerMediaAssets({{MediaKind::Audio, QStringLiteral("media/session/prelude.wav"),
                                                      QStringLiteral("Prelude.wav"), false}}, &error);
        QCOMPARE(ids.size(), 1);
        QVERIFY(library.appendMediaToService(1, ids.first(), &error));
        McpServer server(library);
        const auto response = call(server, QStringLiteral("get_service"), {{QStringLiteral("service_id"), 1}});
        QVERIFY(!toolFailed(response));
        const auto item = data(response).value(QStringLiteral("items")).toArray().last().toObject();
        QCOMPARE(item.value(QStringLiteral("kind")).toString(), QStringLiteral("media"));
        QCOMPARE(item.value(QStringLiteral("media_asset_id")).toInt(), ids.first());
        QVERIFY(item.value(QStringLiteral("song_id")).isNull());
        QCOMPARE(item.value(QStringLiteral("slide_count")).toInt(), 1);
        QCOMPARE(item.value(QStringLiteral("slides")).toArray().first().toObject()
                     .value(QStringLiteral("media_kind")).toString(), QStringLiteral("audio"));
        QCOMPARE(library.songs().size(), count);
    }

    void createsStandaloneSongAndSermonPresentations() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        McpServer server(library);
        const auto count = library.songs().size();
        const auto services = library.services().size();
        QJsonObject song{{"title", "Original Song"}, {"lyrics", "[Verse 1]\n[C]First line\nSecond line\n\n[Chorus]\nSing together"}, {"dry_run", true}};
        auto preview = call(server, "create_song_presentation", song);
        QVERIFY(!toolFailed(preview));
        QCOMPARE(data(preview)["slide_count"].toInt(), 2);
        QCOMPARE(data(preview)["slides"].toArray().first().toString(), QStringLiteral("First line\nSecond line"));
        QCOMPARE(library.songs().size(), count);
        song["dry_run"] = false;
        const auto saved = call(server, "create_song_presentation", song);
        QVERIFY(!toolFailed(saved));
        const int songId = data(saved)["song_id"].toInt();
        QVERIFY(songId > 0);
        QCOMPARE(library.song(songId).slides.size(), 2);
        const auto sermon = call(server, "create_sermon_presentation", {{"title", "Gratitude"}, {"speaker", "Jordan"},
            {"scripture", "Psalm 100"}, {"slides", QJsonArray{"Give thanks", "Practice gratitude"}}});
        QVERIFY(!toolFailed(sermon));
        const int sermonId = data(sermon)["song_id"].toInt();
        QCOMPARE(library.song(sermonId).slides.first(), QStringLiteral("Gratitude\nJordan\nPsalm 100"));
        QCOMPARE(library.song(sermonId).slides.size(), 3);
        QCOMPARE(library.songs().size(), count + 2);
        QCOMPARE(library.services().size(), services);
        song["slides"] = QJsonArray{"Ambiguous"};
        QVERIFY(toolFailed(call(server, "create_song_presentation", song)));
        QVERIFY(toolFailed(call(server, "create_sermon_presentation", {{"title", "Empty"}, {"slides", QJsonArray{}}})));
        QCOMPARE(library.songs().size(), count + 2);
        QVERIFY(!toolFailed(call(server, "create_service_order", {{"name", "Created presentations"}, {"items", QJsonArray{
            QJsonObject{{"song_id", songId}}, QJsonObject{{"song_id", sermonId}}}}})));
    }

    void planningCenterSongSelectLyricsKeepSectionsRepeatsAndRollback() {
        QTemporaryDir directory;
        const auto database = directory.filePath("songselect-bridge.sqlite");
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, database));
        const QString raw = QString(QChar(0xfeff)) + "[Verse1]\r\n[C]First page\r\n\r\nSecond page\r\n\r\nCHORUS\r\nTogether\r\n\r\nRéponse\r\nUne réponse\r\n\r\nChorus\r\nTogether";
        QJsonValue sequence = QJsonArray{"Chorus", "Verse 1", "Réponse", "Chorus"};
        QString lyrics = raw;
        McpServer server(library, [&](const QUrl &url, QString *) {
            if (!url.path().endsWith("/items")) return QJsonObject{{"data", QJsonObject{{"type", "Plan"}, {"id", "20"}}}};
            return QJsonObject{{"data", QJsonArray{QJsonObject{{"id", "30"}, {"attributes", QJsonObject{
                {"title", "Imported SongSelect fixture"}, {"item_type", "song"}, {"custom_arrangement_sequence", sequence}}},
                {"relationships", QJsonObject{{"arrangement", QJsonObject{{"data", QJsonObject{{"id", "60"}}}}}}}}}},
                {"included", QJsonArray{QJsonObject{{"type", "Arrangement"}, {"id", "60"},
                    {"attributes", QJsonObject{{"lyrics", lyrics}}}}}}};
        });
        QJsonObject args{{"service_type_id", "10"}, {"plan_id", "20"}};
        const auto songs = library.songs().size(), services = library.services().size();
        const auto preview = call(server, "import_planning_center_service", args);
        QVERIFY(!toolFailed(preview));
        QVERIFY(data(preview)["ready_to_save"].toBool());
        QCOMPARE(library.songs().size(), songs);
        QCOMPARE(library.services().size(), services);
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(database); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER reject_pco_entry BEFORE INSERT ON service_entries BEGIN SELECT RAISE(ABORT,'fixture rollback'); END"));
        }
        args["dry_run"] = false;
        QVERIFY(toolFailed(call(server, "import_planning_center_service", args)));
        QCOMPARE(library.songs().size(), songs);
        QCOMPARE(library.services().size(), services);
        {
            auto db = QSqlDatabase::database(connection); QSqlQuery query(db);
            QVERIFY(query.exec("DROP TRIGGER reject_pco_entry"));
        }
        QSqlDatabase::removeDatabase(connection);
        const auto saved = call(server, "import_planning_center_service", args);
        QVERIFY2(!toolFailed(saved), qPrintable(QString::fromUtf8(QJsonDocument(saved).toJson())));
        const int serviceId = data(saved)["service_id"].toInt();
        const auto entry = library.service(serviceId).first();
        const auto song = library.song(entry.songId);
        QCOMPARE(song.slides, QStringList({"First page", "Second page", "Together", "Une réponse"}));
        QCOMPARE(song.structure.sections.size(), 3);
        QCOMPARE(song.structure.sections.first().slideCount, 2);
        QCOMPARE(song.structure.sections.last().name, QString("Réponse"));
        QCOMPARE(library.entrySlides(entry.id), QStringList({"Together", "First page", "Second page", "Une réponse", "Together"}));
        LibraryRepository reopened; QVERIFY(reopened.open(&error, database));
        QCOMPARE(reopened.song(entry.songId).structure.toJson(), song.structure.toJson());
        QCOMPARE(reopened.service(serviceId).first().slideOrder, entry.slideOrder);
        sequence = "Chorus, Verse 1";
        QVERIFY(toolFailed(call(server, "import_planning_center_service", args)));
        sequence = QJsonArray{"Chorus"};
        lyrics = "Chorus\nDifferent words\n\nChorus\nConflicting words";
        QVERIFY(toolFailed(call(server, "import_planning_center_service", args)));
        lyrics.clear();
        const auto missing = call(server, "import_planning_center_service", args);
        QVERIFY(toolFailed(missing));
        QVERIFY(data(missing)["unresolved_items"].toArray().first().toObject()["reason"].toString().contains("Lyrics & Chords"));
        QCOMPARE(library.services().size(), services + 1);
    }

    void planningCenterImportPreviewsResolvesAndSavesAtomically() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        QVERIFY(library.createSong("Source Song", {"Verse text", "Chorus text"}, &error));
        const int songId = library.songs("Source Song").first().id;
        QFile fixtureFile(QFINDTESTDATA("fixtures/planning-center-plan.json"));
        QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
        auto fixture = QJsonDocument::fromJson(fixtureFile.readAll()).object();
        int requests = 0;
        McpServer server(library, [&](const QUrl &url, QString *) {
            ++requests;
            if (url.path() == "/services/v2/service_types") return QJsonObject{{"data", QJsonArray{QJsonObject{{"id", "10"}}}}};
            if (url.path().endsWith("/plans")) return QJsonObject{{"data", QJsonArray{fixture["plan"].toObject()["data"]}}};
            if (!url.path().endsWith("/items")) return fixture["plan"].toObject();
            return fixture[url.query().contains("offset=3") ? "page2" : "page1"].toObject();
        });
        const auto beforeSongs = library.songs().size(), beforeServices = library.services().size();
        QJsonObject args{{"service_type_id", "10"}, {"plan_id", "20"}};
        const auto preview = call(server, "import_planning_center_service", args);
        QVERIFY(!toolFailed(preview));
        QCOMPARE(data(preview)["ready_to_save"].toBool(), false);
        QCOMPARE(data(preview)["unresolved_items"].toArray().size(), 1);
        QCOMPARE(data(preview)["source_items"].toArray().first().toObject()["item_id"].toString(), QStringLiteral("1"));
        QCOMPARE(library.songs().size(), beforeSongs);
        args["dry_run"] = false;
        QVERIFY(toolFailed(call(server, "import_planning_center_service", args)));
        QCOMPARE(library.services().size(), beforeServices);
        args["item_overrides"] = QJsonObject{{"5", QJsonObject{{"skip", true}}}};
        const auto imported = call(server, "import_planning_center_service", args);
        QVERIFY2(!toolFailed(imported), qPrintable(QString::fromUtf8(QJsonDocument(imported).toJson())));
        const int id = data(imported)["service_id"].toInt();
        const auto entries = library.service(id);
        QCOMPARE(entries.size(), 4);
        QCOMPARE(entries[1].songId, songId);
        QCOMPARE(entries[1].slideOrder, QVector<int>({1, 0, 1}));
        QCOMPARE(library.entrySlides(entries[0].id), QStringList({"Welcome"}));
        QCOMPARE(library.entrySlides(entries[2].id), QStringList({"Message"}));
        QCOMPARE(library.entrySlides(entries[3].id), QStringList({"Dismissal"}));
        QCOMPARE(library.songs().size(), beforeSongs + 3);
        QCOMPARE(library.services().size(), beforeServices + 1);
        QVERIFY(!data(imported)["warnings"].toArray().isEmpty());
        QVERIFY(requests >= 9);
        const auto linked = call(server, "get_planning_center_plan", {{"plan_url", "https://services.planningcenteronline.com/plans/20"}});
        QVERIFY(!toolFailed(linked));
        QCOMPARE(data(linked)["service_type_id"].toString(), QStringLiteral("10"));
        QVERIFY(toolFailed(call(server, "get_planning_center_plan", {{"plan_url", "https://example.com/plans/20"}})));
        args["item_overrides"] = QJsonObject{{"5", QJsonObject{{"skip", true}}}, {"4", QJsonObject{{"title", "Sermon"}, {"slides", QJsonArray{"Gratitude", "Give thanks"}}}}};
        args["dry_run"] = true;
        QVERIFY(!toolFailed(call(server, "import_planning_center_service", args)));
        args["dry_run"] = false;
        args["item_overrides"] = QJsonObject{{"5", QJsonObject{{"skip", true}}}, {"4", QJsonObject{}}};
        QVERIFY(toolFailed(call(server, "import_planning_center_service", args)));
        QCOMPARE(library.services().size(), beforeServices + 1);
        args["item_overrides"] = QJsonObject{{"999", QJsonObject{{"skip", true}}}};
        QVERIFY(toolFailed(call(server, "import_planning_center_service", args)));
        // Reject foreign pagination before a credential-bearing request can follow it.
        auto firstPage = fixture["page1"].toObject();
        firstPage["links"] = QJsonObject{{"next", "https://example.com/steal"}};
        fixture["page1"] = firstPage;
        QVERIFY(toolFailed(call(server, "import_planning_center_service", args)));
        QCOMPARE(library.services().size(), beforeServices + 1);
        QVERIFY(!PlanningCenterClient::trustedUrl(QUrl("http://api.planningcenteronline.com/services/v2/service_types")));
        QVERIFY(!PlanningCenterClient::trustedUrl(QUrl("https://api.planningcenteronline.com.evil.test/services/v2/")));
        QVERIFY(!PlanningCenterClient::validId("../plans"));
    }

    void planningCenterCanCreateMissingSongsAndRejectInvalidSequences() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        QFile fixtureFile(QFINDTESTDATA("fixtures/planning-center-song-items.json"));
        QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
        auto page = QJsonDocument::fromJson(fixtureFile.readAll()).object();
        McpServer server(library, [&](const QUrl &url, QString *) {
            if (url.path().endsWith("/items")) return page;
            return QJsonObject{{"data", QJsonObject{{"type", "Plan"}, {"id", "20"}, {"attributes", QJsonObject{{"title", "New songs"}}}}}};
        });
        QJsonObject args{{"service_type_id", "10"}, {"plan_id", "20"}, {"dry_run", false}};
        const auto saved = call(server, "import_planning_center_service", args);
        QVERIFY(!toolFailed(saved));
        const int id = data(saved)["service_id"].toInt();
        const auto entry = library.service(id).first();
        QCOMPARE(library.song(entry.songId).slides, QStringList({"First verse", "Sing together"}));
        QCOMPARE(library.song(entry.songId).structure.sections.size(), 2);
        QCOMPARE(library.song(entry.songId).structure.sections.first().name, QString("Verse 1"));
        QCOMPARE(library.entrySlides(entry.id), QStringList({"Sing together", "First verse", "Sing together"}));
        auto entries = page["data"].toArray();
        auto item = entries.first().toObject();
        auto attributes = item["attributes"].toObject();
        attributes["custom_arrangement_sequence"] = QJsonArray{"Unknown section"};
        item["attributes"] = attributes; entries[0] = item; page["data"] = entries;
        const auto blocked = call(server, "import_planning_center_service", args);
        QVERIFY(toolFailed(blocked));
        const int services = library.services().size();
        args["allow_placeholders"] = true;
        QVERIFY(!toolFailed(call(server, "import_planning_center_service", args)));
        QCOMPARE(library.services().size(), services + 1);
        McpServer denied(library, [](const QUrl &, QString *error) { *error = QStringLiteral("Planning Center denied access."); return QJsonObject{}; });
        QVERIFY(toolFailed(call(denied, "get_planning_center_plan", {{"service_type_id", "10"}, {"plan_id", "20"}})));
        QVERIFY(toolFailed(call(denied, "get_planning_center_plan", {{"service_type_id", "../secrets"}, {"plan_id", "20"}})));
    }

    void modernDiscoveryAndIndependentRequests() {
        LibraryRepository library;
        QString error;
        QVERIFY2(library.open(&error, QStringLiteral(":memory:")), qPrintable(error));
        McpServer server(library);
        const auto response = send(server, request(QStringLiteral("server/discover")));
        QVERIFY(!response.contains(QStringLiteral("error")));
        const auto result = response.value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
        QVERIFY(result.value(QStringLiteral("supportedVersions")).toArray().contains(QStringLiteral("2026-07-28")));
        QCOMPARE(result.value(QStringLiteral("supportedVersions")).toArray(), QJsonArray({QStringLiteral("2026-07-28")}));
        QVERIFY(result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
        QVERIFY(result.value(QStringLiteral("_meta")).toObject().contains(QStringLiteral("io.modelcontextprotocol/serverInfo")));
        const auto list = send(server, request(QStringLiteral("tools/list"))).value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
        QCOMPARE(list.size(), 15);
        QSet<QString> names;
        for (const auto &entry : list) {
            const auto tool = entry.toObject();
            names.insert(tool.value(QStringLiteral("name")).toString());
            QCOMPARE(tool.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("additionalProperties")).toBool(true), false);
        }
        QVERIFY(names.contains(QStringLiteral("create_service_order")));
        // A new server object accepts a modern tool call with no handshake.
        McpServer second(library);
        QVERIFY(!toolFailed(call(second, QStringLiteral("list_services"))));
        auto missing = request(QStringLiteral("tools/list"));
        missing.insert(QStringLiteral("params"), QJsonObject{{QStringLiteral("_meta"), QJsonObject{
            {QStringLiteral("io.modelcontextprotocol/protocolVersion"), QStringLiteral("2026-07-28")}}}});
        QCOMPARE(send(server, missing).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32602);
        auto unsupported = request(QStringLiteral("tools/list"));
        unsupported.insert(QStringLiteral("params"), QJsonObject{{QStringLiteral("_meta"), modernMeta(QStringLiteral("2099-01-01"))}});
        const auto failure = send(server, unsupported).value(QStringLiteral("error")).toObject();
        QCOMPARE(failure.value(QStringLiteral("code")).toInt(), -32022);
        QCOMPARE(failure.value(QStringLiteral("data")).toObject().value(QStringLiteral("requested")).toString(), QStringLiteral("2099-01-01"));
        QVERIFY(!toolFailed(call(server, QStringLiteral("list_services"))));
    }

    void legacyHandshakeAndNotifications() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        McpServer server(library);
        QVERIFY(send(server, request(QStringLiteral("tools/list"), {}, false)).contains(QStringLiteral("error")));
        QVERIFY(!send(server, request(QStringLiteral("ping"), {}, false)).contains(QStringLiteral("error")));
        const auto init = request(QStringLiteral("initialize"), {
            {QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")},
            {QStringLiteral("capabilities"), QJsonObject{}},
            {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("test")},
                {QStringLiteral("version"), QStringLiteral("1")}}}}, false);
        QCOMPARE(send(server, init).value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-06-18"));
        QVERIFY(send(server, request(QStringLiteral("tools/list"), {}, false)).contains(QStringLiteral("error")));
        auto notification = request(QStringLiteral("notifications/initialized"), {}, false);
        notification.remove(QStringLiteral("id"));
        QVERIFY(server.handleMessage(QJsonDocument(notification).toJson(QJsonDocument::Compact)).isEmpty());
        const auto response = send(server, request(QStringLiteral("tools/list"), {}, false));
        QVERIFY(!response.contains(QStringLiteral("error")));
        QVERIFY(!response.value(QStringLiteral("result")).toObject().contains(QStringLiteral("resultType")));
        QVERIFY(send(server, init).contains(QStringLiteral("error")));
        notification.insert(QStringLiteral("method"), QStringLiteral("notifications/cancelled"));
        QVERIFY(server.handleMessage(QJsonDocument(notification).toJson(QJsonDocument::Compact)).isEmpty());
    }

    void createsAndReadsOrderedServices() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        QVERIFY(library.createSong(QStringLiteral("Known Worship"), {QStringLiteral("Verse α"), QStringLiteral("Chorus β")}, &error));
        const auto source = library.songs(QStringLiteral("Known Worship")).first();
        McpServer server(library);
        const auto search = data(call(server, QStringLiteral("search_library"), {{QStringLiteral("query"), QStringLiteral("Chorus β")}}));
        QCOMPARE(search.value(QStringLiteral("presentations")).toArray().size(), 1);
        QCOMPARE(search.value(QStringLiteral("presentations")).toArray().first().toObject().value(QStringLiteral("song_id")).toInt(), source.id);
        const auto presentation = data(call(server, QStringLiteral("get_presentation"), {{QStringLiteral("song_id"), source.id}, {QStringLiteral("limit"), 1}}));
        QCOMPARE(presentation.value(QStringLiteral("slides")).toArray().size(), 1);
        QCOMPARE(presentation.value(QStringLiteral("next_offset")).toInt(), 1);
        const QJsonArray items{
            newItem(QStringLiteral("Welcome"), {QStringLiteral("Welcome to worship")}),
            QJsonObject{{QStringLiteral("song_id"), source.id}, {QStringLiteral("slide_order"), QJsonArray{1, 0, 1}}},
            QJsonObject{{QStringLiteral("song_id"), source.id}}};
        const auto creation = call(server, QStringLiteral("create_service_order"), {
            {QStringLiteral("name"), QStringLiteral("Sunday – Grâce")}, {QStringLiteral("items"), items}});
        QVERIFY(!toolFailed(creation));
        const auto receipt = data(creation);
        QVERIFY(receipt.value(QStringLiteral("saved")).toBool());
        const auto id = receipt.value(QStringLiteral("service_id")).toInt();
        QVERIFY(id > 0);
        QCOMPARE(library.service(id).size(), 3);
        const auto order = data(call(server, QStringLiteral("get_service"), {{QStringLiteral("service_id"), id}}));
        QCOMPARE(order.value(QStringLiteral("name")).toString(), QStringLiteral("Sunday – Grâce"));
        const auto savedItems = order.value(QStringLiteral("items")).toArray();
        QCOMPARE(savedItems.size(), 3);
        const auto arranged = savedItems.at(1).toObject();
        QCOMPARE(arranged.value(QStringLiteral("slide_order")).toArray(), QJsonArray({1, 0, 1}));
        const auto slides = arranged.value(QStringLiteral("slides")).toArray();
        QCOMPARE(slides.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Chorus β"));
        QCOMPARE(slides.at(1).toObject().value(QStringLiteral("source_index")).toInt(), 0);
        QCOMPARE(slides.at(2).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Chorus β"));
        QVERIFY(savedItems.at(1).toObject().value(QStringLiteral("entry_id")) != savedItems.at(2).toObject().value(QStringLiteral("entry_id")));
        const auto welcome = library.song(library.service(id).first().songId);
        QCOMPARE(welcome.title, QStringLiteral("Welcome"));
        QCOMPARE(welcome.slides, QStringList({QStringLiteral("Welcome to worship")}));
        const auto text = creation.value(QStringLiteral("result")).toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString();
        QCOMPARE(QJsonDocument::fromJson(text.toUtf8()).object(), receipt);
    }

    void dryRunAndInvalidInputsNeverSave() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        McpServer server(library);
        const auto songCount = library.songs().size();
        const auto serviceCount = library.services().size();
        QJsonObject arguments{{QStringLiteral("name"), QStringLiteral("Preview")},
            {QStringLiteral("items"), QJsonArray{newItem(QStringLiteral("Prayer"), {QStringLiteral("Let us pray")})}},
            {QStringLiteral("dry_run"), true}};
        const auto preview = call(server, QStringLiteral("create_service_order"), arguments);
        QVERIFY(!toolFailed(preview));
        QCOMPARE(data(preview).value(QStringLiteral("saved")).toBool(true), false);
        QCOMPARE(library.songs().size(), songCount);
        QCOMPARE(library.services().size(), serviceCount);
        arguments.insert(QStringLiteral("items"), QJsonArray{newItem(QStringLiteral("Prayer"), {QStringLiteral("Let us pray")}),
            QJsonObject{{QStringLiteral("song_id"), 2147483647}}});
        arguments.insert(QStringLiteral("dry_run"), false);
        QVERIFY(toolFailed(call(server, QStringLiteral("create_service_order"), arguments)));
        QCOMPARE(library.songs().size(), songCount);
        QCOMPARE(library.services().size(), serviceCount);
        auto invalid = newItem(QStringLiteral("Prayer"), {QStringLiteral("Let us pray")});
        invalid.insert(QStringLiteral("slide_order"), QJsonArray{0.5});
        arguments.insert(QStringLiteral("items"), QJsonArray{invalid});
        QVERIFY(toolFailed(call(server, QStringLiteral("create_service_order"), arguments)));
        invalid.insert(QStringLiteral("slide_order"), QJsonArray{1});
        arguments.insert(QStringLiteral("items"), QJsonArray{invalid});
        QVERIFY(toolFailed(call(server, QStringLiteral("create_service_order"), arguments)));
        invalid = newItem(QStringLiteral("Prayer"), {QStringLiteral(" ")});
        arguments.insert(QStringLiteral("items"), QJsonArray{invalid});
        QVERIFY(toolFailed(call(server, QStringLiteral("create_service_order"), arguments)));
        arguments.insert(QStringLiteral("items"), QJsonArray{newItem(QStringLiteral("Prayer"), {QStringLiteral("text")})});
        arguments.insert(QStringLiteral("unexpected"), true);
        QVERIFY(toolFailed(call(server, QStringLiteral("create_service_order"), arguments)));
        QVERIFY(toolFailed(call(server, QStringLiteral("get_presentation"), {{QStringLiteral("song_id"), QStringLiteral("1")}})));
        QVERIFY(toolFailed(call(server, QStringLiteral("list_services"), {{QStringLiteral("limit"), 101}})));
        QCOMPARE(library.songs().size(), songCount);
        QCOMPARE(library.services().size(), serviceCount);
    }

    void servicePaginationAndPreviewTruncationAreExplicit() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        const auto source = library.songs().first();
        QVector<int> repeated(25, 0);
        const int id = library.createServiceOrder(QStringLiteral("Long Order"), {
            ServiceOrderItem{source.id, {}, {}, repeated}, ServiceOrderItem{source.id, {}, {}, {}}}, &error);
        QVERIFY2(id > 0, qPrintable(error));
        McpServer server(library);
        const auto first = data(call(server, QStringLiteral("get_service"), {
            {QStringLiteral("service_id"), id}, {QStringLiteral("limit"), 1}}));
        QCOMPARE(first.value(QStringLiteral("item_count")).toInt(), 2);
        QCOMPARE(first.value(QStringLiteral("next_offset")).toInt(), 1);
        const auto item = first.value(QStringLiteral("items")).toArray().first().toObject();
        QCOMPARE(item.value(QStringLiteral("slides")).toArray().size(), 20);
        QCOMPARE(item.value(QStringLiteral("slide_count")).toInt(), 25);
        QCOMPARE(item.value(QStringLiteral("slide_order")).toArray().size(), 25);
        QVERIFY(item.value(QStringLiteral("slides_truncated")).toBool());
        QVERIFY(!item.value(QStringLiteral("natural_order")).toBool(true));
        const auto second = data(call(server, QStringLiteral("get_service"), {
            {QStringLiteral("service_id"), id}, {QStringLiteral("offset"), 1}, {QStringLiteral("limit"), 1}}));
        QVERIFY(!second.contains(QStringLiteral("next_offset")));
        const auto natural = second.value(QStringLiteral("items")).toArray().first().toObject();
        QVERIFY(natural.value(QStringLiteral("natural_order")).toBool());
        QVERIFY(natural.value(QStringLiteral("slide_order")).toArray().isEmpty());
        QCOMPARE(natural.value(QStringLiteral("slide_count")).toInt(), int(source.slides.size()));
        QCOMPARE(natural.value(QStringLiteral("slides")).toArray().first().toObject().value(QStringLiteral("text")).toString(), source.slides.first());
        // The application permits legacy arrangements larger than the MCP
        // creation limits; their response metadata still has a finite bound.
        QVERIFY(library.setEntrySlideOrder(library.service(id).first().id, QVector<int>(10001, 0), &error));
        const auto legacy = data(call(server, QStringLiteral("get_service"), {
            {QStringLiteral("service_id"), id}, {QStringLiteral("limit"), 1}})).value(QStringLiteral("items")).toArray().first().toObject();
        QCOMPARE(legacy.value(QStringLiteral("slide_order")).toArray().size(), 10000);
        QVERIFY(legacy.value(QStringLiteral("slide_order_truncated")).toBool());
        QCOMPARE(legacy.value(QStringLiteral("slide_count")).toInt(), 10001);
    }

    void protocolRejectsMalformedRequests() {
        LibraryRepository library;
        QString error;
        QVERIFY(library.open(&error, QStringLiteral(":memory:")));
        McpServer server(library);
        QCOMPARE(QJsonDocument::fromJson(server.handleMessage("not json")).object().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32700);
        QCOMPARE(QJsonDocument::fromJson(server.handleMessage("[]")).object().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32600);
        auto invalid = request(QStringLiteral("ping"));
        invalid.insert(QStringLiteral("id"), QJsonValue(QJsonValue::Null));
        QCOMPARE(send(server, invalid).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32600);
        invalid.insert(QStringLiteral("id"), 1.5);
        QCOMPARE(send(server, invalid).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32600);
        QCOMPARE(send(server, request(QStringLiteral("unknown"))).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32601);
        QCOMPARE(call(server, QStringLiteral("unknown")).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32602);
        invalid = request(QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("list_services")}, {QStringLiteral("arguments"), QJsonArray{}}});
        QCOMPARE(send(server, invalid).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32602);
        const auto oversized = server.handleMessage(QByteArray(McpServer::maximumMessageBytes + 1, ' '));
        QCOMPARE(QJsonDocument::fromJson(oversized).object().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32600);
    }

    void cliStdioPersistsAndRecoversAfterOversizedLine() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QProcess process;
        process.start(executable(), {QStringLiteral("--database"), directory.filePath(QStringLiteral("library.sqlite3"))});
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QByteArray input = encoded(request(QStringLiteral("server/discover"), {}, true, 1));
        input += QByteArray(McpServer::maximumMessageBytes + 100, 'x') + '\n';
        input += encoded(request(QStringLiteral("tools/call"), {
            {QStringLiteral("name"), QStringLiteral("create_service_order")},
            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("name"), QStringLiteral("CLI Sunday")},
                {QStringLiteral("items"), QJsonArray{newItem(QStringLiteral("Welcome"), {QStringLiteral("Good morning, church")})}}}}}, true, 3));
        input += encoded(request(QStringLiteral("ping"), {}, true, 4));
        QCOMPARE(process.write(input), qint64(input.size()));
        process.closeWriteChannel();
        QVERIFY2(process.waitForFinished(15000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        const auto output = process.readAllStandardOutput().split('\n');
        QCOMPARE(output.size(), 5); // Four responses and final newline, no log text.
        QJsonParseError parse;
        for (int i = 0; i < 4; ++i) {
            const auto response = QJsonDocument::fromJson(output.at(i), &parse);
            QCOMPARE(parse.error, QJsonParseError::NoError);
            QVERIFY(response.isObject());
        }
        QCOMPARE(QJsonDocument::fromJson(output.at(1)).object().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32600);
        QVERIFY(data(QJsonDocument::fromJson(output.at(2)).object()).value(QStringLiteral("saved")).toBool());
        QCOMPARE(QJsonDocument::fromJson(output.at(3)).object().value(QStringLiteral("id")).toInt(), 4);
        LibraryRepository library;
        QString error;
        QVERIFY2(library.open(&error, directory.filePath(QStringLiteral("library.sqlite3"))), qPrintable(error));
        const auto services = library.services();
        bool found = false;
        for (const auto &service : services) if (service.name == QStringLiteral("CLI Sunday")) {
            found = true;
            QCOMPARE(library.service(service.id).size(), 1);
        }
        QVERIFY(found);
    }
};

QTEST_GUILESS_MAIN(McpServerTest)
#include "mcp_server_test.moc"
