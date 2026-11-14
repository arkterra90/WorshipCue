#include "HelpDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

// Every document comes from compiled resources. Restrict browser loads to the
// installed topic catalogue so links and history cannot open other files/URLs.
class HelpBrowser : public QTextBrowser {
public:
    explicit HelpBrowser(const QHash<QUrl, QByteArray> &documents, QWidget *parent)
        : QTextBrowser(parent), documents_(documents) {}
    void setDocumentSource(const QUrl &url, const QByteArray &markdown) { documents_.insert(url, markdown); }
    QVariant loadResource(int type, const QUrl &url) override {
        if (type != QTextDocument::MarkdownResource || url.hasQuery() || !url.host().isEmpty()) return {};
        const auto document = documents_.constFind(url.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery));
        if (document == documents_.cend()) return {};
        return *document;
    }
private:
    QHash<QUrl, QByteArray> documents_;
};

namespace {
QString readResource(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString shortcutLabel(const QKeySequence &shortcut) {
    QString label = shortcut.toString(QKeySequence::NativeText).toHtmlEscaped();
    label.replace(QLatin1Char('\\'), QStringLiteral("&#92;"));
    label.replace(QLatin1Char('`'), QStringLiteral("&#96;"));
    label.replace(QLatin1Char('|'), QStringLiteral("&#124;"));
    label.replace(QLatin1Char('*'), QStringLiteral("&#42;"));
    label.replace(QLatin1Char('_'), QStringLiteral("&#95;"));
    label.replace(QLatin1Char('['), QStringLiteral("&#91;"));
    label.replace(QLatin1Char(']'), QStringLiteral("&#93;"));
    return label;
}

QString articleMarkdown(const QString &id, const QKeySequence &blackoutShortcut, const QKeySequence &clearShortcut) {
    QString markdown = readResource(QStringLiteral(":/help/%1.md").arg(id));
    markdown.replace(QStringLiteral("{{blackout-shortcut}}"), shortcutLabel(blackoutShortcut));
    markdown.replace(QStringLiteral("{{clear-shortcut}}"), shortcutLabel(clearShortcut));
    markdown.replace(QStringLiteral("{{help-shortcut}}"), shortcutLabel(QKeySequence::HelpContents));
    markdown.replace(QStringLiteral("{{back-shortcut}}"), shortcutLabel(QKeySequence::Back));
    markdown.replace(QStringLiteral("{{forward-shortcut}}"), shortcutLabel(QKeySequence::Forward));
    return markdown;
}
}

HelpDialog::HelpDialog(const QKeySequence &blackoutShortcut, const QKeySequence &clearShortcut, QWidget *parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("helpDialog"));
    setWindowTitle(QStringLiteral("WorshipCue Help"));
    setModal(false);
    resize(1000, 720);
    setMinimumSize(640, 440);

    QHash<QUrl, QByteArray> documents;
    const auto catalogue = QJsonDocument::fromJson(readResource(QStringLiteral(":/help/topics.json")).toUtf8()).array();
    for (const auto &value : catalogue) {
        const auto entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString markdown = articleMarkdown(id, blackoutShortcut, clearShortcut);
        if (id.isEmpty() || markdown.isEmpty()) continue;
        QTextDocument document;
        document.setMarkdown(markdown);
        const QUrl url(QStringLiteral("qrc:/help/%1.md").arg(id));
        topics_.append({id, entry.value(QStringLiteral("title")).toString(), document.toPlainText().toCaseFolded(), url});
        documents.insert(url, markdown.toUtf8());
    }

    auto *layout = new QVBoxLayout(this);
    auto *toolbar = new QHBoxLayout;
    layout->addLayout(toolbar);
    auto *back = new QPushButton(QStringLiteral("Back"), this);
    back->setObjectName(QStringLiteral("helpBack"));
    auto *forward = new QPushButton(QStringLiteral("Forward"), this);
    forward->setObjectName(QStringLiteral("helpForward"));
    auto *home = new QPushButton(QStringLiteral("Home"), this);
    home->setObjectName(QStringLiteral("helpHome"));
    for (auto *button : {back, forward, home}) {
        button->setAutoDefault(false);
        toolbar->addWidget(button);
    }
    toolbar->addStretch();
    auto *offline = new QLabel(QStringLiteral("Available offline"), this);
    toolbar->addWidget(offline);

    auto *splitter = new QSplitter(this);
    splitter->setObjectName(QStringLiteral("helpSplitter"));
    layout->addWidget(splitter, 1);
    auto *sidebar = new QWidget(splitter);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 6, 0);
    search_ = new QLineEdit(sidebar);
    search_->setObjectName(QStringLiteral("helpSearch"));
    search_->setPlaceholderText(QStringLiteral("Search all help topics"));
    search_->setAccessibleName(QStringLiteral("Search all help topics"));
    search_->setMaxLength(256);
    search_->setClearButtonEnabled(true);
    sidebarLayout->addWidget(search_);
    results_ = new QLabel(sidebar);
    results_->setObjectName(QStringLiteral("helpResults"));
    sidebarLayout->addWidget(results_);
    contents_ = new QListWidget(sidebar);
    contents_->setObjectName(QStringLiteral("helpTopics"));
    contents_->setAccessibleName(QStringLiteral("Help topics"));
    contents_->setWordWrap(true);
    sidebarLayout->addWidget(contents_, 1);
    for (const auto &topic : topics_) contents_->addItem(topic.title);

    auto *readingPane = new QWidget(splitter);
    auto *readingLayout = new QVBoxLayout(readingPane);
    readingLayout->setContentsMargins(0, 0, 0, 0);
    browser_ = new HelpBrowser(documents, readingPane);
    browser_->setObjectName(QStringLiteral("helpBrowser"));
    browser_->setAccessibleName(QStringLiteral("Help article"));
    browser_->setOpenLinks(false);
    browser_->setOpenExternalLinks(false);
    browser_->setFrameShape(QFrame::NoFrame);
    browser_->document()->setDocumentMargin(18);
    browser_->document()->setDefaultStyleSheet(QStringLiteral(
        "a { color: #6ba7df; } p, li { line-height: 140%; } "
        "h1 { font-size: 22pt; } h2 { font-size: 15pt; } "
        "pre { white-space: pre-wrap; } th, td { padding: 7px; }"));
    readingLayout->addWidget(browser_);
    empty_ = new QLabel(QStringLiteral("No topics match your search. Try fewer words or clear the search."), readingPane);
    empty_->setObjectName(QStringLiteral("helpEmpty"));
    empty_->setWordWrap(true);
    empty_->setAlignment(Qt::AlignCenter);
    readingLayout->addWidget(empty_, 1);
    splitter->setSizes({280, 700});
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
    connect(search_, &QLineEdit::textChanged, this, &HelpDialog::filterTopics);
    connect(contents_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) browser_->setSource(topics_.at(row).url, QTextDocument::MarkdownResource);
    });
    connect(browser_, &QTextBrowser::sourceChanged, this, &HelpDialog::syncTopic);
    connect(browser_, &QTextBrowser::anchorClicked, this, &HelpDialog::followLink);
    connect(browser_, &QTextBrowser::backwardAvailable, back, &QPushButton::setEnabled);
    connect(browser_, &QTextBrowser::forwardAvailable, forward, &QPushButton::setEnabled);
    connect(back, &QPushButton::clicked, browser_, &QTextBrowser::backward);
    connect(forward, &QPushButton::clicked, browser_, &QTextBrowser::forward);
    connect(home, &QPushButton::clicked, this, [this] { openTopic(QStringLiteral("getting-started")); });
    auto *backShortcut = new QShortcut(QKeySequence::Back, this);
    connect(backShortcut, &QShortcut::activated, back, &QPushButton::click);
    auto *forwardShortcut = new QShortcut(QKeySequence::Forward, this);
    connect(forwardShortcut, &QShortcut::activated, forward, &QPushButton::click);
    auto *find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, this, [this] { search_->setFocus(); search_->selectAll(); });
    back->setEnabled(false);
    forward->setEnabled(false);
    filterTopics();
    if (topics_.isEmpty()) {
        empty_->setText(QStringLiteral("Help resources could not be loaded. Reinstall the complete WorshipCue application."));
    }
}

bool HelpDialog::openTopic(const QString &id) {
    for (const auto &topic : topics_) {
        if (topic.id == id) {
            browser_->setSource(topic.url, QTextDocument::MarkdownResource);
            // setSource may reuse the current document without a source signal.
            syncTopic(topic.url);
            return true;
        }
    }
    return false;
}

void HelpDialog::refreshAudienceShortcuts(const QKeySequence &blackoutShortcut, const QKeySequence &clearShortcut) {
    for (auto &topic : topics_) {
        if (topic.id != QStringLiteral("shortcuts")) continue;
        const QString markdown = articleMarkdown(topic.id, blackoutShortcut, clearShortcut);
        QTextDocument document;
        document.setMarkdown(markdown);
        topic.searchableText = document.toPlainText().toCaseFolded();
        browser_->setDocumentSource(topic.url, markdown.toUtf8());
        if (browser_->source().adjusted(QUrl::RemoveFragment) == topic.url) browser_->reload();
        filterTopics();
        break;
    }
}

QStringList HelpDialog::searchTerms() const {
    return search_->text().toCaseFolded().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

void HelpDialog::filterTopics() {
    const auto terms = searchTerms();
    int visibleCount = 0;
    QListWidgetItem *first = nullptr;
    for (int row = 0; row < topics_.size(); ++row) {
        bool matches = true;
        const auto &topic = topics_.at(row);
        const QString text = topic.title.toCaseFolded() + QLatin1Char('\n') + topic.searchableText;
        for (const auto &term : terms) matches = matches && text.contains(term);
        auto *item = contents_->item(row);
        item->setHidden(!matches);
        if (matches) {
            ++visibleCount;
            if (!first) first = item;
        }
    }
    results_->setText(terms.isEmpty() ? QStringLiteral("%1 topics").arg(visibleCount)
                                    : QStringLiteral("%1 matching topics").arg(visibleCount));
    browser_->setVisible(visibleCount > 0);
    empty_->setVisible(visibleCount == 0);
    if (!contents_->currentItem() || contents_->currentItem()->isHidden()) contents_->setCurrentItem(first);
    highlightMatches();
}

void HelpDialog::syncTopic(const QUrl &url) {
    const QUrl page = url.adjusted(QUrl::RemoveFragment);
    for (int row = 0; row < topics_.size(); ++row) {
        if (topics_.at(row).url != page) continue;
        const QSignalBlocker block(contents_);
        auto *item = contents_->item(row);
        contents_->setCurrentItem(item);
        if (item->isHidden()) search_->clear();
        browser_->show();
        empty_->hide();
        contents_->scrollToItem(item);
        highlightMatches();
        break;
    }
}

void HelpDialog::followLink(const QUrl &link) {
    const QUrl resolved = browser_->source().resolved(link);
    if (resolved.hasQuery() || !resolved.host().isEmpty()) return;
    const QUrl page = resolved.adjusted(QUrl::RemoveFragment);
    for (const auto &topic : topics_) {
        if (topic.url == page) {
            browser_->setSource(resolved, QTextDocument::MarkdownResource);
            return;
        }
    }
}

void HelpDialog::highlightMatches() {
    QList<QTextEdit::ExtraSelection> matches;
    for (const auto &term : searchTerms()) {
        QTextCursor cursor(browser_->document());
        while (matches.size() < 200) {
            cursor = browser_->document()->find(term, cursor);
            if (cursor.isNull()) break;
            QTextEdit::ExtraSelection match;
            match.cursor = cursor;
            match.format.setBackground(QColor(QStringLiteral("#f6d86b")));
            match.format.setForeground(Qt::black);
            matches.append(match);
        }
    }
    browser_->setExtraSelections(matches);
}
