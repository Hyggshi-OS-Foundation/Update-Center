#pragma once

#include <QMainWindow>
#include <QVector>
#include <QPair>
#include <QIcon>
#include "updateitem.h"
#include "historymanager.h"
#include "repositorymanager.h"

class AptManager;
class RepositoryManager;
class QStackedWidget;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onNavChanged(int row);
    void onCheckUpdatesClicked();
    void onSelectAllToggled();
    void onInstallSelectedClicked();
    void onLanguageChanged(int index);
    void onDarkModeToggled(bool checked);
    void onAddRepoClicked();
    void retranslateUi();

    // AptManager callbacks
    void onRefreshOutput(const QString& line);
    void onRefreshFinished(bool success, const QString& errorMessage);
    void onListFinished(const QVector<UpdateItem>& items, const QString& errorMessage);
    void onInstallOutput(const QString& line);
    void onInstallSummaryReady(int upgraded, int notUpgraded, bool restartRequired);
    void onInstallFinished(bool success, const QString& errorMessage);
    void onHistoryLoaded(const QVector<HistoryTransaction>& transactions);
    void onRepoStatus(const QString& message);
    void onRepoKeyFetched(const QString& fetchedFingerprint);
    void onRepoAddFinished(bool ok, const QString& errorMessage);

private:
    // ---- page builders ----
    QWidget* buildSidebar();
    QWidget* buildOverviewPage();
    QWidget* buildUpdatesPage();
    QWidget* buildHistoryPage();
    QWidget* buildSettingsPage();

    // Fade + slide a page in when the user switches sidebar tabs.
    void animatePageIn(QWidget* page);

    void refreshOverviewCard();
    void populateUpdatesTable();
    void populateHistoryTable();

    // ---- theme ----
    void applyTheme(bool dark);

    // ---- updates page helpers ----
    void updateSelectionSummary();
    QIcon iconForPackage(const QString& packageName) const;
    void showInstallCompleteDialog(int upgraded, int notUpgraded, bool restartRequired);

    // ---- repo sources (settings page) ----
    void loadRepoSources();
    void saveRepoSources();
    void populateRepoList();

    // ---- sidebar ----
    QListWidget* m_navList = nullptr;
    QStackedWidget* m_stack = nullptr;

    // ---- overview widgets ----
    QLabel* m_overviewHeading = nullptr;
    QLabel* m_overviewSubtitle = nullptr;
    QLabel* m_statusIconLabel = nullptr;
    QLabel* m_statusTextLabel = nullptr;
    QLabel* m_lastCheckedLabel = nullptr;
    QLabel* m_lastCheckedValue = nullptr;
    QLabel* m_installedVersionLabel = nullptr;
    QLabel* m_installedVersionValue = nullptr;
    QLabel* m_channelLabelOverview = nullptr;
    QLabel* m_channelValueOverview = nullptr;
    QLabel* m_pendingLabel = nullptr;
    QLabel* m_pendingValue = nullptr;
    QPushButton* m_checkUpdatesBtn = nullptr;
    QProgressBar* m_checkProgress = nullptr;
    QLabel* m_checkStatusLine = nullptr; // live output from apt-get update
    QString m_lastCheckedTime;

    // ---- updates page widgets ----
    QLabel* m_updatesHeading = nullptr;
    QLabel* m_updatesSubtitle = nullptr;
    QTableWidget* m_updatesTable = nullptr;
    QLabel* m_noUpdatesTitle = nullptr;
    QLabel* m_noUpdatesSubtitle = nullptr;
    QPushButton* m_selectAllBtn = nullptr;
    QPushButton* m_installSelectedBtn = nullptr;
    QProgressBar* m_installProgress = nullptr;
    QLabel* m_installStatusLine = nullptr; // live output from apt-get install
    QLabel* m_selectionSummaryLabel = nullptr; // "N updates selected · Download: X MB"
    bool m_selectAllState = true;
    bool m_installInProgress = false;
    int m_lastInstalledCount = 0;

    // ---- pending install summary (filled in just before onInstallFinished) ----
    int m_pendingUpgraded = 0;
    int m_pendingNotUpgraded = 0;
    bool m_pendingRestartRequired = false;

    // ---- history page widgets ----
    QLabel* m_historyHeading = nullptr;
    QLabel* m_historySubtitle = nullptr;
    QTreeWidget* m_historyTree = nullptr;
    QLabel* m_historyEmptyLabel = nullptr;

    // ---- settings page widgets ----
    QLabel* m_settingsHeading = nullptr;
    QLabel* m_settingsSubtitle = nullptr;
    QLabel* m_settingsLanguageLabel = nullptr;
    QComboBox* m_languageCombo = nullptr;
    QCheckBox* m_autoCheckBox = nullptr;
    QCheckBox* m_notifyCheckBox = nullptr;
    QCheckBox* m_darkModeCheckBox = nullptr;
    QLabel* m_settingsChannelLabel = nullptr;
    QComboBox* m_channelCombo = nullptr;
    QLabel* m_settingsAboutLabel = nullptr;
    QLabel* m_settingsAboutText = nullptr;

    // ---- settings: repository sources ----
    QLabel* m_settingsRepoHeading = nullptr;
    QLabel* m_settingsRepoSubtitle = nullptr;
    QListWidget* m_repoListWidget = nullptr;
    QLabel* m_repoEmptyLabel = nullptr;
    QLabel* m_repoStatusLabel = nullptr; // live status while adding a repository
    QPushButton* m_addRepoBtn = nullptr;
    QVector<RepoInfo> m_repoSources; // user-added custom repositories (structured)
    RepoInfo m_pendingRepo;          // repo currently being added (RepositoryManager)

    // ---- app title (sidebar header) ----
    QLabel* m_appTitleLabel = nullptr;

    // ---- theme state ----
    bool m_darkMode = false;

    // ---- data ----
    QVector<UpdateItem> m_updates;
    QVector<HistoryTransaction> m_transactions;
    AptManager* m_apt = nullptr;
    HistoryManager* m_history = nullptr;
    RepositoryManager* m_repoMgr = nullptr;
};
