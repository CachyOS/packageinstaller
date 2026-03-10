// Copyright (C) 2025 Vladislav Nepogodin
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

mod pacman_cache;

use std::fmt::Write as _;

use alpm::{Alpm, AlpmList, AnyEvent, TransFlag};
use alpm_utils::DbListExt;
use anyhow::{Context, Result};
// use tracing::{error, info, warn};

#[cxx::bridge(namespace = "cachyos_pi::rustlib")]
mod ffi {
    pub struct StringStruct {
        pub first: String,
        pub second: String,
    }

    #[derive(Debug)]
    pub struct PackageView {
        pub name: String,
        pub version: String,
        pub desc: String,
        // it is used to conviniently extract upgradable condidates
        pub upgradable: bool,
    }

    extern "Rust" {
        type AlpmManager;

        fn init_alpm_manager() -> Result<Box<AlpmManager>>;

        fn refresh(&mut self) -> Result<()>;

        fn sync_trans_pub(&mut self, targets: &Vec<String>) -> Result<String>;

        fn prepare_add_trans_pub(
            &mut self,
            targets: &Vec<String>,
            conflict_msg: &mut String,
        ) -> Result<()>;

        fn prepare_remove_trans_pub(
            &mut self,
            targets: &Vec<String>,
            conflict_msg: &mut String,
        ) -> Result<()>;

        fn display_install_targets_pub(
            &mut self,
            targets: &Vec<String>,
            verbose: bool,
        ) -> Result<StringStruct>;

        fn display_remove_targets_pub(
            &mut self,
            targets: &Vec<String>,
            verbose: bool,
        ) -> Result<StringStruct>;

        fn get_package_view(&self, pkgname: &str) -> Result<PackageView>;

        fn get_list_of_packages(&self) -> Vec<PackageView>;
    }

    extern "Rust" {
        fn is_valid_alpm_dbs() -> Result<bool>;
    }

    #[namespace = "utils"]
    unsafe extern "C++" {
        include!("logging_utils.hpp");
        fn log_error_msg(message: &CxxString);
        fn log_warn_msg(message: &CxxString);
        fn log_info_msg(message: &CxxString);
    }
}

fn init_alpm_manager() -> Result<Box<AlpmManager>> {
    let manager = AlpmManager::new("/etc/pacman.conf".into(), "/".into())
        .context("Failed to initialize alpm manager")?;
    Ok(Box::new(manager))
}

fn is_valid_alpm_dbs() -> Result<bool> {
    let manager = AlpmManager::new("/etc/pacman.conf".into(), "/".into())
        .context("Failed to initialize alpm manager")?;
    Ok(manager.is_valid_dbs())
}

fn cb_event(any_event: AnyEvent, _ctx: &mut i32) {
    let event = any_event.event();
    match event {
        alpm::Event::CheckDepsStart => {
            log_info_msg("ALPM: checking dependencies...".into());
        }
        alpm::Event::ResolveDepsStart => {
            log_info_msg("ALPM: resolving dependencies...".into());
        }
        alpm::Event::InterConflictsStart => {
            log_info_msg("ALPM: looking for conflicting packages...".into());
        }
        alpm::Event::TransactionStart => {
            log_info_msg("ALPM: :: Processing package changes...".into());
        }
        alpm::Event::FileConflictsStart => {
            log_info_msg("ALPM: ALPM_EVENT_FILECONFLICTS_START".into());
        }
        alpm::Event::FileConflictsDone => {
            log_info_msg("ALPM: ALPM_EVENT_FILECONFLICTS_DONE".into());
        }
        alpm::Event::InterConflictsDone => {
            log_info_msg("ALPM: ALPM_EVENT_INTERCONFLICTS_DONE".into());
        }

        /*
        // Ignored events.
        alpm::Event::KeyDownloadStart
        | alpm::Event::ScriptletInfo(_)
        | alpm::Event::PkgRetrieveStart(_)
        | alpm::Event::OptDepRemoval(_)
        | alpm::Event::RetrieveStart
        | alpm::Event::DatabaseMissing(_)
        | alpm::Event::PackageOperationDone(_)
        | alpm::Event::RetrieveDone
        | alpm::Event::RetrieveFailed
        | alpm::Event::PkgRetrieveDone(_)
        | alpm::Event::PkgRetrieveFailed(_)
        | alpm::Event::HookStart(_)
        | alpm::Event::HookRunStart(_)
        | alpm::Event::PackageOperationStart(_)
        | alpm::Event::DiskSpaceStart
        | alpm::Event::LoadStart
        | alpm::Event::KeyringStart
        | alpm::Event::IntegrityStart
        | alpm::Event::PacnewCreated(_)
        | alpm::Event::PacsaveCreated(_)
        | alpm::Event::CheckDepsDone
        | alpm::Event::ResolveDepsDone
        | alpm::Event::TransactionDone
        | alpm::Event::IntegrityDone
        | alpm::Event::KeyringDone
        | alpm::Event::KeyDownloadDone
        | alpm::Event::LoadDone
        | alpm::Event::DiskSpaceDone
        | alpm::Event::HookRunDone(_)
        | alpm::Event::HookDone(_) => {} // No-op
        */
        _ => {}
    }
}

fn cb_log(level: alpm::LogLevel, message: &str, _ctx: &mut i32) {
    if level == alpm::LogLevel::DEBUG || level == alpm::LogLevel::FUNCTION {
        return;
    }
    let out_str = format!("ALPM: {}", message);

    match level {
        alpm::LogLevel::ERROR => {
            log_error_msg(format!("{out_str}"));
        },
        alpm::LogLevel::WARNING => {
            log_warn_msg(format!("{out_str}"));
        },
        _ => {},
    }
}

fn display_sizes(number: i64) -> String {
    if number < 1024 {
        format!("{}", number)
    } else if number < 1024 * 1024 {
        format!("{} KB", number / 1024)
    } else if number < 1024 * 1024 * 1024 {
        format!("{} MB", number / 1024 / 1024)
    } else {
        format!("{} GB", number / 1024 / 1024 / 1024)
    }
}

fn display_targets(handle: &Alpm, verbose: bool, status_text: &mut String) -> String {
    let mut res = String::new();
    let mut isize = 0;
    let mut rsize = 0;
    let mut dlsize = 0;

    // colculate size for installation packages trans
    for install_pkg in handle.trans_add() {
        dlsize += install_pkg.download_size();
        isize += install_pkg.isize();

        let _ = write!(res, "{}-{}", install_pkg.name(), install_pkg.version());
        res.push(if verbose { '\n' } else { ' ' });
    }
    // colculate size for installation packages trans
    for remove_pkg in handle.trans_remove() {
        rsize += remove_pkg.isize();

        let _ = write!(res, "{}-{} [removal]", remove_pkg.name(), remove_pkg.version());
        res.push(if verbose { '\n' } else { ' ' });
    }

    // in case we have no transaction targets
    if res.is_empty() {
        return res;
    }

    if dlsize > 0 {
        status_text.push_str(&format!("Total Download Size: {}\n", display_sizes(dlsize)));
    }
    if isize > 0 {
        status_text.push_str(&format!("Total Installed Size: {}\n", display_sizes(isize)));
    }
    if rsize > 0 && isize == 0 {
        status_text.push_str(&format!("Total Removed Size: {}\n", display_sizes(rsize)));
    }
    if isize > 0 && rsize > 0 {
        status_text.push_str(&format!("Net Upgrade Size: {}\n", display_sizes(isize - rsize)));
    }

    res.push('\n');
    res
}

fn print_broken_dep(trans_add: &[ffi::PackageView], miss: &alpm::DepMissing) {
    let depstring = miss.depend().to_string();
    if miss.causing_pkg().is_none() {
        log_warn_msg(format!(
            "unable to satisfy dependency '{}' required by {}",
            depstring,
            miss.target()
        ));
    } else if let Some(causing_pkg) = miss.causing_pkg() {
        if let Some(pkg) = trans_add.iter().find(|p| p.name == causing_pkg) {
            log_warn_msg(format!(
                "installing {} ({}) breaks dependency '{}' required by {}",
                causing_pkg,
                pkg.version,
                depstring,
                miss.target()
            ));
        } else {
            log_warn_msg(format!(
                "removing {} breaks dependency '{}' required by {}",
                causing_pkg,
                depstring,
                miss.target()
            ));
        }
    }
}

pub struct AlpmManager {
    handle: Alpm,
    pacmanconf_path: String,
    rootdir: String,
}

impl AlpmManager {
    pub fn new(pacmanconf_path: String, rootdir: String) -> Result<Self> {
        let pacman = pacmanconf::Config::with_opts(None, Some(&pacmanconf_path), Some(&rootdir))
            .context("Failed to initialize pacman config")?;
        let handle = alpm_utils::alpm_with_conf(&pacman).context("Failed to initialize alpm")?;

        // maybe not set for now
        handle.set_log_cb(0, cb_log);
        handle.set_event_cb(0, cb_event);

        Ok(AlpmManager { handle, pacmanconf_path, rootdir })
    }

    pub fn refresh(&mut self) -> Result<()> {
        let pacman =
            pacmanconf::Config::with_opts(None, Some(&self.pacmanconf_path), Some(&self.rootdir))
                .context("Failed to initialize pacman config")?;
        let handle = alpm_utils::alpm_with_conf(&pacman).context("Failed to initialize alpm")?;

        self.handle = handle;
        // maybe not set for now
        self.handle.set_log_cb(0, cb_log);
        self.handle.set_event_cb(0, cb_event);
        Ok(())
    }

    pub fn is_valid_dbs(&self) -> bool {
        let count = self.handle.syncdbs().iter().map(|db| db.pkgs().len()).sum::<usize>();
        count != 0
    }

    pub fn display_targets(&self, verbosepkglists: bool, status_text: &mut String) -> String {
        display_targets(&self.handle, verbosepkglists, status_text)
    }

    pub fn add_targets_to_install(&mut self, targets: &[String]) -> Result<()> {
        // set what flags we want to enable for the transaction;
        let flags =
            TransFlag::DB_ONLY | TransFlag::ALL_DEPS | TransFlag::ALL_EXPLICIT | TransFlag::NO_LOCK;

        // initialise the transaction
        if let Err(e) = self.handle.trans_init(flags) {
            log_error_msg(format!("failed to create a new transaction ({e})"));
            self.handle.trans_release().context("Failed to release transaction")?;
            anyhow::bail!("{e}");
        }

        // add the packages we want to install
        let dbs = self.handle.syncdbs();
        for target in targets {
            if let Ok(pkg) = dbs.pkg(target.as_str()) {
                if let Err(e) = self.handle.trans_add_pkg(pkg) {
                    log_error_msg(format!("failed to add package to be installed ({e})"));
                    anyhow::bail!("{e}");
                }
                break;
            }
        }

        Ok(())
    }

    pub fn add_targets_to_remove(&mut self, targets: &[String]) -> Result<()> {
        // set what flags we want to enable for the transaction;
        let flags =
            TransFlag::DB_ONLY | TransFlag::ALL_DEPS | TransFlag::ALL_EXPLICIT | TransFlag::NO_LOCK;

        // initialise the transaction
        if let Err(e) = self.handle.trans_init(flags) {
            log_error_msg(format!("failed to create a new transaction ({e})"));
            self.handle.trans_release().context("Failed to release transaction")?;
            anyhow::bail!("{e}");
        }

        // add the packages we want to remove
        let db_local = self.handle.localdb();
        for target in targets {
            if let Ok(pkg) = db_local.pkg(target.as_str()) {
                if let Err(e) = self.handle.trans_remove_pkg(pkg) {
                    log_error_msg(format!("failed to add package to be removed ({e})"));
                    anyhow::bail!("{e}");
                }
            }
        }
        Ok(())
    }

    fn get_syncdb(&self, dbname: &str) -> Option<&alpm::Db> {
        self.handle.syncdbs().iter().find(|db| db.name() == dbname)
    }

    fn process_pkg(&self, pkg: &alpm::Package) -> Result<()> {
        if let Err(err) = self.handle.trans_add_pkg(pkg) {
            log_error_msg(format!("error: '{}': {err}", pkg.name()));
            anyhow::bail!("{err}");
        } else {
            Ok(())
        }
    }

    fn process_group(&self, dblist: &AlpmList<&alpm::Db>, group: &str) -> Result<()> {
        let db_group = dblist.iter().find_map(|db| db.group(group).ok());
        if db_group.is_none() {
            log_error_msg(format!("target not found: {group}"));
            anyhow::bail!("target not found: {group}");
        }

        let db_group = db_group.unwrap();
        let pkgs = db_group.packages();

        for pkg in pkgs {
            let _ = self.process_pkg(pkg).context("Failed to process group pkg")?;
        }

        Ok(())
    }

    fn process_targname(&self, dblist: &AlpmList<&alpm::Db>, targname: &str) -> Result<()> {
        if let Some(pkg) = dblist.find_satisfier(targname) {
            self.process_pkg(pkg)
        } else {
            self.process_group(dblist, targname)
        }
    }

    fn process_target(&self, target: &str) -> Result<()> {
        // package from specific repo
        let target = alpm_utils::Targ::from(target);
        if let Some(dbname) = target.repo {
            if let Some(db) = self.get_syncdb(dbname) {
                let usage = db.usage().context("Failed to get database usage")?;
                db.set_usage(usage | alpm::Usage::INSTALL).context("Failed to set db usage")?;

                let mut dblist = alpm::AlpmListMut::new();
                dblist.push(db);

                self.process_targname(&dblist.list(), target.pkg)
                    .context("Failed to process target")?;
                db.set_usage(usage).context("Failed to set db usage")?;

                return Ok(());
            } else {
                log_error_msg(format!("database not found: {dbname}"));
                anyhow::bail!("database not found: {dbname}");
            }
        }

        // package from any repo
        let dblist = self.handle.syncdbs();
        self.process_targname(&dblist, target.pkg)
    }

    pub fn prepare_add_trans_pub(
        &mut self,
        targets: &Vec<String>,
        conflict_msg: &mut String,
    ) -> Result<()> {
        self.prepare_add_trans(targets, conflict_msg)
    }

    fn prepare_add_trans(
        &mut self,
        targets: &[String],
        conflict_msg: &mut String,
    ) -> Result<()> {
        self.add_targets_to_install(targets).context("Failed to add targets to install")?;

        self.sync_prepare_execute(conflict_msg)
    }

    pub fn prepare_remove_trans_pub(
        &mut self,
        targets: &Vec<String>,
        conflict_msg: &mut String,
    ) -> Result<()> {
        self.prepare_remove_trans(targets, conflict_msg)
    }

    fn prepare_remove_trans(
        &mut self,
        targets: &[String],
        conflict_msg: &mut String,
    ) -> Result<()> {
        self.add_targets_to_remove(targets).context("Failed to add targets to install")?;

        self.sync_prepare_execute(conflict_msg)
    }

    fn sync_prepare_execute(&mut self, conflict_msg: &mut String) -> Result<()> {
        let trans_add = self
            .handle
            .trans_add()
            .iter()
            .map(|pkg| ffi::PackageView {
                name: pkg.name().to_string(),
                version: pkg.version().to_string(),
                desc: pkg.desc().map(|s| s.to_string()).unwrap_or_default(),
                upgradable: false,
            })
            .collect::<Vec<_>>();

        // nothing to do: just exit without complaining
        if trans_add.is_empty() {
            self.handle.trans_release().context("Failed to release transaction")?;
            return Ok(());
        }

        // prepare the transaction
        if let Err(err) = self.handle.trans_prepare() {
            log_error_msg(format!("failed to prepare transaction ({err})"));

            match err.data() {
                Some(alpm::PrepareData::PkgInvalidArch(data)) => {
                    for pkg in data {
                        log_info_msg(format!(
                            "package {} does not have a valid architecture",
                            pkg.name()
                        ));
                    }
                },
                Some(alpm::PrepareData::UnsatisfiedDeps(data)) => {
                    for miss in data {
                        print_broken_dep(&trans_add, &miss);
                    }
                },
                Some(alpm::PrepareData::ConflictingDeps(data)) => {
                    for conflict in data {
                        let pkg_conflict1 = conflict.package1().name();
                        let pkg_conflict2 = conflict.package2().name();

                        // only print reason if it contains new information
                        if conflict.reason().depmod() == alpm::DepMod::Any {
                            conflict_msg.push_str(&format!(
                                "'{pkg_conflict1}' and '{pkg_conflict2}' are in conflict\n",
                            ));

                            log_info_msg(format!(
                                "'{pkg_conflict1}' and '{pkg_conflict2}' are in conflict"
                            ));
                        } else {
                            let reason = conflict.reason();

                            conflict_msg.push_str(&format!(
                                "'{pkg_conflict1}' and '{pkg_conflict2}' are in conflict \
                                 ({reason})\n",
                            ));
                            log_info_msg(format!(
                                "'{pkg_conflict1}' and '{pkg_conflict2}' are in conflict \
                                 ({reason})",
                            ));
                        }
                    }
                },
                _ => log_info_msg("err data invalid".to_owned()),
            }
        }

        self.handle.trans_release().context("Failed to release transaction")?;
        Ok(())
    }

    pub fn sync_trans_pub(&mut self, targets: &Vec<String>) -> Result<String> {
        let mut conflict_msg = String::new();
        self.sync_trans(targets, &mut conflict_msg)?;

        Ok(conflict_msg)
    }

    pub fn sync_trans(&mut self, targets: &[String], conflict_msg: &mut String) -> Result<()> {
        // set what flags we want to enable for the transaction;
        let flags =
            TransFlag::DB_ONLY | TransFlag::ALL_DEPS | TransFlag::ALL_EXPLICIT | TransFlag::NO_LOCK;
        self.handle.trans_init(flags).context("Failed to init transaction")?;

        for targ in targets {
            if let Err(err) = self.process_target(targ).context("Failed to process target") {
                self.handle.trans_release().context("Failed to release transaction")?;
                anyhow::bail!("{err}");
            }
        }

        self.sync_prepare_execute(conflict_msg)
    }

    pub fn display_install_targets_pub(
        &mut self,
        targets: &Vec<String>,
        verbose: bool,
    ) -> Result<ffi::StringStruct> {
        self.add_targets_to_install(targets).context("Failed to add targets to install")?;

        let mut status_text = String::new();
        let details_text = self.display_targets(verbose, &mut status_text);
        self.handle.trans_release().context("Failed to release transaction")?;

        Ok(ffi::StringStruct { first: status_text, second: details_text })
    }

    pub fn display_remove_targets_pub(
        &mut self,
        targets: &Vec<String>,
        verbose: bool,
    ) -> Result<ffi::StringStruct> {
        self.add_targets_to_remove(targets).context("Failed to add targets to remove")?;

        let mut status_text = String::new();
        let details_text = self.display_targets(verbose, &mut status_text);
        self.handle.trans_release().context("Failed to release transaction")?;

        Ok(ffi::StringStruct { first: status_text, second: details_text })
    }

    pub fn get_package_view(&self, pkgname: &str) -> Result<ffi::PackageView> {
        let pkg_view = self.handle.syncdbs().iter().find_map(|db| match db.pkg(pkgname) {
            Ok(pkg) => Some(ffi::PackageView {
                name: pkg.name().to_string(),
                version: pkg.version().to_string(),
                desc: pkg.desc().map(|s| s.to_string()).unwrap_or_default(),
                upgradable: false,
            }),
            Err(_) => None,
        });
        if let Some(pkg) = pkg_view {
            Ok(pkg)
        } else {
            anyhow::bail!("package not found")
        }
    }

    pub fn get_list_of_packages(&self) -> Vec<ffi::PackageView> {
        pacman_cache::get_list_of_packages(&self.handle)
    }
}

fn log_error_msg(msg: String) {
    cxx::let_cxx_string!(logging_msg = msg);
    ffi::log_error_msg(&logging_msg);
}

fn log_warn_msg(msg: String) {
    cxx::let_cxx_string!(logging_msg = msg);
    ffi::log_warn_msg(&logging_msg);
}

fn log_info_msg(msg: String) {
    cxx::let_cxx_string!(logging_msg = msg);
    ffi::log_info_msg(&logging_msg);
}
