#include "aptmanager.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QFileInfo>

namespace {
// apt's output is localized (e.g. Vietnamese), which breaks our regex-based
// parsing. Force the C locale for commands whose output we parse, so it's
// always in English no matter what language the desktop is set to. This
// only affects these child processes, not the app's own UI language.
QProcessEnvironment cLocaleEnvironment()
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("LANG", "C");
    env.insert("LC_ALL", "C");
    env.insert("LANGUAGE", "C");
    return env;
}
} // namespace

AptManager::AptManager(QObject* parent)
    : QObject(parent)
{
}

void AptManager::cancelCurrent()
{
    if (m_currentProcess && m_currentProcess->state() != QProcess::NotRunning) {
        m_currentProcess->kill();
    }
}

// ------------------------------------------------------------
// Step 1: apt-get update (via pkexec — needs root)
// ------------------------------------------------------------
void AptManager::refreshIndex()
{
    auto* proc = new QProcess(this);
    m_currentProcess = proc;
    m_refreshOutputBuffer.clear();
    proc->setProgram("pkexec");
    proc->setArguments({ "apt-get", "update" });
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
        m_refreshOutputBuffer += chunk;
        const auto lines = chunk.split('\n', Qt::SkipEmptyParts);
        for (const QString& l : lines)
            emit refreshOutput(l.trimmed());
    });

    connect(proc, &QProcess::finished, this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);
        QString err;
        if (!ok) {
            if (exitCode == 126 || exitCode == 127) {
                err = QStringLiteral("Authentication was cancelled or pkexec is unavailable.");
            } else {
                err = QStringLiteral("apt-get update exited with code %1.").arg(exitCode);
                // Extract Err: / E: lines from the buffered output so the user
                // can see which repository failed without reading the full log.
                QStringList errLines;
                for (const QString& line : m_refreshOutputBuffer.split('\n', Qt::SkipEmptyParts)) {
                    const QString t = line.trimmed();
                    if (t.startsWith(QLatin1String("Err:")) || t.startsWith(QLatin1String("E:")))
                        errLines.append(t);
                }
                if (!errLines.isEmpty())
                    err += QStringLiteral("\n\n") + errLines.join('\n');
            }
        }
        emit refreshFinished(ok, err);
        if (m_currentProcess == proc)
            m_currentProcess = nullptr;
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        emit refreshFinished(false, proc->errorString());
        if (m_currentProcess == proc)
            m_currentProcess = nullptr;
    });

    proc->start();
}

// ------------------------------------------------------------
// Step 2: apt list --upgradable (no root needed)
// ------------------------------------------------------------
void AptManager::listUpgradable()
{
    auto* proc = new QProcess(this);
    proc->setProgram("apt");
    proc->setArguments({ "list", "--upgradable" });
    proc->setProcessEnvironment(cLocaleEnvironment());

    connect(proc, &QProcess::finished, this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
        QVector<UpdateItem> items;
        QString err;

        if (status == QProcess::NormalExit && exitCode == 0) {
            const QString output = QString::fromUtf8(proc->readAllStandardOutput());
            const auto lines = output.split('\n', Qt::SkipEmptyParts);

            // Example line:
            // firefox/jammy-updates 128.0+build2-0ubuntu1 amd64 [upgradable from: 127.0+build1-0ubuntu1]
            static const QRegularExpression re(
                R"(^(\S+)/\S+\s+(\S+)\s+\S+\s+\[upgradable from:\s*([^\]]+)\])");

            for (const QString& line : lines) {
                if (line.startsWith(QStringLiteral("Listing...")))
                    continue;
                const auto match = re.match(line);
                if (!match.hasMatch())
                    continue;

                UpdateItem item;
                item.name = match.captured(1);
                item.newVersion = match.captured(2);
                item.currentVersion = match.captured(3).trimmed();
                item.selected = true;
                item.sizeBytes = fetchDownloadSize(item.name);
                items.append(item);
            }
        } else {
            err = QString::fromUtf8(proc->readAllStandardError());
            if (err.isEmpty())
                err = QStringLiteral("apt list --upgradable exited with code %1.").arg(exitCode);
        }

        emit listFinished(items, err);
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        emit listFinished({}, proc->errorString());
    });

    proc->start();
}

// Blocking helper: ask apt-cache for the download size of one package.
// Kept short-timeout so a slow/missing entry can't hang the UI for long.
qint64 AptManager::fetchDownloadSize(const QString& packageName) const
{
    QProcess proc;
    proc.setProcessEnvironment(cLocaleEnvironment());
    proc.start(QStringLiteral("apt-cache"), { QStringLiteral("show"), packageName });
    if (!proc.waitForFinished(3000))
        return 0;

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression re(R"(^Size:\s*(\d+))", QRegularExpression::MultilineOption);
    const auto match = re.match(output);
    return match.hasMatch() ? match.captured(1).toLongLong() : 0;
}

// ------------------------------------------------------------
// Step 3: apt-get install --only-upgrade -y <packages> (via pkexec)
// ------------------------------------------------------------
void AptManager::installPackages(const QStringList& packageNames)
{
    if (packageNames.isEmpty()) {
        emit installFinished(false, QStringLiteral("No packages selected."));
        return;
    }

    auto* proc = new QProcess(this);
    m_currentProcess = proc;
    m_installOutputBuffer.clear();

    QStringList args = { "apt-get", "install", "--only-upgrade", "-y" };
    args << packageNames;

    proc->setProgram("pkexec");
    proc->setArguments(args);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProcessEnvironment(cLocaleEnvironment());

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
        m_installOutputBuffer += chunk;
        const auto lines = chunk.split('\n', Qt::SkipEmptyParts);
        for (const QString& l : lines)
            emit installOutput(l.trimmed());
    });

    connect(proc, &QProcess::finished, this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);
        QString err;
        if (!ok) {
            err = exitCode == 126 || exitCode == 127
                ? QStringLiteral("Authentication was cancelled or pkexec is unavailable.")
                : QStringLiteral("apt-get install exited with code %1.").arg(exitCode);
        }

        if (ok) {
            // Example: "3 upgraded, 0 newly installed, 0 to remove and 15 not upgraded."
            static const QRegularExpression summaryRe(
                R"((\d+)\s+upgraded,\s+\d+\s+newly installed,\s+\d+\s+to remove and\s+(\d+)\s+not upgraded)");
            const auto match = summaryRe.match(m_installOutputBuffer);
            const int upgraded = match.hasMatch() ? match.captured(1).toInt() : 0;
            const int notUpgraded = match.hasMatch() ? match.captured(2).toInt() : 0;
            const bool restartRequired = QFileInfo::exists(QStringLiteral("/var/run/reboot-required"));
            emit installSummaryReady(upgraded, notUpgraded, restartRequired);
        }

        emit installFinished(ok, err);
        if (m_currentProcess == proc)
            m_currentProcess = nullptr;
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        emit installFinished(false, proc->errorString());
        if (m_currentProcess == proc)
            m_currentProcess = nullptr;
    });

    proc->start();
}
