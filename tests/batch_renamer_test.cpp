#include "../src/media/BatchRenamer.h"
#include "../src/ui/MediaImportDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

class BatchRenamerTest : public QObject {
    Q_OBJECT
private slots:
    void nameAndNumberPreservesExtensionsAndOrder() {
        BatchRenameOptions options;
        options.text = QStringLiteral("Sunday Announcements");
        options.start = 7;
        options.padding = 3;
        QString error;
        const auto names = BatchRenamer::names({QStringLiteral("/pictures/1.JPG"), QStringLiteral("/pictures/2.png"), QStringLiteral("/clips/welcome.mp4")}, options, &error);
        QCOMPARE(names, QStringList({QStringLiteral("Sunday Announcements 007.JPG"), QStringLiteral("Sunday Announcements 008.png"), QStringLiteral("Sunday Announcements 009.mp4")}));
        QVERIFY(error.isEmpty());
    }
    void finderStyleReplaceAndAddAreLiteral() {
        BatchRenameOptions options;
        options.mode = BatchRenameOptions::Mode::Replace;
        options.find = QStringLiteral("IMG_");
        options.replacement = QStringLiteral("Offering ");
        QCOMPARE(BatchRenamer::names({QStringLiteral("IMG_01.jpg"), QStringLiteral("IMG_02.jpg")}, options), QStringList({QStringLiteral("Offering 01.jpg"), QStringLiteral("Offering 02.jpg")}));
        options.mode = BatchRenameOptions::Mode::Add;
        options.text = QStringLiteral(" Sunday");
        options.prefix = false;
        QCOMPARE(BatchRenamer::names({QStringLiteral("Welcome.jpg")}, options), QStringList({QStringLiteral("Welcome Sunday.jpg")}));
        options.prefix = true;
        options.text = QStringLiteral("Sunday ");
        QCOMPARE(BatchRenamer::names({QStringLiteral("Welcome.jpg")}, options), QStringList({QStringLiteral("Sunday Welcome.jpg")}));
    }
    void unsafeAndDuplicateNamesAreRejected_data() {
        QTest::addColumn<QStringList>("names");
        QTest::newRow("path") << QStringList({QStringLiteral("../outside.jpg")});
        QTest::newRow("backslash") << QStringList({QStringLiteral("folder\\outside.jpg")});
        QTest::newRow("reserved") << QStringList({QStringLiteral("CON.jpg")});
        QTest::newRow("reserved-extension") << QStringList({QStringLiteral("LPT1.picture.jpg")});
        QTest::newRow("trailing-space") << QStringList({QStringLiteral("name.jpg ")});
        QTest::newRow("trailing-dot") << QStringList({QStringLiteral("name.jpg.")});
        QTest::newRow("colon") << QStringList({QStringLiteral("name:slide.jpg")});
        QTest::newRow("case-collision") << QStringList({QStringLiteral("Welcome.JPG"), QStringLiteral("welcome.jpg")});
        QTest::newRow("unicode-collision") << QStringList({QString::fromUtf8("Café.jpg"), QString::fromUtf8("Cafe\xcc\x81.jpg")});
    }
    void unsafeAndDuplicateNamesAreRejected() {
        QFETCH(QStringList, names);
        QString error;
        QVERIFY(!BatchRenamer::validateNames(names, &error));
        QVERIFY(!error.isEmpty());
    }
    void invalidRuleDoesNotProducePartialPlan() {
        BatchRenameOptions options;
        QString error;
        QVERIFY(BatchRenamer::names({QStringLiteral("1.jpg")}, options, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        options.mode = BatchRenameOptions::Mode::Replace;
        QVERIFY(BatchRenamer::names({QStringLiteral("1.jpg")}, options, &error).isEmpty());
        options.find = QStringLiteral("1");
        options.replacement = QStringLiteral("2");
        QVERIFY(BatchRenamer::names({QStringLiteral("1.jpg"), QStringLiteral("2.jpg")}, options, &error).isEmpty());
    }
    void dialogPreviewsNamesAndBlocksCollisionBeforeCommit() {
        MediaImportDialog dialog({QStringLiteral("1.jpg"), QStringLiteral("2.jpg")});
        dialog.setPresentationTitle(QStringLiteral("Sunday Announcements"));
        QCOMPARE(dialog.title(), QStringLiteral("Sunday Announcements"));
        QCOMPARE(dialog.names(), QStringList({QStringLiteral("Sunday Announcements 01.jpg"), QStringLiteral("Sunday Announcements 02.jpg")}));
        auto *preview = dialog.findChild<QTableWidget *>(QStringLiteral("mediaRenamePreview"));
        QVERIFY(preview);
        QCOMPARE(preview->item(0, 0)->text(), QStringLiteral("1.jpg"));
        QCOMPARE(preview->item(0, 1)->text(), QStringLiteral("Sunday Announcements 01.jpg"));
        auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("renameMode"));
        mode->setCurrentIndex(1);
        dialog.findChild<QLineEdit *>(QStringLiteral("renameFind"))->setText(QStringLiteral("1"));
        dialog.findChild<QLineEdit *>(QStringLiteral("renameReplacement"))->setText(QStringLiteral("2"));
        auto *actions = dialog.findChild<QDialogButtonBox *>(QStringLiteral("mediaImportActions"));
        QVERIFY(!actions->button(QDialogButtonBox::Ok)->isEnabled());
        dialog.reject();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        MediaImportDialog namingOnly({QStringLiteral("1.jpg")}, nullptr, true);
        QVERIFY(namingOnly.findChild<QLineEdit *>(QStringLiteral("presentationTitle"))->isHidden());
    }
};

QTEST_MAIN(BatchRenamerTest)
#include "batch_renamer_test.moc"
