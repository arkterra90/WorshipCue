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
#include <QTextLayout>
#include <QTextDocumentFragment>
#include <QTextBoundaryFinder>
#include <algorithm>
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

QRectF styledBounds(QTextDocument &document) {
    document.documentLayout()->documentSize();
    qreal left = 0, right = document.textWidth(), bottom = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        const auto rectangle = document.documentLayout()->blockBoundingRect(block);
        const auto *layout = block.layout();
        for (int i = 0; layout && i < layout->lineCount(); ++i) {
            const auto line = layout->lineAt(i);
            if (line.textLength() == 0) continue;
            left = qMin(left, rectangle.x() + line.x());
            right = qMax(right, rectangle.x() + line.x() + line.naturalTextWidth());
            bottom = qMax(bottom, rectangle.y() + line.y() + line.height());
        }
    }
    return QRectF(left, 0, right - left, bottom);
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

void RichTextSupport::load(QTextDocument *document, const QString &plainText, const QString &html, const SlideLook &look) {
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
    if (look.id > 0 && !look.richTextOverrides) {
        QFont font = defaultFont();
        if (!look.settings.fontFamily.isEmpty()) font.setFamily(look.settings.fontFamily);
        font.setPixelSize(look.settings.fontSize);
        font.setWeight(QFont::Weight(look.settings.fontWeight));
        font.setItalic(look.settings.italic);
        font.setUnderline(look.settings.underline);
        font.setStrikeOut(look.settings.strikeOut);
        document->setDefaultFont(font);
        QTextCursor cursor(document);
        cursor.select(QTextCursor::Document);
        QTextCharFormat format;
        format.setFont(font);
        format.setForeground(QColor(look.settings.textColor));
        cursor.setCharFormat(format);
        cursor.setBlockCharFormat(format);
        QTextBlockFormat paragraph;
        const Qt::Alignment alignments[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
        paragraph.setAlignment(alignments[look.settings.horizontalAlignment]);
        cursor.setBlockFormat(paragraph);
    }
    document->setModified(false);
}

PresentationLook RichTextSupport::captureLook(const QString &plainText, const QString &html, const SlideLook &look) {
    TextOnlyDocument document;
    load(&document, plainText, html, look);
    PresentationLook settings = look.id > 0 ? look.settings : PresentationLook{};
    QTextCursor cursor(&document);
    bool found = false;
    for (auto block = document.begin(); block.isValid() && !found; block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid() || fragment.text().trimmed().isEmpty()) continue;
            cursor.setPosition(fragment.position() + 1);
            found = true;
            break;
        }
    }
    const auto format = cursor.charFormat();
    const QFont font = format.font().resolve(defaultFont());
    settings.fontFamily = font.family();
    settings.fontSize = font.pixelSize() > 0 ? font.pixelSize() : qRound(font.pointSizeF() * 96 / 72);
    settings.fontWeight = int(font.weight());
    settings.italic = font.italic();
    settings.underline = font.underline();
    settings.strikeOut = font.strikeOut();
    if (format.foreground().style() == Qt::SolidPattern) settings.textColor = format.foreground().color().name(QColor::HexArgb);
    const auto alignment = document.begin().blockFormat().alignment();
    settings.horizontalAlignment = alignment.testFlag(Qt::AlignRight) ? 2 : alignment.testFlag(Qt::AlignLeft) ? 0 : 1;
    return settings;
}

QSizeF RichTextSupport::measuredSize(const QString &plainText, const QString &html, const SlideLook &look) {
    TextOnlyDocument document;
    load(&document, plainText, html, look);
    document.setTextWidth(look.settings.textRect().width());
    return styledBounds(document).size();
}

bool RichTextSupport::overflows(const QString &plainText, const QString &html, const SlideLook &look) {
    const auto size = measuredSize(plainText, html, look);
    const auto area = look.settings.textRect();
    return size.width() > area.width() + 0.5 || size.height() > area.height() + 0.5;
}

SplitTextResult RichTextSupport::split(const QString &plainText, const QString &html, const SlideLook &look) {
    SplitTextResult result;
    TextOnlyDocument original, styled;
    load(&original, plainText, html);
    load(&styled, plainText, html, look);
    if (original.toPlainText() != plainText) {
        result.error = QStringLiteral("The slide's plain text and formatted text differ. Save it in the text editor before splitting.");
        return result;
    }
    if (!overflows(plainText, html, look)) { result.pages.append({plainText, html}); return result; }
    styled.setTextWidth(look.settings.textRect().width());
    styled.documentLayout()->documentSize();
    QVector<int> breaks;
    for (auto block = styled.begin(); block.isValid(); block = block.next()) {
        const auto *layout = block.layout();
        for (int i = 0; layout && i < layout->lineCount(); ++i) {
            const auto line = layout->lineAt(i);
            breaks.append(qMin(int(plainText.size()), block.position() + line.textStart() + line.textLength()));
        }
        if (block.next().isValid()) breaks.append(block.next().position());
    }
    QTextBoundaryFinder sentences(QTextBoundaryFinder::Sentence, plainText);
    for (int position = sentences.toNextBoundary(); position >= 0; position = sentences.toNextBoundary()) breaks.append(position);
    breaks.append(int(plainText.size()));
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    const auto page = [&original, &plainText](int first, int last) {
        QTextCursor cursor(&original);
        cursor.setPosition(first);
        cursor.setPosition(last, QTextCursor::KeepAnchor);
        return SplitTextPage{plainText.mid(first, last - first), RichTextSupport::sanitize(QTextDocumentFragment(cursor).toHtml())};
    };
    int start = 0;
    while (start < plainText.size()) {
        const int first = int(std::upper_bound(breaks.cbegin(), breaks.cend(), start) - breaks.cbegin());
        int low = first, high = int(breaks.size()) - 1, best = -1;
        while (low <= high) {
            const int middle = low + (high - low) / 2;
            const auto candidate = page(start, breaks.at(middle));
            if (overflows(candidate.text, candidate.html, look)) high = middle - 1;
            else { best = middle; low = middle + 1; }
        }
        if (best < 0 || result.pages.size() >= 10000) {
            result.pages.clear();
            result.error = QStringLiteral("A rendered line cannot fit this text box, or splitting exceeds 10000 pages. Enlarge the box, reduce the font, or choose Fit Text.");
            return result;
        }
        const int end = breaks.at(best);
        result.pages.append(page(start, end));
        start = end;
    }
    return result;
}

void RichTextSupport::paint(QPainter &painter, const QRectF &area,
                           const QString &plainText, const QString &html, const SlideLook &look) {
    if (area.isEmpty()) return;
    TextOnlyDocument document;
    load(&document, plainText, html, look);
    if (document.toPlainText().trimmed().isEmpty()) return;
    document.setTextWidth(area.width());
    const QRectF bounds = look.id > 0 ? styledBounds(document) : QRectF(QPointF(0, 0), document.size());
    if (bounds.height() <= 0) return;
    qreal scale = 1;
    if (look.id == 0 || look.fitText || bounds.height() > area.height() || bounds.width() > area.width())
        scale = qMin(qreal(1), qMin(area.height() / bounds.height(), area.width() / qMax(qreal(1), bounds.width())));
    const auto paragraphAlignment = document.begin().blockFormat().alignment();
    const int richAlignment = paragraphAlignment.testFlag(Qt::AlignRight) ? 2 : paragraphAlignment.testFlag(Qt::AlignLeft) ? 0 : 1;
    const int horizontal = look.id > 0 ? (look.richTextOverrides ? richAlignment : look.settings.horizontalAlignment) : 1;
    const int vertical = look.id > 0 ? look.settings.verticalAlignment : 1;
    const qreal x = horizontal == 0 ? 0 : horizontal == 2 ? area.width() - bounds.width() * scale
        : (area.width() - bounds.width() * scale) / 2;
    const qreal y = vertical == 0 ? 0 : vertical == 2 ? area.height() - bounds.height() * scale
        : (area.height() - bounds.height() * scale) / 2;
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    painter.translate(area.x() + x - bounds.left() * scale, area.y() + y);
    painter.scale(scale, scale);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, Qt::white);
    document.documentLayout()->draw(&painter, context);
    painter.restore();
}
