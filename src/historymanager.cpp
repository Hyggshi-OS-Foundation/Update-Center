#include "historymanager.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <algorithm>

namespace {
// How far apart (seconds) two dpkg.log actions may be and still be considered
// part of the same transaction/session.
constexpr qint64 kDpkgClusterGapSecs = 5 * 60;

QString stripArch(QString name)
{
    name = name.trimmed();
    const int colon = name.lastIndexOf(':');
    if (colon > 0)
        name = name.left(colon);
    return name;
}
} // namespace

HistoryManager::HistoryManager(QObject* parent)
    : QObject(parent)
{
}

void HistoryManager::refresh()
{
    emit historyLoaded(loadLogs());
}

QString HistoryManager::readFile(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QString HistoryManager::gunzip(const QString& path) const
{
    QProcess proc;
    proc.setProgram(QStringLiteral("gzip"));
    proc.setArguments({ QStringLiteral("-dc"), path });
    proc.start();
    if (!proc.waitForFinished(5000))
        return QString();
    return QString::fromUtf8(proc.readAllStandardOutput());
}

QVector<HistoryTransaction> HistoryManager::loadLogs() const
{
    // Primary source: apt history.log, which groups packages into clean
    // Start-Date..End-Date transactions. Join main + rotated logs. Plain files
    // are read directly; older rotated ones (*.gz) are gzipped.
    QString aptContent;
    aptContent += readFile(QStringLiteral("/var/log/apt/history.log"));
    for (int i = 1; i <= 6; ++i) {
        const QString plain = QStringLiteral("/var/log/apt/history.log.%1").arg(i);
        if (QFileInfo::exists(plain)) {
            aptContent += readFile(plain);
            continue;
        }
        const QString gz = plain + QStringLiteral(".gz");
        if (QFileInfo::exists(gz))
            aptContent += gunzip(gz);
    }

    if (!aptContent.trimmed().isEmpty()) {
        auto txs = parseAptHistory(aptContent);
        if (!txs.isEmpty())
            return txs;
    }

    // Fallback: dpkg.log (world-readable). Parse per-package actions and
    // cluster consecutive ones into coarse transactions.
    QString dpkgContent;
    dpkgContent += readFile(QStringLiteral("/var/log/dpkg.log"));
    for (int i = 1; i <= 6; ++i) {
        const QString plain = QStringLiteral("/var/log/dpkg.log.%1").arg(i);
        if (QFileInfo::exists(plain)) {
            dpkgContent += readFile(plain);
            continue;
        }
        const QString gz = plain + QStringLiteral(".gz");
        if (QFileInfo::exists(gz))
            dpkgContent += gunzip(gz);
    }

    if (!dpkgContent.trimmed().isEmpty()) {
        auto txs = parseDpkgLog(dpkgContent);
        if (!txs.isEmpty())
            return txs;
    }

    return {};
}

// ---------------------------------------------------------------------------
// apt history.log parsing
// ---------------------------------------------------------------------------
QVector<HistoryTransaction> HistoryManager::parseAptHistory(const QString& content) const
{
    QVector<HistoryTransaction> result;
    const auto rawLines = content.split('\n');

    QStringList block;
    auto flush = [&]() {
        if (!block.isEmpty()) {
            parseAptBlock(block, result);
            block.clear();
        }
    };

    for (int i = 0; i < rawLines.size(); ++i) {
        QString line = rawLines[i];
        if (!line.isEmpty() && line.back() == '\r')
            line.chop(1);
        if (line.startsWith(QStringLiteral("Start-Date:"))) {
            flush();
            block.append(line);
        } else if (!block.isEmpty()) {
            // Keep whole lines (apt writes very long Install/Upgrade lists).
            block.append(line);
        }
    }
    flush();

    // history.log is append-only (oldest first); show newest first in the UI.
    std::reverse(result.begin(), result.end());
    return result;
}

void HistoryManager::parseAptBlock(const QStringList& lines,
                                   QVector<HistoryTransaction>& out) const
{
    HistoryTransaction txn;
    bool hasError = false;

    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("Start-Date:"))) {
            const QString s = line.mid(QStringLiteral("Start-Date:").length()).simplified();
            txn.date = QDateTime::fromString(s, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        } else if (line.startsWith(QStringLiteral("End-Date:"))) {
            // nothing needed — date came from Start-Date
        } else if (line.startsWith(QStringLiteral("Commandline:"))) {
            txn.commandline = line.mid(QStringLiteral("Commandline:").length()).trimmed();
        } else if (line.startsWith(QStringLiteral("Error:"))) {
            hasError = true;
        } else if (line.startsWith(QStringLiteral("Requested-By:"))) {
            // not a package action — skip
        } else if (line.startsWith(QStringLiteral("Architecture:"))) {
            // not a package action — skip
        } else {
            // Action lines: "Install:", "Upgrade:", "Remove:", "Purge:", "Downgrade:"
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QString action = line.left(colon).trimmed();
            if (action.isEmpty())
                continue;
            const QString values = line.mid(colon + 1).trimmed();
            if (!values.isEmpty())
                appendPackages(values, action, txn.entries);
        }
    }

    txn.success = !hasError;
    txn.result = hasError ? QStringLiteral("failed") : QStringLiteral("success");

    if (txn.date.isValid() && !txn.entries.isEmpty())
        out.append(txn);
}

void HistoryManager::appendPackages(const QString& list, const QString& action,
                                    QVector<HistoryEntry>& out) const
{
    // The list is comma-separated, but commas also appear *inside* the
    // parenthesized version lists: "name:arch (oldver, newver)". Split only on
    // top-level commas so those stay intact.
    int depth = 0;
    int start = 0;
    const int n = list.size();
    for (int i = 0; i <= n; ++i) {
        const QChar c = (i < n) ? list.at(i) : QLatin1Char(',');
        if (c == QLatin1Char('('))
            ++depth;
        else if (c == QLatin1Char(')'))
            --depth;
        else if (c == QLatin1Char(',') && depth == 0) {
            parseSingleToken(list.mid(start, i - start), action, out);
            start = i + 1;
        }
    }
    if (start < n)
        parseSingleToken(list.mid(start), action, out);
}

void HistoryManager::parseSingleToken(const QString& token, const QString& action,
                                      QVector<HistoryEntry>& out) const
{
    QString tok = token.trimmed();
    if (tok.isEmpty())
        return;

    // Package name is the part before the first space, arch stripped.
    QString name = tok;
    const int space = tok.indexOf(' ');
    if (space > 0)
        name = tok.left(space);
    name = stripArch(name);
    if (name.isEmpty())
        return;

    // Version info is whatever is inside parentheses.
    QString version;
    const int lparen = tok.indexOf('(');
    const int rparen = tok.lastIndexOf(')');
    if (lparen >= 0 && rparen > lparen)
        version = tok.mid(lparen + 1, rparen - lparen - 1).trimmed();

    // Drop trailing install flags appended after the version: apt writes
    // "name:arch (1.2.3, automatic)" etc.
    while (true) {
        bool stripped = false;
        for (const QString& flag : { QStringLiteral("automatic"),
                                     QStringLiteral("required"),
                                     QStringLiteral("important"),
                                     QStringLiteral("optional") }) {
            const QString suffix = QStringLiteral(", ") + flag;
            if (version.endsWith(suffix)) {
                version.chop(suffix.size());
                stripped = true;
            }
        }
        if (!stripped)
            break;
    }
    // Upgrades are recorded as "(oldver, newver)"; render as "oldver => newver".
    version = version.replace(QStringLiteral(", "), QStringLiteral(" => ")).trimmed();

    out.append({ name, action, version });
}


// ---------------------------------------------------------------------------
// dpkg.log fallback parsing
// ---------------------------------------------------------------------------
QVector<HistoryTransaction> HistoryManager::parseDpkgLog(const QString& content) const
{
    struct Rec {
        QDateTime when;
        QString   action;
        QString   package;
        QString   version;
    };
    QVector<Rec> records;

    // dpkg.log line: "YYYY-MM-DD HH:MM:SS <action> <name[:arch]> <old> <new>".
    // Keep only meaningful mutations; skip "status"/"startup"/"configure".
    const auto lines = content.split('\n');
    for (const QString& raw : lines) {
        QString line = raw;
        if (!line.isEmpty() && line.back() == '\r')
            line.chop(1);
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 5)
            continue;
        const QString action = parts[2];
        if (action != QStringLiteral("install") &&
            action != QStringLiteral("upgrade") &&
            action != QStringLiteral("remove") &&
            action != QStringLiteral("purge") &&
            action != QStringLiteral("downgrade"))
            continue;

        Rec r;
        r.when = QDateTime::fromString(parts[0] + QStringLiteral(" ") + parts[1],
                                       QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        r.action = action;
        r.package = stripArch(parts[3]);
        r.version = parts.value(5); // new version (empty for remove)
        if (r.when.isValid() && !r.package.isEmpty())
            records.append(r);
    }

    // Sort chronologically (files may be joined out of order) then cluster.
    std::sort(records.begin(), records.end(),
              [](const Rec& a, const Rec& b) { return a.when < b.when; });

    QVector<HistoryTransaction> txs;
    HistoryTransaction current;
    for (const Rec& r : records) {
        if (!current.date.isValid() ||
            r.when.toSecsSinceEpoch() - current.date.toSecsSinceEpoch() > kDpkgClusterGapSecs) {
            if (current.date.isValid() && !current.entries.isEmpty())
                txs.append(current);
            current = HistoryTransaction();
            current.date = r.when;
            current.success = true;
            current.result = QStringLiteral("success");
        }
        current.entries.append({ r.package, r.action, r.version });
    }
    if (current.date.isValid() && !current.entries.isEmpty())
        txs.append(current);

    // Newest first.
    std::reverse(txs.begin(), txs.end());
    return txs;
}

