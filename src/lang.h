#pragma once

#include <QMap>
#include <QString>
#include <QObject>

// Ngôn ngữ hỗ trợ / Supported languages
enum class Language {
    EN,
    VI
};

// Lớp quản lý ngôn ngữ đơn giản (singleton), không phụ thuộc Qt Linguist
// Simple singleton translation manager, no Qt Linguist tooling required.
class Lang : public QObject
{
    Q_OBJECT

public:
    static Lang& instance();

    void setLanguage(Language lang);
    Language language() const;

    // Lấy chuỗi theo khóa / Get translated string by key
    QString t(const QString& key) const;

signals:
    void languageChanged();

private:
    Lang();
    void loadStrings();

    Language m_lang = Language::EN;
    QMap<QString, QMap<QString, QString>> m_strings; // key -> {en, vi}
};

// Macro tiện lợi / convenience macro
#define TR(key) Lang::instance().t(QStringLiteral(key))
