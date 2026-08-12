#pragma once

#include <QString>

// Mô tả một mục cập nhật (dữ liệu mẫu — hãy thay bằng nguồn dữ liệu thật của bạn)
// Describes a single update entry (mock data — wire this up to your real update source).
struct UpdateItem
{
    QString name;
    QString currentVersion;
    QString newVersion;
    qint64  sizeBytes = 0;
    bool    selected = true;

    QString sizeString() const
    {
        const double mb = sizeBytes / (1024.0 * 1024.0);
        return QString::number(mb, 'f', 1) + " MB";
    }
};
