#include "renderer.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_vulkan.h"
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>

// ============================================================
//  Debug / validation layer callback
// ============================================================

#ifdef ENABLE_VALIDATION_LAYERS
static constexpr const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};
static constexpr uint32_t VALIDATION_LAYER_COUNT = 1;

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}
#endif

// ============================================================
//  File I/O
// ============================================================

std::vector<char> Renderer::read_file(const char* path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        return {};
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

// ============================================================
//  Buffer creation helper
// ============================================================

bool Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props,
                             VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo info{};
    info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size        = size;
    info.usage       = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &info, nullptr, &buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device_, buffer, &reqs);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = reqs.size;
    alloc.memoryTypeIndex = find_memory_type(reqs.memoryTypeBits, props);

    if (vkAllocateMemory(device_, &alloc, nullptr, &memory) != VK_SUCCESS)
        return false;

    vkBindBufferMemory(device_, buffer, memory, 0);
    return true;
}

// ============================================================
//  Top-level init / shutdown
// ============================================================

bool Renderer::init(SDL_Window* window, const Mesh& level_mesh) {
    window_ = window;

    // Resolve shader directory relative to the executable
    const char* base = SDL_GetBasePath();
    shader_dir_ = base ? std::string(base) + "shaders/" : "shaders/";

    if (!create_instance())              return false;
    if (!setup_debug_messenger())        return false;
    if (!create_surface())               return false;
    if (!pick_physical_device())         return false;
    if (!create_device())                return false;
    if (!create_swapchain())             return false;
    if (!create_image_views())           return false;
    if (!create_depth_resources())       return false;
    if (!create_render_pass())           return false;
    if (!create_descriptor_set_layout()) return false;
    if (!create_pipeline())              return false;
    if (!create_transparent_pipeline())  return false;
    if (!create_particle_pipeline())     return false;
    if (!create_framebuffers())          return false;
    if (!create_command_pool())          return false;
    if (!create_mesh_buffers(level_mesh)) return false;
    if (!create_uniform_buffers())       return false;
    if (!create_descriptor_pool())       return false;
    if (!create_descriptor_sets())       return false;
    if (!create_command_buffers())       return false;
    if (!create_sync_objects())          return false;

    // ImGui descriptor pool (separate from scene descriptors)
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
        };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes    = pool_sizes;

        if (vkCreateDescriptorPool(device_, &pi, nullptr, &imgui_pool_) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create ImGui descriptor pool\n");
            return false;
        }
    }

    fprintf(stdout, "Renderer initialized (%u vertices, %u indices).\n",
            static_cast<uint32_t>(level_mesh.vertices.size()), index_count_);
    return true;
}

void Renderer::wait_idle() {
    vkDeviceWaitIdle(device_);
}

void Renderer::reload_mesh(const Mesh& new_mesh) {
    vkDeviceWaitIdle(device_);

    // Destroy old buffers
    vkDestroyBuffer(device_, vertex_buffer_, nullptr);
    vkFreeMemory(device_, vertex_buffer_memory_, nullptr);
    vkDestroyBuffer(device_, index_buffer_, nullptr);
    vkFreeMemory(device_, index_buffer_memory_, nullptr);

    // Create new ones
    create_mesh_buffers(new_mesh);

    fprintf(stdout, "Renderer reloaded mesh (%zu vertices, %u indices)\n",
            new_mesh.vertices.size(), index_count_);
}

void Renderer::shutdown() {
    vkDeviceWaitIdle(device_);

    for (size_t i = 0; i < image_available_.size(); i++)
        vkDestroySemaphore(device_, image_available_[i], nullptr);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device_, render_finished_[i], nullptr);
        vkDestroyFence(device_, in_flight_[i], nullptr);
    }

    vkDestroyCommandPool(device_, command_pool_, nullptr);

    // Mesh buffers
    vkDestroyBuffer(device_, vertex_buffer_, nullptr);
    vkFreeMemory(device_, vertex_buffer_memory_, nullptr);
    vkDestroyBuffer(device_, index_buffer_, nullptr);
    vkFreeMemory(device_, index_buffer_memory_, nullptr);

    // Entity buffers
    if (entity_vb_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, entity_vb_, nullptr); vkFreeMemory(device_, entity_vb_mem_, nullptr); }
    if (entity_ib_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, entity_ib_, nullptr); vkFreeMemory(device_, entity_ib_mem_, nullptr); }

    // UBOs
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(device_, ubo_buffers_[i], nullptr);
        vkFreeMemory(device_, ubo_memory_[i], nullptr);
    }

    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    vkDestroyDescriptorPool(device_, imgui_pool_, nullptr);

    cleanup_swapchain();

    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    vkDestroyPipeline(device_, transparent_pipeline_, nullptr);
    vkDestroyPipeline(device_, particle_pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, particle_layout_, nullptr);

    // Transparent mesh buffers
    if (transparent_vb_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, transparent_vb_, nullptr); vkFreeMemory(device_, transparent_vb_mem_, nullptr); }
    if (transparent_ib_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, transparent_ib_, nullptr); vkFreeMemory(device_, transparent_ib_mem_, nullptr); }

    // Particle buffers
    if (particle_vb_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, particle_vb_, nullptr); vkFreeMemory(device_, particle_vb_mem_, nullptr); }
    if (particle_ib_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, particle_ib_, nullptr); vkFreeMemory(device_, particle_ib_mem_, nullptr); }
    vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
    vkDestroyRenderPass(device_, render_pass_, nullptr);

    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);

#ifdef ENABLE_VALIDATION_LAYERS
    auto destroy_fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy_fn) destroy_fn(instance_, debug_messenger_, nullptr);
#endif

    vkDestroyInstance(instance_, nullptr);
    fprintf(stdout, "Renderer shut down.\n");
}

// ============================================================
//  1. Instance
// ============================================================

bool Renderer::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "Movement Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
    app_info.pEngineName        = "Custom";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 2, 0);
    app_info.apiVersion         = VK_API_VERSION_1_3;

    uint32_t sdl_ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
    std::vector<const char*> extensions(sdl_exts, sdl_exts + sdl_ext_count);

#ifdef ENABLE_VALIDATION_LAYERS
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo        = &app_info;
    info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

#ifdef ENABLE_VALIDATION_LAYERS
    info.enabledLayerCount   = VALIDATION_LAYER_COUNT;
    info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

    if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed\n");
        return false;
    }
    return true;
}

// ============================================================
//  2. Debug messenger
// ============================================================

bool Renderer::setup_debug_messenger() {
#ifdef ENABLE_VALIDATION_LAYERS
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity  = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType      = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback  = debug_callback;

    auto create_fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (!create_fn || create_fn(instance_, &info, nullptr, &debug_messenger_) != VK_SUCCESS)
        fprintf(stderr, "Warning: failed to set up debug messenger\n");
#endif
    return true;
}

// ============================================================
//  3. Surface
// ============================================================

bool Renderer::create_surface() {
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// ============================================================
//  4. Physical device
// ============================================================

bool Renderer::pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) { fprintf(stderr, "No Vulkan GPU found\n"); return false; }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (auto& dev : devices) {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, qf.data());

        bool gfx = false, pres = false;
        for (uint32_t i = 0; i < qf_count; i++) {
            if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { graphics_family_ = i; gfx = true; }
            VkBool32 ps = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &ps);
            if (ps) { present_family_ = i; pres = true; }
            if (gfx && pres) break;
        }
        if (!gfx || !pres) continue;

        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, exts.data());

        bool has_swap = false;
        for (auto& e : exts)
            if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { has_swap = true; break; }
        if (!has_swap) continue;

        physical_device_ = dev;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        fprintf(stdout, "GPU: %s\n", props.deviceName);
        return true;
    }

    fprintf(stderr, "No suitable GPU found\n");
    return false;
}

// ============================================================
//  5. Logical device + queues
// ============================================================

bool Renderer::create_device() {
    std::set<uint32_t> unique_families = {graphics_family_, present_family_};
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    float priority = 1.0f;

    for (uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queue_infos.push_back(qi);
    }

    const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount    = static_cast<uint32_t>(queue_infos.size());
    info.pQueueCreateInfos       = queue_infos.data();
    info.enabledExtensionCount   = 1;
    info.ppEnabledExtensionNames = device_extensions;
    info.pEnabledFeatures        = &features;

    if (vkCreateDevice(physical_device_, &info, nullptr, &device_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDevice failed\n");
        return false;
    }

    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_,  0, &present_queue_);
    return true;
}

// ============================================================
//  6. Swapchain
// ============================================================

bool Renderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);

    uint32_t fmt_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &fmt_count, formats.data());

    VkSurfaceFormatKHR chosen_format = formats[0];
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen_format = f; break;
        }
    swapchain_format_ = chosen_format.format;

    uint32_t pm_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &pm_count, nullptr);
    std::vector<VkPresentModeKHR> modes(pm_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &pm_count, modes.data());

    VkPresentModeKHR chosen_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { chosen_mode = m; break; }

    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        swapchain_extent_ = caps.currentExtent;
    } else {
        int w, h;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        swapchain_extent_.width  = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
        swapchain_extent_.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{};
    info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface          = surface_;
    info.minImageCount    = image_count;
    info.imageFormat      = chosen_format.format;
    info.imageColorSpace  = chosen_format.colorSpace;
    info.imageExtent      = swapchain_extent_;
    info.imageArrayLayers = 1;
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.preTransform     = caps.currentTransform;
    info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode      = chosen_mode;
    info.clipped          = VK_TRUE;

    uint32_t families[] = {graphics_family_, present_family_};
    if (graphics_family_ != present_family_) {
        info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices   = families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateSwapchainKHR failed\n");
        return false;
    }

    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());
    return true;
}

// ============================================================
//  7. Image views
// ============================================================

bool Renderer::create_image_views() {
    swapchain_views_.resize(swapchain_images_.size());
    for (size_t i = 0; i < swapchain_images_.size(); i++) {
        VkImageViewCreateInfo info{};
        info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image    = swapchain_images_[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format   = swapchain_format_;
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel   = 0;
        info.subresourceRange.levelCount     = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device_, &info, nullptr, &swapchain_views_[i]) != VK_SUCCESS) {
            fprintf(stderr, "vkCreateImageView failed\n");
            return false;
        }
    }
    return true;
}

// ============================================================
//  8. Depth buffer
// ============================================================

bool Renderer::create_depth_resources() {
    // Create depth image
    VkImageCreateInfo img_info{};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = depth_format_;
    img_info.extent.width  = swapchain_extent_.width;
    img_info.extent.height = swapchain_extent_.height;
    img_info.extent.depth  = 1;
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &img_info, nullptr, &depth_image_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create depth image\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, depth_image_, &mem_reqs);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = mem_reqs.size;
    alloc.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &alloc, nullptr, &depth_memory_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate depth image memory\n");
        return false;
    }
    vkBindImageMemory(device_, depth_image_, depth_memory_, 0);

    // Create depth image view
    VkImageViewCreateInfo view_info{};
    view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image    = depth_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = depth_format_;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device_, &view_info, nullptr, &depth_view_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create depth image view\n");
        return false;
    }

    return true;
}

// ============================================================
//  9. Render pass (color + depth)
// ============================================================

bool Renderer::create_render_pass() {
    VkAttachmentDescription attachments[2]{};

    // Color attachment
    attachments[0].format         = swapchain_format_;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    attachments[1].format         = depth_format_;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments    = attachments;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dep;

    if (vkCreateRenderPass(device_, &info, nullptr, &render_pass_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateRenderPass failed\n");
        return false;
    }
    return true;
}

// ============================================================
//  10. Descriptor set layout
// ============================================================

bool Renderer::create_descriptor_set_layout() {
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding         = 0;
    ubo_binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &ubo_binding;

    if (vkCreateDescriptorSetLayout(device_, &info, nullptr, &desc_set_layout_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDescriptorSetLayout failed\n");
        return false;
    }
    return true;
}

// ============================================================
//  11. Graphics pipeline
// ============================================================

VkShaderModule Renderer::load_shader(const char* path) {
    auto code = read_file(path);
    if (code.empty()) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module;
    if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateShaderModule failed for %s\n", path);
        return VK_NULL_HANDLE;
    }
    return module;
}

bool Renderer::create_pipeline() {
    VkShaderModule vert = load_shader((shader_dir_ + "mesh.vert.spv").c_str());
    VkShaderModule frag = load_shader((shader_dir_ + "mesh.frag.spv").c_str());
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // Vertex input: Vertex3D = pos(vec3) + normal(vec3) + color(vec3)
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex3D);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].binding  = 0; attrs[0].location = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = offsetof(Vertex3D, pos);
    attrs[1].binding  = 0; attrs[1].location = 1;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = offsetof(Vertex3D, normal);
    attrs[2].binding  = 0; attrs[2].location = 2;
    attrs[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset   = offsetof(Vertex3D, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dyn_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth   = 1.0f;
    raster.cullMode    = VK_CULL_MODE_BACK_BIT;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test: ON, depth write: ON
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable       = VK_TRUE;
    depth_stencil.depthWriteEnable      = VK_TRUE;
    depth_stencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable     = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    // Push constant: model matrix (mat4 = 64 bytes)
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset     = 0;
    push.size       = sizeof(float) * 16;  // mat4

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &desc_set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push;

    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreatePipelineLayout failed\n");
        return false;
    }

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount          = 2;
    pi.pStages             = stages;
    pi.pVertexInputState   = &vertex_input;
    pi.pInputAssemblyState = &input_assembly;
    pi.pViewportState      = &viewport_state;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState   = &ms;
    pi.pDepthStencilState  = &depth_stencil;
    pi.pColorBlendState    = &blend;
    pi.pDynamicState       = &dynamic;
    pi.layout              = pipeline_layout_;
    pi.renderPass          = render_pass_;
    pi.subpass             = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateGraphicsPipelines failed\n");
        return false;
    }

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return true;
}

// ============================================================
//  12. Framebuffers (color + depth)
// ============================================================

// ============================================================
//  Transparent mesh pipeline (alpha blend, depth test, no depth write, no cull)
//  Uses the same shaders and layout as the opaque mesh pipeline.
// ============================================================

bool Renderer::create_transparent_pipeline() {
    VkShaderModule vert = load_shader((shader_dir_ + "mesh.vert.spv").c_str());
    VkShaderModule frag = load_shader((shader_dir_ + "mesh.frag.spv").c_str());
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // Same vertex layout as opaque mesh
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex3D);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)};

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dyn_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth   = 1.0f;
    raster.cullMode    = VK_CULL_MODE_NONE; // no culling for transparent geo
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test ON, depth write OFF (transparent draws behind opaques but doesn't block)
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable       = VK_TRUE;
    depth_stencil.depthWriteEnable      = VK_FALSE;
    depth_stencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable     = VK_FALSE;

    // Standard alpha blending: src*srcAlpha + dst*(1-srcAlpha)
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable         = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.colorBlendOp        = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_att.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount          = 2;
    pi.pStages             = stages;
    pi.pVertexInputState   = &vertex_input;
    pi.pInputAssemblyState = &input_assembly;
    pi.pViewportState      = &viewport_state;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState   = &ms;
    pi.pDepthStencilState  = &depth_stencil;
    pi.pColorBlendState    = &blend;
    pi.pDynamicState       = &dynamic;
    pi.layout              = pipeline_layout_;   // same layout as opaque
    pi.renderPass          = render_pass_;
    pi.subpass             = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &transparent_pipeline_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateGraphicsPipelines (transparent) failed\n");
        return false;
    }

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return true;
}

// ============================================================
//  Particle pipeline (additive blend, no depth write, cull off)
// ============================================================

bool Renderer::create_particle_pipeline() {
    VkShaderModule vert = load_shader((shader_dir_ + "particle.vert.spv").c_str());
    VkShaderModule frag = load_shader((shader_dir_ + "particle.frag.spv").c_str());
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // Vertex input: ParticleVertex
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(ParticleVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0].binding = 0; attrs[0].location = 0;
    attrs[0].format  = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset  = offsetof(ParticleVertex, center);

    attrs[1].binding = 0; attrs[1].location = 1;
    attrs[1].format  = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset  = offsetof(ParticleVertex, corner);

    attrs[2].binding = 0; attrs[2].location = 2;
    attrs[2].format  = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset  = offsetof(ParticleVertex, color);

    attrs[3].binding = 0; attrs[3].location = 3;
    attrs[3].format  = VK_FORMAT_R32G32_SFLOAT;
    attrs[3].offset  = offsetof(ParticleVertex, params);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 4;
    vertex_input.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dyn_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth   = 1.0f;
    raster.cullMode    = VK_CULL_MODE_NONE;  // billboards are double-sided
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test ON (don't draw behind walls), depth write OFF (transparent)
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable       = VK_TRUE;
    depth_stencil.depthWriteEnable      = VK_FALSE;  // don't write to depth
    depth_stencil.depthCompareOp        = VK_COMPARE_OP_LESS;

    // ADDITIVE blending: src + dst (Blend One One)
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable         = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.colorBlendOp        = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    // Push constant: float time
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset     = 0;
    push.size       = sizeof(float);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &desc_set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push;

    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &particle_layout_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreatePipelineLayout (particle) failed\n");
        return false;
    }

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount          = 2;
    pi.pStages             = stages;
    pi.pVertexInputState   = &vertex_input;
    pi.pInputAssemblyState = &input_assembly;
    pi.pViewportState      = &viewport_state;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState   = &ms;
    pi.pDepthStencilState  = &depth_stencil;
    pi.pColorBlendState    = &blend;
    pi.pDynamicState       = &dynamic;
    pi.layout              = particle_layout_;
    pi.renderPass          = render_pass_;
    pi.subpass             = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &particle_pipeline_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateGraphicsPipelines (particle) failed\n");
        return false;
    }

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return true;
}

// ============================================================
//  Particle buffer upload
// ============================================================

void Renderer::upload_particles(const std::vector<ParticleVertex>& verts,
                                 const std::vector<uint32_t>& indices)
{
    particle_idx_count_ = static_cast<uint32_t>(indices.size());
    if (particle_idx_count_ == 0) return;

    VkDeviceSize vb_size = sizeof(ParticleVertex) * verts.size();
    VkDeviceSize ib_size = sizeof(uint32_t) * indices.size();

    if (vb_size > particle_vb_cap_) {
        if (particle_vb_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, particle_vb_, nullptr); vkFreeMemory(device_, particle_vb_mem_, nullptr); }
        particle_vb_cap_ = vb_size * 2;
        create_buffer(particle_vb_cap_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      particle_vb_, particle_vb_mem_);
    }
    if (ib_size > particle_ib_cap_) {
        if (particle_ib_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, particle_ib_, nullptr); vkFreeMemory(device_, particle_ib_mem_, nullptr); }
        particle_ib_cap_ = ib_size * 2;
        create_buffer(particle_ib_cap_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      particle_ib_, particle_ib_mem_);
    }

    void* data;
    vkMapMemory(device_, particle_vb_mem_, 0, vb_size, 0, &data);
    memcpy(data, verts.data(), vb_size);
    vkUnmapMemory(device_, particle_vb_mem_);

    vkMapMemory(device_, particle_ib_mem_, 0, ib_size, 0, &data);
    memcpy(data, indices.data(), ib_size);
    vkUnmapMemory(device_, particle_ib_mem_);
}

bool Renderer::create_framebuffers() {
    framebuffers_.resize(swapchain_views_.size());
    for (size_t i = 0; i < swapchain_views_.size(); i++) {
        VkImageView views[] = { swapchain_views_[i], depth_view_ };

        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = render_pass_;
        info.attachmentCount = 2;
        info.pAttachments    = views;
        info.width           = swapchain_extent_.width;
        info.height          = swapchain_extent_.height;
        info.layers          = 1;

        if (vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            fprintf(stderr, "vkCreateFramebuffer failed\n");
            return false;
        }
    }
    return true;
}

// ============================================================
//  13. Command pool
// ============================================================

bool Renderer::create_command_pool() {
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = graphics_family_;

    if (vkCreateCommandPool(device_, &info, nullptr, &command_pool_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateCommandPool failed\n");
        return false;
    }
    return true;
}

// ============================================================
//  14. Mesh buffers (vertex + index)
// ============================================================

uint32_t Renderer::find_memory_type(uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    fprintf(stderr, "Failed to find suitable memory type\n");
    return 0;
}

bool Renderer::create_mesh_buffers(const Mesh& mesh) {
    index_count_ = static_cast<uint32_t>(mesh.indices.size());

    // Vertex buffer
    VkDeviceSize vb_size = sizeof(Vertex3D) * mesh.vertices.size();
    if (!create_buffer(vb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       vertex_buffer_, vertex_buffer_memory_))
        return false;

    void* data;
    vkMapMemory(device_, vertex_buffer_memory_, 0, vb_size, 0, &data);
    memcpy(data, mesh.vertices.data(), vb_size);
    vkUnmapMemory(device_, vertex_buffer_memory_);

    // Index buffer
    VkDeviceSize ib_size = sizeof(uint32_t) * mesh.indices.size();
    if (!create_buffer(ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       index_buffer_, index_buffer_memory_))
        return false;

    vkMapMemory(device_, index_buffer_memory_, 0, ib_size, 0, &data);
    memcpy(data, mesh.indices.data(), ib_size);
    vkUnmapMemory(device_, index_buffer_memory_);

    return true;
}

// ============================================================
//  15. Uniform buffers (one per frame in flight, persistently mapped)
// ============================================================

bool Renderer::create_uniform_buffers() {
    VkDeviceSize size = sizeof(SceneData);
    ubo_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    ubo_memory_.resize(MAX_FRAMES_IN_FLIGHT);
    ubo_mapped_.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (!create_buffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           ubo_buffers_[i], ubo_memory_[i]))
            return false;
        vkMapMemory(device_, ubo_memory_[i], 0, size, 0, &ubo_mapped_[i]);
    }
    return true;
}

// ============================================================
//  16. Descriptor pool
// ============================================================

bool Renderer::create_descriptor_pool() {
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = 1;
    info.pPoolSizes    = &pool_size;
    info.maxSets       = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device_, &info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDescriptorPool failed\n");
        return false;
    }
    return true;
}

// ============================================================
//  17. Descriptor sets
// ============================================================

bool Renderer::create_descriptor_sets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, desc_set_layout_);

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = descriptor_pool_;
    alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc.pSetLayouts        = layouts.data();

    descriptor_sets_.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device_, &alloc, descriptor_sets_.data()) != VK_SUCCESS) {
        fprintf(stderr, "vkAllocateDescriptorSets failed\n");
        return false;
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo buf_info{};
        buf_info.buffer = ubo_buffers_[i];
        buf_info.offset = 0;
        buf_info.range  = sizeof(SceneData);

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = descriptor_sets_[i];
        write.dstBinding      = 0;
        write.dstArrayElement = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &buf_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    return true;
}

// ============================================================
//  18. Command buffers
// ============================================================

bool Renderer::create_command_buffers() {
    command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = command_pool_;
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(device_, &info, command_buffers_.data()) != VK_SUCCESS) {
        fprintf(stderr, "vkAllocateCommandBuffers failed\n");
        return false;
    }
    return true;
}

// ============================================================
//  19. Sync objects
// ============================================================

bool Renderer::create_sync_objects() {
    // image_available_ needs one semaphore per swapchain image to avoid
    // reusing a semaphore the presentation engine still holds.
    uint32_t sc_count = static_cast<uint32_t>(swapchain_images_.size());
    image_available_.resize(sc_count);
    render_finished_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_.resize(MAX_FRAMES_IN_FLIGHT);
    image_avail_idx_ = 0;

    VkSemaphoreCreateInfo sem{};
    sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < sc_count; i++) {
        if (vkCreateSemaphore(device_, &sem, nullptr, &image_available_[i]) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create image_available semaphore\n");
            return false;
        }
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device_, &sem, nullptr, &render_finished_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fence, nullptr, &in_flight_[i]) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create sync objects\n");
            return false;
        }
    }
    return true;
}

// ============================================================
//  Swapchain recreation
// ============================================================

void Renderer::cleanup_swapchain() {
    // Depth buffer
    vkDestroyImageView(device_, depth_view_, nullptr);
    vkDestroyImage(device_, depth_image_, nullptr);
    vkFreeMemory(device_, depth_memory_, nullptr);

    for (auto fb : framebuffers_)      vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto view : swapchain_views_) vkDestroyImageView(device_, view, nullptr);
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);

    framebuffers_.clear();
    swapchain_views_.clear();
    swapchain_images_.clear();
}

void Renderer::recreate_swapchain() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    while (w == 0 || h == 0) {
        SDL_WaitEvent(nullptr);
        SDL_GetWindowSizeInPixels(window_, &w, &h);
    }

    vkDeviceWaitIdle(device_);
    cleanup_swapchain();
    create_swapchain();
    create_image_views();
    create_depth_resources();
    create_framebuffers();
    resize_requested_ = false;
}

// ============================================================
//  Draw frame
// ============================================================

void Renderer::upload_entity_mesh(const Mesh& mesh) {
    entity_idx_count_ = static_cast<uint32_t>(mesh.indices.size());
    if (entity_idx_count_ == 0) return;

    VkDeviceSize vb_size = sizeof(Vertex3D) * mesh.vertices.size();
    VkDeviceSize ib_size = sizeof(uint32_t) * mesh.indices.size();

    // Grow buffers if needed (never shrink)
    if (vb_size > entity_vb_capacity_) {
        if (entity_vb_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, entity_vb_, nullptr);
            vkFreeMemory(device_, entity_vb_mem_, nullptr);
        }
        entity_vb_capacity_ = vb_size * 2;  // double to reduce reallocations
        create_buffer(entity_vb_capacity_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      entity_vb_, entity_vb_mem_);
    }
    if (ib_size > entity_ib_capacity_) {
        if (entity_ib_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, entity_ib_, nullptr);
            vkFreeMemory(device_, entity_ib_mem_, nullptr);
        }
        entity_ib_capacity_ = ib_size * 2;
        create_buffer(entity_ib_capacity_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      entity_ib_, entity_ib_mem_);
    }

    // Upload
    void* data;
    vkMapMemory(device_, entity_vb_mem_, 0, vb_size, 0, &data);
    memcpy(data, mesh.vertices.data(), vb_size);
    vkUnmapMemory(device_, entity_vb_mem_);

    vkMapMemory(device_, entity_ib_mem_, 0, ib_size, 0, &data);
    memcpy(data, mesh.indices.data(), ib_size);
    vkUnmapMemory(device_, entity_ib_mem_);
}

void Renderer::upload_viewmodel_mesh(const Mesh& mesh) {
    viewmodel_idx_count_ = static_cast<uint32_t>(mesh.indices.size());
    if (viewmodel_idx_count_ == 0) return;

    VkDeviceSize vb_size = sizeof(Vertex3D) * mesh.vertices.size();
    VkDeviceSize ib_size = sizeof(uint32_t) * mesh.indices.size();

    if (vb_size > viewmodel_vb_cap_) {
        if (viewmodel_vb_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, viewmodel_vb_, nullptr);
            vkFreeMemory(device_, viewmodel_vb_mem_, nullptr);
        }
        viewmodel_vb_cap_ = vb_size * 2;
        create_buffer(viewmodel_vb_cap_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      viewmodel_vb_, viewmodel_vb_mem_);
    }
    if (ib_size > viewmodel_ib_cap_) {
        if (viewmodel_ib_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, viewmodel_ib_, nullptr);
            vkFreeMemory(device_, viewmodel_ib_mem_, nullptr);
        }
        viewmodel_ib_cap_ = ib_size * 2;
        create_buffer(viewmodel_ib_cap_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      viewmodel_ib_, viewmodel_ib_mem_);
    }

    void* data;
    vkMapMemory(device_, viewmodel_vb_mem_, 0, vb_size, 0, &data);
    memcpy(data, mesh.vertices.data(), vb_size);
    vkUnmapMemory(device_, viewmodel_vb_mem_);

    vkMapMemory(device_, viewmodel_ib_mem_, 0, ib_size, 0, &data);
    memcpy(data, mesh.indices.data(), ib_size);
    vkUnmapMemory(device_, viewmodel_ib_mem_);

    viewmodel_uploaded_ = true;
}

void Renderer::upload_transparent_mesh(const Mesh& mesh) {
    transparent_idx_count_ = static_cast<uint32_t>(mesh.indices.size());
    if (transparent_idx_count_ == 0) return;

    VkDeviceSize vb_size = sizeof(Vertex3D) * mesh.vertices.size();
    VkDeviceSize ib_size = sizeof(uint32_t) * mesh.indices.size();

    if (vb_size > transparent_vb_cap_) {
        if (transparent_vb_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, transparent_vb_, nullptr);
            vkFreeMemory(device_, transparent_vb_mem_, nullptr);
        }
        transparent_vb_cap_ = vb_size * 2;
        create_buffer(transparent_vb_cap_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      transparent_vb_, transparent_vb_mem_);
    }
    if (ib_size > transparent_ib_cap_) {
        if (transparent_ib_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, transparent_ib_, nullptr);
            vkFreeMemory(device_, transparent_ib_mem_, nullptr);
        }
        transparent_ib_cap_ = ib_size * 2;
        create_buffer(transparent_ib_cap_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      transparent_ib_, transparent_ib_mem_);
    }

    void* data;
    vkMapMemory(device_, transparent_vb_mem_, 0, vb_size, 0, &data);
    memcpy(data, mesh.vertices.data(), vb_size);
    vkUnmapMemory(device_, transparent_vb_mem_);

    vkMapMemory(device_, transparent_ib_mem_, 0, ib_size, 0, &data);
    memcpy(data, mesh.indices.data(), ib_size);
    vkUnmapMemory(device_, transparent_ib_mem_);
}

void Renderer::draw_frame(const SceneData& scene, const Mesh* entity_mesh,
                          const std::vector<ParticleVertex>* particle_verts,
                          const std::vector<uint32_t>* particle_indices,
                          float total_time,
                          const Mesh* viewmodel_mesh,
                          const HMM_Mat4* viewmodel_model,
                          const SceneData* viewmodel_scene,
                          const HMM_Mat4* viewmodel_mag_model,
                          uint32_t mag_index_start,
                          uint32_t mag_index_count,
                          const Mesh* transparent_mesh) {
    vkWaitForFences(device_, 1, &in_flight_[current_frame_], VK_TRUE, UINT64_MAX);

    // Use a separate semaphore per swapchain image to avoid reusing one
    // the presentation engine still holds.
    uint32_t sem_idx = image_avail_idx_;
    image_avail_idx_ = (image_avail_idx_ + 1) % static_cast<uint32_t>(image_available_.size());

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX,
        image_available_[sem_idx], VK_NULL_HANDLE, &image_index
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR || resize_requested_) {
        recreate_swapchain();
        return;
    }

    vkResetFences(device_, 1, &in_flight_[current_frame_]);

    // Update UBO for this frame
    memcpy(ubo_mapped_[current_frame_], &scene, sizeof(SceneData));

    // Record command buffer
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin);

    VkClearValue clear_values[2]{};
    clear_values[0].color        = {{0.02f, 0.02f, 0.03f, 1.0f}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = render_pass_;
    rp.framebuffer       = framebuffers_[image_index];
    rp.renderArea.extent = swapchain_extent_;
    rp.clearValueCount   = 2;
    rp.pClearValues      = clear_values;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.width    = static_cast<float>(swapchain_extent_.width);
    viewport.height   = static_cast<float>(swapchain_extent_.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapchain_extent_;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind descriptor set (UBO)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1,
                            &descriptor_sets_[current_frame_], 0, nullptr);

    // Bind mesh
    VkBuffer     vbufs[] = {vertex_buffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);

    // Push model matrix (identity — level is at world origin)
    HMM_Mat4 model = HMM_M4D(1.0f);
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(HMM_Mat4), &model);

    // Draw level
    vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);

    // Draw entities
    if (entity_mesh && !entity_mesh->indices.empty()) {
        upload_entity_mesh(*entity_mesh);

        if (entity_idx_count_ > 0) {
            VkBuffer     ebufs[] = {entity_vb_};
            VkDeviceSize eoffs[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, ebufs, eoffs);
            vkCmdBindIndexBuffer(cmd, entity_ib_, 0, VK_INDEX_TYPE_UINT32);

            // Identity model matrix (entities are already in world space)
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(HMM_Mat4), &model);

            vkCmdDrawIndexed(cmd, entity_idx_count_, 1, 0, 0, 0);
        }
    }

    // Draw transparent geometry (after all opaques, before viewmodel)
    if (transparent_mesh && !transparent_mesh->indices.empty()) {
        upload_transparent_mesh(*transparent_mesh);

        if (transparent_idx_count_ > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparent_pipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 0, 1,
                                    &descriptor_sets_[current_frame_], 0, nullptr);

            VkBuffer     tbufs[] = {transparent_vb_};
            VkDeviceSize toffs[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, tbufs, toffs);
            vkCmdBindIndexBuffer(cmd, transparent_ib_, 0, VK_INDEX_TYPE_UINT32);

            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(HMM_Mat4), &model);

            vkCmdDrawIndexed(cmd, transparent_idx_count_, 1, 0, 0, 0);

            // Re-bind opaque pipeline for viewmodel
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 0, 1,
                                    &descriptor_sets_[current_frame_], 0, nullptr);
        }
    }

    // Draw viewmodel (clear depth first so gun renders on top of world)
    if (viewmodel_mesh && viewmodel_model && viewmodel_scene &&
        !viewmodel_mesh->indices.empty()) {
        upload_viewmodel_mesh(*viewmodel_mesh);

        if (viewmodel_idx_count_ > 0) {
            // Clear depth buffer only (so viewmodel draws on top of everything)
            VkClearAttachment clear_depth{};
            clear_depth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            clear_depth.clearValue.depthStencil = {1.0f, 0};
            VkClearRect clear_rect{};
            clear_rect.rect.extent = swapchain_extent_;
            clear_rect.baseArrayLayer = 0;
            clear_rect.layerCount = 1;
            vkCmdClearAttachments(cmd, 1, &clear_depth, 1, &clear_rect);

            // Update UBO with viewmodel scene data (tighter near plane)
            memcpy(ubo_mapped_[current_frame_], viewmodel_scene, sizeof(SceneData));

            // Re-bind opaque pipeline + descriptor set
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 0, 1,
                                    &descriptor_sets_[current_frame_], 0, nullptr);

            VkBuffer     vmbufs[] = {viewmodel_vb_};
            VkDeviceSize vmoffs[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vmbufs, vmoffs);
            vkCmdBindIndexBuffer(cmd, viewmodel_ib_, 0, VK_INDEX_TYPE_UINT32);

            if (mag_index_count > 0 && viewmodel_mag_model) {
                // Draw body (everything except mag range) with body transform
                vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(HMM_Mat4), viewmodel_model);

                // Draw indices before the mag range
                if (mag_index_start > 0) {
                    vkCmdDrawIndexed(cmd, mag_index_start, 1, 0, 0, 0);
                }
                // Draw indices after the mag range
                uint32_t after_mag = mag_index_start + mag_index_count;
                if (after_mag < viewmodel_idx_count_) {
                    vkCmdDrawIndexed(cmd, viewmodel_idx_count_ - after_mag, 1, after_mag, 0, 0);
                }

                // Draw mag with its own transform
                vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(HMM_Mat4), viewmodel_mag_model);
                vkCmdDrawIndexed(cmd, mag_index_count, 1, mag_index_start, 0, 0);
            } else {
                // No mag separation — draw entire viewmodel with one transform
                vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(HMM_Mat4), viewmodel_model);
                vkCmdDrawIndexed(cmd, viewmodel_idx_count_, 1, 0, 0, 0);
            }

            // Restore original scene UBO for subsequent draws (particles, etc.)
            memcpy(ubo_mapped_[current_frame_], &scene, sizeof(SceneData));
        }
    }

    // Draw particles (additive blend, after opaques, before ImGui)
    if (particle_verts && particle_indices && !particle_indices->empty()) {
        upload_particles(*particle_verts, *particle_indices);

        if (particle_idx_count_ > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particle_pipeline_);

            // Re-bind descriptor set (UBO) for particle shaders
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    particle_layout_, 0, 1,
                                    &descriptor_sets_[current_frame_], 0, nullptr);

            // Push time
            vkCmdPushConstants(cmd, particle_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(float), &total_time);

            VkBuffer     pbufs[] = {particle_vb_};
            VkDeviceSize poffs[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, pbufs, poffs);
            vkCmdBindIndexBuffer(cmd, particle_ib_, 0, VK_INDEX_TYPE_UINT32);

            // Re-set viewport/scissor for particle pipeline
            VkViewport vp{};
            vp.width    = static_cast<float>(swapchain_extent_.width);
            vp.height   = static_cast<float>(swapchain_extent_.height);
            vp.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &vp);

            VkRect2D sc{};
            sc.extent = swapchain_extent_;
            vkCmdSetScissor(cmd, 0, 1, &sc);

            vkCmdDrawIndexed(cmd, particle_idx_count_, 1, 0, 0, 0);
        }
    }

    // Draw ImGui on top
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit
    VkSemaphore          wait_sems[]   = {image_available_[sem_idx]};
    VkSemaphore          signal_sems[] = {render_finished_[current_frame_]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = wait_sems;
    submit.pWaitDstStageMask    = wait_stages;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = signal_sems;

    vkQueueSubmit(graphics_queue_, 1, &submit, in_flight_[current_frame_]);

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = signal_sems;
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchain_;
    present.pImageIndices      = &image_index;

    result = vkQueuePresentKHR(present_queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        recreate_swapchain();

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}
