#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QStringList>
#include <QUrl>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class HelpBrowser;

class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(const QKeySequence &blackoutShortcut, const QKeySequence &clearShortcut,
                        QWidget *parent = nullptr);
    bool openTopic(const QString &id);
    void refreshAudienceShortcuts(const QKeySequence &blackoutShortcut, const QKeySequence &clearShortcut);

private:
    struct Topic {
        QString id;
        QString title;
        QString searchableText;
        QUrl url;
    };
    QVector<Topic> topics_;
    QLineEdit *search_ = nullptr;
    QListWidget *contents_ = nullptr;
    HelpBrowser *browser_ = nullptr;
    QLabel *results_ = nullptr;
    QLabel *empty_ = nullptr;
    void filterTopics();
    void syncTopic(const QUrl &url);
    void followLink(const QUrl &link);
    void highlightMatches();
    QStringList searchTerms() const;
};
