#pragma once

#include <QStringList>

struct BatchRenameOptions {
    enum class Mode { Keep, Format, Replace, Add };
    Mode mode = Mode::Format;
    QString text;
    QString find;
    QString replacement;
    bool prefix = true;
    int start = 1;
    int padding = 2;
};

// Pure filename planning: original files are never mutated by this class.
class BatchRenamer {
public:
    static QStringList names(const QStringList &filenames, const BatchRenameOptions &options,
                             QString *error = nullptr);
    static bool validateNames(const QStringList &names, QString *error = nullptr);
};
