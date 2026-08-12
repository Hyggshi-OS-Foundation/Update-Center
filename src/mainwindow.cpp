#include "mainwindow.h"
#include "lang.h"
#include "aptmanager.h"
#include "historymanager.h"
#include "repositorymanager.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QFrame>
#include <QDateTime>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QSpacerItem>
#include <QMessageBox>
#include <QSettings>
#include <QDialog>
#include <QLineEdit>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QPainter>
#include <QPixmap>
#include <QFont>
#include <QFontMetrics>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <algorithm>

namespace {
QFrame* makeCard(QWidget* parent = nullptr)
{
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 40));
    card->setGraphicsEffect(shadow);
    return card;
}

// Human-readable "X.X MB" formatting shared by the selection summary and
// (indirectly, via UpdateItem::sizeString()) the updates table.
QString humanSize(qint64 bytes)
{
    const double mb = bytes / (1024.0 * 1024.0);
    return QString::number(mb, 'f', 1) + " MB";
}

// Last-resort icon: a small rounded avatar with the package's first letter,
// colored deterministically from its name. Used when neither the system
// icon theme nor our alias table has anything for a package — keeps the
// updates list from ever looking "broken" on minimal/server installs.
QIcon generateFallbackIcon(const QString& name)
{
    static const QVector<QColor> palette = {
        QColor("#3E7BFA"), QColor("#1E9E5A"), QColor("#C97A1D"),
        QColor("#B24BF3"), QColor("#E0527A"), QColor("#22A6B3"),
        QColor("#6C63FF"), QColor("#F2545B"),
    };
    const uint h = qHash(name);
    const QColor bg = palette[h % static_cast<uint>(palette.size())];

    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, 32, 32);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(13);
    p.setFont(f);
    const QString letter = name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
    p.drawText(QRect(0, 0, 32, 32), Qt::AlignCenter, letter);
    p.end();
    return QIcon(pix);
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    resize(1080, 680);
    setMinimumSize(860, 560);

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(buildSidebar());

    m_stack = new QStackedWidget(central);
    m_stack->addWidget(buildOverviewPage());
    m_stack->addWidget(buildUpdatesPage());
    m_stack->addWidget(buildHistoryPage());
    m_stack->addWidget(buildSettingsPage());
    rootLayout->addWidget(m_stack, 1);

    setCentralWidget(central);

    // Theme: restore the saved preference (defaults to light) now that the
    // dark-mode checkbox exists, then load the matching stylesheet.
    QSettings settings;
    m_darkMode = settings.value("darkMode", false).toBool();
    m_darkModeCheckBox->blockSignals(true);
    m_darkModeCheckBox->setChecked(m_darkMode);
    m_darkModeCheckBox->blockSignals(false);
    applyTheme(m_darkMode);

    m_apt = new AptManager(this);
    connect(m_apt, &AptManager::refreshOutput, this, &MainWindow::onRefreshOutput);
    connect(m_apt, &AptManager::refreshFinished, this, &MainWindow::onRefreshFinished);
    connect(m_apt, &AptManager::listFinished, this, &MainWindow::onListFinished);
    connect(m_apt, &AptManager::installOutput, this, &MainWindow::onInstallOutput);
    connect(m_apt, &AptManager::installSummaryReady, this, &MainWindow::onInstallSummaryReady);
    connect(m_apt, &AptManager::installFinished, this, &MainWindow::onInstallFinished);

    // History is loaded from the real apt/dpkg logs (never fabricated in the
    // GUI) via HistoryManager; refresh() parses them and emits historyLoaded().
    m_history = new HistoryManager(this);
    connect(m_history, &HistoryManager::historyLoaded, this, &MainWindow::onHistoryLoaded);
    m_history->refresh();

    // Adding repositories is done through a structured, GPG-verified flow
    // (RepositoryManager) — never by running an arbitrary shell command as root.
    m_repoMgr = new RepositoryManager(this);
    connect(m_repoMgr, &RepositoryManager::statusChanged, this, &MainWindow::onRepoStatus);
    connect(m_repoMgr, &RepositoryManager::keyFetched, this, &MainWindow::onRepoKeyFetched);
    connect(m_repoMgr, &RepositoryManager::addFinished, this, &MainWindow::onRepoAddFinished);

    connect(&Lang::instance(), &Lang::languageChanged, this, &MainWindow::retranslateUi);

    loadRepoSources();
    populateRepoList();

    retranslateUi();
    m_navList->setCurrentRow(0);
    refreshOverviewCard();
    populateUpdatesTable();
    populateHistoryTable();
}

// ============================================================
// Theme
// ============================================================
void MainWindow::applyTheme(bool dark)
{
    m_darkMode = dark;
    QFile styleFile(dark ? ":/resources/style_dark.qss" : ":/resources/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    }
}

void MainWindow::onDarkModeToggled(bool checked)
{
    applyTheme(checked);
    QSettings settings;
    settings.setValue("darkMode", checked);
}

// ============================================================
// Sidebar
// ============================================================
QWidget* MainWindow::buildSidebar()
{
    auto* sidebar = new QWidget();
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(220);

    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget();
    header->setObjectName("sidebarHeader");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 22, 20, 22);
    auto* logo = new QLabel("\u21BB"); // update / refresh glyph
    logo->setObjectName("sidebarLogo");
    m_appTitleLabel = new QLabel();
    m_appTitleLabel->setObjectName("sidebarTitle");
    headerLayout->addWidget(logo);
    headerLayout->addWidget(m_appTitleLabel);
    headerLayout->addStretch();
    layout->addWidget(header);

    m_navList = new QListWidget();
    m_navList->setObjectName("navList");
    m_navList->setFrameShape(QFrame::NoFrame);
    m_navList->setFocusPolicy(Qt::NoFocus);
    m_navList->setUniformItemSizes(true);

    const QStringList icons = { "\u2302", "\u2B07", "\u25F7", "\u2699" }; // house, down-arrow, history, gear
    for (int i = 0; i < 4; ++i) {
        auto* item = new QListWidgetItem(m_navList);
        item->setSizeHint(QSize(0, 46));
        item->setData(Qt::UserRole, icons[i]);
    }
    layout->addWidget(m_navList, 1);

    connect(m_navList, &QListWidget::currentRowChanged, this, &MainWindow::onNavChanged);

    return sidebar;
}

void MainWindow::onNavChanged(int row)
{
    if (row < 0)
        return;
    m_stack->setCurrentIndex(row);
    animatePageIn(m_stack->currentWidget());
}

void MainWindow::animatePageIn(QWidget* page)
{
    if (!page)
        return;
    // Fade the page in from transparent while it slides up a few pixels. The
    // QGraphicsOpacityEffect is removed (and deleted) automatically when we
    // reset it to nullptr on completion — QWidget owns the effect.
    auto* effect = new QGraphicsOpacityEffect(page);
    page->setGraphicsEffect(effect);
    effect->setOpacity(0.0);

    auto* group = new QParallelAnimationGroup(page);

    auto* fade = new QPropertyAnimation(effect, "opacity", group);
    fade->setDuration(220);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(fade);

    auto* slide = new QPropertyAnimation(page, "pos", group);
    slide->setDuration(220);
    slide->setStartValue(QPoint(0, 14));
    slide->setEndValue(QPoint(0, 0));
    slide->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(slide);

    connect(group, &QParallelAnimationGroup::finished, page, [page]() {
        page->setGraphicsEffect(nullptr); // removes & deletes the fade effect
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================
// Overview page
// ============================================================
QWidget* MainWindow::buildOverviewPage()
{
    auto* page = new QWidget();
    page->setObjectName("pageOverview");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(36, 32, 36, 32);
    layout->setSpacing(18);

    m_overviewHeading = new QLabel();
    m_overviewHeading->setObjectName("pageHeading");
    m_overviewSubtitle = new QLabel();
    m_overviewSubtitle->setObjectName("pageSubtitle");
    layout->addWidget(m_overviewHeading);
    layout->addWidget(m_overviewSubtitle);

    // Status card
    auto* statusCard = makeCard();
    auto* statusLayout = new QHBoxLayout(statusCard);
    statusLayout->setContentsMargins(24, 24, 24, 24);
    statusLayout->setSpacing(18);

    m_statusIconLabel = new QLabel("\u2713");
    m_statusIconLabel->setObjectName("statusIcon");
    m_statusIconLabel->setFixedSize(56, 56);
    m_statusIconLabel->setAlignment(Qt::AlignCenter);

    auto* statusTextLayout = new QVBoxLayout();
    statusTextLayout->setSpacing(4);
    m_statusTextLabel = new QLabel();
    m_statusTextLabel->setObjectName("statusText");
    auto* lastCheckedRow = new QHBoxLayout();
    m_lastCheckedLabel = new QLabel();
    m_lastCheckedLabel->setObjectName("mutedLabel");
    m_lastCheckedValue = new QLabel();
    m_lastCheckedValue->setObjectName("mutedLabel");
    lastCheckedRow->addWidget(m_lastCheckedLabel);
    lastCheckedRow->addWidget(m_lastCheckedValue);
    lastCheckedRow->addStretch();
    statusTextLayout->addWidget(m_statusTextLabel);
    statusTextLayout->addLayout(lastCheckedRow);

    m_checkProgress = new QProgressBar();
    m_checkProgress->setObjectName("checkProgress");
    m_checkProgress->setRange(0, 0); // indeterminate — apt doesn't report a clean percentage
    m_checkProgress->setTextVisible(false);
    m_checkProgress->setFixedHeight(6);
    m_checkProgress->hide();
    statusTextLayout->addWidget(m_checkProgress);

    m_checkStatusLine = new QLabel();
    m_checkStatusLine->setObjectName("mutedLabel");
    m_checkStatusLine->hide();
    statusTextLayout->addWidget(m_checkStatusLine);

    m_checkUpdatesBtn = new QPushButton();
    m_checkUpdatesBtn->setObjectName("primaryButton");
    m_checkUpdatesBtn->setCursor(Qt::PointingHandCursor);
    m_checkUpdatesBtn->setFixedWidth(190);
    connect(m_checkUpdatesBtn, &QPushButton::clicked, this, &MainWindow::onCheckUpdatesClicked);

    statusLayout->addWidget(m_statusIconLabel);
    statusLayout->addLayout(statusTextLayout, 1);
    statusLayout->addWidget(m_checkUpdatesBtn, 0, Qt::AlignTop);

    layout->addWidget(statusCard);

    // Info grid card
    auto* infoCard = makeCard();
    auto* infoGrid = new QGridLayout(infoCard);
    infoGrid->setContentsMargins(24, 20, 24, 20);
    infoGrid->setHorizontalSpacing(40);
    infoGrid->setVerticalSpacing(6);

    m_installedVersionLabel = new QLabel();
    m_installedVersionLabel->setObjectName("mutedLabel");
    m_installedVersionValue = new QLabel("1.0.0");
    m_installedVersionValue->setObjectName("infoValue");

    m_channelLabelOverview = new QLabel();
    m_channelLabelOverview->setObjectName("mutedLabel");
    m_channelValueOverview = new QLabel();
    m_channelValueOverview->setObjectName("infoValue");

    m_pendingLabel = new QLabel();
    m_pendingLabel->setObjectName("mutedLabel");
    m_pendingValue = new QLabel("0");
    m_pendingValue->setObjectName("infoValue");

    infoGrid->addWidget(m_installedVersionLabel, 0, 0);
    infoGrid->addWidget(m_channelLabelOverview, 0, 1);
    infoGrid->addWidget(m_pendingLabel, 0, 2);
    infoGrid->addWidget(m_installedVersionValue, 1, 0);
    infoGrid->addWidget(m_channelValueOverview, 1, 1);
    infoGrid->addWidget(m_pendingValue, 1, 2);
    infoGrid->setColumnStretch(3, 1);

    layout->addWidget(infoCard);
    layout->addStretch();

    return page;
}

void MainWindow::onCheckUpdatesClicked()
{
    m_checkProgress->show();
    m_checkStatusLine->setText(TR("status_refreshing_index"));
    m_checkStatusLine->show();
    m_checkUpdatesBtn->setEnabled(false);
    m_checkUpdatesBtn->setText(TR("btn_checking"));
    m_statusTextLabel->setText(TR("status_checking"));
    m_statusIconLabel->setText("\u21BB");
    m_statusIconLabel->setProperty("state", "checking");
    style()->unpolish(m_statusIconLabel);
    style()->polish(m_statusIconLabel);

    // Step 1: refresh the apt package index (needs root — triggers a pkexec
    // polkit prompt). Step 2 (listUpgradable) runs once this finishes, see
    // onRefreshFinished().
    m_apt->refreshIndex();
}

void MainWindow::onRefreshOutput(const QString& line)
{
    if (!line.isEmpty())
        m_checkStatusLine->setText(line);
}

void MainWindow::onRefreshFinished(bool success, const QString& errorMessage)
{
    if (!success) {
        m_checkProgress->hide();
        m_checkStatusLine->hide();
        m_checkUpdatesBtn->setEnabled(true);
        refreshOverviewCard();
        QMessageBox::warning(this, TR("err_title"), TR("err_refresh_failed") + "\n\n" + errorMessage);
        return;
    }

    m_checkStatusLine->setText(TR("status_listing"));
    m_apt->listUpgradable();
}

void MainWindow::onListFinished(const QVector<UpdateItem>& items, const QString& errorMessage)
{
    m_checkProgress->hide();
    m_checkStatusLine->hide();
    m_checkUpdatesBtn->setEnabled(true);

    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(this, TR("err_title"), TR("err_list_failed") + "\n\n" + errorMessage);
    }

    m_updates = items;
    m_lastCheckedTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");

    populateUpdatesTable();
    refreshOverviewCard();
}

void MainWindow::refreshOverviewCard()
{
    m_checkUpdatesBtn->setText(TR("btn_check_updates"));

    const bool hasUpdates = !m_updates.isEmpty();
    m_statusIconLabel->setText(hasUpdates ? QString::fromUtf8("\u2191") : QString::fromUtf8("\u2713"));
    m_statusIconLabel->setProperty("state", hasUpdates ? "warn" : "ok");
    style()->unpolish(m_statusIconLabel);
    style()->polish(m_statusIconLabel);

    m_statusTextLabel->setText(hasUpdates ? TR("status_updates_found") : TR("status_up_to_date"));

    m_lastCheckedLabel->setText(TR("last_checked_label"));
    m_lastCheckedValue->setText(m_lastCheckedTime.isEmpty() ? TR("never") : m_lastCheckedTime);

    m_installedVersionLabel->setText(TR("card_installed_label"));
    m_channelLabelOverview->setText(TR("card_channel_label"));
    m_channelValueOverview->setText(TR("channel_stable"));
    m_pendingLabel->setText(TR("card_pending_label"));
    m_pendingValue->setText(QString::number(m_updates.size()));
}

// ============================================================
// Updates page
// ============================================================
QWidget* MainWindow::buildUpdatesPage()
{
    auto* page = new QWidget();
    page->setObjectName("pageUpdates");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(36, 32, 36, 32);
    layout->setSpacing(14);

    m_updatesHeading = new QLabel();
    m_updatesHeading->setObjectName("pageHeading");
    m_updatesSubtitle = new QLabel();
    m_updatesSubtitle->setObjectName("pageSubtitle");
    layout->addWidget(m_updatesHeading);
    layout->addWidget(m_updatesSubtitle);

    auto* card = makeCard();
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);
    cardLayout->setSpacing(0);

    m_updatesTable = new QTableWidget(0, 4);
    m_updatesTable->setObjectName("updatesTable");
    m_updatesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_updatesTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_updatesTable->verticalHeader()->setVisible(false);
    m_updatesTable->horizontalHeader()->setStretchLastSection(false);
    m_updatesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_updatesTable->setColumnWidth(0, 44);
    m_updatesTable->setColumnWidth(2, 110);
    m_updatesTable->setColumnWidth(3, 100);
    m_updatesTable->setShowGrid(false);
    m_updatesTable->setFrameShape(QFrame::NoFrame);
    m_updatesTable->setFocusPolicy(Qt::NoFocus);
    cardLayout->addWidget(m_updatesTable);

    auto* emptyWrap = new QWidget();
    auto* emptyLayout = new QVBoxLayout(emptyWrap);
    emptyLayout->setContentsMargins(0, 60, 0, 60);
    emptyLayout->setSpacing(6);
    m_noUpdatesTitle = new QLabel();
    m_noUpdatesTitle->setObjectName("emptyTitle");
    m_noUpdatesTitle->setAlignment(Qt::AlignCenter);
    m_noUpdatesSubtitle = new QLabel();
    m_noUpdatesSubtitle->setObjectName("mutedLabel");
    m_noUpdatesSubtitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_noUpdatesTitle);
    emptyLayout->addWidget(m_noUpdatesSubtitle);
    cardLayout->addWidget(emptyWrap);
    emptyWrap->setObjectName("emptyWrap");

    layout->addWidget(card, 1);

    auto* footer = new QHBoxLayout();
    m_selectAllBtn = new QPushButton();
    m_selectAllBtn->setObjectName("secondaryButton");
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &MainWindow::onSelectAllToggled);

    m_installProgress = new QProgressBar();
    m_installProgress->setObjectName("checkProgress");
    m_installProgress->setRange(0, 0); // indeterminate
    m_installProgress->setTextVisible(false);
    m_installProgress->setFixedHeight(6);
    m_installProgress->hide();

    m_installStatusLine = new QLabel();
    m_installStatusLine->setObjectName("mutedLabel");
    m_installStatusLine->hide();

    // Shown whenever we're not mid-install: "N updates selected · Download: X MB"
    m_selectionSummaryLabel = new QLabel();
    m_selectionSummaryLabel->setObjectName("mutedLabel");

    m_installSelectedBtn = new QPushButton();
    m_installSelectedBtn->setObjectName("primaryButton");
    m_installSelectedBtn->setCursor(Qt::PointingHandCursor);
    m_installSelectedBtn->setFixedWidth(190);
    connect(m_installSelectedBtn, &QPushButton::clicked, this, &MainWindow::onInstallSelectedClicked);

    auto* progressCol = new QVBoxLayout();
    progressCol->setSpacing(2);
    progressCol->addWidget(m_installProgress);
    progressCol->addWidget(m_installStatusLine);
    progressCol->addWidget(m_selectionSummaryLabel);

    footer->addWidget(m_selectAllBtn);
    footer->addLayout(progressCol, 1);
    footer->addWidget(m_installSelectedBtn);
    layout->addLayout(footer);

    return page;
}

QIcon MainWindow::iconForPackage(const QString& packageName) const
{
    // Try a direct match from the current system icon theme first — this
    // covers common apps whose apt package name matches their icon name
    // (e.g. "firefox", "gimp", "thunderbird").
    QIcon themed = QIcon::fromTheme(packageName);
    if (!themed.isNull())
        return themed;

    // A handful of Debian/Ubuntu package name -> icon-theme name aliases,
    // for packages whose apt name doesn't match their icon name.
    static const QMap<QString, QString> aliases = {
        { "nautilus",           "system-file-manager" },
        { "nautilus-data",      "system-file-manager" },
        { "systemd",            "preferences-system" },
        { "systemd-resolved",   "network-workgroup" },
        { "systemd-oomd",       "utilities-system-monitor" },
        { "systemd-sysv",       "preferences-system" },
        { "systemd-cryptsetup", "object-locked" },
        { "udev",               "drive-removable-media" },
        { "libudev1",           "drive-removable-media" },
        { "firefox-esr",        "firefox" },
        { "libreoffice-core",   "libreoffice-startcenter" },
        { "gnome-shell",        "gnome-panel" },
        { "network-manager",    "network-wired" },
        { "pulseaudio",         "audio-card" },
        { "xserver-xorg-core",  "video-display" },
    };
    const QString alias = aliases.value(packageName);
    if (!alias.isEmpty()) {
        QIcon aliased = QIcon::fromTheme(alias);
        if (!aliased.isNull())
            return aliased;
    }

    QIcon generic = QIcon::fromTheme(QStringLiteral("package-x-generic"));
    if (!generic.isNull())
        return generic;

    return generateFallbackIcon(packageName);
}

void MainWindow::populateUpdatesTable()
{
    m_updatesTable->setRowCount(0);
    for (const auto& u : std::as_const(m_updates)) {
        const int row = m_updatesTable->rowCount();
        m_updatesTable->insertRow(row);

        auto* checkWidget = new QWidget();
        auto* checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        auto* checkBox = new QCheckBox();
        checkBox->setChecked(u.selected);
        int capturedRow = row;
        connect(checkBox, &QCheckBox::toggled, this, [this, capturedRow](bool checked) {
            if (capturedRow < m_updates.size())
                m_updates[capturedRow].selected = checked;
            updateSelectionSummary();
        });
        checkLayout->addWidget(checkBox);
        m_updatesTable->setCellWidget(row, 0, checkWidget);

        // Icon + package name, side by side, in a single cell widget.
        auto* nameWidget = new QWidget();
        auto* nameLayout = new QHBoxLayout(nameWidget);
        nameLayout->setContentsMargins(8, 0, 8, 0);
        nameLayout->setSpacing(10);
        auto* iconLabel = new QLabel();
        iconLabel->setPixmap(iconForPackage(u.name).pixmap(22, 22));
        iconLabel->setFixedSize(22, 22);
        auto* nameLabel = new QLabel(u.name);
        nameLabel->setObjectName("pkgNameLabel");
        nameLayout->addWidget(iconLabel);
        nameLayout->addWidget(nameLabel, 1);
        m_updatesTable->setCellWidget(row, 1, nameWidget);

        auto* versionItem = new QTableWidgetItem(u.currentVersion + "  \u2192  " + u.newVersion);
        auto* sizeItem = new QTableWidgetItem(u.sizeString());
        versionItem->setTextAlignment(Qt::AlignCenter);
        sizeItem->setTextAlignment(Qt::AlignCenter);

        m_updatesTable->setItem(row, 2, versionItem);
        m_updatesTable->setItem(row, 3, sizeItem);
        m_updatesTable->setRowHeight(row, 46);
    }

    const bool empty = m_updates.isEmpty();
    m_updatesTable->setVisible(!empty);
    m_updatesTable->parentWidget()->findChild<QWidget*>("emptyWrap")->setVisible(empty);
    m_installSelectedBtn->setEnabled(!empty);
    m_selectAllBtn->setEnabled(!empty);
    updateSelectionSummary();
}

void MainWindow::updateSelectionSummary()
{
    int count = 0;
    qint64 totalBytes = 0;
    for (const auto& u : std::as_const(m_updates)) {
        if (u.selected) {
            ++count;
            totalBytes += u.sizeBytes;
        }
    }
    m_selectionSummaryLabel->setText(
        TR("selection_summary").arg(count) + "   \u00B7   " + TR("selection_download").arg(humanSize(totalBytes)));
    m_selectionSummaryLabel->setVisible(!m_updates.isEmpty() && !m_installInProgress);
}

void MainWindow::onSelectAllToggled()
{
    m_selectAllState = !m_selectAllState;
    for (int row = 0; row < m_updatesTable->rowCount(); ++row) {
        auto* w = m_updatesTable->cellWidget(row, 0);
        if (auto* cb = w ? w->findChild<QCheckBox*>() : nullptr)
            cb->setChecked(m_selectAllState);
    }
    updateSelectionSummary();
}

void MainWindow::onInstallSelectedClicked()
{
    if (m_installInProgress)
        return;

    QStringList selectedNames;
    for (const auto& u : std::as_const(m_updates)) {
        if (u.selected)
            selectedNames << u.name;
    }
    if (selectedNames.isEmpty())
        return;

    const auto reply = QMessageBox::question(
        this, TR("confirm_install_title"),
        TR("confirm_install_text") + "\n\n" + selectedNames.join(", "),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    m_lastInstalledCount = selectedNames.size();
    m_installInProgress = true;
    m_installProgress->show();
    m_installStatusLine->setText(TR("installing"));
    m_installStatusLine->show();
    m_selectionSummaryLabel->hide();
    m_installSelectedBtn->setEnabled(false);
    m_installSelectedBtn->setText(TR("installing"));
    m_selectAllBtn->setEnabled(false);

    // Requires root — triggers a pkexec polkit prompt.
    m_apt->installPackages(selectedNames);
}

void MainWindow::onInstallOutput(const QString& line)
{
    if (!line.isEmpty())
        m_installStatusLine->setText(line);
}

void MainWindow::onInstallSummaryReady(int upgraded, int notUpgraded, bool restartRequired)
{
    m_pendingUpgraded = upgraded;
    m_pendingNotUpgraded = notUpgraded;
    m_pendingRestartRequired = restartRequired;
}

void MainWindow::onInstallFinished(bool success, const QString& errorMessage)
{
    m_installInProgress = false;
    m_installProgress->hide();
    m_installStatusLine->hide();
    m_installSelectedBtn->setText(TR("btn_install_selected"));
    m_installSelectedBtn->setEnabled(true);
    m_selectAllBtn->setEnabled(true);

    if (!success) {
        QMessageBox::warning(this, TR("err_title"), TR("err_install_failed") + "\n\n" + errorMessage);
        updateSelectionSummary();
        return;
    }

    // The installed packages are now recorded in apt/dpkg's real logs, so just
    // keep whatever wasn't selected as still pending and reload history from
    // disk (the GUI never fabricates history rows).
    QVector<UpdateItem> remaining;
    for (const auto& u : std::as_const(m_updates)) {
        if (!u.selected)
            remaining.append(u);
    }
    m_updates = remaining;

    populateUpdatesTable();
    refreshOverviewCard();

    // Re-parse the log so this transaction shows up in History.
    m_history->refresh();

    showInstallCompleteDialog(m_pendingUpgraded, m_pendingNotUpgraded, m_pendingRestartRequired);
}

void MainWindow::showInstallCompleteDialog(int upgraded, int notUpgraded, bool restartRequired)
{
    // apt's own summary line is the most accurate source, but fall back to
    // "how many we asked it to install" if parsing it failed for any reason
    // (e.g. unexpected locale, unusual apt output format).
    const int parsedProcessed = upgraded + notUpgraded;
    const int processed = parsedProcessed > 0 ? parsedProcessed : m_lastInstalledCount;
    const int shownUpgraded = upgraded > 0 ? upgraded : m_lastInstalledCount;
    const int shownUpToDate = std::max(processed - shownUpgraded, 0);

    QDialog dialog(this);
    dialog.setObjectName("installCompleteDialog");
    dialog.setWindowTitle(TR("install_complete_title"));
    dialog.setFixedWidth(380);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 28, 28, 24);
    layout->setSpacing(14);

    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(16);
    auto* iconLabel = new QLabel("\u2713");
    iconLabel->setObjectName("statusIcon");
    iconLabel->setProperty("state", "ok");
    iconLabel->setFixedSize(56, 56);
    iconLabel->setAlignment(Qt::AlignCenter);
    auto* titleLabel = new QLabel(TR("install_complete_title"));
    titleLabel->setObjectName("statusText");
    titleLabel->setWordWrap(true);
    headerRow->addWidget(iconLabel);
    headerRow->addWidget(titleLabel, 1);
    layout->addLayout(headerRow);

    auto addStat = [&](const QString& text) {
        auto* l = new QLabel(text);
        l->setObjectName("mutedLabel");
        layout->addWidget(l);
    };
    addStat(TR("install_complete_processed").arg(processed));
    addStat(TR("install_complete_upgraded").arg(shownUpgraded));
    if (shownUpToDate > 0)
        addStat(TR("install_complete_uptodate").arg(shownUpToDate));

    if (restartRequired) {
        auto* restartNote = new QLabel(TR("install_complete_restart_note"));
        restartNote->setObjectName("infoValue");
        restartNote->setWordWrap(true);
        layout->addSpacing(6);
        layout->addWidget(restartNote);
    }

    layout->addSpacing(10);
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    if (restartRequired) {
        auto* laterBtn = new QPushButton(TR("btn_restart_later"));
        laterBtn->setObjectName("secondaryButton");
        laterBtn->setCursor(Qt::PointingHandCursor);
        connect(laterBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

        auto* nowBtn = new QPushButton(TR("btn_restart_now"));
        nowBtn->setObjectName("primaryButton");
        nowBtn->setCursor(Qt::PointingHandCursor);
        QDialog* dialogPtr = &dialog;
        connect(nowBtn, &QPushButton::clicked, this, [this, dialogPtr]() {
            dialogPtr->accept();
            const auto reply = QMessageBox::question(
                this, TR("restart_confirm_title"), TR("restart_confirm_text"),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes)
                QProcess::startDetached(QStringLiteral("pkexec"), { "systemctl", "reboot" });
        });

        btnRow->addWidget(laterBtn);
        btnRow->addWidget(nowBtn);
    } else {
        auto* closeBtn = new QPushButton(TR("btn_close"));
        closeBtn->setObjectName("primaryButton");
        closeBtn->setCursor(Qt::PointingHandCursor);
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
        btnRow->addWidget(closeBtn);
    }

    layout->addLayout(btnRow);
    dialog.exec();
}

// ============================================================
// History page
// ============================================================
QWidget* MainWindow::buildHistoryPage()
{
    auto* page = new QWidget();
    page->setObjectName("pageHistory");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(36, 32, 36, 32);
    layout->setSpacing(14);

    m_historyHeading = new QLabel();
    m_historyHeading->setObjectName("pageHeading");
    m_historySubtitle = new QLabel();
    m_historySubtitle->setObjectName("pageSubtitle");
    layout->addWidget(m_historyHeading);
    layout->addWidget(m_historySubtitle);

    auto* card = makeCard();
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);

    // One row per apt transaction; clicking a row expands its package list.
    m_historyTree = new QTreeWidget();
    m_historyTree->setObjectName("historyTable");
    m_historyTree->setColumnCount(3);
    m_historyTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_historyTree->setRootIsDecorated(true);
    m_historyTree->setIndentation(22);
    m_historyTree->setFrameShape(QFrame::NoFrame);
    m_historyTree->setFocusPolicy(Qt::NoFocus);

    auto* header = m_historyTree->header();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setMinimumSectionSize(72);
    cardLayout->addWidget(m_historyTree);

    m_historyEmptyLabel = new QLabel();
    m_historyEmptyLabel->setObjectName("mutedLabel");
    m_historyEmptyLabel->setAlignment(Qt::AlignCenter);
    m_historyEmptyLabel->setContentsMargins(0, 40, 0, 40);
    cardLayout->addWidget(m_historyEmptyLabel);

    layout->addWidget(card, 1);
    return page;
}

void MainWindow::onHistoryLoaded(const QVector<HistoryTransaction>& transactions)
{
    m_transactions = transactions;
    populateHistoryTable();
}

void MainWindow::populateHistoryTable()
{
    m_historyTree->clear();

    for (const auto& txn : std::as_const(m_transactions)) {
        auto* top = new QTreeWidgetItem(m_historyTree);
        top->setText(0, txn.date.isValid()
                         ? txn.date.toString("yyyy-MM-dd HH:mm")
                         : QStringLiteral("—"));
        // Summary line: "System Update · N packages updated".
        QString summary = TR("history_system_update");
        summary += QStringLiteral(" · ") +
                   QString::number(txn.packageCount()) + QStringLiteral(" ") +
                   TR("history_packages");
        top->setText(1, summary);
        top->setText(2, txn.success ? TR("result_success") : TR("result_failed"));
        top->setToolTip(0, txn.commandline.isEmpty() ? QString() : txn.commandline);

        const QFont bold = m_historyTree->font();
        for (int c = 0; c < 3; ++c) {
            top->setFont(c, bold);
            if (c != 1)
                top->setTextAlignment(c, Qt::AlignCenter);
        }

        // Child rows = the individual packages in this transaction.
        for (const auto& e : txn.entries) {
            auto* child = new QTreeWidgetItem(top);
            child->setText(1, QStringLiteral("✓ ") + e.package);
            child->setText(2, e.version);
        }
    }

    // Newest transaction first (HistoryManager already returns newest-first).
    const bool empty = m_transactions.isEmpty();
    m_historyTree->setVisible(!empty);
    m_historyEmptyLabel->setVisible(empty);
}

// ============================================================
// Settings page
// ============================================================
QWidget* MainWindow::buildSettingsPage()
{
    auto* page = new QWidget();
    page->setObjectName("pageSettings");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(36, 32, 36, 32);
    layout->setSpacing(14);

    m_settingsHeading = new QLabel();
    m_settingsHeading->setObjectName("pageHeading");
    m_settingsSubtitle = new QLabel();
    m_settingsSubtitle->setObjectName("pageSubtitle");
    layout->addWidget(m_settingsHeading);
    layout->addWidget(m_settingsSubtitle);

    auto* card = makeCard();
    auto* grid = new QGridLayout(card);
    grid->setContentsMargins(24, 22, 24, 22);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(18);

    int rowIdx = 0;

    m_settingsLanguageLabel = new QLabel();
    m_languageCombo = new QComboBox();
    m_languageCombo->addItem("English");
    m_languageCombo->addItem("Tiếng Việt");
    connect(m_languageCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onLanguageChanged);
    grid->addWidget(m_settingsLanguageLabel, rowIdx, 0);
    grid->addWidget(m_languageCombo, rowIdx, 1);
    rowIdx++;

    m_settingsChannelLabel = new QLabel();
    m_channelCombo = new QComboBox();
    grid->addWidget(m_settingsChannelLabel, rowIdx, 0);
    grid->addWidget(m_channelCombo, rowIdx, 1);
    rowIdx++;

    m_autoCheckBox = new QCheckBox();
    m_autoCheckBox->setChecked(true);
    grid->addWidget(m_autoCheckBox, rowIdx, 0, 1, 2);
    rowIdx++;

    m_notifyCheckBox = new QCheckBox();
    m_notifyCheckBox->setChecked(true);
    grid->addWidget(m_notifyCheckBox, rowIdx, 0, 1, 2);
    rowIdx++;

    m_darkModeCheckBox = new QCheckBox();
    connect(m_darkModeCheckBox, &QCheckBox::toggled, this, &MainWindow::onDarkModeToggled);
    grid->addWidget(m_darkModeCheckBox, rowIdx, 0, 1, 2);
    rowIdx++;

    grid->setColumnStretch(1, 1);
    layout->addWidget(card);

    // ---- Repository sources ----
    auto* repoCard = makeCard();
    auto* repoLayout = new QVBoxLayout(repoCard);
    repoLayout->setContentsMargins(24, 20, 24, 20);
    repoLayout->setSpacing(10);

    auto* repoHeaderRow = new QHBoxLayout();
    auto* repoHeaderCol = new QVBoxLayout();
    repoHeaderCol->setSpacing(2);
    m_settingsRepoHeading = new QLabel();
    m_settingsRepoHeading->setObjectName("infoValue");
    m_settingsRepoSubtitle = new QLabel();
    m_settingsRepoSubtitle->setObjectName("mutedLabel");
    repoHeaderCol->addWidget(m_settingsRepoHeading);
    repoHeaderCol->addWidget(m_settingsRepoSubtitle);
    repoHeaderRow->addLayout(repoHeaderCol, 1);

    m_addRepoBtn = new QPushButton();
    m_addRepoBtn->setObjectName("secondaryButton");
    m_addRepoBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addRepoBtn, &QPushButton::clicked, this, &MainWindow::onAddRepoClicked);
    repoHeaderRow->addWidget(m_addRepoBtn, 0, Qt::AlignTop);
    repoLayout->addLayout(repoHeaderRow);

    m_repoListWidget = new QListWidget();
    m_repoListWidget->setObjectName("repoList");
    m_repoListWidget->setFrameShape(QFrame::NoFrame);
    m_repoListWidget->setFocusPolicy(Qt::NoFocus);
    m_repoListWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_repoListWidget->setUniformItemSizes(true);
    repoLayout->addWidget(m_repoListWidget);

    m_repoEmptyLabel = new QLabel();
    m_repoEmptyLabel->setObjectName("mutedLabel");
    m_repoEmptyLabel->setAlignment(Qt::AlignCenter);
    m_repoEmptyLabel->setContentsMargins(0, 12, 0, 12);
    repoLayout->addWidget(m_repoEmptyLabel);

    m_repoStatusLabel = new QLabel();
    m_repoStatusLabel->setObjectName("mutedLabel");
    m_repoStatusLabel->setWordWrap(true);
    m_repoStatusLabel->setContentsMargins(0, 4, 0, 0);
    m_repoStatusLabel->hide();
    repoLayout->addWidget(m_repoStatusLabel);

    layout->addWidget(repoCard);

    auto* aboutCard = makeCard();
    auto* aboutLayout = new QVBoxLayout(aboutCard);
    aboutLayout->setContentsMargins(24, 20, 24, 20);
    m_settingsAboutLabel = new QLabel();
    m_settingsAboutLabel->setObjectName("infoValue");
    m_settingsAboutText = new QLabel();
    m_settingsAboutText->setObjectName("mutedLabel");
    aboutLayout->addWidget(m_settingsAboutLabel);
    aboutLayout->addWidget(m_settingsAboutText);
    layout->addWidget(aboutCard);

    layout->addStretch();
    return page;
}

void MainWindow::onLanguageChanged(int index)
{
    Lang::instance().setLanguage(index == 1 ? Language::VI : Language::EN);
}

// ============================================================
// Settings: repository sources
// ============================================================
void MainWindow::loadRepoSources()
{
    QSettings settings;
    const int count = settings.beginReadArray("repoSources");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RepoInfo r;
        r.name = settings.value("name").toString();
        r.id = settings.value("id").toString();
        r.url = settings.value("url").toString();
        r.suite = settings.value("suite").toString();
        r.components = settings.value("components").toStringList();
        r.keyUrl = settings.value("keyUrl").toString();
        r.fingerprint = settings.value("fingerprint").toString();
        r.keyringPath = settings.value("keyringPath").toString();
        r.official = false;
        if (!r.name.isEmpty() && !r.id.isEmpty())
            m_repoSources.append(r);
    }
    settings.endArray();
}

void MainWindow::saveRepoSources()
{
    QSettings settings;
    settings.beginWriteArray("repoSources");
    for (int i = 0; i < m_repoSources.size(); ++i) {
        settings.setArrayIndex(i);
        const RepoInfo& r = m_repoSources[i];
        settings.setValue("name", r.name);
        settings.setValue("id", r.id);
        settings.setValue("url", r.url);
        settings.setValue("suite", r.suite);
        settings.setValue("components", r.components);
        settings.setValue("keyUrl", r.keyUrl);
        settings.setValue("fingerprint", r.fingerprint);
        settings.setValue("keyringPath", r.keyringPath);
    }
    settings.endArray();
}

void MainWindow::populateRepoList()
{
    m_repoListWidget->clear();

    // --- Official curated repository (trusted, no shell command) ---
    {
        const RepoInfo& official = RepositoryManager::officialRepository();
        const bool enabled = RepositoryManager::isEnabled(official);
        auto* item = new QListWidgetItem(m_repoListWidget);
        item->setSizeHint(QSize(0, 52));

        auto* row = new QWidget();
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(12, 6, 12, 6);
        rowLayout->setSpacing(2);

        auto* top = new QHBoxLayout();
        top->setSpacing(10);
        auto* nameLabel = new QLabel(official.name);
        nameLabel->setObjectName("infoValue");
        top->addWidget(nameLabel);
        top->addStretch();
        auto* badge = new QLabel(TR("repo_badge_official"));
        badge->setObjectName("repoBadge");
        badge->setStyleSheet(QStringLiteral("color: #1E9E5A;"));
        top->addWidget(badge);
        auto* gpgBadge = new QLabel(TR("repo_badge_gpg"));
        gpgBadge->setObjectName("repoBadge");
        gpgBadge->setStyleSheet(QStringLiteral("color: #1E9E5A;"));
        top->addWidget(gpgBadge);
        rowLayout->addLayout(top);

        auto* urlLabel = new QLabel(official.url);
        urlLabel->setObjectName("mutedLabel");
        urlLabel->setStyleSheet(QStringLiteral("font-family: monospace;"));
        rowLayout->addWidget(urlLabel);

        if (enabled) {
            auto* status = new QLabel(QStringLiteral("✓ ") + TR("repo_badge_enabled"));
            status->setObjectName("repoBadge");
            status->setStyleSheet(QStringLiteral("color: #1E9E5A;"));
            rowLayout->addWidget(status);
        } else {
            auto* status = new QLabel(official.suite + QStringLiteral(" · ") +
                                      official.componentsString());
            status->setObjectName("mutedLabel");
            rowLayout->addWidget(status);
        }

        m_repoListWidget->setItemWidget(item, row);
    }

    // --- User-added custom repositories (structured, GPG-verified) ---
    for (const auto& repo : std::as_const(m_repoSources)) {
        auto* item = new QListWidgetItem(m_repoListWidget);
        item->setSizeHint(QSize(0, 52));

        auto* row = new QWidget();
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(12, 6, 12, 6);
        rowLayout->setSpacing(2);

        auto* top = new QHBoxLayout();
        top->setSpacing(10);
        auto* nameLabel = new QLabel(repo.name);
        nameLabel->setObjectName("infoValue");
        top->addWidget(nameLabel);
        top->addStretch();
        const bool enabled = RepositoryManager::isEnabled(repo);
        auto* state = new QLabel(enabled ? (QStringLiteral("✓ ") + TR("repo_badge_enabled"))
                                         : TR("repo_badge_disabled"));
        state->setObjectName("repoBadge");
        state->setStyleSheet(enabled ? QStringLiteral("color: #1E9E5A;")
                                     : QStringLiteral("color: #C97A1D;"));
        top->addWidget(state);
        rowLayout->addLayout(top);

        auto* urlLabel = new QLabel(repo.url);
        urlLabel->setObjectName("mutedLabel");
        urlLabel->setStyleSheet(QStringLiteral("font-family: monospace;"));
        rowLayout->addWidget(urlLabel);

        m_repoListWidget->setItemWidget(item, row);
    }

    const int rows = 1 + m_repoSources.size();
    m_repoListWidget->setFixedHeight(qMin(rows, 5) * 52);
    m_repoEmptyLabel->setVisible(m_repoSources.isEmpty());
}

void MainWindow::onRepoStatus(const QString& message)
{
    m_repoStatusLabel->setText(message);
    m_repoStatusLabel->setStyleSheet(QString());
    m_repoStatusLabel->show();
}

void MainWindow::onRepoAddFinished(bool ok, const QString& errorMessage)
{
    m_addRepoBtn->setEnabled(true);
    m_repoStatusLabel->show();
    if (ok) {
        m_repoStatusLabel->setText(TR("repo_added_ok"));
        m_repoStatusLabel->setStyleSheet(QStringLiteral("color: #1E9E5A;"));
        m_repoSources.append(m_pendingRepo);
        saveRepoSources();
        populateRepoList();
    } else {
        m_repoStatusLabel->setText(TR("err_repo_failed") + (errorMessage.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" — ") + errorMessage));
        m_repoStatusLabel->setStyleSheet(QStringLiteral("color: #E0527A;"));
    }
}

void MainWindow::onAddRepoClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(TR("repo_dialog_title"));
    dialog.setFixedWidth(470);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(8);

    auto fieldLabel = [&](const QString& text) {
        auto* lbl = new QLabel(text);
        lbl->setObjectName("mutedLabel");
        layout->addWidget(lbl);
    };
    // Red outline via a dynamic "error" property + QSS, keeps normal input styling.
    auto setError = [](QWidget* w, bool err) {
        if (w->property("error").toBool() == err)
            return;
        w->setProperty("error", err);
        w->style()->unpolish(w);
        w->style()->polish(w);
    };

    fieldLabel(TR("repo_dialog_name_label"));
    auto* nameEdit = new QLineEdit(QStringLiteral("My Repository"));
    layout->addWidget(nameEdit);

    fieldLabel(TR("repo_dialog_url_label"));
    auto* urlEdit = new QLineEdit(QStringLiteral("https://"));
    urlEdit->setPlaceholderText(tr("https://example.com/apt"));
    layout->addWidget(urlEdit);

    fieldLabel(TR("repo_dialog_suite_label"));
    auto* suiteCombo = new QComboBox();
    suiteCombo->setEditable(true);
    suiteCombo->addItems({ QStringLiteral("stable"), QStringLiteral("testing"),
                           QStringLiteral("trixie"), QStringLiteral("noble"),
                           QStringLiteral("resolute"), QStringLiteral("bookworm"),
                           QStringLiteral("jammy") });
    suiteCombo->setCurrentText(QStringLiteral("stable"));
    suiteCombo->setPlaceholderText(tr("e.g. stable, trixie, bookworm"));
    layout->addWidget(suiteCombo);

    fieldLabel(TR("repo_dialog_components_label"));
    auto* compCombo = new QComboBox();
    compCombo->setEditable(true);
    compCombo->addItems({ QStringLiteral("main"),
                          QStringLiteral("main contrib non-free"),
                          QStringLiteral("main contrib non-free-firmware"),
                          QStringLiteral("main universe"),
                          QStringLiteral("main restricted universe multiverse") });
    compCombo->setCurrentText(QStringLiteral("main"));
    compCombo->setPlaceholderText(tr("e.g. main contrib non-free"));
    layout->addWidget(compCombo);

    fieldLabel(TR("repo_dialog_key_url_label"));
    auto* keyEdit = new QLineEdit();
    keyEdit->setPlaceholderText(tr("https://example.com/key.asc"));
    layout->addWidget(keyEdit);

    fieldLabel(TR("repo_dialog_fingerprint_label"));
    auto* fpEdit = new QLineEdit();
    fpEdit->setPlaceholderText(tr("40-hex GPG key fingerprint (REQUIRED)"));
    fpEdit->setMaxLength(49);
    layout->addWidget(fpEdit);

    auto* hintLabel = new QLabel(TR("repo_dialog_hint"));
    hintLabel->setObjectName("mutedLabel");
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto* btnRow = new QHBoxLayout();
    auto* cancelBtn = new QPushButton(TR("btn_cancel"));
    cancelBtn->setObjectName("secondaryButton");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto* addBtn = new QPushButton(TR("btn_add_repo"));
    addBtn->setObjectName("primaryButton");
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(addBtn);
    layout->addLayout(btnRow);

    // --- Realtime validation: disable Add + red-out invalid required fields ---
    auto revalidate = [&]() {
        const QString url = urlEdit->text().trimmed();
        const QString keyUrl = keyEdit->text().trimmed();
        const QString fp = fpEdit->text().trimmed();
        const bool nameOk = !nameEdit->text().trimmed().isEmpty();
        const bool urlOk = url.startsWith(QStringLiteral("https://")) && url != QStringLiteral("https://");
        const bool suiteOk = !suiteCombo->currentText().trimmed().isEmpty();
        const bool compsOk = !compCombo->currentText().trimmed().isEmpty();
        const bool keyOk = keyUrl.startsWith(QStringLiteral("https://"));
        const bool fpOk = normalizeFingerprint(fp).size() == 40;

        setError(nameEdit, false);
        setError(urlEdit, !url.isEmpty() && !urlOk);
        setError(keyEdit, !keyUrl.isEmpty() && !keyOk);
        setError(fpEdit, !fp.isEmpty() && !fpOk); // red until exactly 40 hex

        addBtn->setEnabled(nameOk && urlOk && suiteOk && compsOk && keyOk && fpOk);
    };

    connect(nameEdit, &QLineEdit::textChanged, &dialog, revalidate);
    connect(urlEdit, &QLineEdit::textChanged, &dialog, revalidate);
    connect(suiteCombo, &QComboBox::editTextChanged, &dialog, revalidate);
    connect(compCombo, &QComboBox::editTextChanged, &dialog, revalidate);
    connect(keyEdit, &QLineEdit::textChanged, &dialog, revalidate);
    connect(fpEdit, &QLineEdit::textChanged, &dialog, revalidate);
    revalidate();

    if (dialog.exec() != QDialog::Accepted)
        return;

    RepoInfo repo;
    repo.name = nameEdit->text().trimmed();
    repo.url = urlEdit->text().trimmed();
    repo.suite = suiteCombo->currentText().trimmed();
    repo.components = compCombo->currentText().trimmed().split(' ', Qt::SkipEmptyParts);
    repo.keyUrl = keyEdit->text().trimmed();
    repo.fingerprint = fpEdit->text().trimmed();
    repo.official = false;

    // Derive a safe slug (^[a-z0-9][a-z0-9-]*$) from the display name.
    QString slug = repo.name.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-')))
        slug.remove(0, 1);
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    if (slug.isEmpty())
        slug = QStringLiteral("repo");
    repo.id = slug;
    repo.keyringPath = QStringLiteral("/usr/share/keyrings/") + slug +
                       QStringLiteral("-archive-keyring.gpg");

    if (normalizeFingerprint(repo.fingerprint).size() != 40 ||
        !repo.isValid(/*requireFingerprint=*/true)) {
        QMessageBox::warning(this, TR("err_title"), TR("err_repo_invalid"));
        return;
    }

    m_pendingRepo = repo;
    m_addRepoBtn->setEnabled(false);
    // Safe, structured, GPG-verified add — fetches the key and shows a preview
    // (onRepoKeyFetched) before anything is installed as root.
    m_repoMgr->addRepository(repo);
}

void MainWindow::onRepoKeyFetched(const QString& fetched)
{
    const QString typed = normalizeFingerprint(m_pendingRepo.fingerprint);
    const bool matched = (typed == normalizeFingerprint(fetched));
    const QString fmtFetched = RepositoryManager::formatFingerprint(fetched);
    const QString fmtTyped = RepositoryManager::formatFingerprint(typed);

    QDialog dlg(this);
    dlg.setWindowTitle(TR("repo_preview_title"));
    dlg.setFixedWidth(440);
    auto* l = new QVBoxLayout(&dlg);
    l->setContentsMargins(24, 24, 24, 20);
    l->setSpacing(10);

    auto* verdict = new QLabel(matched ? (QStringLiteral("✓ ") + TR("repo_preview_match"))
                                       : (QStringLiteral("✗ ") + TR("repo_preview_mismatch")));
    verdict->setStyleSheet(matched ? QStringLiteral("color: #1E9E5A; font-size: 15px; font-weight: 600;")
                                   : QStringLiteral("color: #E0527A; font-size: 15px; font-weight: 600;"));
    l->addWidget(verdict);

    auto* fetchedLbl = new QLabel(TR("repo_preview_fetched") + QStringLiteral("\n") + fmtFetched);
    fetchedLbl->setObjectName("mutedLabel");
    fetchedLbl->setStyleSheet(QStringLiteral("font-family: monospace;"));
    fetchedLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(fetchedLbl);

    auto* typedLbl = new QLabel(TR("repo_preview_typed") + QStringLiteral("\n") + fmtTyped);
    typedLbl->setObjectName("mutedLabel");
    typedLbl->setStyleSheet(QStringLiteral("font-family: monospace;"));
    typedLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(typedLbl);

    auto* hint = new QLabel(TR("repo_preview_hint"));
    hint->setObjectName("mutedLabel");
    hint->setWordWrap(true);
    l->addWidget(hint);

    auto* row = new QHBoxLayout();
    auto* cancelBtn = new QPushButton(TR("btn_cancel"));
    cancelBtn->setObjectName("secondaryButton");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    row->addWidget(cancelBtn);
    row->addStretch();
    if (matched) {
        auto* enableBtn = new QPushButton(TR("btn_enable_repo"));
        enableBtn->setObjectName("primaryButton");
        enableBtn->setCursor(Qt::PointingHandCursor);
        connect(enableBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        row->addWidget(enableBtn);
    }
    l->addLayout(row);

    if (dlg.exec() == QDialog::Accepted) {
        m_repoMgr->confirmAdd();
    } else {
        m_repoMgr->cancel();
        m_addRepoBtn->setEnabled(true);
        m_repoStatusLabel->hide();
    }
}

// ============================================================
// Retranslate everything
// ============================================================
void MainWindow::retranslateUi()
{
    setWindowTitle(TR("window_title"));
    m_appTitleLabel->setText(TR("app_title"));

    const QStringList navKeys = { "nav_overview", "nav_updates", "nav_history", "nav_settings" };
    const QStringList icons = { "\u2302", "\u2B07", "\u25F7", "\u2699" };
    for (int i = 0; i < m_navList->count(); ++i) {
        auto* item = m_navList->item(i);
        item->setText("  " + icons[i] + "   " + Lang::instance().t(navKeys[i]));
    }

    // Overview
    m_overviewHeading->setText(TR("overview_heading"));
    m_overviewSubtitle->setText(TR("overview_subtitle"));
    m_channelCombo->blockSignals(true);
    m_channelCombo->clear();
    m_channelCombo->addItem(TR("channel_stable"));
    m_channelCombo->addItem(TR("channel_beta"));
    m_channelCombo->blockSignals(false);
    refreshOverviewCard();

    // Updates page
    m_updatesHeading->setText(TR("updates_heading"));
    m_updatesSubtitle->setText(TR("updates_subtitle"));
    m_updatesTable->setHorizontalHeaderLabels({ "", TR("col_name"), TR("col_version"), TR("col_size") });
    m_noUpdatesTitle->setText(TR("no_updates_title"));
    m_noUpdatesSubtitle->setText(TR("no_updates_subtitle"));
    m_selectAllBtn->setText(TR("btn_select_all"));
    m_installSelectedBtn->setText(m_installInProgress ? TR("installing") : TR("btn_install_selected"));
    updateSelectionSummary();

    // History page
    m_historyHeading->setText(TR("history_heading"));
    m_historySubtitle->setText(TR("history_subtitle"));
    m_historyTree->setHeaderLabels({ TR("col_date"), TR("col_item"), TR("col_result") });
    m_historyEmptyLabel->setText(TR("history_empty"));
    populateHistoryTable();

    // Settings page
    m_settingsHeading->setText(TR("settings_heading"));
    m_settingsSubtitle->setText(TR("settings_subtitle"));
    m_settingsLanguageLabel->setText(TR("settings_language"));
    m_settingsChannelLabel->setText(TR("settings_channel"));
    m_autoCheckBox->setText(TR("settings_autocheck"));
    m_notifyCheckBox->setText(TR("settings_notify"));
    m_darkModeCheckBox->setText(TR("settings_darkmode"));
    m_settingsAboutLabel->setText(TR("settings_about"));
    m_settingsAboutText->setText(TR("settings_about_text"));

    m_settingsRepoHeading->setText(TR("settings_repo_heading"));
    m_settingsRepoSubtitle->setText(TR("settings_repo_subtitle"));
    m_addRepoBtn->setText(TR("btn_add_repo"));
    m_repoEmptyLabel->setText(TR("repo_empty"));
    m_repoStatusLabel->setText(QString());
    m_repoStatusLabel->hide();
    populateRepoList();
}