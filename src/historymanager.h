#pragma once

#include <QObject>
#include <QVector>
#include <QDateTime>

// A single package touched by one apt transaction, parsed from apt/dpkg logs.
struct HistoryEntry
{
    QString package; // plain name, arch stripped (e.g. "systemd")
    QString action;  // "Install" / "Upgrade" / "Remove" / ...
    QString version; // version info, e.g. "3.2.0" or "old => new"
};

// One apt transaction = one Start-Date..End-Date block in /var/log/apt/history.log
// (or a time-cluster of packages in /var/log/dpkg.log). This groups the many
// packages touched by a single "apt upgrade" run into one History row instead of
// flooding the History page with one row per package.
struct HistoryTransaction
{
    QDateTime date;          // when the transaction started
    QString   commandline;   // e.g. "apt-get install --only-upgrade -y ..." (may be empty)
    bool      success = true;
    QString   result;        // human-readable result, filled from translation later
    QVector<HistoryEntry> entries;

    int packageCount() const { return entries.size(); }
    bool isValid() const { return date.isValid(); }
};

// Reads real apt/dpkg history from disk and exposes it as a list of grouped
// transactions (the "model" data). The GUI never fabricates history rows — it
// only renders whatever this class loads from the system logs.
class HistoryManager : public QObject
{
    Q_OBJECT

public:
    explicit HistoryManager(QObject* parent = nullptr);

    // Re-read the logs and emit historyLoaded() with the result.
    void refresh();

signals:
    void historyLoaded(const QVector<HistoryTransaction>& transactions);

private:
    QVector<HistoryTransaction> loadLogs() const;

    // apt history.log -> perfectly grouped transactions.
    QVector<HistoryTransaction> parseAptHistory(const QString& content) const;
    void parseAptBlock(const QStringList& block, QVector<HistoryTransaction>& out) const;
    void appendPackages(const QString& list, const QString& action,
                        QVector<HistoryEntry>& out) const;
    void parseSingleToken(const QString& token, const QString& action,
                          QVector<HistoryEntry>& out) const;

    // dpkg.log fallback -> per-package lines clustered into transactions.
    QVector<HistoryTransaction> parseDpkgLog(const QString& content) const;

    // Reads one plain-text log file (returns "" if missing/unreadable).
    QString readFile(const QString& path) const;
    // Decompresses a gzip file via the gzip binary ("" on failure).
    QString gunzip(const QString& path) const;
};
