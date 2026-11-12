#include "SlideTextDelegate.h"
#include "../core/MediaSlide.h"

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QListView>
#include <QPainter>
#include <QStyle>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QtMath>

namespace {
constexpr int horizontalMargin = 14;
constexpr int topMargin = 10;
constexpr int bottomMargin = 12;
constexpr int headerGap = 6;

QString bodyText(const QModelIndex &index) {
    const QString lyrics = index.data(Qt::UserRole + 1).toString();
    if (!lyrics.trimmed().isEmpty()) return lyrics;
    const auto media = index.data(Qt::UserRole + 3).value<SlideMedia>();
    if (media.kind != MediaKind::None) {
        const QString name = media.name.isEmpty() ? QFileInfo(media.path).fileName() : media.name;
        const QString kind = media.kind == MediaKind::Video ? QStringLiteral("Video") : QStringLiteral("Picture");
        return name.isEmpty() ? kind : kind + QLatin1Char('\n') + name;
    }
    return QStringLiteral("Empty slide");
}

QFont bodyFont(const QStyleOptionViewItem &option, int pointSize) {
    QFont font = option.font;
    font.setPointSize(pointSize);
    font.setBold(false);
    return font;
}

QFont headerFont(const QStyleOptionViewItem &option, int pointSize) {
    QFont font = option.font;
    font.setPointSize(qMax(10, pointSize - 3));
    font.setBold(true);
    return font;
}

void prepareDocument(QTextDocument &document, const QString &text, const QFont &font, int width) {
    document.setDocumentMargin(0);
    document.setDefaultFont(font);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(textOption);
    document.setPlainText(text);
    document.setTextWidth(qMax(1, width));
}
}

SlideTextDelegate::SlideTextDelegate(QObject *parent) : QStyledItemDelegate(parent) {
    if (auto *itemView = view()) itemView->viewport()->installEventFilter(this);
}

QAbstractItemView *SlideTextDelegate::view() const {
    return qobject_cast<QAbstractItemView *>(parent());
}

int SlideTextDelegate::availableWidth(const QStyleOptionViewItem &option) const {
    if (const auto *itemView = view()) {
        const auto *list = qobject_cast<const QListView *>(itemView);
        return qMax(1, itemView->viewport()->width() - (list ? 2 * list->spacing() : 0));
    }
    return option.rect.width() > 0 ? option.rect.width() : 360;
}

void SlideTextDelegate::setTextPointSize(int pointSize) {
    pointSize = qBound(12, pointSize, 20);
    if (pointSize == textPointSize_) return;
    textPointSize_ = pointSize;
    invalidateRowSizes();
}

void SlideTextDelegate::invalidateRowSizes() {
    auto *itemView = view();
    if (!itemView || itemView->itemDelegate() != this || !itemView->model()) return;
    const auto root = itemView->rootIndex();
    for (int row = 0; row < itemView->model()->rowCount(root); ++row)
        emit sizeHintChanged(itemView->model()->index(row, 0, root));
    itemView->viewport()->update();
}

bool SlideTextDelegate::eventFilter(QObject *watched, QEvent *event) {
    auto *itemView = view();
    if (itemView && watched == itemView->viewport() && event->type() == QEvent::Resize && !relayoutPending_) {
        relayoutPending_ = true;
        QTimer::singleShot(0, this, [this] {
            relayoutPending_ = false;
            invalidateRowSizes();
        });
    }
    return QStyledItemDelegate::eventFilter(watched, event);
}

QSize SlideTextDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QStyleOptionViewItem styled(option);
    initStyleOption(&styled, index);
    const int width = availableWidth(option);
    QTextDocument document;
    prepareDocument(document, bodyText(index), bodyFont(styled, textPointSize_), width - 2 * horizontalMargin);
    const int headerHeight = QFontMetrics(headerFont(styled, textPointSize_)).height();
    return QSize(width, topMargin + headerHeight + headerGap + qCeil(document.size().height()) + bottomMargin);
}

void SlideTextDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QStyleOptionViewItem styled(option);
    initStyleOption(&styled, index);
    const QString heading = styled.text;
    // Let the platform/style sheet draw selection, hover, focus, and the item background.
    styled.text.clear();
    styled.icon = QIcon();
    styled.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
    const QStyle *style = styled.widget ? styled.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &styled, painter, styled.widget);

    painter->save();
    painter->setClipRect(option.rect);
    const QRect content = option.rect.adjusted(horizontalMargin, topMargin, -horizontalMargin, -bottomMargin);
    const bool selected = styled.state.testFlag(QStyle::State_Selected);
    const QColor foreground = styled.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    QColor metadata = foreground;
    metadata.setAlphaF(selected ? 0.85 : 0.72);
    const QFont smallFont = headerFont(styled, textPointSize_);
    const QFontMetrics metrics(smallFont);
    painter->setFont(smallFont);
    painter->setPen(metadata);
    painter->drawText(QRect(content.left(), content.top(), content.width(), metrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(heading, Qt::ElideRight, qMax(1, content.width())));

    QTextDocument document;
    prepareDocument(document, bodyText(index), bodyFont(styled, textPointSize_), content.width());
    painter->translate(content.left(), content.top() + metrics.height() + headerGap);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = styled.palette;
    context.palette.setColor(QPalette::Text, foreground);
    document.documentLayout()->draw(painter, context);
    painter->restore();
}
