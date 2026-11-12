#include "../src/ui/ReorderListWidget.h"
#include "../src/ui/SlideGridDelegate.h"

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
    void slideCardsHaveConsistentHitAreas_data() {
        QTest::addColumn<int>("thumbnailWidth");
        QTest::newRow("small") << 160;
        QTest::newRow("default") << 224;
        QTest::newRow("large") << 320;
    }

    void slideCardsHaveConsistentHitAreas() {
        QFETCH(int, thumbnailWidth);
        ReorderListWidget list;
        list.setItemDelegate(new SlideGridDelegate(&list));
        list.setViewMode(QListView::IconMode);
        list.setResizeMode(QListView::Adjust);
        list.setMovement(QListView::Snap);
        list.setWrapping(true);
        list.setWordWrap(false);
        list.setSpacing(6);
        list.setMouseTracking(true);
        list.setIconSize(QSize(thumbnailWidth, thumbnailWidth * 9 / 16));
        const QSize cardSize(thumbnailWidth + 24, thumbnailWidth * 9 / 16 + 38);
        list.setGridSize(cardSize);
        // Exercise the same padding as the operator workspace. Qt's default
        // icon delegate otherwise shrinks each hit area to its icon/label.
        list.setStyleSheet(QStringLiteral(
            "QListWidget::item { padding: 7px 5px; border: 1px solid transparent; }"
            "QListWidget::item:hover { background: #2b3c53; }"));
        QPixmap thumbnail(320, 180);
        thumbnail.fill(Qt::darkBlue);
        for (const auto &label : {QStringLiteral("1"), QStringLiteral("2 • A long media filename.mov"),
                                  QStringLiteral("3 • LIVE"), QStringLiteral("4 • MISSING")})
            new QListWidgetItem(QIcon(thumbnail), label, &list);
        list.resize(760, 360);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        QSignalSpy clicked(&list, &QListWidget::itemClicked);
        QSignalSpy entered(&list, &QListWidget::itemEntered);

        for (const int viewportWidth : {760, 450}) {
            list.resize(viewportWidth, 360);
            list.doItemsLayout();
            for (int row = 0; row < list.count(); ++row) {
                auto *item = list.item(row);
                list.scrollToItem(item, QAbstractItemView::PositionAtCenter);
                QCoreApplication::processEvents();
                const QRect card = list.visualItemRect(item);
                QCOMPARE(card.size(), cardSize);
                const auto points = {card.topLeft() + QPoint(1, 1), card.topRight() + QPoint(-1, 1),
                                     card.center(), card.bottomLeft() + QPoint(1, -1),
                                     card.bottomRight() + QPoint(-1, -1)};
                for (const auto &point : points) {
                    QVERIFY(list.viewport()->rect().contains(point));
                    QCOMPARE(list.itemAt(point), item);
                    // Approach each point from outside the card, so every
                    // corner must independently establish hover and selection.
                    QTest::mouseMove(list.viewport(), QPoint(-1, -1));
                    entered.clear();
                    QTest::mouseMove(list.viewport(), point);
                    QTRY_VERIFY(!entered.isEmpty());
                    QCOMPARE(qvariant_cast<QListWidgetItem *>(entered.last().first()), item);
                    clicked.clear();
                    QTest::mouseClick(list.viewport(), Qt::LeftButton, Qt::NoModifier, point);
                    QCOMPARE(clicked.count(), 1);
                    QCOMPARE(list.currentItem(), item);
                }
            }
            // Live labels change while the grid is visible. The card bounds
            // and hit area must remain stable when the label becomes shorter.
            list.item(2)->setText(QStringLiteral("3"));
            list.doItemsLayout();
            QCOMPARE(list.visualItemRect(list.item(2)).size(), cardSize);
        }
    }

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
