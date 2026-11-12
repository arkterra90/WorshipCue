#pragma once

#include <QFont>
#include <QRectF>
#include <QString>

class QPainter;
class QTextDocument;

// The editor and audience share logical font sizes and the same text fit.
// HTML is used only to retain text formatting, never as a resource container.
class RichTextSupport {
public:
    static QFont defaultFont();
    static QString sanitize(const QString &html);
    static void load(QTextDocument *document, const QString &plainText, const QString &html);
    static void paint(QPainter &painter, const QRectF &area,
                      const QString &plainText, const QString &html);
};
