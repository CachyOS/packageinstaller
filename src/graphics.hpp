// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace graphics {

/// Renders with Vulkan when a hardware GPU offers it, otherwise with Qt's default.
/// Unless QSG_RHI_BACKEND is already provided from the environment.
void selectBackend();

}  // namespace graphics
