#include "../src/ui/HelpDialog.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextFragment>
#include <QtTest>

namespace {
int visibleTopics(QListWidget *list) {
    int count = 0;
    for (int row = 0; row < list->count(); ++row) if (!list->item(row)->isHidden()) ++count;
    return count;
}
}

class HelpDialogTest : public QObject {
    Q_OBJECT
private slots:
    void everyRequiredTopicAndLinkIsEmbeddedAndReadableOffline() {
        QTemporaryDir emptyDirectory;
        QVERIFY(emptyDirectory.isValid());
        const QString previousDirectory = QDir::currentPath();
        const auto restoreDirectory = qScopeGuard([&] { QDir::setCurrent(previousDirectory); });
        QVERIFY(QDir::setCurrent(emptyDirectory.path()));
        QFile index(QStringLiteral(":/help/topics.json"));
        QVERIFY(index.open(QIODevice::ReadOnly));
        QJsonParseError parse;
        const auto catalogue = QJsonDocument::fromJson(index.readAll(), &parse).array();
        QCOMPARE(parse.error, QJsonParseError::NoError);
        QCOMPARE(catalogue.size(), 16);
        const QSet<QString> required{"getting-started", "creating-presentations", "content-library", "media-library",
            "service-playlists", "songs-arrangements", "formatting-looks", "navigation", "outputs-stage", "transitions",
            "midi", "mcp", "licensing", "storage", "troubleshooting", "shortcuts"};
        QSet<QString> ids;
        QSet<QUrl> pages;
        for (const auto &entry : catalogue) {
            const auto topic = entry.toObject();
            const QString id = topic.value("id").toString();
            QVERIFY(!ids.contains(id));
            QVERIFY(!topic.value("title").toString().isEmpty());
            ids.insert(id);
            pages.insert(QUrl(QStringLiteral("qrc:/help/%1.md").arg(id)));
            QFile article(QStringLiteral(":/help/%1.md").arg(id));
            QVERIFY2(article.open(QIODevice::ReadOnly), qPrintable(article.fileName()));
            QVERIFY(!article.readAll().isEmpty());
        }
        QCOMPARE(ids, required);
        QCOMPARE(QDir(QStringLiteral(":/help")).entryList({"*.md"}, QDir::Files).size(), 16);
        HelpDialog help(QKeySequence("Alt+B"), QKeySequence("Alt+C"));
        auto *browser = help.findChild<QTextBrowser *>("helpBrowser");
        QVERIFY(browser);
        QVERIFY(browser->isReadOnly());
        for (const auto &entry : catalogue) {
            const auto topic = entry.toObject();
            QVERIFY(help.openTopic(topic.value("id").toString()));
            QVERIFY2(browser->toPlainText().startsWith(topic.value("title").toString()), qPrintable(browser->toPlainText()));
            QVERIFY(!browser->toPlainText().contains("{{"));
            int links = 0;
            for (auto block = browser->document()->begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    const auto fragment = it.fragment();
                    if (!fragment.isValid() || !fragment.charFormat().isAnchor()) continue;
                    const QUrl link(fragment.charFormat().anchorHref());
                    QVERIFY2(pages.contains(browser->source().resolved(link).adjusted(QUrl::RemoveFragment)), qPrintable(link.toString()));
                    ++links;
                }
            }
            QVERIFY(links > 0);
        }
        QVERIFY(help.openTopic("shortcuts"));
        QVERIFY(browser->toPlainText().contains(QKeySequence("Alt+B").toString(QKeySequence::NativeText)));
        QVERIFY(browser->toPlainText().contains(QKeySequence("Alt+C").toString(QKeySequence::NativeText)));
        help.refreshAudienceShortcuts(QKeySequence("Alt+F9"), QKeySequence("Alt+F10"));
        QVERIFY(browser->toPlainText().contains(QKeySequence("Alt+F9").toString(QKeySequence::NativeText)));
        QVERIFY(browser->toPlainText().contains(QKeySequence("Alt+F10").toString(QKeySequence::NativeText)));
        for (const auto &shortcut : {QKeySequence("Alt+|"), QKeySequence("Alt+\\"), QKeySequence("Alt+`"), QKeySequence("Alt+*")}) {
            help.refreshAudienceShortcuts(shortcut, QKeySequence("Alt+F10"));
            QVERIFY2(browser->toPlainText().contains(shortcut.toString(QKeySequence::NativeText)), qPrintable(browser->toPlainText()));
        }
        QVERIFY(help.openTopic("licensing"));
        QVERIFY(browser->toPlainText().contains("License Activation & Devices"));
        QVERIFY(browser->toPlainText().contains("seven days"));
        QVERIFY(browser->toPlainText().contains("five installations"));
        QVERIFY(!help.openTopic("missing-topic"));
    }

    void fullTextSearchHandlesCaseMultipleWordsEmptyResultsAndHistory() {
        HelpDialog help(QKeySequence("Ctrl+B"), QKeySequence("Ctrl+Shift+C"));
        help.show();
        auto *search = help.findChild<QLineEdit *>("helpSearch");
        auto *topics = help.findChild<QListWidget *>("helpTopics");
        auto *browser = help.findChild<QTextBrowser *>("helpBrowser");
        auto *empty = help.findChild<QLabel *>("helpEmpty");
        QCOMPARE(visibleTopics(topics), 16);
        search->setText("  MANAGED   SYMLINKS  ");
        QCOMPARE(visibleTopics(topics), 1);
        QCOMPARE(browser->source(), QUrl("qrc:/help/storage.md"));
        QVERIFY(!browser->extraSelections().isEmpty());
        QVERIFY(help.findChild<QLabel *>("helpResults")->text().contains("1 matching"));
        search->setText("zz-no-matching-topic-zz");
        QCOMPARE(visibleTopics(topics), 0);
        QVERIFY(empty->isVisible());
        QVERIFY(browser->isHidden());
        // Opening the current source must also recover from an empty search.
        QVERIFY(help.openTopic("storage"));
        QVERIFY(search->text().isEmpty());
        QCOMPARE(visibleTopics(topics), 16);
        QVERIFY(browser->isVisible());
        QVERIFY(help.openTopic("midi"));
        help.findChild<QPushButton *>("helpBack")->click();
        QCOMPARE(browser->source(), QUrl("qrc:/help/storage.md"));
        help.findChild<QPushButton *>("helpForward")->click();
        QCOMPARE(browser->source(), QUrl("qrc:/help/midi.md"));
        search->setText("learn next input");
        QVERIFY(visibleTopics(topics) > 0);
        QVERIFY(help.openTopic("formatting-looks"));
        QVERIFY(search->text().isEmpty());
        help.findChild<QPushButton *>("helpHome")->click();
        QCOMPARE(browser->source(), QUrl("qrc:/help/getting-started.md"));
        search->setText(" \t  ");
        QCOMPARE(visibleTopics(topics), 16);
        QVERIFY(browser->extraSelections().isEmpty());
    }

    void followsArticleLinksAndRestrictsLoadsToInstalledHelp() {
        HelpDialog help(QKeySequence("Ctrl+B"), QKeySequence("Ctrl+Shift+C"));
        help.show();
        auto *browser = help.findChild<QTextBrowser *>("helpBrowser");
        QVERIFY(help.openTopic("midi"));
        QTextCursor linkCursor;
        for (auto block = browser->document()->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                if (fragment.charFormat().anchorHref() != "troubleshooting.md") continue;
                linkCursor = QTextCursor(browser->document());
                linkCursor.setPosition(fragment.position() + fragment.length() / 2);
            }
        }
        QVERIFY(!linkCursor.isNull());
        browser->setTextCursor(linkCursor);
        browser->ensureCursorVisible();
        const QPoint point = browser->cursorRect(linkCursor).center();
        QCOMPARE(browser->anchorAt(point), QString("troubleshooting.md"));
        QTest::mouseClick(browser->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(browser->source(), QUrl("qrc:/help/troubleshooting.md"));
        const auto source = browser->source();
        for (const auto &link : {QUrl("https://example.com/"), QUrl("file:///etc/passwd"), QUrl("../../README.md"),
                                 QUrl("qrc:/help/missing.md"), QUrl("qrc:/help/midi.md?unexpected=1")}) {
            browser->anchorClicked(link);
            QCOMPARE(browser->source(), source);
            QVERIFY(!browser->loadResource(QTextDocument::MarkdownResource, link).isValid());
        }
    }

    void standardFindFocusesHelpAndEscapeClosesIt() {
        HelpDialog help(QKeySequence("Ctrl+B"), QKeySequence("Ctrl+Shift+C"));
        help.show();
        help.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&help));
        auto *browser = help.findChild<QTextBrowser *>("helpBrowser");
        auto *search = help.findChild<QLineEdit *>("helpSearch");
        browser->setFocus();
        QTRY_VERIFY(browser->hasFocus());
        QTest::keySequence(browser, QKeySequence::Find);
        QTRY_VERIFY(search->hasFocus());
        QTest::keyClicks(search, "countdown");
        QVERIFY(search->text() == "countdown");
        QTest::keyClick(search, Qt::Key_Escape);
        QVERIFY(!help.isVisible());
    }
};

QTEST_MAIN(HelpDialogTest)
#include "help_dialog_test.moc"
