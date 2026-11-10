#include "BatchRenamer.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <limits>

namespace {
bool reject(const QString &message, QString *error) {
    if (error) *error = message;
    return false;
}
}

bool BatchRenamer::validateNames(const QStringList &names, QString *error) {
    if (error) error->clear();
    if (names.isEmpty()) return reject(QStringLiteral("Choose at least one media file."), error);
    static const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*\\x{0000}-\\x{001F}\\x{007F}]"));
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"), QRegularExpression::CaseInsensitiveOption);
    QSet<QString> unique;
    for (const QString &name : names) {
        if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
            || name != name.trimmed() || name.endsWith(QLatin1Char('.'))
            || name.toUtf8().size() > 240 || invalid.match(name).hasMatch()
            || reserved.match(name.section(QLatin1Char('.'), 0, 0)).hasMatch())
            return reject(QStringLiteral("“%1” is not a valid filename on macOS and Windows. Remove reserved characters, trailing dots/spaces, or use a shorter name.").arg(name), error);
        const QString key = name.normalized(QString::NormalizationForm_C).toCaseFolded();
        if (unique.contains(key))
            return reject(QStringLiteral("Two files would be named “%1”. Choose a unique name and number or a different naming rule.").arg(name), error);
        unique.insert(key);
    }
    return true;
}

QStringList BatchRenamer::names(const QStringList &filenames, const BatchRenameOptions &options, QString *error) {
    if (error) error->clear();
    if (options.mode == BatchRenameOptions::Mode::Format
        && (options.text.trimmed().isEmpty() || options.start < 0 || options.padding < 1 || options.padding > 10
            || filenames.size() > std::numeric_limits<int>::max() - options.start)) {
        reject(QStringLiteral("Enter a name, a starting number of zero or greater, and between 1 and 10 number digits."), error);
        return {};
    }
    if (options.mode == BatchRenameOptions::Mode::Replace && options.find.isEmpty()) {
        reject(QStringLiteral("Enter the text to replace."), error);
        return {};
    }
    QStringList result;
    for (int index = 0; index < filenames.size(); ++index) {
        const QFileInfo source(filenames.at(index));
        QString stem = source.completeBaseName();
        const QString extension = source.suffix();
        switch (options.mode) {
        case BatchRenameOptions::Mode::Keep: break;
        case BatchRenameOptions::Mode::Format:
            stem = options.text.trimmed() + QLatin1Char(' ')
                + QString::number(options.start + index).rightJustified(options.padding, QLatin1Char('0'));
            break;
        case BatchRenameOptions::Mode::Replace:
            stem.replace(options.find, options.replacement, Qt::CaseSensitive);
            break;
        case BatchRenameOptions::Mode::Add:
            stem = options.prefix ? options.text + stem : stem + options.text;
            break;
        }
        result.append(extension.isEmpty() ? stem : stem + QLatin1Char('.') + extension);
    }
    if (!validateNames(result, error)) return {};
    return result;
}
