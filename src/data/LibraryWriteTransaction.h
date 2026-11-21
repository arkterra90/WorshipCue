#pragma once

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

class LibraryWriteTransaction {
public:
    explicit LibraryWriteTransaction(QSqlDatabase &database) : database_(database) {
        // Acquire the writer slot before reading. A deferred read snapshot
        // cannot always upgrade after another WAL connection commits.
        QSqlQuery query(database_);
        active_ = query.exec("BEGIN IMMEDIATE");
        if (!active_) error_ = query.lastError().text();
    }

    ~LibraryWriteTransaction() {
        if (active_) database_.rollback();
    }

    LibraryWriteTransaction(const LibraryWriteTransaction &) = delete;
    LibraryWriteTransaction &operator=(const LibraryWriteTransaction &) = delete;

    bool active() const { return active_; }
    QString error() const { return error_; }

    bool commit() {
        if (!active_ || !database_.commit()) return false;
        active_ = false;
        return true;
    }

private:
    QSqlDatabase &database_;
    bool active_ = false;
    QString error_;
};
