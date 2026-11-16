// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// What the root helper and the GUI have to agree on.
namespace helper {

/// The only program besides pacman that the helper starts.
inline constexpr char kRateMirrors[] = "/usr/bin/cachyos-rate-mirrors";

/// Printed before the helper hands over to pacman, followed by the command. Output
/// without it means pkexec never started the helper.
inline constexpr char kStarted[] = "cachyos-pi-helper: running ";

}  // namespace helper
