#include "RichTextSupport.h"

#include <QAbstractTextDocumentLayout>
#include <QBrush>
#include <QCache>
#include <QFontDatabase>
#include <QPainter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextOption>
#include <QVariant>

namespace {
class TextOnlyDocument final : public QTextDocument {
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

void configure(QTextDocument *document) {
    document->setDefaultFont(RichTextSupport::defaultFont());
    document->setDocumentMargin(0);
    QTextOption option = document->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignHCenter);
    document->setDefaultTextOption(option);
}

QTextCharFormat safeCharacterFormat(const QTextCharFormat &source) {
    QTextCharFormat format;
    format.setFont(RichTextSupport::defaultFont());
    format.setForeground(Qt::white);
    if (source.hasProperty(QTextFormat::FontFamilies))
        format.setFontFamilies(source.fontFamilies().toStringList());
    if (source.hasProperty(QTextFormat::FontPointSize) && source.fontPointSize() > 0) {
        format.clearProperty(QTextFormat::FontPixelSize);
        format.setFontPointSize(qBound(qreal(1), source.fontPointSize(), qreal(512)));
    } else if (source.hasProperty(QTextFormat::FontPixelSize)) {
        format.clearProperty(QTextFormat::FontPointSize);
        format.setProperty(QTextFormat::FontPixelSize,
            qBound(1, source.property(QTextFormat::FontPixelSize).toInt(), 683));
    }
    if (source.hasProperty(QTextFormat::FontWeight)) format.setFontWeight(source.fontWeight());
    if (source.hasProperty(QTextFormat::FontItalic)) format.setFontItalic(source.fontItalic());
    if (source.hasProperty(QTextFormat::TextUnderlineStyle)) format.setUnderlineStyle(source.underlineStyle());
    if (source.hasProperty(QTextFormat::FontStrikeOut)) format.setFontStrikeOut(source.fontStrikeOut());
    if (source.foreground().style() == Qt::SolidPattern && source.foreground().color().isValid())
        format.setForeground(source.foreground().color());
    if (source.background().style() == Qt::SolidPattern && source.background().color().isValid())
        format.setBackground(source.background().color());
    return format;
}
}

QFont RichTextSupport::defaultFont() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(112);
    font.setBold(true);
    return font;
}

QString RichTextSupport::sanitize(const QString &html) {
    if (html.isEmpty() || html.size() > 1024 * 1024) return {};
    // Video frames may repaint the same slide many times a second. Bound the
    // cache by approximate string memory rather than reparsing its HTML for
    // every decoded frame. Each rendering thread owns its own cache.
    thread_local QCache<QString, QString> sanitized(4096);
    if (const auto *cached = sanitized.object(html)) return *cached;
    // The intermediate parser has no resource loader. Even pasted HTML with
    // image URLs, CSS resources, or a local filename cannot read those files.
    TextOnlyDocument source;
    configure(&source);
    source.setDefaultStyleSheet(QStringLiteral("p { margin: 0; text-align: center; } body { color: white; }"));
    source.setHtml(html);

    TextOnlyDocument clean;
    configure(&clean);
    QTextCursor cursor(&clean);
    bool first = true;
    for (QTextBlock block = source.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat paragraph;
        const Qt::Alignment alignment = block.blockFormat().alignment() & Qt::AlignHorizontal_Mask;
        paragraph.setAlignment(alignment ? alignment : Qt::AlignHCenter);
        paragraph.setTopMargin(0);
        paragraph.setBottomMargin(0);
        if (!first) cursor.insertBlock(paragraph); else cursor.setBlockFormat(paragraph);
        first = false;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            QString text = fragment.text();
            text.remove(QChar::ObjectReplacementCharacter);
            if (!text.isEmpty()) cursor.insertText(text, safeCharacterFormat(fragment.charFormat()));
        }
    }
    const QString result = clean.toHtml();
    const int cost = qMax(1, int((html.size() + result.size()) * sizeof(QChar) / 1024));
    sanitized.insert(html, new QString(result), cost);
    return result;
}

void RichTextSupport::load(QTextDocument *document, const QString &plainText, const QString &html) {
    if (!document) return;
    configure(document);
    const QString clean = sanitize(html);
    if (!clean.isEmpty()) {
        document->setHtml(clean);
    } else {
        document->setPlainText(plainText);
        QTextCursor cursor(document);
        cursor.select(QTextCursor::Document);
        QTextCharFormat format;
        format.setFont(defaultFont());
        format.setForeground(Qt::white);
        cursor.mergeCharFormat(format);
        QTextBlockFormat paragraph;
        paragraph.setAlignment(Qt::AlignHCenter);
        paragraph.setTopMargin(0);
        paragraph.setBottomMargin(0);
        cursor.mergeBlockFormat(paragraph);
    }
    document->setModified(false);
}

void RichTextSupport::paint(QPainter &painter, const QRectF &area,
                           const QString &plainText, const QString &html) {
    if (area.isEmpty()) return;
    TextOnlyDocument document;
    load(&document, plainText, html);
    if (document.toPlainText().trimmed().isEmpty()) return;
    document.setTextWidth(area.width());
    const qreal height = document.size().height();
    if (height <= 0) return;
    const qreal scale = qMin(qreal(1), area.height() / height);
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    painter.translate(area.x() + (area.width() - document.size().width() * scale) / 2,
                      area.y() + (area.height() - height * scale) / 2);
    painter.scale(scale, scale);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, Qt::white);
    document.documentLayout()->draw(&painter, context);
    painter.restore();
}
