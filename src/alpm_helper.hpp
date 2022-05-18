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

#ifndef ALPM_HELPER_HPP
#define ALPM_HELPER_HPP

#include <alpm.h>

#include <string>
#include <vector>

void setup_alpm(alpm_handle_t* handle);
void destroy_alpm(alpm_handle_t* handle);
void refresh_alpm(alpm_handle_t** handle, alpm_errno_t* err);

int sync_trans(alpm_handle_t* handle, const std::vector<std::string>& targets, int flags, std::string& conflict_msg);

std::string display_targets(alpm_handle_t* handle, bool verbosepkglists, std::string& status_text);

void add_targets_to_install(alpm_handle_t* handle, const std::vector<std::string>& vec);

void add_targets_to_remove(alpm_handle_t* handle, const std::vector<std::string>& vec);

#endif  // ALPM_HELPER_HPP
