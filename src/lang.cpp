#include "lang.h"

Lang& Lang::instance()
{
    static Lang inst;
    return inst;
}

Lang::Lang()
{
    loadStrings();
}

void Lang::setLanguage(Language lang)
{
    if (m_lang == lang)
        return;
    m_lang = lang;
    emit languageChanged();
}

Language Lang::language() const
{
    return m_lang;
}

QString Lang::t(const QString& key) const
{
    const auto entry = m_strings.constFind(key);
    if (entry == m_strings.constEnd())
        return key; // fallback: show the key itself so missing strings are obvious

    const QString langKey = (m_lang == Language::EN) ? QStringLiteral("en") : QStringLiteral("vi");
    return entry->value(langKey, key);
}

void Lang::loadStrings()
{
    auto add = [this](const QString& key, const QString& en, const QString& vi) {
        m_strings[key] = { {"en", en}, {"vi", vi} };
    };

    // ---- App / Window ----
    add("app_title",           "Update Center",                       "Trung tâm Cập nhật");
    add("window_title",        "Update Center",                       "Trung tâm Cập nhật");

    // ---- Sidebar navigation ----
    add("nav_overview",        "Overview",                            "Tổng quan");
    add("nav_updates",         "Updates",                             "Cập nhật");
    add("nav_history",         "History",                             "Lịch sử");
    add("nav_settings",        "Settings",                            "Cài đặt");

    // ---- Overview page ----
    add("overview_heading",    "System Status",                       "Trạng thái hệ thống");
    add("overview_subtitle",   "Keep your system up to date and secure.", "Giữ hệ thống của bạn luôn cập nhật và an toàn.");
    add("status_up_to_date",   "Your system is up to date",           "Hệ thống của bạn đã được cập nhật");
    add("status_checking",     "Checking for updates…",               "Đang kiểm tra cập nhật…");
    add("status_updates_found","Updates are available",               "Có bản cập nhật mới");
    add("last_checked_label",  "Last checked:",                       "Kiểm tra lần cuối:");
    add("never",                "Never",                               "Chưa bao giờ");
    add("btn_check_updates",   "Check for Updates",                   "Kiểm tra cập nhật");
    add("btn_checking",        "Checking…",                           "Đang kiểm tra…");
    add("status_refreshing_index", "Updating package index…",         "Đang cập nhật danh sách gói…");
    add("status_listing",      "Checking for upgradable packages…",   "Đang kiểm tra các gói có thể nâng cấp…");
    add("err_title",           "Update Center",                       "Trung tâm Cập nhật");
    add("err_refresh_failed",  "Failed to update the package index:", "Cập nhật danh sách gói thất bại:");
    add("err_list_failed",     "Failed to list upgradable packages:", "Không thể liệt kê gói có thể nâng cấp:");
    add("err_install_failed",  "Installation failed:",                "Cài đặt thất bại:");
    add("confirm_install_title","Confirm Installation",               "Xác nhận cài đặt");
    add("confirm_install_text","This will run apt-get with administrator privileges to install the selected updates. Continue?",
                                "Thao tác này sẽ chạy apt-get với quyền quản trị để cài đặt các bản cập nhật đã chọn. Tiếp tục?");
    add("btn_yes",              "Continue",                            "Tiếp tục");
    add("btn_cancel",           "Cancel",                              "Huỷ");
    add("card_installed_label","Installed version",                   "Phiên bản đã cài");
    add("card_channel_label",  "Update channel",                      "Kênh cập nhật");
    add("card_pending_label",  "Pending updates",                     "Cập nhật đang chờ");

    // ---- Updates page ----
    add("updates_heading",     "Available Updates",                   "Các bản cập nhật khả dụng");
    add("updates_subtitle",    "Select the updates you want to install.", "Chọn các bản cập nhật bạn muốn cài đặt.");
    add("col_name",            "Name",                                "Tên");
    add("col_version",         "Version",                             "Phiên bản");
    add("col_size",            "Size",                                "Dung lượng");
    add("no_updates_title",    "No updates available",                "Không có bản cập nhật nào");
    add("no_updates_subtitle", "Click \"Check for Updates\" on the Overview page.", "Nhấn \"Kiểm tra cập nhật\" ở trang Tổng quan.");
    add("btn_select_all",      "Select All",                          "Chọn tất cả");
    add("btn_install_selected","Install Selected",                    "Cài đặt mục đã chọn");
    add("installing",          "Installing…",                         "Đang cài đặt…");
    add("install_done",        "Installation complete",               "Cài đặt hoàn tất");
    add("selection_summary",   "%1 updates selected",                 "%1 bản cập nhật đã chọn");
    add("selection_download",  "Download: %1",                        "Dung lượng tải: %1");

    // ---- Install-complete dialog ----
    add("install_complete_title",    "Update completed",              "Cập nhật hoàn tất");
    add("install_complete_processed","%1 packages processed",         "%1 gói đã được xử lý");
    add("install_complete_upgraded", "%1 upgraded",                   "%1 đã nâng cấp");
    add("install_complete_uptodate", "%1 already up to date",         "%1 đã ở phiên bản mới nhất");
    add("install_complete_restart_note", "Some updates require a restart.", "Một số bản cập nhật yêu cầu khởi động lại.");
    add("btn_restart_later",   "Restart Later",                       "Khởi động lại sau");
    add("btn_restart_now",     "Restart Now",                         "Khởi động lại ngay");
    add("btn_close",           "Close",                               "Đóng");
    add("restart_confirm_title","Restart Now?",                       "Khởi động lại ngay?");
    add("restart_confirm_text","This will restart your computer immediately. Save your work first.",
                                "Thao tác này sẽ khởi động lại máy tính ngay lập tức. Hãy lưu công việc trước.");

    // ---- History page ----
    add("history_heading",     "Update History",                      "Lịch sử cập nhật");
    add("history_subtitle",    "A record of previously installed updates.", "Danh sách các bản cập nhật đã cài trước đó.");
    add("col_date",            "Date",                                "Ngày");
    add("col_item",            "Item",                                "Mục");
    add("col_result",          "Result",                              "Kết quả");
    add("result_success",      "Success",                             "Thành công");
    add("result_failed",       "Failed",                              "Thất bại");
    add("history_system_update","System Update",                      "Cập nhật hệ thống");
    add("history_packages",    "packages",                            "gói");
    add("history_empty",       "No update history yet.",              "Chưa có lịch sử cập nhật.");

    // ---- Settings page ----
    add("settings_heading",    "Settings",                            "Cài đặt");
    add("settings_subtitle",   "Customize Update Center to your liking.", "Tùy chỉnh Trung tâm Cập nhật theo ý bạn.");
    add("settings_language",   "Language",                            "Ngôn ngữ");
    add("settings_autocheck",  "Automatically check for updates",     "Tự động kiểm tra cập nhật");
    add("settings_notify",     "Notify me when updates are available","Thông báo khi có bản cập nhật mới");
    add("settings_darkmode",   "Dark mode",                           "Chế độ tối");
    add("settings_channel",    "Update channel",                      "Kênh cập nhật");
    add("channel_stable",      "Stable",                              "Ổn định");
    add("channel_beta",        "Beta",                                "Thử nghiệm (Beta)");
    add("settings_about",      "About",                               "Giới thiệu");
    add("settings_about_text", "Update Center v1.0.0 — built with Qt 6.", "Trung tâm Cập nhật v1.0.0 — xây dựng bằng Qt 6.");

    // ---- Settings: repository sources ----
    add("settings_repo_heading", "Repository Sources",                "Nguồn kho lưu trữ");
    add("settings_repo_subtitle","Official and user-added apt repositories.", "Các kho apt chính thức và do người dùng thêm.");
    add("btn_add_repo",        "Add Repository",                      "Thêm nguồn kho");
    add("repo_empty",          "No additional repository sources yet.", "Chưa có nguồn kho lưu trữ bổ sung nào.");

    add("repo_badge_official", "Official",                            "Chính thức");
    add("repo_badge_gpg",      "GPG signed",                          "Ký GPG");
    add("repo_badge_enabled",  "Repository enabled",                  "Đã bật kho");
    add("repo_badge_disabled", "Not enabled",                         "Chưa bật");

    add("repo_dialog_title",   "Add Repository Source",               "Thêm nguồn kho lưu trữ");
    add("repo_dialog_name_label", "Name",                             "Tên");
    add("repo_dialog_url_label",  "Repository URL (apt)",             "Địa chỉ kho (apt)");
    add("repo_dialog_suite_label", "Suite",                           "Suite");
    add("repo_dialog_components_label", "Components",                 "Components");
    add("repo_dialog_key_url_label", "GPG key URL (https)",           "Địa chỉ khóa GPG (https)");
    add("repo_dialog_fingerprint_label", "GPG key fingerprint (40 hex)", "Vân tay khóa GPG (40 hex)");
    add("repo_dialog_hint",    "The GPG signing key is downloaded over HTTPS, "
                               "verified against the fingerprint above, then installed "
                               "and the repository enabled via APT. No shell command is run.",
                                "Khóa ký GPG được tải qua HTTPS, xác minh với vân tay ở trên, "
                                "rồi cài và bật kho qua APT. Không chạy lệnh shell nào.");
    add("repo_preview_title",  "Verify Signing Key",                  "Xác minh khóa ký");
    add("repo_preview_match",  "Fingerprints match",                 "Vân tay khớp");
    add("repo_preview_mismatch","Fingerprints do NOT match",         "Vân tay KHÔNG khớp");
    add("repo_preview_fetched","Fetched key fingerprint:",           "Vân tay khóa đã tải:");
    add("repo_preview_typed",  "Your fingerprint:",                  "Vân tay bạn nhập:");
    add("repo_preview_hint",   "Compare the two fingerprints carefully. "
                               "Only enable the repository if they match exactly.",
                               "So sánh kỹ hai vân tay. Chỉ bật kho nếu chúng khớp hoàn toàn.");
    add("btn_enable_repo",     "Enable Repository",                  "Bật nguồn kho");
    add("repo_added_ok",       "Repository added and enabled.",       "Đã thêm và bật nguồn kho.");
    add("err_repo_invalid",    "Invalid repository details. The URL must be https:// and "
                               "the fingerprint must be a 40-character GPG fingerprint.",
                                "Thông tin kho không hợp lệ. Địa chỉ phải là https:// và vân tay "
                                "phải là 40 ký tự GPG.");
    add("err_repo_failed",     "Failed to add the repository source.", "Không thể thêm nguồn kho lưu trữ.");
}
