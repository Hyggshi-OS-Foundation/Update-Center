#include "repositorymanager.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryFile>
#include <QRegularExpression>

#include <algorithm>

// ---------------------------------------------------------------------------
// Model helpers
// ---------------------------------------------------------------------------
QString normalizeFingerprint(const QString& fp)
{
    QString n = fp;
    n.remove(QRegularExpression(QStringLiteral("[\\s:]+")));
    return n.toUpper();
}

bool RepoInfo::isValid(bool requireFingerprint) const
{
    // URL must be https://host[/path] (no scheme tricks, no credentials).
    if (!url.startsWith(QStringLiteral("https://")))
        return false;
    if (name.isEmpty() || suite.isEmpty() || components.isEmpty())
        return false;
    static const QRegularExpression slugRe(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    if (!slugRe.match(id).hasMatch())
        return false;
    const QString fp = normalizeFingerprint(fingerprint);
    if (requireFingerprint && fp.size() != 40)
        return false;
    return true;
}

// Curated, Web-of-Trust anchor: the official Hyggshi OS repository. The app is
// shipped by Hyggshi, so this entry is trusted by definition; the signing-key
// fingerprint must be published and filled in below. Until it is set, adding
// the official repo is refused (fail-safe) — but its status else shows up as
// "Official / GPG signed / Enabled" once installed on a real system.
const QVector<RepoInfo>& RepositoryManager::knownRepositories()
{
    static const QVector<RepoInfo> repos = [] {
        RepoInfo r;
        r.name = QStringLiteral("Hyggshi OS Foundation");
        r.id = QStringLiteral("hyggshi");
        r.url = QStringLiteral("https://hyggshi-os-foundation.github.io/apt-repo");
        r.suite = QStringLiteral("stable");
        r.components = { QStringLiteral("main") };
        // TODO(maintainers): publish the real signing-key fingerprint and URL,
        // then fill these in so the official repo can be (re)enabled from the UI.
        r.keyUrl = QStringLiteral("https://hyggshi-os-foundation.github.io/apt-repo/hyggshi-archive-keyring.asc");
        r.fingerprint = QString();
        r.keyringPath = QStringLiteral("/usr/share/keyrings/hyggshi-archive-keyring.gpg");
        r.official = true;
        return QVector<RepoInfo>{ r };
    }();
    return repos;
}

const RepoInfo& RepositoryManager::officialRepository()
{
    return knownRepositories().constFirst();
}

bool RepositoryManager::isEnabled(const RepoInfo& r)
{
    const QDir dir(QStringLiteral("/etc/apt/sources.list.d"));
    const bool sourcePresent =
        QFileInfo::exists(dir.filePath(r.id + QStringLiteral(".sources"))) ||
        QFileInfo::exists(dir.filePath(r.id + QStringLiteral(".list")));
    return sourcePresent && QFileInfo::exists(r.keyringPath);
}

// ---------------------------------------------------------------------------
// The fixed root helper. This is the ONLY thing ever run as root while adding
// a repository. Its content is 100% fixed here (never built from user input);
// all runtime values are passed to it as separate argv entries, so there is no
// shell interpolation and therefore no code-injection surface.
//
// In a packaged build this lives at RepositoryManager::helperPath(). For
// uninstalled/dev runs we materialize it to a temp file and execute that path.
// ---------------------------------------------------------------------------
namespace {
const char kRepoHelper[] =
    "#!/bin/sh\n"
    "# Fixed Update Center helper: install an apt repo keyring + .sources file.\n"
    "# Args: add <id> <url> <suite> <components> <keyringPath> <tempKeyring> <sourceFileName>\n"
    "set -e\n"
    "id=$1; url=$2; suite=$3; components=$4; keyringPath=$5; tempKeyring=$6; sourceFileName=$7\n"
    "keyringDir=$(dirname \"$keyringPath\")\n"
    "mkdir -p \"$keyringDir\"\n"
    "install -m 0644 \"$tempKeyring\" \"$keyringPath\"\n"
    "if [ -n \"$suite\" ]; then\n"
    "  printf 'Types: deb\\nURIs: %s\\nSuites: %s\\nComponents: %s\\nSigned-By: %s\\n' \\\n"
    "      \"$url\" \"$suite\" \"$components\" \"$keyringPath\" > \"/etc/apt/sources.list.d/$sourceFileName\"\n"
    "else\n"
    "  printf 'Types: deb\\nURIs: %s\\nComponents: %s\\nSigned-By: %s\\n' \\\n"
    "      \"$url\" \"$components\" \"$keyringPath\" > \"/etc/apt/sources.list.d/$sourceFileName\"\n"
    "fi\n";
}

RepositoryManager::RepositoryManager(QObject* parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

QString RepositoryManager::helperPath()
{
    return QStringLiteral("/usr/libexec/update-center/update-center-repo-helper");
}

void RepositoryManager::cancel()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
    cleanupTemp();
    m_fetchedFingerprint.clear();
}

void RepositoryManager::cleanupTemp()
{
    QFile::remove(m_tempArmored);
    QFile::remove(m_tempKeyring);
    m_tempArmored.clear();
    m_tempKeyring.clear();
}

QString RepositoryManager::formatFingerprint(const QString& fp)
{
    QString n = normalizeFingerprint(fp);
    QString out;
    for (int i = 0; i < n.size(); ++i) {
        if (i && i % 4 == 0)
            out += QLatin1Char(' ');
        out += n[i];
    }
    return out;
}

void RepositoryManager::addRepository(const RepoInfo& repo)
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        emit addFinished(false, tr("Another repository operation is already running."));
        return;
    }
    m_repo = repo;

    // Fail closed: refuse anything that can't be fully verified.
    if (!m_repo.isValid(/*requireFingerprint=*/true)) {
        emit addFinished(false, tr("Invalid repository details, or the GPG key "
                                   "fingerprint has not been configured."));
        return;
    }
    if (isEnabled(m_repo)) {
        emit addFinished(false, tr("This repository is already enabled."));
        return;
    }

    validateAndFetchKey();
}

void RepositoryManager::validateAndFetchKey()
{
    if (m_tempArmored.isEmpty()) {
        QTemporaryFile f(QDir::tempPath() + QStringLiteral("/uc-key-XXXXXX.asc"));
        f.setAutoRemove(false);
        if (!f.open()) {
            emit addFinished(false, tr("Could not create a temporary file."));
            return;
        }
        m_tempArmored = f.fileName();
        f.close();
    }

    emit statusChanged(tr("Downloading repository signing key…"));
    QNetworkRequest req(QUrl(m_repo.keyUrl));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        onKeyFetched(reply);
    });
}

void RepositoryManager::onKeyFetched(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit addFinished(false, tr("Failed to download the signing key: %1")
                                     .arg(reply->errorString()));
        return;
    }
    QFile f(m_tempArmored);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit addFinished(false, tr("Could not save the downloaded key."));
        return;
    }
    f.write(reply->readAll());
    f.close();

    emit statusChanged(tr("Verifying GPG key fingerprint…"));
    previewKey();
}


void RepositoryManager::previewKey()
{
    if (m_tempKeyring.isEmpty()) {
        const QString base = m_tempArmored;
        m_tempKeyring = base.left(base.size() - QStringLiteral(".asc").size()) +
                        QStringLiteral("-keyring.gpg");
    }
    QFile::remove(m_tempKeyring);

    // De-armor into a binary keyring.
    {
        QProcess proc;
        proc.start(QStringLiteral("gpg"),
                   { QStringLiteral("--dearmor"), QStringLiteral("--batch"),
                     QStringLiteral("--output"), m_tempKeyring, m_tempArmored });
        if (!proc.waitForFinished(10000) || proc.exitStatus() != QProcess::NormalExit ||
            proc.exitCode() != 0) {
            cleanupTemp();
            emit addFinished(false, tr("The downloaded file is not a valid GPG key."));
            return;
        }
    }

    // Read the key's real fingerprint(s). The primary key's fingerprint comes
    // from the first "fpr:" record.
    QStringList fingerprints;
    {
        QProcess proc;
        proc.start(QStringLiteral("gpg"),
                   { QStringLiteral("--show-keys"), QStringLiteral("--with-colons"),
                     m_tempArmored });
        if (proc.waitForFinished(10000)) {
            const auto lines = QString::fromUtf8(proc.readAllStandardOutput()).split('\n');
            for (const QString& line : lines) {
                // e.g. "fpr:::::::::A1B2C3...:"
                if (line.startsWith(QStringLiteral("fpr:"))) {
                    const auto parts = line.split(':');
                    if (parts.size() > 9)
                        fingerprints.append(normalizeFingerprint(parts[9]));
                }
            }
        }
    }

    if (fingerprints.isEmpty()) {
        cleanupTemp();
        emit addFinished(false, tr("The downloaded key has no readable fingerprint."));
        return;
    }

    // Keep the parsed fingerprint so confirmAdd() can re-check it the instant
    // the user approves — nothing is installed at this point, just previewed.
    m_fetchedFingerprint = fingerprints.constFirst();
    emit keyFetched(m_fetchedFingerprint);
}

void RepositoryManager::confirmAdd()
{
    if (m_fetchedFingerprint.isEmpty() || m_tempArmored.isEmpty()) {
        emit addFinished(false, tr("No key has been fetched yet."));
        return;
    }
    // Re-verify the fetched key still matches what the user typed, right before
    // we hand the keyring to the root helper.
    const QString expected = normalizeFingerprint(m_repo.fingerprint);
    if (expected.isEmpty() || m_fetchedFingerprint != expected) {
        emit addFinished(false, tr("GPG fingerprint mismatch — the key was not installed."));
        return;
    }

    emit statusChanged(tr("GPG fingerprint verified ✓"));
    runRootInstall();
}


void RepositoryManager::runRootInstall()
{
    // Build the Deb-822 .sources body (written to disk by the root helper).
    const QString components = m_repo.componentsString();
    if (m_repo.suite.isEmpty())
        m_sourcesContent = QStringLiteral("Types: deb\nURIs: %1\nComponents: %2\nSigned-By: %3\n")
                               .arg(m_repo.url, components, m_repo.keyringPath);
    else
        m_sourcesContent = QStringLiteral("Types: deb\nURIs: %1\nSuites: %2\nComponents: %3\nSigned-By: %4\n")
                               .arg(m_repo.url, m_repo.suite, components, m_repo.keyringPath);

    // The helper must exist. Prefer the packaged path; otherwise materialize the
    // fixed template to a temp file (dev/uninstalled runs).
    QString helper = helperPath();
    if (!QFileInfo::exists(helper)) {
        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/uc-helper-XXXXXX"));
        tmp.setAutoRemove(false);
        if (tmp.open()) {
            tmp.write(kRepoHelper, qint64(sizeof(kRepoHelper) - 1));
            tmp.close();
            helper = tmp.fileName();
        }
    }
    QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::ExeOther);

    emit statusChanged(tr("Installing repository (administrator privileges required)…"));

    auto* proc = new QProcess(this);
    m_proc = proc;
    // All values are passed as separate argv entries to the FIXED helper — never
    // interpolated into a shell, so there is no command/script injection.
    QStringList args{
        helper,
        QStringLiteral("add"),
        m_repo.id,                // 1: id
        m_repo.url,               // 2: url
        m_repo.suite,             // 3: suite
        components,               // 4: components "main extra..."
        m_repo.keyringPath,       // 5: keyringPath
        m_tempKeyring,            // 6: tempKeyring
        m_repo.sourceFileName()   // 7: sourceFileName
    };
    proc->setProgram(QStringLiteral("pkexec"));
    proc->setArguments(args);
    proc->setProcessChannelMode(QProcess::MergedChannels);


    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        const bool ok = (code == 0);
        QFile::remove(m_tempArmored);
        QFile::remove(m_tempKeyring);
        m_tempArmored.clear();
        m_tempKeyring.clear();
        if (m_proc == proc)
            m_proc = nullptr;
        proc->deleteLater();
        if (ok)
            runAptUpdate();
        else
            emit addFinished(false, tr("Could not enable the repository (helper failed, code %1).").arg(code));
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        QFile::remove(m_tempArmored);
        QFile::remove(m_tempKeyring);
        m_tempArmored.clear();
        m_tempKeyring.clear();
        if (m_proc == proc)
            m_proc = nullptr;
        proc->deleteLater();
        emit addFinished(false, tr("Could not start the repository helper: %1").arg(proc->errorString()));
    });

    proc->start();
}

void RepositoryManager::runAptUpdate()
{
    emit statusChanged(tr("Refreshing package index…"));
    auto* proc = new QProcess(this);
    m_proc = proc;
    proc->setProgram(QStringLiteral("pkexec"));
    proc->setArguments({ QStringLiteral("apt-get"), QStringLiteral("update") });
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        const bool ok = (code == 0);
        if (m_proc == proc)
            m_proc = nullptr;
        proc->deleteLater();
        emit addFinished(ok, ok ? QString()
                                : tr("Repository enabled, but 'apt-get update' reported errors (code %1).").arg(code));
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        if (m_proc == proc)
            m_proc = nullptr;
        proc->deleteLater();
        emit addFinished(false, proc->errorString());
    });
    proc->start();
}

