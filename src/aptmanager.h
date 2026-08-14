#pragma once

#include <QObject>
#include <QVector>
#include <QStringList>
#include "updateitem.h"

class QProcess;

// Bọc các lệnh apt thật (apt-get update, apt list --upgradable,
// apt-get install --only-upgrade) qua QProcess + pkexec để lấy quyền root
// bằng hộp thoại polkit đồ hoạ, thay vì chạy cả ứng dụng bằng sudo.
//
// Wraps real apt commands (apt-get update, apt list --upgradable,
// apt-get install --only-upgrade) via QProcess + pkexec, so the app
// requests root only for the commands that need it, using the graphical
// polkit prompt instead of running the whole app as root.
//
// Requirements on the target machine: a Debian/Ubuntu-based system with
// apt, apt-cache, and pkexec (policykit-1) installed, and a polkit
// authentication agent running in the desktop session (present by default
// on GNOME/KDE/XFCE etc.).
class AptManager : public QObject
{
    Q_OBJECT

public:
    explicit AptManager(QObject* parent = nullptr);

    // Bước 1: làm mới danh sách gói (cần quyền root)
    // Step 1: refresh the package index (requires root)
    void refreshIndex();

    // Bước 2: liệt kê các gói có thể nâng cấp (không cần root)
    // Step 2: list upgradable packages (no root required)
    void listUpgradable();

    // Bước 3: cài đặt các gói đã chọn (cần quyền root)
    // Step 3: install the selected packages (requires root)
    void installPackages(const QStringList& packageNames);

    // Huỷ tiến trình cài đặt/kiểm tra đang chạy, nếu có
    // Cancel any in-flight refresh/install process
    void cancelCurrent();

signals:
    void refreshOutput(const QString& line);
    void refreshFinished(bool success, const QString& errorMessage);

    void listFinished(const QVector<UpdateItem>& items, const QString& errorMessage);

    void installOutput(const QString& line);
    // Parsed from apt-get's own "X upgraded, Y newly installed, Z to remove
    // and W not upgraded." summary line, plus a check of
    // /var/run/reboot-required. Emitted right before installFinished so the
    // UI can show a proper install-complete summary dialog.
    void installSummaryReady(int upgraded, int notUpgraded, bool restartRequired);
    void installFinished(bool success, const QString& errorMessage);

private:
    qint64 fetchDownloadSize(const QString& packageName) const;

    QProcess* m_currentProcess = nullptr;
    QString m_refreshOutputBuffer;
    QString m_installOutputBuffer;
};
