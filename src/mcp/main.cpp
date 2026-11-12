#include "McpServer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WorshipCue"));
    QCoreApplication::setApplicationName(QStringLiteral("WorshipCue"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("WorshipCue local MCP server (newline-delimited JSON-RPC over stdio)."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption database(QStringLiteral("database"), QStringLiteral("Path to a WorshipCue SQLite library; defaults to the application's library."), QStringLiteral("path"));
    parser.addOption(database);
    parser.process(app);
    if (parser.isSet(database) && parser.value(database).isEmpty()) {
        qCritical().noquote() << "--database requires a nonempty path.";
        return 2;
    }
    if (!parser.positionalArguments().isEmpty()) {
        qCritical().noquote() << "Unexpected positional arguments. Use --help for usage.";
        return 2;
    }
    LibraryRepository library;
    QString error;
    if (!library.open(&error, parser.value(database))) {
        qCritical().noquote() << "Cannot open WorshipCue library:" << error;
        return 1;
    }
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    McpServer server(library);
    QByteArray line;
    bool oversized = false;
    auto dispatch = [&]() {
        const auto response = oversized ? server.handleMessage(QByteArray(McpServer::maximumMessageBytes + 1, ' ')) : server.handleMessage(line);
        line.clear();
        oversized = false;
        if (response.isEmpty()) return true;
        return std::fwrite(response.constData(), 1, size_t(response.size()), stdout) == size_t(response.size())
            && std::fputc('\n', stdout) != EOF && std::fflush(stdout) == 0;
    };
    int character;
    while ((character = std::fgetc(stdin)) != EOF) {
        if (character == '\n') {
            if (!dispatch()) return 1;
        } else if (!oversized) {
            if (line.size() >= McpServer::maximumMessageBytes) { line.clear(); oversized = true; }
            else line.append(char(character));
        }
    }
    if ((!line.isEmpty() || oversized) && !dispatch()) return 1;
    return std::ferror(stdin) ? 1 : 0;
}
