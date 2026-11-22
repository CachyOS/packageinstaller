// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics.hpp"

#include <QQuickWindow>

#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif

#include <algorithm>

namespace {

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
    if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND") && hasVulkanGpu()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    }
}

}  // namespace graphics
