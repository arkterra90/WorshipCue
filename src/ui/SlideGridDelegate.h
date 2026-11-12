#pragma once

#include <QStyledItemDelegate>

// Keep the painted slide card and its hit area the same size, independently of
// its number, media filename, or live/missing labels.
class SlideGridDelegate : public QStyledItemDelegate {
public:
    explicit SlideGridDelegate(QObject *parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
};
