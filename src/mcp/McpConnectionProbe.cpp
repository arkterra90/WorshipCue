#include "McpConnectionProbe.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

McpConnectionProbe::McpConnectionProbe(QObject *parent) : QObject(parent) {
    timer_.setSingleShot(true);
    timer_.setInterval(10000);
    connect(&timer_, &QTimer::timeout, this, [this] { fail("Connection test timed out. Close any library maintenance operation and retry."); });
    connect(&process_, &QProcess::started, this, [this] {
        send(1, "initialize", {{"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "WorshipCue connection test"}, {"version", "1"}}}});
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &McpConnectionProbe::readOutput);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail("The MCP companion could not start. Check its executable permissions and deployed Qt runtime.");
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        if (failure_.isEmpty() && (!complete_ || code != 0 || status != QProcess::NormalExit))
            failure_ = "The MCP companion stopped before completing the test. Check Qt/SQLite runtime deployment and the library path.";
        finish();
    });
}

McpConnectionProbe::~McpConnectionProbe() {
    disconnect(&process_, nullptr, this, nullptr);
    if (process_.state() != QProcess::NotRunning) { process_.kill(); process_.waitForFinished(1000); }
}

void McpConnectionProbe::start(const QString &executable, const QString &databasePath) {
    if (running_) return;
    output_.clear();
    failure_.clear();
    complete_ = false;
    expectedId_ = 1;
    running_ = true;
    if (databasePath == ":memory:" || !QFileInfo(databasePath).isFile()) {
        fail("Open an existing saved library before testing the connection.");
        return;
    }
    if (!QFileInfo(executable).isExecutable()) {
        fail("The MCP companion is missing or is not executable. Rebuild WorshipCue or reinstall the complete application.");
        return;
    }
    timer_.start();
    process_.start(executable, {"--database", databasePath});
}

void McpConnectionProbe::send(int id, const QString &method, const QJsonObject &params) {
    QJsonObject request{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    if (id) request.insert("id", id);
    process_.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

void McpConnectionProbe::readOutput() {
    output_ += process_.readAllStandardOutput();
    if (!running_ || !failure_.isEmpty()) return;
    if (output_.size() > 1024 * 1024) { fail("The companion returned an oversized diagnostic response."); return; }
    while (output_.contains('\n')) {
        const auto line = output_.left(output_.indexOf('\n'));
        output_.remove(0, line.size() + 1);
        QJsonParseError parse;
        const auto response = QJsonDocument::fromJson(line, &parse).object();
        if (parse.error != QJsonParseError::NoError || response["jsonrpc"] != "2.0"
            || response["id"].toInt(-1) != expectedId_ || !response["result"].isObject() || response.contains("error")) {
            fail("The companion returned an invalid MCP response. Ensure the GUI and companion are from the same build.");
            return;
        }
        const auto result = response["result"].toObject();
        if (expectedId_ == 1) {
            if (result["protocolVersion"] != "2025-11-25" || !result["capabilities"].toObject()["tools"].isObject()) {
                fail("The companion could not negotiate the supported MCP protocol and tools capability."); return;
            }
            expectedId_ = 2;
            send(0, "notifications/initialized");
            send(2, "tools/list");
        } else if (expectedId_ == 2) {
            QSet<QString> tools;
            for (const auto &value : result["tools"].toArray()) tools.insert(value.toObject()["name"].toString());
            if (!tools.contains("create_service_order") || !tools.contains("list_media_assets") || !tools.contains("save_song_arrangement")) {
                fail("The companion is out of date. Rebuild or reinstall WorshipCue and update the client executable path."); return;
            }
            expectedId_ = 3;
            send(3, "tools/call", {{"name", "list_services"}, {"arguments", QJsonObject{{"limit", 1}}}});
        } else {
            if (result["isError"].toBool() || !result["structuredContent"].toObject()["services"].isArray()) {
                fail("The companion could not read this library. Check database access and close library maintenance operations."); return;
            }
            complete_ = true;
            expectedId_ = 4;
            process_.closeWriteChannel();
        }
    }
}

void McpConnectionProbe::fail(const QString &message) {
    if (!running_) return;
    failure_ = message;
    if (process_.state() != QProcess::NotRunning) process_.kill();
    else finish();
}

void McpConnectionProbe::finish() {
    if (!running_) return;
    running_ = false;
    timer_.stop();
    if (failure_.isEmpty() && !output_.isEmpty())
        failure_ = "The companion returned incomplete trailing data. Check that stdout contains only newline-delimited MCP responses.";
    // Do not forward arbitrary environment/plugin diagnostics into copyable UI
    // reports: no credentials, library content, or environment values are logged.
    process_.readAllStandardError();
    emit finished(failure_.isEmpty(), failure_.isEmpty()
        ? QStringLiteral("Connection test passed: companion startup, MCP handshake, tool discovery, and library read. Reconnect your AI client using the configuration below. This does not test client-specific settings or Planning Center credentials.")
        : failure_);
}
