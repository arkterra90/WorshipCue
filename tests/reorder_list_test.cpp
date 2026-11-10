#include "../src/ui/ReorderListWidget.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QtTest/QtTest>

class TestableReorderListWidget : public ReorderListWidget {
public:
    using ReorderListWidget::dragEnterEvent;
    using ReorderListWidget::dragMoveEvent;
    using ReorderListWidget::dropEvent;
};

class ReorderListTest : public QObject {
    Q_OBJECT
private:
    static QVector<int> ids(const ReorderListWidget &list) {
        QVector<int> result;
        for (int i = 0; i < list.count(); ++i) result.append(list.item(i)->data(Qt::UserRole).toInt());
        return result;
    }

    static void populate(ReorderListWidget &list) {
        for (int id : {10, 20, 30, 40}) {
            // Repeated titles must retain the correct underlying entry identity.
            auto *item = new QListWidgetItem(QStringLiteral("Repeated title"), &list);
            item->setData(Qt::UserRole, id);
            item->setData(Qt::UserRole + 1, QStringLiteral("Slide %1").arg(id));
        }
        list.setCurrentRow(0);
    }

private slots:
    void modelOrderChangesInBothViewModes_data() {
        QTest::addColumn<int>("viewMode");
        QTest::newRow("service-list") << int(QListView::ListMode);
        QTest::newRow("slide-grid") << int(QListView::IconMode);
    }

    void modelOrderChangesInBothViewModes() {
        QFETCH(int, viewMode);
        ReorderListWidget list;
        list.setViewMode(QListView::ViewMode(viewMode));
        list.setDragDropMode(QAbstractItemView::InternalMove);
        populate(list);
        auto *original = list.item(0);
        QSignalSpy changed(&list, &ReorderListWidget::orderChanged);
        QVector<int> observed;
        connect(&list, &ReorderListWidget::orderChanged, this, [&] { observed = ids(list); });

        QVERIFY(list.moveItem(0, 3));
        QCOMPARE(ids(list), QVector<int>({20, 30, 40, 10}));
        QCOMPARE(observed, ids(list));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(list.currentItem(), original);
        QCOMPARE(list.currentRow(), 3);
        QCOMPARE(original->data(Qt::UserRole + 1).toString(), QStringLiteral("Slide 10"));

        QVERIFY(list.moveItem(3, 1));
        QCOMPARE(ids(list), QVector<int>({20, 10, 30, 40}));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(list.currentItem(), original);
        QCOMPARE(list.currentRow(), 1);
        QCOMPARE(list.count(), 4);
    }

    void invalidMovesDoNotMutateOrEmit() {
        ReorderListWidget list;
        QSignalSpy changed(&list, &ReorderListWidget::orderChanged);
        QVERIFY(!list.moveItem(0, 0));
        populate(list);
        const auto before = ids(list);
        QVERIFY(!list.moveItem(-1, 0));
        QVERIFY(!list.moveItem(0, -1));
        QVERIFY(!list.moveItem(4, 0));
        QVERIFY(!list.moveItem(0, 4));
        QVERIFY(!list.moveItem(2, 2));
        QCOMPARE(ids(list), before);
        QCOMPARE(list.currentRow(), 0);
        QCOMPARE(changed.count(), 0);
    }

    void externalDropsCannotReorderOrInsert() {
        TestableReorderListWidget list;
        list.setDragDropMode(QAbstractItemView::InternalMove);
        populate(list);
        const auto before = ids(list);
        QSignalSpy changed(&list, &ReorderListWidget::orderChanged);
        QMimeData mime;
        mime.setData(QStringLiteral("application/x-worshipcue-reorder"), QByteArrayLiteral("1"));
        mime.setData(QStringLiteral("application/x-qabstractitemmodeldatalist"), QByteArrayLiteral("foreign model"));
        mime.setText(QStringLiteral("External content"));

        // Keep the Qt 6.4 constructor for supported older installations.
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        const QPointF position(10, 10);
#else
        const QPoint position(10, 10);
#endif
        QDragEnterEvent enter(position, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        list.dragEnterEvent(&enter);
        QVERIFY(!enter.isAccepted());
        QDragMoveEvent move(position, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        list.dragMoveEvent(&move);
        QVERIFY(!move.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        list.dropEvent(&drop);
        QVERIFY(!drop.isAccepted());
        QCOMPARE(ids(list), before);
        QCOMPARE(list.count(), 4);
        QCOMPARE(changed.count(), 0);
    }
};

QTEST_MAIN(ReorderListTest)
#include "reorder_list_test.moc"
