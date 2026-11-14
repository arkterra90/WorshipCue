#pragma once

#include "../core/PresentationLook.h"
#include <QFont>
#include <QVector>
#include <QRectF>
#include <QString>

class QPainter;
class QTextDocument;

struct SplitTextPage {
    QString text;
    QString html;
};
struct SplitTextResult {
    QVector<SplitTextPage> pages;
    QString error;
};

// The editor and audience share logical font sizes and the same text fit.
// HTML is used only to retain text formatting, never as a resource container.
class RichTextSupport {
public:
    static QFont defaultFont();
    static QString sanitize(const QString &html);
    static void load(QTextDocument *document, const QString &plainText, const QString &html, const SlideLook &look = {});
    static PresentationLook captureLook(const QString &plainText, const QString &html, const SlideLook &look = {});
    static QSizeF measuredSize(const QString &plainText, const QString &html, const SlideLook &look);
    static bool overflows(const QString &plainText, const QString &html, const SlideLook &look);
    static SplitTextResult split(const QString &plainText, const QString &html, const SlideLook &look);
    static void paint(QPainter &painter, const QRectF &area,
                      const QString &plainText, const QString &html, const SlideLook &look = {});
};
