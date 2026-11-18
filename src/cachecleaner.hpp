// Copyright (C) 2026 Yan Henridev
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#ifndef CACHECLEANER_HPP
#define CACHECLEANER_HPP

#include <QDialog>
#include <QProcess>
#include <QStringList>

class QCloseEvent;
class QKeyEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class QDialogButtonBox;

/// @brief Dialog that removes old package versions from the pacman cache.
///
/// It mirrors the workflow of Octopi's cache cleaner: a dry run of
/// `paccache` is used to show the candidates and the space that can be
/// freed, then the user confirms before the actual removal, which is run
/// with root privileges through pkexec.
class CacheCleaner : public QDialog {
    Q_OBJECT

 public:
    explicit CacheCleaner(QWidget* parent = nullptr);
    ~CacheCleaner() override;

 protected:
    void closeEvent(QCloseEvent* event) override;
    void done(int result) override;
    void keyPressEvent(QKeyEvent* event) override;
    void reject() override;

 private:
    enum class State {
        Idle,
        Scanning,
        Cleaning,
    };

    struct CacheGroup {
        bool uninstalled{};
        QListWidget* list{};
        QSpinBox* keep{};
        QPushButton* refresh{};
        QPushButton* clean{};
        QLabel* summary{};
        QProcess* process{};
        State state{State::Idle};
        bool rescan_pending{};
        QStringList candidates{};
        QString saved_size{};
    };

    void setup_ui();
    void create_group(CacheGroup& group, QVBoxLayout* parent_layout, const QString& title, bool uninstalled);
    void refresh(CacheGroup& group);
    void request_clean(CacheGroup& group);
    void on_process_finished(CacheGroup& group, int exit_code, QProcess::ExitStatus exit_status);
    void on_process_error(CacheGroup& group, QProcess::ProcessError error);
    void parse_dry_run(CacheGroup& group, const QString& output);
    void update_group(CacheGroup& group);
    void update_control_states();
    void save_settings() const;
    [[nodiscard]] bool is_busy() const noexcept;

    CacheGroup m_installed{};
    CacheGroup m_uninstalled{};
    QDialogButtonBox* m_button_box{};
};

#endif  // CACHECLEANER_HPP
