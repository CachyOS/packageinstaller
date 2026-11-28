// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics.hpp"

#include <QQuickWindow>

#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif

#include <QString>
#include <QStringList>
#include <QStringView>

#include <algorithm>
#include <array>

namespace {

bool isGnomeName(QStringView name) {
    return name.compare(u"GNOME", Qt::CaseInsensitive) == 0 || name.startsWith(u"GNOME-", Qt::CaseInsensitive);
}

bool isGnomeSession() {
    // NOTE: disable Vulkan on GNOME. Qt Wayland draws no client-side decorations for Vulkan windows
    // (QTBUG-120950), and GNOME doesn't support server-side ones, so the window would stay without title/controls.
    // XDG_CURRENT_DESKTOP is a colon-separated list, e.g. "ubuntu:GNOME" or "GNOME-Classic:GNOME".
    const auto desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts);
    if (std::ranges::any_of(desktops, isGnomeName)) {
        return true;
    }
    // Sessions started without a display manager that sets XDG_CURRENT_DESKTOP
    // (startx, greetd, ly) may still name the session in one of these.
    // DESKTOP_SESSION can also be a path to the session file, e.g. "/usr/share/xsessions/gnome".
    return std::ranges::any_of(std::array{"XDG_SESSION_DESKTOP", "DESKTOP_SESSION"}, [](const char* variable) {
        const auto value = qEnvironmentVariable(variable);
        return isGnomeName(QStringView{value}.mid(value.lastIndexOf(u'/') + 1));
    });
}

bool hasVulkanGpu() {
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    // QML has no fallback of its own once render is enforced,
    // so ask check if it's valid at all.
    QVulkanInstance instance;
    if (!instance.create()) {
        return false;
    }
    uint32_t count  = 0;
    auto* functions = instance.functions();
    if (functions->vkEnumeratePhysicalDevices(instance.vkInstance(), &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    QList<VkPhysicalDevice> devices(count);
    functions->vkEnumeratePhysicalDevices(instance.vkInstance(), &count, devices.data());
    return std::ranges::any_of(devices, [functions](VkPhysicalDevice device) {
        VkPhysicalDeviceProperties properties{};
        functions->vkGetPhysicalDeviceProperties(device, &properties);
        return properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU;
    });
#else
    return false;
#endif
}

}  // namespace

namespace graphics {

void selectBackend() {
    if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND") && !isGnomeSession() && hasVulkanGpu()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    }
}

}  // namespace graphics
