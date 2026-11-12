#include "SlideEditorDialog.h"
#include "RichTextSupport.h"
#include "SlideRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QFontInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBlockFormat>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

namespace {
class TextOnlyDocument : public QTextDocument {
public:
    using QTextDocument::QTextDocument;
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

QToolButton *tool(const QString &text, const QString &name, const QString &tip, QWidget *parent,
                  bool checkable = false) {
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setObjectName(name);
    button->setToolTip(tip);
    button->setCheckable(checkable);
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumSize(32, 30);
    return button;
}
}

class SlideCanvasView : public QGraphicsView {
public:
    QGraphicsTextItem *editableText = nullptr;
    explicit SlideCanvasView(QWidget *parent = nullptr) : QGraphicsView(parent) {
        setScene(new QGraphicsScene(this));
        scene()->setSceneRect(0, 0, 1920, 1080);
        setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setBackgroundBrush(QColor(QStringLiteral("#0d131e")));
        setAcceptDrops(false);
        setMinimumSize(420, 260);
    }
protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        fitInView(sceneRect(), Qt::KeepAspectRatio);
    }
    void mousePressEvent(QMouseEvent *event) override {
        QGraphicsView::mousePressEvent(event);
        // A blank slide still has an editable text box. Clicking its canvas
        // gives it a cursor even when there are no glyphs to click yet.
        if (event->button() == Qt::LeftButton && editableText && !editableText->hasFocus()) {
            editableText->setFocus(Qt::MouseFocusReason);
            QTextCursor cursor = editableText->textCursor();
            cursor.movePosition(QTextCursor::End);
            editableText->setTextCursor(cursor);
        }
    }
};

class SlideTextItem : public QGraphicsTextItem {
public:
    std::function<void()> cursorChanged;
    SlideTextItem() {
        setTextInteractionFlags(Qt::TextEditorInteraction);
        setDefaultTextColor(Qt::white);
        setAcceptDrops(false);
        setZValue(1);
    }
    void paste() {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (!mime) return;
        QTextCursor cursor = textCursor();
        cursor.beginEditBlock();
        if (mime->hasHtml()) {
            const QString html = RichTextSupport::sanitize(mime->html());
            TextOnlyDocument safe;
            RichTextSupport::load(&safe, {}, html);
            cursor.insertFragment(QTextDocumentFragment(&safe));
        } else if (mime->hasText()) {
            cursor.insertText(mime->text());
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        if (cursorChanged) cursorChanged();
    }
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->matches(QKeySequence::Paste)) {
            paste();
            event->accept();
            return;
        }
        QGraphicsTextItem::keyPressEvent(event);
        if (cursorChanged) cursorChanged();
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override {
        QGraphicsTextItem::mouseReleaseEvent(event);
        if (cursorChanged) cursorChanged();
    }
    void inputMethodEvent(QInputMethodEvent *event) override {
        QGraphicsTextItem::inputMethodEvent(event);
        if (cursorChanged) cursorChanged();
    }
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
        QMenu menu;
        auto *undo = menu.addAction(QStringLiteral("Undo"), [this] { document()->undo(); });
        undo->setEnabled(document()->isUndoAvailable());
        auto *redo = menu.addAction(QStringLiteral("Redo"), [this] { document()->redo(); });
        redo->setEnabled(document()->isRedoAvailable());
        menu.addSeparator();
        auto copySelection = [this] {
            auto *mime = new QMimeData;
            const QTextDocumentFragment fragment(textCursor());
            mime->setText(fragment.toPlainText());
            mime->setHtml(RichTextSupport::sanitize(fragment.toHtml()));
            QApplication::clipboard()->setMimeData(mime);
        };
        auto *cut = menu.addAction(QStringLiteral("Cut"), [this, copySelection] {
            copySelection();
            QTextCursor cursor = textCursor();
            cursor.removeSelectedText();
            setTextCursor(cursor);
        });
        cut->setEnabled(textCursor().hasSelection());
        auto *copy = menu.addAction(QStringLiteral("Copy"), copySelection);
        copy->setEnabled(textCursor().hasSelection());
        auto *paste = menu.addAction(QStringLiteral("Paste"), [this] { this->paste(); });
        const auto *mime = QApplication::clipboard()->mimeData();
        paste->setEnabled(mime && (mime->hasText() || mime->hasHtml()));
        menu.addAction(QStringLiteral("Select All"), [this] {
            QTextCursor cursor = textCursor();
            cursor.select(QTextCursor::Document);
            setTextCursor(cursor);
        });
        menu.exec(event->screenPos());
        if (cursorChanged) cursorChanged();
        event->accept();
    }
};

SlideEditorDialog::SlideEditorDialog(const Song &song, int sourceSlide, QWidget *parent,
    const QPixmap &background, const QVector<QPixmap> &mediaFrames)
    : QDialog(parent), source_(song), mediaFrames_(mediaFrames), background_(background) {
    setObjectName(QStringLiteral("slideEditorDialog"));
    setWindowTitle(song.id ? QStringLiteral("Edit Slides") : QStringLiteral("New Presentation"));
    resize(1100, 740);
    setMinimumSize(900, 620);
    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #171d28; color: #eaf0f8; }
        QLabel { color: #d7e2ef; }
        QLabel#slideEditorHint { color: #a5b2c5; }
        QLineEdit, QListWidget, QFontComboBox, QSpinBox { background: #202a3a; color: #eaf0f8;
            border: 1px solid #35445c; border-radius: 4px; padding: 5px; }
        QListWidget::item { padding: 6px; border-radius: 4px; }
        QListWidget::item:selected { background: #284d74; color: white; }
        QToolButton, QPushButton { background: #2c3b51; color: #eaf0f8; border: 1px solid #455976;
            border-radius: 4px; padding: 6px; }
        QToolButton:checked { background: #237651; border-color: #4ac28c; }
        QToolButton:disabled, QPushButton:disabled { color: #768394; }
        QMenu { background: #202a3a; color: #eaf0f8; }
    )"));
    auto *layout = new QVBoxLayout(this);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(new QLabel(QStringLiteral("Presentation"), this));
    titleEdit_ = new QLineEdit(song.title, this);
    titleEdit_->setObjectName(QStringLiteral("slideEditorTitle"));
    titleEdit_->setPlaceholderText(QStringLiteral("Presentation title"));
    titleRow->addWidget(titleEdit_, 1);
    layout->addLayout(titleRow);

    auto *formatRow = new QHBoxLayout;
    fontFamily_ = new QFontComboBox(this);
    fontFamily_->setObjectName(QStringLiteral("slideEditorFont"));
    fontFamily_->setMaximumWidth(230);
    fontFamily_->setToolTip(QStringLiteral("Font family for the selection, or the whole slide"));
    formatRow->addWidget(fontFamily_, 1);
    fontSize_ = new QSpinBox(this);
    fontSize_->setObjectName(QStringLiteral("slideEditorFontSize"));
    fontSize_->setRange(8, 400);
    fontSize_->setSuffix(QStringLiteral(" px"));
    fontSize_->setToolTip(QStringLiteral("Font size on the 1920 × 1080 slide"));
    fontSize_->setMinimumWidth(95);
    fontSize_->setMaximumWidth(110);
    formatRow->addWidget(fontSize_);
    bold_ = tool(QStringLiteral("B"), QStringLiteral("slideBold"), QStringLiteral("Bold"), this, true);
    italic_ = tool(QStringLiteral("I"), QStringLiteral("slideItalic"), QStringLiteral("Italic"), this, true);
    underline_ = tool(QStringLiteral("U"), QStringLiteral("slideUnderline"), QStringLiteral("Underline"), this, true);
    QFont boldFont = bold_->font(); boldFont.setBold(true); bold_->setFont(boldFont);
    QFont italicFont = italic_->font(); italicFont.setItalic(true); italic_->setFont(italicFont);
    QFont underlineFont = underline_->font(); underlineFont.setUnderline(true); underline_->setFont(underlineFont);
    color_ = tool(QStringLiteral("Color"), QStringLiteral("slideTextColor"), QStringLiteral("Text color"), this);
    left_ = tool(QStringLiteral("Left"), QStringLiteral("slideAlignLeft"), QStringLiteral("Align left"), this, true);
    center_ = tool(QStringLiteral("Center"), QStringLiteral("slideAlignCenter"), QStringLiteral("Align center"), this, true);
    right_ = tool(QStringLiteral("Right"), QStringLiteral("slideAlignRight"), QStringLiteral("Align right"), this, true);
    undo_ = tool(QStringLiteral("Undo"), QStringLiteral("slideUndo"), QStringLiteral("Undo this slide's last edit"), this);
    redo_ = tool(QStringLiteral("Redo"), QStringLiteral("slideRedo"), QStringLiteral("Redo this slide's last edit"), this);
    for (auto *button : {bold_, italic_, underline_, color_, left_, center_, right_, undo_, redo_})
        formatRow->addWidget(button);
    formatRow->addStretch();
    layout->addLayout(formatRow);

    auto *split = new QSplitter(Qt::Horizontal, this);
    auto *slideColumn = new QWidget(split);
    auto *slideLayout = new QVBoxLayout(slideColumn);
    slideLayout->setContentsMargins(0, 0, 0, 0);
    slideLayout->addWidget(new QLabel(QStringLiteral("SLIDES"), slideColumn));
    navigator_ = new QListWidget(slideColumn);
    navigator_->setObjectName(QStringLiteral("slideEditorSlides"));
    navigator_->setIconSize(QSize(144, 81));
    navigator_->setViewMode(QListView::IconMode);
    navigator_->setFlow(QListView::TopToBottom);
    navigator_->setWrapping(false);
    navigator_->setMovement(QListView::Static);
    navigator_->setResizeMode(QListView::Adjust);
    navigator_->setGridSize(QSize(158, 112));
    navigator_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigator_->setMinimumWidth(175);
    navigator_->setMaximumWidth(240);
    navigator_->setWordWrap(false);
    navigator_->setSpacing(4);
    slideLayout->addWidget(navigator_, 1);
    if (!song.id) {
        auto *slideTools = new QHBoxLayout;
        auto *add = new QPushButton(QStringLiteral("+ Slide"), slideColumn);
        add->setObjectName(QStringLiteral("slideEditorAddSlide"));
        auto *duplicate = new QPushButton(QStringLiteral("Copy"), slideColumn);
        duplicate->setObjectName(QStringLiteral("slideEditorDuplicateSlide"));
        auto *remove = new QPushButton(QStringLiteral("−"), slideColumn);
        remove->setObjectName(QStringLiteral("slideEditorRemoveSlide"));
        remove->setToolTip(QStringLiteral("Remove selected slide"));
        slideTools->addWidget(add);
        slideTools->addWidget(duplicate);
        slideTools->addWidget(remove);
        slideLayout->addLayout(slideTools);
        connect(add, &QPushButton::clicked, this, [this] { addSlide(false); });
        connect(duplicate, &QPushButton::clicked, this, [this] { addSlide(true); });
        connect(remove, &QPushButton::clicked, this, &SlideEditorDialog::removeSlide);
    }
    auto *canvasColumn = new QWidget(split);
    auto *canvasLayout = new QVBoxLayout(canvasColumn);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    slideInfo_ = new QLabel(canvasColumn);
    slideInfo_->setObjectName(QStringLiteral("slideEditorInfo"));
    canvasLayout->addWidget(slideInfo_);
    canvas_ = new SlideCanvasView(canvasColumn);
    canvas_->setObjectName(QStringLiteral("slideTextCanvas"));
    canvasLayout->addWidget(canvas_, 1);
    backgroundItem_ = canvas_->scene()->addPixmap(QPixmap());
    auto *bounds = canvas_->scene()->addRect(QRectF(154, 130, 1612, 820),
        QPen(QColor(125, 180, 219, 100), 2, Qt::DashLine));
    bounds->setAcceptedMouseButtons(Qt::NoButton);
    bounds->setZValue(2);
    textItem_ = new SlideTextItem;
    canvas_->scene()->addItem(textItem_);
    canvas_->editableText = textItem_;
    textItem_->cursorChanged = [this] { updateFormatControls(); };
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({190, 850});
    layout->addWidget(split, 1);
    auto *hint = new QLabel(QStringLiteral(
        "Click the slide text to edit. Select words to format; with no selection, formatting applies to the whole slide. "
        "Changes stay in this editor until you choose Save. The live output stays unchanged."), this);
    hint->setObjectName(QStringLiteral("slideEditorHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    actions->setObjectName(QStringLiteral("slideEditorActions"));
    connect(actions, &QDialogButtonBox::accepted, this, &SlideEditorDialog::validateAndAccept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(actions);

    connect(navigator_, &QListWidget::currentRowChanged, this, &SlideEditorDialog::loadSlide);
    connect(fontFamily_, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        QTextCharFormat format; format.setFontFamilies({font.family()}); applyCharacterFormat(format);
    });
    connect(fontSize_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int size) {
        QTextCharFormat format; format.setProperty(QTextFormat::FontPixelSize, size); applyCharacterFormat(format);
    });
    connect(bold_, &QToolButton::clicked, this, [this](bool checked) {
        QTextCharFormat format; format.setFontWeight(checked ? QFont::Bold : QFont::Normal); applyCharacterFormat(format);
    });
    connect(italic_, &QToolButton::clicked, this, [this](bool checked) {
        QTextCharFormat format; format.setFontItalic(checked); applyCharacterFormat(format);
    });
    connect(underline_, &QToolButton::clicked, this, [this](bool checked) {
        QTextCharFormat format; format.setFontUnderline(checked); applyCharacterFormat(format);
    });
    connect(color_, &QToolButton::clicked, this, &SlideEditorDialog::chooseTextColor);
    connect(left_, &QToolButton::clicked, this, [this] { applyAlignment(Qt::AlignLeft); });
    connect(center_, &QToolButton::clicked, this, [this] { applyAlignment(Qt::AlignHCenter); });
    connect(right_, &QToolButton::clicked, this, [this] { applyAlignment(Qt::AlignRight); });
    connect(undo_, &QToolButton::clicked, this, [this] { if (currentDocument()) currentDocument()->undo(); });
    connect(redo_, &QToolButton::clicked, this, [this] { if (currentDocument()) currentDocument()->redo(); });

    QStringList initial = song.slides;
    if (initial.isEmpty()) initial.append(QString());
    for (int i = 0; i < initial.size(); ++i) {
        drafts_.append(createDraft(initial.at(i), song.richText.value(i)));
        new QListWidgetItem(navigator_);
    }
    mediaFrames_.resize(drafts_.size());
    for (int i = 0; i < drafts_.size(); ++i) refreshThumbnail(i);
    navigator_->setCurrentRow(qBound(0, sourceSlide, int(drafts_.size()) - 1));
}

QString SlideEditorDialog::title() const { return titleEdit_->text().trimmed(); }

QStringList SlideEditorDialog::slides() const {
    QStringList result;
    for (const auto &draft : drafts_) result.append(draft.document->toPlainText());
    return result;
}

QStringList SlideEditorDialog::richText() const {
    QStringList result;
    for (const auto &draft : drafts_)
        result.append(draft.changed ? RichTextSupport::sanitize(draft.document->toHtml()) : draft.originalHtml);
    return result;
}

int SlideEditorDialog::selectedSlideIndex() const { return activeSlide_; }

QTextDocument *SlideEditorDialog::currentDocument() const {
    return activeSlide_ >= 0 && activeSlide_ < drafts_.size() ? drafts_.at(activeSlide_).document : nullptr;
}

SlideEditorDialog::Draft SlideEditorDialog::createDraft(const QString &plain, const QString &html) {
    Draft draft;
    draft.document = new TextOnlyDocument(this);
    draft.document->setObjectName(QStringLiteral("slideEditorDocument%1").arg(drafts_.size()));
    draft.document->setUndoRedoEnabled(false);
    RichTextSupport::load(draft.document, plain, html);
    if (html.isEmpty()) {
        // Start legacy plain slides at the font size their audience renderer
        // already uses, rather than showing a differently wrapped 112px draft.
        QFont font = RichTextSupport::defaultFont();
        const QRect area(154, 130, 1612, 820);
        for (int pixels = 112; pixels >= 12; pixels -= 2) {
            font.setPixelSize(pixels);
            const QRect bounds = QFontMetrics(font).boundingRect(area, Qt::AlignCenter | Qt::TextWordWrap, plain);
            if (bounds.width() <= area.width() && bounds.height() <= area.height()) break;
        }
        draft.document->setDefaultFont(font);
        QTextCursor cursor(draft.document);
        cursor.select(QTextCursor::Document);
        QTextCharFormat format;
        format.setProperty(QTextFormat::FontPixelSize, font.pixelSize());
        cursor.mergeCharFormat(format);
    }
    draft.document->setTextWidth(1612);
    draft.document->setUndoRedoEnabled(true);
    draft.originalHtml = html;
    auto *document = draft.document;
    connect(document, &QTextDocument::contentsChanged, this, [this, document] { documentChanged(document); });
    connect(document, &QTextDocument::undoAvailable, this, [this] { updateFormatControls(); });
    connect(document, &QTextDocument::redoAvailable, this, [this] { updateFormatControls(); });
    return draft;
}

void SlideEditorDialog::documentChanged(QTextDocument *document) {
    for (int i = 0; i < drafts_.size(); ++i) {
        if (drafts_.at(i).document != document) continue;
        drafts_[i].changed = true;
        refreshThumbnail(i);
        if (i == activeSlide_) {
            updateTextGeometry();
            updateFormatControls();
        }
        return;
    }
}

void SlideEditorDialog::ensureMediaFrame(int index) {
    if (index < 0 || index >= mediaFrames_.size() || !mediaFrames_.at(index).isNull()) return;
    const SlideMedia media = source_.media.value(index);
    if (media.kind != MediaKind::Image || media.path.isEmpty()) return;
    QImageReader reader(media.path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && (sourceSize.width() > 1920 || sourceSize.height() > 1080))
        reader.setScaledSize(sourceSize.scaled(QSize(1920, 1080), Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.width() > 1920 || image.height() > 1080)
        image = image.scaled(QSize(1920, 1080), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    mediaFrames_[index] = QPixmap::fromImage(image);
}

void SlideEditorDialog::loadSlide(int index) {
    if (index < 0 || index >= drafts_.size()) return;
    loading_ = true;
    activeSlide_ = index;
    ensureMediaFrame(index);
    LivePresentation state;
    state.slides = {QString()};
    state.index = 0;
    state.media = {source_.media.value(index)};
    state.clear = true;
    QPixmap image(1920, 1080);
    image.fill(Qt::black);
    QPainter painter(&image);
    SlideRenderer::paintAudience(painter, image.rect(), state, background_, mediaFrames_.value(index));
    painter.end();
    backgroundItem_->setPixmap(image);
    textItem_->setDocument(drafts_.at(index).document);
    const auto media = source_.media.value(index);
    const QString type = media.kind == MediaKind::None ? QStringLiteral("Text")
        : media.kind == MediaKind::Image ? QStringLiteral("Picture + text") : QStringLiteral("Video + text");
    slideInfo_->setText(QStringLiteral("Slide %1 of %2 • %3").arg(index + 1).arg(drafts_.size()).arg(type));
    loading_ = false;
    updateTextGeometry();
    updateFormatControls();
    refreshThumbnail(index);
}

void SlideEditorDialog::updateTextGeometry() {
    if (!currentDocument()) return;
    const qreal height = qMax(qreal(1), currentDocument()->size().height());
    const qreal scale = qMin(qreal(1), qreal(820) / height);
    textItem_->setScale(scale);
    textItem_->setPos(154 + (1612 - 1612 * scale) / 2, 130 + (820 - height * scale) / 2);
}

void SlideEditorDialog::updateFormatControls() {
    if (loading_ || !currentDocument()) return;
    const QTextCursor cursor = textItem_->textCursor();
    // QGraphicsTextItem can emit document changes before its insertion cursor
    // is updated (for example setHtml or rich paste). Read the actual text run
    // at the selection rather than reporting its stale insertion format.
    const int last = qMax(0, currentDocument()->characterCount() - 1);
    int position = qBound(0, cursor.hasSelection() ? cursor.selectionStart() : cursor.position(), last);
    const QTextBlock block = currentDocument()->findBlock(position);
    if (!cursor.hasSelection() && block.isValid() && position > block.position()) --position;
    QTextCharFormat format = block.isValid() ? block.charFormat() : cursor.charFormat();
    if (block.isValid()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && position >= fragment.position()
                && position < fragment.position() + fragment.length()) {
                format = fragment.charFormat();
                break;
            }
        }
    }
    const QFont font = format.font().resolve(RichTextSupport::defaultFont());
    const QSignalBlocker familyBlock(fontFamily_);
    const QSignalBlocker sizeBlock(fontSize_);
    fontFamily_->setCurrentFont(font);
    QString family = font.family();
    int familyIndex = fontFamily_->findText(family, Qt::MatchFixedString);
    if (familyIndex < 0) {
        const QString resolved = QFontInfo(font).family();
        familyIndex = fontFamily_->findText(resolved, Qt::MatchFixedString);
        if (familyIndex >= 0) family = resolved;
    }
    if (familyIndex >= 0) {
        fontFamily_->setCurrentIndex(familyIndex);
    } else {
        // macOS exposes the system font through a private family alias that
        // QFontComboBox does not list. Do not display its unrelated first font.
        fontFamily_->setEditText(family.startsWith(QLatin1Char('.')) ? QStringLiteral("System font") : family);
    }
    int pixels = format.intProperty(QTextFormat::FontPixelSize);
    if (pixels <= 0 && format.fontPointSize() > 0) pixels = qRound(format.fontPointSize() * 96 / 72);
    if (pixels <= 0) pixels = RichTextSupport::defaultFont().pixelSize();
    fontSize_->setValue(qMax(8, pixels));
    bold_->setChecked(font.bold());
    italic_->setChecked(font.italic());
    underline_->setChecked(font.underline());
    const Qt::Alignment alignment = block.isValid() ? block.blockFormat().alignment() : cursor.blockFormat().alignment();
    left_->setChecked(alignment.testFlag(Qt::AlignLeft));
    center_->setChecked(alignment.testFlag(Qt::AlignHCenter));
    right_->setChecked(alignment.testFlag(Qt::AlignRight));
    undo_->setEnabled(currentDocument()->isUndoAvailable());
    redo_->setEnabled(currentDocument()->isRedoAvailable());
    QPixmap swatch(16, 16);
    swatch.fill(format.foreground().style() == Qt::NoBrush ? Qt::white : format.foreground().color());
    color_->setIcon(QIcon(swatch));
}

void SlideEditorDialog::refreshThumbnail(int index) {
    if (index < 0 || index >= navigator_->count() || index >= drafts_.size()) return;
    const auto &draft = drafts_.at(index);
    LivePresentation state;
    state.slides = {draft.document->toPlainText()};
    state.richText = {draft.changed ? RichTextSupport::sanitize(draft.document->toHtml()) : draft.originalHtml};
    state.media = {source_.media.value(index)};
    state.index = 0;
    QPixmap thumbnail(144, 81);
    thumbnail.fill(Qt::black);
    QPainter painter(&thumbnail);
    SlideRenderer::paintAudience(painter, thumbnail.rect(), state, background_, mediaFrames_.value(index));
    painter.end();
    auto *item = navigator_->item(index);
    item->setIcon(QIcon(thumbnail));
    const QString firstLine = draft.document->toPlainText().split(QLatin1Char('\n')).value(0).simplified();
    item->setText(QStringLiteral("Slide %1").arg(index + 1));
    item->setToolTip(firstLine.isEmpty() ? QStringLiteral("Blank slide") : firstLine);
}

void SlideEditorDialog::applyCharacterFormat(const QTextCharFormat &format) {
    if (loading_ || !currentDocument()) return;
    QTextCursor cursor = textItem_->textCursor();
    const bool wholeSlide = !cursor.hasSelection();
    const int position = cursor.position();
    if (wholeSlide) cursor.select(QTextCursor::Document);
    cursor.beginEditBlock();
    if (format.hasProperty(QTextFormat::FontPixelSize)) {
        // Pasted HTML can carry point sizes. Replacing a size must remove
        // that old unit while retaining each run's color/emphasis/family.
        struct Run { int first; int last; QTextCharFormat format; };
        QVector<Run> runs;
        const int first = cursor.selectionStart();
        const int last = cursor.selectionEnd();
        for (QTextBlock block = currentDocument()->findBlock(first); block.isValid() && block.position() < last;
             block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                const int begin = qMax(first, fragment.position());
                const int end = qMin(last, fragment.position() + fragment.length());
                if (fragment.isValid() && begin < end) runs.append({begin, end, fragment.charFormat()});
            }
        }
        if (runs.isEmpty()) {
            QTextCharFormat replacement = cursor.charFormat();
            replacement.clearProperty(QTextFormat::FontPointSize);
            replacement.merge(format);
            cursor.setCharFormat(replacement);
        }
        for (const auto &run : runs) {
            QTextCharFormat replacement = run.format;
            replacement.clearProperty(QTextFormat::FontPointSize);
            replacement.merge(format);
            QTextCursor range(currentDocument());
            range.setPosition(run.first);
            range.setPosition(run.last, QTextCursor::KeepAnchor);
            range.setCharFormat(replacement);
        }
    } else {
        cursor.mergeCharFormat(format);
    }
    cursor.endEditBlock();
    if (wholeSlide) cursor.setPosition(qMin(position, currentDocument()->characterCount() - 1));
    textItem_->setTextCursor(cursor);
    updateFormatControls();
}

void SlideEditorDialog::applyAlignment(Qt::Alignment alignment) {
    if (!currentDocument()) return;
    QTextCursor cursor = textItem_->textCursor();
    const bool wholeSlide = !cursor.hasSelection();
    const int position = cursor.position();
    if (wholeSlide) cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setAlignment(alignment);
    cursor.beginEditBlock();
    cursor.mergeBlockFormat(format);
    cursor.endEditBlock();
    if (wholeSlide) cursor.setPosition(qMin(position, currentDocument()->characterCount() - 1));
    textItem_->setTextCursor(cursor);
    updateFormatControls();
}

void SlideEditorDialog::chooseTextColor() {
    if (!currentDocument()) return;
    QColor initial = textItem_->textCursor().charFormat().foreground().color();
    if (!initial.isValid()) initial = Qt::white;
    const QColor selected = QColorDialog::getColor(initial, this, QStringLiteral("Slide text color"));
    if (!selected.isValid()) return;
    QTextCharFormat format;
    format.setForeground(selected);
    applyCharacterFormat(format);
}

void SlideEditorDialog::addSlide(bool duplicate) {
    if (source_.id) return;
    const QString plain = duplicate && currentDocument() ? currentDocument()->toPlainText() : QString();
    const QString html = duplicate && currentDocument() ? RichTextSupport::sanitize(currentDocument()->toHtml()) : QString();
    drafts_.append(createDraft(plain, html));
    mediaFrames_.resize(drafts_.size());
    new QListWidgetItem(navigator_);
    navigator_->setCurrentRow(int(drafts_.size()) - 1);
    refreshThumbnail(int(drafts_.size()) - 1);
}

void SlideEditorDialog::removeSlide() {
    if (source_.id || drafts_.size() <= 1 || activeSlide_ < 0) return;
    const int removed = activeSlide_;
    const QSignalBlocker block(navigator_);
    auto *document = drafts_.at(removed).document;
    drafts_.removeAt(removed);
    mediaFrames_.removeAt(removed);
    delete navigator_->takeItem(removed);
    const int selected = qMin(removed, int(drafts_.size()) - 1);
    navigator_->setCurrentRow(selected);
    loadSlide(selected);
    document->deleteLater();
    for (int i = 0; i < drafts_.size(); ++i) refreshThumbnail(i);
}

void SlideEditorDialog::validateAndAccept() {
    if (title().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Presentation title"), QStringLiteral("Enter a presentation title before saving."));
        titleEdit_->setFocus();
        return;
    }
    if (!source_.id) {
        bool hasText = false;
        for (const auto &text : slides()) if (!text.trimmed().isEmpty()) { hasText = true; break; }
        if (!hasText) {
            QMessageBox::warning(this, QStringLiteral("Slide content"), QStringLiteral("Enter text on at least one slide before saving."));
            return;
        }
    }
    accept();
}
