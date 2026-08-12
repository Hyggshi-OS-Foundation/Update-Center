#pragma once

#include <QObject>
#include <QVector>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// A structured, typed description of an apt repository. This is the ONLY thing
// the GUI may pass to add a repository — never a raw shell command. Trust is
// delegated to APT + GPG: the signing key is downloaded over HTTPS and verified
// against an expected fingerprint BEFORE it is installed and the repo enabled.
struct RepoInfo
{
    QString name;           // display name, e.g. "Hyggshi OS Foundation"
    QString id;             // safe slug (^[a-z0-9][a-z0-9-]*$) used for filenames
    QString url;            // apt base URL, HTTPS only
    QString suite;          // Deb-822 "Suites:" (e.g. "stable")
    QStringList components; // Deb-822 "Components:" (e.g. {"main"})
    QString keyUrl;         // HTTPS URL to fetch the armored GPG key from
    QString fingerprint;    // expected 40-hex fingerprint, spaces ignored
    QString keyringPath;    // destination keyring (/usr/share/keyrings/<id>.gpg)
    bool official = false;  // curated repository shipped & trusted by the app

    bool isValid(bool requireFingerprint = true) const;
    QString componentsString() const { return components.join(' '); }
    QString sourceFileName() const { return id + QStringLiteral(".sources"); }
};

// Adds apt repositories. In contrast to the old code (which ran an arbitrary
// user shell command as root), this class:
//   1. downloads the GPG key as the normal user and extracts its real
//      fingerprint, which is emitted (preview) so the UI can show it to the
//      user for visual confirmation against what they typed,
//   2. on confirmAdd(), installs the keyring + a .sources file and runs
//      `apt update` via a FIXED helper invoked through pkexec, with values
//      passed as argv entries (never interpolated into a shell), so there is
//      no code/script injection.
class RepositoryManager : public QObject
{
    Q_OBJECT

public:
    explicit RepositoryManager(QObject* parent = nullptr);

    // Curated repositories (the official Hyggshi OS repo first).
    static const QVector<RepoInfo>& knownRepositories();
    static const RepoInfo& officialRepository();

    // Read-only probes (no root needed): has this repo's keyring and .sources
    // file already been installed on this system?
    static bool isEnabled(const RepoInfo& r);

    // Pretty-print a fingerprint as 4-hex-char groups, e.g. "ABCD EF01 ...".
    static QString formatFingerprint(const QString& fp);

    // Async add. Fetches + extracts the key, then emits keyFetched() with the
    // real fingerprint. The UI shows a preview and calls confirmAdd() (or
    // cancel()) based on the user's visual match.
    void addRepository(const RepoInfo& repo);

    // Called after the user confirms the previewed fingerprint matches. Runs
    // the (root) install + apt update. No-op unless a key was fetched.
    void confirmAdd();
    void cancel();

signals:
    void statusChanged(const QString& message);
    void keyFetched(const QString& fetchedFingerprint);
    void addFinished(bool ok, const QString& errorMessage);

private:
    void validateAndFetchKey();
    void onKeyFetched(QNetworkReply* reply);
    void previewKey();      // synchronous: de-armor + extract fingerprint, emit
    void runRootInstall();  // pkexec the FIXED helper (argv, no shell)
    void runAptUpdate();
    void cleanupTemp();

    // Absolute path of the fixed root helper (must ship with the package and
    // be installed to this location). See repo-helper template in the .cpp.
    static QString helperPath();

    RepoInfo m_repo;
    QNetworkAccessManager* m_net = nullptr;
    QProcess* m_proc = nullptr;
    QString m_tempArmored;       // downloaded armored key (temp file)
    QString m_tempKeyring;       // de-armored keyring (temp file)
    QString m_fetchedFingerprint;// real fingerprint extracted from the key
    QString m_sourcesContent;    // Deb-822 .sources body to be installed by helper
};

// Normalize a fingerprint: strip spaces/colons and uppercase.
QString normalizeFingerprint(const QString& fp);
