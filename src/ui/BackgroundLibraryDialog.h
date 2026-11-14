#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;
class QLabel;
class QPushButton;

// Included backgrounds are bundled resources; imported still images are copied
// into the library independently of the assignment.
// Cancelling this dialog leaves the presentation's current assignment intact.
class BackgroundLibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit BackgroundLibraryDialog(const QString &databaseDirectory,
                                     const QString &currentPath,
                                     QWidget *parent = nullptr);
    QString selectedPath() const;
    bool importFiles(const QStringList &paths, QString *error);
    void setAssignmentEnabled(bool enabled);

signals:
    void fileImported(const QString &managedPath);

private:
    QString directory_;
    QListWidget *backgrounds_ = nullptr;
    QLabel *hint_ = nullptr;
    QPushButton *assign_ = nullptr;
    bool assignmentEnabled_ = true;
    void reload(const QString &selectPath);
    void chooseFiles();
};
