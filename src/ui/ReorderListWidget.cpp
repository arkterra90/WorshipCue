#include "ReorderListWidget.h"
#include "MediaAssetListWidget.h"

#include <QAbstractItemModel>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>

namespace {
constexpr const char *reorderMimeType = "application/x-worshipcue-reorder";
}

ReorderListWidget::ReorderListWidget(QWidget *parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDropIndicatorShown(false); // Paint an insertion marker for both view modes.
}

bool ReorderListWidget::moveItem(int from, int to) {
    if (from < 0 || to < 0 || from >= count() || to >= count() || from == to) return false;
    {
        // Observers should see one complete reorder, never the temporary removal.
        const QSignalBlocker block(this);
        QListWidgetItem *moved = takeItem(from);
        insertItem(to, moved);
        setCurrentItem(moved);
        scrollToItem(moved);
    }
    emit orderChanged();
    return true;
}

bool ReorderListWidget::moveSelectedItems(int insertionRow) {
    if (insertionRow < 0 || insertionRow > count()) return false;
    QVector<int> rows;
    for (int i = 0; i < count(); ++i) if (item(i)->isSelected()) rows.append(i);
    if (rows.isEmpty() || (insertionRow < count() && item(insertionRow)->isSelected())) return false;
    int destination = insertionRow;
    for (int row : rows) if (row < insertionRow) --destination;
    QVector<QListWidgetItem *> moved;
    auto *current = currentItem();
    {
        const QSignalBlocker block(this);
        for (int row : rows) moved.append(item(row));
        for (auto it = rows.crbegin(); it != rows.crend(); ++it) takeItem(*it);
        for (int i = 0; i < moved.size(); ++i) {
            insertItem(destination + i, moved.at(i));
            moved.at(i)->setSelected(true);
        }
        setCurrentItem(current, QItemSelectionModel::NoUpdate);
        scrollToItem(current);
    }
    emit orderChanged();
    return true;
}

void ReorderListWidget::startDrag(Qt::DropActions supportedActions) {
    if (dragDropMode() != QAbstractItemView::InternalMove || currentRow() < 0
        || !(supportedActions & Qt::MoveAction)) return;
    auto *mime = model()->mimeData(selectedIndexes());
    if (!mime) return;
    mime->setData(QString::fromLatin1(reorderMimeType), QByteArrayLiteral("1"));
    // Own the drag lifecycle: the base implementation may remove source rows
    // after a MoveAction, but dropEvent already moves the actual item for us.
    QDrag drag(this);
    drag.setMimeData(mime);
    const QRect itemRect = visualItemRect(currentItem()).intersected(viewport()->rect());
    if (!itemRect.isEmpty()) {
        drag.setPixmap(viewport()->grab(itemRect));
        const QPoint cursor = viewport()->mapFromGlobal(QCursor::pos()) - itemRect.topLeft();
        drag.setHotSpot(QPoint(qBound(0, cursor.x(), itemRect.width() - 1),
                              qBound(0, cursor.y(), itemRect.height() - 1)));
    }
    drag.exec(Qt::MoveAction, Qt::MoveAction);
    clearDropRow();
}

int ReorderListWidget::destinationAt(const QPoint &position) const {
    if (const auto *target = itemAt(position)) return row(target);
    return count() - 1;
}

void ReorderListWidget::updateDropRow(const QPoint &position) {
    dropRow_ = destinationAt(position);
    externalMediaAppend_ = !itemAt(position);
    // Continue scrolling while the pointer moves along an edge during a drag.
    const int margin = autoScrollMargin();
    if (position.y() < margin) verticalScrollBar()->setValue(verticalScrollBar()->value() - verticalScrollBar()->singleStep());
    else if (position.y() > viewport()->height() - margin)
        verticalScrollBar()->setValue(verticalScrollBar()->value() + verticalScrollBar()->singleStep());
    viewport()->update();
}

void ReorderListWidget::clearDropRow() {
    dropRow_ = -1;
    externalMediaDrag_ = false;
    externalMediaAppend_ = false;
    viewport()->update();
}

void ReorderListWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (mediaDropsEnabled_ && event->mimeData()->hasFormat(QString::fromLatin1(mediaAssetMimeType))) {
        externalMediaDrag_ = true;
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    if (event->source() != this || dragDropMode() != QAbstractItemView::InternalMove
        || !event->mimeData()->hasFormat(QString::fromLatin1(reorderMimeType))) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    updateDropRow(event->position().toPoint());
}

void ReorderListWidget::dragMoveEvent(QDragMoveEvent *event) {
    if (mediaDropsEnabled_ && event->mimeData()->hasFormat(QString::fromLatin1(mediaAssetMimeType))) {
        externalMediaDrag_ = true;
        event->setDropAction(Qt::CopyAction);
        event->accept();
        updateDropRow(event->position().toPoint());
        return;
    }
    if (event->source() != this || dragDropMode() != QAbstractItemView::InternalMove
        || !event->mimeData()->hasFormat(QString::fromLatin1(reorderMimeType))) {
        clearDropRow();
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    updateDropRow(event->position().toPoint());
}

void ReorderListWidget::dragLeaveEvent(QDragLeaveEvent *event) {
    clearDropRow();
    event->accept();
}

void ReorderListWidget::dropEvent(QDropEvent *event) {
    clearDropRow();
    if (mediaDropsEnabled_ && event->mimeData()->hasFormat(QString::fromLatin1(mediaAssetMimeType))) {
        bool valid = false;
        const int assetId = event->mimeData()->data(QString::fromLatin1(mediaAssetMimeType)).toInt(&valid);
        if (!valid || assetId <= 0) { event->ignore(); return; }
        const auto *before = itemAt(event->position().toPoint());
        emit mediaDropped(assetId, before ? before->data(Qt::UserRole).toInt() : 0);
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    if (event->source() != this || dragDropMode() != QAbstractItemView::InternalMove
        || !event->mimeData()->hasFormat(QString::fromLatin1(reorderMimeType))
        || currentRow() < 0) {
        event->ignore();
        return;
    }
    if (selectedItems().size() > 1) {
        const auto *target = itemAt(event->position().toPoint());
        moveSelectedItems(target ? row(target) : count());
    } else {
        const int from = currentRow();
        const int to = destinationAt(event->position().toPoint());
        moveItem(from, to);
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void ReorderListWidget::paintEvent(QPaintEvent *event) {
    QListWidget::paintEvent(event);
    if (dropRow_ < 0 || dropRow_ >= count()) return;
    const bool group = selectedItems().size() > 1;
    if (!externalMediaDrag_ && (group ? !externalMediaAppend_ && item(dropRow_)->isSelected()
                                    : dropRow_ == currentRow())) return;
    const QRect target = visualItemRect(item(dropRow_)).adjusted(1, 1, -1, -1);
    const bool after = externalMediaDrag_ || group ? externalMediaAppend_ : dropRow_ > currentRow();
    QPainter painter(viewport());
    painter.setPen(QPen(palette().highlight().color(), 3));
    if (viewMode() == QListView::IconMode) {
        const int x = after ? target.right() : target.left();
        painter.drawLine(x, target.top(), x, target.bottom());
    } else {
        const int y = after ? target.bottom() : target.top();
        painter.drawLine(target.left(), y, target.right(), y);
    }
}
