#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct wl_compositor *compositor;
static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, "wl_compositor"))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener listener = {global, removed};
static int report(const char *gate, VkResult result) {
    printf("panvk_probe.%s=%d\n", gate, result);
    fflush(stdout);
    return result == VK_SUCCESS;
}
#include "gpu_readback.h"

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    uint32_t n = 0;
    if (!report("instance_extensions", vkEnumerateInstanceExtensionProperties(NULL, &n, NULL))) return 1;
    VkExtensionProperties *ext = calloc(n, sizeof(*ext));
    if (!ext) return 1;
    VkResult r = vkEnumerateInstanceExtensionProperties(NULL, &n, ext);
    int surface = 0, wayland = 0;
    if (r == VK_SUCCESS) for (uint32_t i = 0; i < n; i++) {
        printf("panvk_probe.instance_extension=%s\n", ext[i].extensionName);
        surface |= !strcmp(ext[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
        wayland |= !strcmp(ext[i].extensionName, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
    }
    free(ext);
    const char *enabled[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .enabledExtensionCount = surface && wayland ? 2 : 0, .ppEnabledExtensionNames = enabled};
    VkInstance instance;
    if (!report("instance_create", vkCreateInstance(&ci, NULL, &instance))) return 1;
    n = 1;
    VkPhysicalDevice physical;
    r = vkEnumeratePhysicalDevices(instance, &n, &physical);
    report("physical_enumerate", r);
    printf("panvk_probe.physical_returned=%u\n", n);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || !n) { vkDestroyInstance(instance, NULL); return 1; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    printf("panvk_probe.device=%s vendor=%x device=%x\n", props.deviceName, props.vendorID, props.deviceID);
    n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, NULL);
    VkQueueFamilyProperties *queues = calloc(n, sizeof(*queues));
    if (!queues) { vkDestroyInstance(instance, NULL); return 1; }
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, queues);
    uint32_t q;
    for (q = 0; q < n; q++) if (queues[q].queueCount &&
        (queues[q].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
        (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) break;
    free(queues);
    if (q == n) { report("graphics_compute_queue", VK_ERROR_FEATURE_NOT_PRESENT); vkDestroyInstance(instance, NULL); return 1; }
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = q, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi};
    VkDevice device;
    if (!report("logical_create_minimal", vkCreateDevice(physical, &di, NULL, &device))) {
        vkDestroyInstance(instance, NULL); return 1;
    }
    if (!probe_gpu_readback(physical, device, q)) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return 2;
    }
    struct wl_display *display = wl_display_connect(NULL);
    struct wl_registry *registry = NULL;
    struct wl_surface *window = NULL;
    if (display && surface && wayland) {
        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &listener, NULL);
        if (wl_display_roundtrip(display) >= 0 && compositor) {
            window = wl_compositor_create_surface(compositor);
            VkWaylandSurfaceCreateInfoKHR wi = {.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                .display = display, .surface = window};
            VkSurfaceKHR target;
            if (report("wayland_surface_create", vkCreateWaylandSurfaceKHR(instance, &wi, NULL, &target))) {
                VkBool32 supported = 0;
                report("surface_support_query", vkGetPhysicalDeviceSurfaceSupportKHR(physical, q, target, &supported));
                printf("panvk_probe.queue_present_supported=%u\n", supported);
                vkDestroySurfaceKHR(instance, target, NULL);
            }
        } else printf("panvk_probe.wayland_surface_create=not-tested (no compositor)\n");
    } else printf("panvk_probe.wayland_surface_create=not-tested (display or extensions unavailable)\n");
    if (window) wl_surface_destroy(window);
    if (compositor) wl_compositor_destroy(compositor);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    printf("panvk_probe.gamescope_and_steam=not-tested\n");
    return 0;
}
