#include "SlideGridDelegate.h"

#include <QListView>

SlideGridDelegate::SlideGridDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize SlideGridDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const auto *list = qobject_cast<const QListView *>(parent());
    if (list && list->viewMode() == QListView::IconMode && list->gridSize().isValid())
        return list->gridSize();
    return QStyledItemDelegate::sizeHint(option, index);
}
