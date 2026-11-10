#pragma once

#include <QListWidget>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QPaintEvent;

// Internal drag-and-drop always changes model order, including in IconMode.
// QListView's default IconMode movement can otherwise only reposition icons.
class ReorderListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit ReorderListWidget(QWidget *parent = nullptr);
    bool moveItem(int from, int to);

signals:
    void orderChanged();

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int dropRow_ = -1;
    int destinationAt(const QPoint &position) const;
    void updateDropRow(const QPoint &position);
    void clearDropRow();
};
