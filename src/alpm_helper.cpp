// Copyright (C) 2022 Vladislav Nepogodin
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

#include "alpm_helper.hpp"
#include "ini.hpp"
#include "utils.hpp"

#include <sys/utsname.h>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

typedef struct _pm_target_t {
    alpm_pkg_t* remove;
    alpm_pkg_t* install;
    int is_explicit;
} pm_target_t;

/* callback to handle messages/notifications from libalpm */
void cb_event(void* ctx, alpm_event_t* event);

/* callback to handle display of progress */
void cb_progress(void* ctx, alpm_progress_t event, const char* pkgname,
    int percent, size_t howmany, size_t remain);

/* callback to handle messages/notifications from pacman library */
__attribute__((format(printf, 3, 0))) void cb_log(void* ctx, alpm_loglevel_t level, const char* fmt, va_list args);

void cb_event(void* ctx, alpm_event_t* event) {
    (void)ctx;

    std::string opr{};
    switch (event->type) {
    case ALPM_EVENT_CHECKDEPS_START:
        spdlog::info("ALPM: checking dependencies...");
        break;
    case ALPM_EVENT_RESOLVEDEPS_START:
        spdlog::info("ALPM: resolving dependencies...");
        break;
    case ALPM_EVENT_INTERCONFLICTS_START:
        spdlog::info("ALPM: looking for conflicting packages...");
        break;
    case ALPM_EVENT_TRANSACTION_START:
        spdlog::info("ALPM: :: Processing package changes...");
        break;

    case ALPM_EVENT_FILECONFLICTS_START:
        spdlog::info("ALPM: ALPM_EVENT_FILECONFLICTS_START");
        break;
    case ALPM_EVENT_FILECONFLICTS_DONE:
        spdlog::info("ALPM: ALPM_EVENT_FILECONFLICTS_DONE");
        break;
    case ALPM_EVENT_INTERCONFLICTS_DONE:
        spdlog::info("ALPM: ALPM_EVENT_INTERCONFLICTS_DONE");
        break;

    // all the simple done events, with fallthrough for each
    case ALPM_EVENT_KEY_DOWNLOAD_START:
    case ALPM_EVENT_SCRIPTLET_INFO:
    case ALPM_EVENT_PKG_RETRIEVE_START:
    case ALPM_EVENT_OPTDEP_REMOVAL:
    case ALPM_EVENT_DB_RETRIEVE_START:
    case ALPM_EVENT_DATABASE_MISSING:
    case ALPM_EVENT_PACKAGE_OPERATION_DONE:
    case ALPM_EVENT_DB_RETRIEVE_DONE:
    case ALPM_EVENT_DB_RETRIEVE_FAILED:
    case ALPM_EVENT_PKG_RETRIEVE_DONE:
    case ALPM_EVENT_PKG_RETRIEVE_FAILED:
    case ALPM_EVENT_HOOK_START:
    case ALPM_EVENT_HOOK_RUN_START:
    case ALPM_EVENT_PACKAGE_OPERATION_START:
    case ALPM_EVENT_DISKSPACE_START:
    case ALPM_EVENT_LOAD_START:
    case ALPM_EVENT_KEYRING_START:
    case ALPM_EVENT_INTEGRITY_START:
    case ALPM_EVENT_PACNEW_CREATED:
    case ALPM_EVENT_PACSAVE_CREATED:
    case ALPM_EVENT_CHECKDEPS_DONE:
    case ALPM_EVENT_RESOLVEDEPS_DONE:
    case ALPM_EVENT_TRANSACTION_DONE:
    case ALPM_EVENT_INTEGRITY_DONE:
    case ALPM_EVENT_KEYRING_DONE:
    case ALPM_EVENT_KEY_DOWNLOAD_DONE:
    case ALPM_EVENT_LOAD_DONE:
    case ALPM_EVENT_DISKSPACE_DONE:
    case ALPM_EVENT_HOOK_RUN_DONE:
    case ALPM_EVENT_HOOK_DONE:
        /* nothing */
        return;
    }
}

void cb_progress(void* ctx, alpm_progress_t event, const char* pkgname, int percent, size_t howmany, size_t remain) {
    (void)ctx;
    std::string_view opr{};
    // set text of message to display
    switch (event) {
    case ALPM_PROGRESS_ADD_START:
        opr = "installing";
        break;
    case ALPM_PROGRESS_UPGRADE_START:
        opr = "upgrading";
        break;
    case ALPM_PROGRESS_DOWNGRADE_START:
        opr = "downgrading";
        break;
    case ALPM_PROGRESS_REINSTALL_START:
        opr = "reinstalling";
        break;
    case ALPM_PROGRESS_REMOVE_START:
        opr = "removing";
        break;
    case ALPM_PROGRESS_CONFLICTS_START:
        opr = "checking for file conflicts";
        break;
    case ALPM_PROGRESS_DISKSPACE_START:
        opr = "checking available disk space";
        break;
    case ALPM_PROGRESS_INTEGRITY_START:
        opr = "checking package integrity";
        break;
    case ALPM_PROGRESS_KEYRING_START:
        opr = "checking keys in keyring";
        break;
    case ALPM_PROGRESS_LOAD_START:
        opr = "loading package files";
        break;
    default:
        return;
    }

    if (percent == 100) {
        spdlog::info("({}/{}) {} done", remain, howmany, pkgname);
        return;
    }
    spdlog::info("({}/{}) {}", remain, howmany, opr);
}

void cb_log(void* ctx, alpm_loglevel_t level, const char* fmt, va_list args) {
    (void)ctx;
    if (!fmt || strlen(fmt) == 0) {
        return;
    }

    if (level == ALPM_LOG_DEBUG || level == ALPM_LOG_FUNCTION) {
        return;
    }

    std::vector<char> temp{};
    std::size_t length{63};
    while (temp.size() <= length) {
        temp.resize(length + 1);
        const auto status = std::vsnprintf(temp.data(), temp.size(), fmt, args);
        if (status < 0) {
            return;
        }
        length = static_cast<std::size_t>(status);
    }
    const auto converted_str = std::string{temp.data(), length};
    const auto out_str       = fmt::format("ALPM: {}", converted_str);
    switch (level) {
    case ALPM_LOG_ERROR:
        spdlog::error("{}", out_str);
        break;
    case ALPM_LOG_WARNING:
        spdlog::warn("{}", out_str);
        break;
    default:
        break;
    }
}

namespace utils {
namespace {
inline std::size_t replace_all(std::string& inout, const std::string_view& what, const std::string_view& with) {
    std::size_t count{};
    std::size_t pos{};
    while (std::string::npos != (pos = inout.find(what.data(), pos, what.length()))) {
        inout.replace(pos, what.length(), with.data(), with.length());
        pos += with.length(), ++count;
    }
    return count;
}

inline std::size_t remove_all(std::string& inout, const std::string_view& what) {
    return replace_all(inout, what, "");
}

void parse_cachedirs(alpm_handle_t* handle) noexcept {
    static constexpr auto cachedir = "/var/cache/pacman/pkg/";

    alpm_list_t* cachedirs = nullptr;
    cachedirs              = alpm_list_add(cachedirs, const_cast<void*>(reinterpret_cast<const void*>(cachedir)));
    alpm_option_set_cachedirs(handle, cachedirs);
}

void parse_includes(alpm_handle_t* handle, alpm_db_t* db, const auto& section, const auto& file) noexcept {
    const auto* archs = alpm_option_get_architectures(handle);
    const auto* arch  = reinterpret_cast<const char*>(archs->data);

    mINI::INIFile file_nested(file);
    // next, create a structure that will hold data
    mINI::INIStructure mirrorlist;

    // now we can read the file
    file_nested.read(mirrorlist);
    for (const auto& mirror : mirrorlist) {
        auto repo = mirror.second.begin()->second;
        if (repo.starts_with("/")) {
            continue;
        }
        utils::replace_all(repo, "$arch", arch);
        utils::replace_all(repo, "$repo", section.c_str());
        alpm_db_add_server(db, repo.c_str());
    }
}

void parse_repos(alpm_handle_t* handle) noexcept {
    static constexpr auto pacman_conf_path = "/etc/pacman.conf";

    mINI::INIFile file(pacman_conf_path);
    // next, create a structure that will hold data
    mINI::INIStructure ini;

    // now we can read the file
    file.read(ini);
    for (const auto& it : ini) {
        const auto& section = it.first;
        const auto& nested  = it.second;
        if (section == "options") {
            for (const auto& it_nested : nested) {
                if (it_nested.first != "architecture") {
                    continue;
                }
                // add CacheDir
                const auto& archs = ::utils::make_multiline(it_nested.second, false, " ");
                for (const auto& arch : archs) {
                    if (arch == "auto") {
                        struct utsname un;
                        uname(&un);
                        char* tmp = un.machine;
                        if (tmp != nullptr) {
                            alpm_option_add_architecture(handle, tmp);
                        }
                        continue;
                    }

                    alpm_option_add_architecture(handle, arch.c_str());
                }
            }
            continue;
        }
        auto* db = alpm_register_syncdb(handle, section.c_str(), ALPM_SIG_USE_DEFAULT);

        for (const auto& it_nested : nested) {
            const auto& param = it_nested.first;
            const auto& value = it_nested.second;
            if (param == "include") {
                parse_includes(handle, db, section, value);
            }
        }
    }
}
}  // namespace
}  // namespace utils

namespace {
/* prepare a list of pkgs to display */
std::string _display_targets(const std::vector<pm_target_t>& targets, bool verbose, std::string& status_text) {
    if (targets.empty()) {
        return {};
    }

    std::string res{};
    off_t isize{};
    off_t rsize{};
    off_t dlsize{};

    /* gather package info */
    for (const auto& target : targets) {
        if (target.install != nullptr) {
            dlsize += alpm_pkg_download_size(target.install);
            isize += alpm_pkg_get_isize(target.install);
        }

        if (target.remove != nullptr) {
            /* add up size of all removed packages */
            rsize += alpm_pkg_get_isize(target.remove);
        }
    }

    /* form data for both verbose and non-verbose display */
    for (const auto& target : targets) {
        if (target.install != nullptr) {
            res += fmt::format("{}-{}", alpm_pkg_get_name(target.install), alpm_pkg_get_version(target.install));
        } else if (isize == 0) {
            res += fmt::format("{}-{}", alpm_pkg_get_name(target.remove), alpm_pkg_get_version(target.remove));
        } else {
            res += fmt::format("{}-{} [removal]", alpm_pkg_get_name(target.remove), alpm_pkg_get_version(target.remove));
        }

        res += (verbose) ? "\n" : " ";
    }

    // presentation
    const auto& proper_present = [](auto&& number) {
        if (number < 1024)
            return fmt::format("{}", number);
        else if (number < 1024 * 1024)
            return fmt::format("{} KB", number / 1024);
        else if (number < 1024 * 1024 * 1024)
            return fmt::format("{} MB", number / 1024 / 1024);

        return fmt::format("{} GB", number / 1024 / 1024 / 1024);
    };

    if (dlsize > 0) {
        status_text += fmt::format("Total Download Size: {}\n", proper_present(dlsize));
    }

    if (isize > 0) {
        status_text += fmt::format("Total Installed Size: {}\n", proper_present(isize));
    }
    if (rsize > 0 && isize == 0) {
        status_text += fmt::format("Total Removed Size: {}\n", proper_present(rsize));
    }
    /* only show this net value if different from raw installed size */
    if (isize > 0 && rsize > 0) {
        status_text += fmt::format("Net Upgrade Size: {}\n", proper_present(isize - rsize));
    }

    res += "\n";
    return res;
}

}  // namespace

void setup_alpm(alpm_handle_t* handle) {
    utils::parse_repos(handle);
    utils::parse_cachedirs(handle);

    alpm_option_set_logcb(handle, cb_log, nullptr);
    alpm_option_set_progresscb(handle, cb_progress, nullptr);
    alpm_option_set_eventcb(handle, cb_event, nullptr);
}

void destroy_alpm(alpm_handle_t* handle) {
    alpm_unregister_all_syncdbs(handle);
    alpm_release(handle);
}

void refresh_alpm(alpm_handle_t** handle, alpm_errno_t* err) {
    destroy_alpm(*handle);
    *handle = alpm_initialize("/", "/var/lib/pacman/", err);
    setup_alpm(*handle);
}

std::string display_targets(alpm_handle_t* handle, bool verbosepkglists, std::string& status_text) {
    std::vector<pm_target_t> targets{};
    alpm_db_t* db_local = alpm_get_localdb(handle);

    for (alpm_list_t* i = alpm_trans_get_add(handle); i; i = alpm_list_next(i)) {
        auto* pkg = static_cast<alpm_pkg_t*>(i->data);
        pm_target_t targ;
        targ.install = pkg;
        targ.remove  = alpm_db_get_pkg(db_local, alpm_pkg_get_name(pkg));
        targets.emplace_back(targ);
    }
    for (alpm_list_t* i = alpm_trans_get_remove(handle); i; i = alpm_list_next(i)) {
        auto* pkg = static_cast<alpm_pkg_t*>(i->data);
        pm_target_t targ;
        targ.install = nullptr;
        targ.remove  = pkg;
        // if(alpm_list_find(config->explicit_removes, pkg, pkg_cmp)) {
        //     targ->is_explicit = 1;
        // }
        targets.emplace_back(targ);
    }
    return _display_targets(targets, verbosepkglists, status_text);
}

void add_targets_to_install(alpm_handle_t* handle, const std::vector<std::string>& vec) {
    /* Step 0: create a new transaction */
    if (alpm_trans_init(handle, ALPM_TRANS_FLAG_ALLDEPS | ALPM_TRANS_FLAG_ALLEXPLICIT) != 0) {
        spdlog::error("failed to create a new transaction ({})\n", alpm_strerror(alpm_errno(handle)));
        alpm_trans_release(handle);
        return;
    }

    /* Step 1: add targets to the created transaction */
    auto* dbs = alpm_get_syncdbs(handle);

    for (const auto& el : vec) {
        for (alpm_list_t* i = dbs; i != nullptr; i = i->next) {
            auto* db  = reinterpret_cast<alpm_db_t*>(i->data);
            auto* pkg = alpm_db_get_pkg(db, el.c_str());
            if (pkg) {
                if (alpm_add_pkg(handle, pkg) != 0) {
                    spdlog::error("failed to add package to be installed ({})\n", alpm_strerror(alpm_errno(handle)));
                }
                break;
            }
        }
    }
}

void add_targets_to_remove(alpm_handle_t* handle, const std::vector<std::string>& vec) {
    /* Step 0: create a new transaction */
    if (alpm_trans_init(handle, ALPM_TRANS_FLAG_ALLDEPS | ALPM_TRANS_FLAG_ALLEXPLICIT) != 0) {
        spdlog::error("failed to create a new transaction ({})\n", alpm_strerror(alpm_errno(handle)));
        alpm_trans_release(handle);
        return;
    }

    /* Step 1: add targets to the created transaction */
    auto* db_local = alpm_get_localdb(handle);

    for (const auto& el : vec) {
        auto* pkg = alpm_db_get_pkg(db_local, el.c_str());
        if (pkg) {
            if (alpm_remove_pkg(handle, pkg) != 0) {
                spdlog::error("failed to add package to be removed ({})\n", alpm_strerror(alpm_errno(handle)));
            }
        }
    }
}

static void trans_init_error(alpm_handle_t* handle) {
    alpm_errno_t err = alpm_errno(handle);
    spdlog::error("error: failed to init transaction ({})", alpm_strerror(err));
    if (err == ALPM_ERR_HANDLE_LOCK) {
        const char* lockfile = alpm_option_get_lockfile(handle);
        spdlog::error("error: could not lock database: {}", strerror(errno));
        if (access(lockfile, F_OK) == 0) {
            spdlog::error("\n  if you're sure a package manager is not already\n"
                          "  running, you can remove {}", lockfile);
        }
    }
}

static int trans_release(alpm_handle_t* handle) {
    if (alpm_trans_release(handle) == -1) {
        spdlog::error("error: failed to release transaction ({})", alpm_strerror(alpm_errno(handle)));
        return -1;
    }
    return 0;
}

static alpm_db_t* get_db(alpm_handle_t* handle, const char* dbname) {
    for (alpm_list_t* i = alpm_get_syncdbs(handle); i; i = i->next) {
        auto* db = static_cast<alpm_db_t*>(i->data);
        if (strcmp(alpm_db_get_name(db), dbname) == 0) {
            return db;
        }
    }
    return nullptr;
}

static bool process_pkg(alpm_handle_t* handle, alpm_pkg_t* pkg) {
    if (alpm_add_pkg(handle, pkg) == -1) {
        alpm_errno_t err = alpm_errno(handle);
        spdlog::error("error: '{}': {}\n", alpm_pkg_get_name(pkg), alpm_strerror(err));
        return true;
    }
    return false;
}

static bool group_exists(alpm_list_t* dbs, const char* name) {
    for (alpm_list_t* i = dbs; i; i = i->next) {
        auto* db = static_cast<alpm_db_t*>(i->data);

        if (alpm_db_get_group(db, name)) {
            return true;
        }
    }

    return false;
}

static int process_group(alpm_handle_t* handle, alpm_list_t* dbs, const char* group, int error) {
    alpm_list_t* pkgs = alpm_find_group_pkgs(dbs, group);
    size_t count      = alpm_list_count(pkgs);

    if (!count) {
        if (group_exists(dbs, group)) {
            return 0;
        }

        spdlog::error("error: target not found: {}", group);
        return 1;
    }

    int ret = 0;
    if (error) {
        /* we already know another target errored. there is no reason to prompt the
         * user here; we already validated the group name so just move on since we
         * won't actually be installing anything anyway. */
        goto cleanup;
    }

    for (alpm_list_t* i = pkgs; i; i = alpm_list_next(i)) {
        auto* pkg = static_cast<alpm_pkg_t*>(i->data);

        if (process_pkg(handle, pkg) == 1) {
            ret = 1;
            goto cleanup;
        }
    }

cleanup:
    alpm_list_free(pkgs);
    return ret;
}

static int process_targname(alpm_handle_t* handle, alpm_list_t* dblist, const char* targname, int error) {
    alpm_pkg_t* pkg = alpm_find_dbs_satisfier(handle, dblist, targname);

    /* skip ignored packages when user says no */
    if (alpm_errno(handle) == ALPM_ERR_PKG_IGNORED) {
        spdlog::warn("skipping target: {}", targname);
        return 0;
    }

    if (pkg) {
        return process_pkg(handle, pkg);
    }
    /* fallback on group */
    return process_group(handle, dblist, targname, error);
}

static int process_target(alpm_handle_t* handle, const char* target, int error) {
    /* process targets */
    char* targstring = strdup(target);
    char* targname   = strchr(targstring, '/');
    int ret{};
    alpm_list_t* dblist;

    if (targname && targname != targstring) {
        *targname = '\0';
        targname++;
        const char* dbname = targstring;
        alpm_db_t* db      = get_db(handle, dbname);
        if (!db) {
            spdlog::error("error: database not found: {}", dbname);
            ret = 1;
            goto cleanup;
        }

        int usage;
        /* explicitly mark this repo as valid for installs since
         * a repo name was given with the target */
        alpm_db_get_usage(db, &usage);
        alpm_db_set_usage(db, usage | ALPM_DB_USAGE_INSTALL);

        dblist = alpm_list_add(nullptr, db);
        ret    = process_targname(handle, dblist, targname, error);
        alpm_list_free(dblist);

        /* restore old usage, so we don't possibly disturb later
         * targets */
        alpm_db_set_usage(db, usage);
    } else {
        targname = targstring;
        dblist   = alpm_get_syncdbs(handle);
        ret      = process_targname(handle, dblist, targname, error);
    }

cleanup:
    free(targstring);
    if (ret && access(target, R_OK) == 0) {
        spdlog::warn("'{}' is a file, did you mean {} instead of {}?", target, "-U/--upgrade", "-S/--sync");
    }
    return ret;
}

static int check_syncdbs(alpm_handle_t* handle, size_t need_repos) {
    alpm_list_t* sync_dbs = alpm_get_syncdbs(handle);
    if (need_repos && sync_dbs == nullptr) {
        spdlog::error("error: no usable package repositories configured.");
        return 1;
    }
    return 0;
}

static int trans_init(alpm_handle_t* handle, int flags) {
    int ret;
    check_syncdbs(handle, 0);

    ret = alpm_trans_init(handle, flags);
    if (ret == -1) {
        trans_init_error(handle);
        return -1;
    }
    return 0;
}

static void print_broken_dep(alpm_handle_t* handle, alpm_depmissing_t* miss) {
    const std::string depstring = alpm_dep_compute_string(miss->depend);
    alpm_list_t* trans_add      = alpm_trans_get_add(handle);
    alpm_pkg_t* pkg;
    if (miss->causingpkg == nullptr) {
        /* package being installed/upgraded has unresolved dependency */
        spdlog::warn("unable to satisfy dependency '{}' required by {}",
            depstring, miss->target);
    } else if ((pkg = alpm_pkg_find(trans_add, miss->causingpkg))) {
        /* upgrading a package breaks a local dependency */
        spdlog::warn("installing {} ({}) breaks dependency '{}' required by {}",
            miss->causingpkg, alpm_pkg_get_version(pkg), depstring, miss->target);
    } else {
        /* removing a package breaks a local dependency */
        spdlog::warn("removing {} breaks dependency '{}' required by {}",
            miss->causingpkg, depstring, miss->target);
    }
}

static int sync_prepare_execute(alpm_handle_t* handle, std::string& conflict_msg) {
    alpm_list_t *packages, *data = nullptr;
    int retval{};

    /* Step 2: "compute" the transaction based on targets and flags */
    if (alpm_trans_prepare(handle, &data) == -1) {
        alpm_errno_t err = alpm_errno(handle);
        spdlog::error("error: failed to prepare transaction ({})", alpm_strerror(err));
        switch (err) {
        case ALPM_ERR_PKG_INVALID_ARCH:
            for (alpm_list_t* i = data; i; i = alpm_list_next(i)) {
                const std::string pkg = static_cast<char*>(i->data);
                spdlog::info("package {} does not have a valid architecture", pkg);
            }
            break;
        case ALPM_ERR_UNSATISFIED_DEPS:
            for (alpm_list_t* i = data; i; i = alpm_list_next(i)) {
                auto* miss = static_cast<alpm_depmissing_t*>(i->data);
                print_broken_dep(handle, miss);
                alpm_depmissing_free(miss);
            }
            break;

        case ALPM_ERR_CONFLICTING_DEPS:
            for (alpm_list_t* i = data; i; i = alpm_list_next(i)) {
                auto* conflict = static_cast<alpm_conflict_t*>(i->data);
                /* only print reason if it contains new information */
                if (conflict->reason->mod == ALPM_DEP_MOD_ANY) {
                    conflict_msg += fmt::format("'{}' and '{}' are in conflict\n", conflict->package1, conflict->package2);
                    spdlog::info("'{}' and '{}' are in conflict", conflict->package1, conflict->package2);
                } else {
                    char* reason = alpm_dep_compute_string(conflict->reason);
                    if (reason == nullptr) {
                        conflict_msg += fmt::format("'{}' and '{}' are in conflict (null)\n", conflict->package1, conflict->package2);
                        spdlog::info("'{}' and '{}' are in conflict (null)", conflict->package1, conflict->package2);
                    } else {
                        conflict_msg += fmt::format("'{}' and '{}' are in conflict ({})\n", conflict->package1, conflict->package2, reason);
                        spdlog::info("'{}' and '{}' are in conflict ({})", conflict->package1, conflict->package2, reason);
                        free(reason);
                    }
                }
                alpm_conflict_free(conflict);
            }
            break;
        default:
            break;
        }
        retval = 1;
        goto cleanup;
    }

    packages = alpm_trans_get_add(handle);
    if (packages == nullptr) {
        /* nothing to do: just exit without complaining */
        goto cleanup;
    }

    /* Step 4: release transaction resources */
cleanup:
    alpm_list_free(data);
    if (trans_release(handle) == -1) {
        retval = 1;
    }

    return retval;
}

int sync_trans(alpm_handle_t* handle, const std::vector<std::string>& targets, int flags, std::string& conflict_msg) {
    /* Step 1: create a new transaction... */
    if (trans_init(handle, flags) == -1) {
        return 1;
    }

    /* process targets */
    int retval{};
    for (auto&& targ : targets) {
        if (process_target(handle, targ.c_str(), retval) == 1) {
            retval = 1;
        }
    }

    if (retval) {
        trans_release(handle);
        return retval;
    }

    return sync_prepare_execute(handle, conflict_msg);
}
