#pragma once

#include <QListWidget>
#include <QMimeData>

inline constexpr const char *mediaAssetMimeType = "application/x-worshipcue-media-asset";

class MediaAssetListWidget : public QListWidget {
public:
    explicit MediaAssetListWidget(QWidget *parent = nullptr) : QListWidget(parent) {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setSelectionMode(QAbstractItemView::SingleSelection);
    }

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(mediaAssetMimeType)}; }
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override {
        auto *data = new QMimeData;
        if (!items.isEmpty())
            data->setData(QString::fromLatin1(mediaAssetMimeType), items.first()->data(Qt::UserRole).toByteArray());
        return data;
    }
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
};
