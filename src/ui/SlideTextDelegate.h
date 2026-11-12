#pragma once

#include <QStyledItemDelegate>

class QAbstractItemView;

// An operator-readable view of slide content; the audience renderer is unchanged.
class SlideTextDelegate : public QStyledItemDelegate {
public:
    explicit SlideTextDelegate(QObject *parent = nullptr);

    void setTextPointSize(int pointSize);
    int textPointSize() const { return textPointSize_; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    int textPointSize_ = 14;
    bool relayoutPending_ = false;

    QAbstractItemView *view() const;
    int availableWidth(const QStyleOptionViewItem &option) const;
    void invalidateRowSizes();
};
