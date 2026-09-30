#include <QApplication>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include "mainwindow.h"

// ---------------------------------------------------------------------------
// Read the value of a key from /etc/os-release (e.g. "ID" -> "hyggshios").
// Lines are in the form KEY=value or KEY="value".
// ---------------------------------------------------------------------------
static QString readOsReleaseField(const QString& key)
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return {};
    QTextStream in(&f);
    const QString prefix = key + QLatin1Char('=');
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(prefix)) {
            QString value = line.mid(prefix.size());
            // Strip surrounding quotes if present.
            if ((value.startsWith('"') && value.endsWith('"')) ||
                (value.startsWith('\'') && value.endsWith('\''))) {
                value = value.mid(1, value.size() - 2);
            }
            return value;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Install the Hyggshi OS OTA client (runs the upstream install script through
// pkexec so polkit handles the privilege elevation).
// ---------------------------------------------------------------------------
static void installOtaClient()
{
    // We use pkexec bash -c "..." so that the curl | bash pipeline runs as
    // root while the GUI stays unprivileged.
    const QString script =
        QStringLiteral("curl -fsSL "
            "https://raw.githubusercontent.com/"
            "Hyggshi-OS-Research-Technology/Hyggshi-OS-Releases/"
            "main/hyggshi-os-ota/client/install.sh "
            "| bash");

    QProcess proc;
    proc.start(QStringLiteral("pkexec"),
               { QStringLiteral("bash"), QStringLiteral("-c"), script });
    proc.waitForFinished(-1);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Update Center");
    app.setOrganizationName("Hyggshi OS");
    app.setWindowIcon(QIcon(":/resources/updatecenter.svg"));

    // -----------------------------------------------------------------------
    // OS compatibility check — Update Center is designed exclusively for
    // Hyggshi OS.  Reject any other distribution before the main window
    // is even created so the user gets a clear, actionable message.
    // -----------------------------------------------------------------------
    const QString osId = readOsReleaseField(QStringLiteral("ID")).toLower();
    if (osId != QLatin1String("hyggshios")) {
        const QString detected = osId.isEmpty() ? QStringLiteral("unknown") : osId;
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Hyggshi OS Update Center"),
            QStringLiteral(
                "This application is designed exclusively for Hyggshi OS.\n\n"
                "Detected OS ID: \"") + detected + QStringLiteral("\"") +
            QStringLiteral("\n\nUpdate Center cannot run on this distribution."));
        return 1;
    }

    // -----------------------------------------------------------------------
    // OTA client bootstrap — if the Hyggshi OTA client binary is not yet
    // present on this machine, install it automatically before opening the
    // main window.
    // -----------------------------------------------------------------------
    if (!QFile::exists(QStringLiteral("/usr/bin/hyggshi-ota")) &&
        !QFile::exists(QStringLiteral("/usr/local/bin/hyggshi-ota"))) {
        QMessageBox infoBox;
        infoBox.setWindowTitle(QStringLiteral("Hyggshi OS Update Center"));
        infoBox.setIcon(QMessageBox::Information);
        infoBox.setText(QStringLiteral(
            "The Hyggshi OS OTA client is not installed on this system.\n"
            "It will be installed now. You may be asked to enter your password."));
        infoBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        infoBox.setDefaultButton(QMessageBox::Ok);
        if (infoBox.exec() == QMessageBox::Ok) {
            installOtaClient();
        }
    }

    MainWindow window;
    window.setWindowIcon(QIcon(":/resources/updatecenter.svg"));
    window.show();

    return app.exec();
}
