#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vector>
#include <cstdint>

#include "vendor/HandmadeMath.h"
#include "mesh.h"

// Must match the UBO layout in the shaders (std140)
struct SceneData {
    HMM_Mat4 view;
    HMM_Mat4 projection;
    HMM_Vec4 light_dir;    // xyz = direction toward light
    HMM_Vec4 camera_pos;   // xyz = world position
};

class Renderer {
public:
    bool init(SDL_Window* window, const Mesh& level_mesh);
    void shutdown();
    void draw_frame(const SceneData& scene);
    void on_resize() { resize_requested_ = true; }

private:
    SDL_Window* window_ = nullptr;

    // --- Core ---
    VkInstance               instance_        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR             surface_         = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device_ = VK_NULL_HANDLE;
    VkDevice                 device_          = VK_NULL_HANDLE;

    // --- Queues ---
    VkQueue  graphics_queue_  = VK_NULL_HANDLE;
    VkQueue  present_queue_   = VK_NULL_HANDLE;
    uint32_t graphics_family_ = 0;
    uint32_t present_family_  = 0;

    // --- Swapchain ---
    VkSwapchainKHR             swapchain_        = VK_NULL_HANDLE;
    VkFormat                   swapchain_format_  = VK_FORMAT_UNDEFINED;
    VkExtent2D                 swapchain_extent_  = {0, 0};
    std::vector<VkImage>       swapchain_images_;
    std::vector<VkImageView>   swapchain_views_;
    std::vector<VkFramebuffer> framebuffers_;

    // --- Depth buffer ---
    VkFormat       depth_format_ = VK_FORMAT_D32_SFLOAT;
    VkImage        depth_image_  = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView    depth_view_   = VK_NULL_HANDLE;

    // --- Pipeline ---
    VkRenderPass          render_pass_     = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_        = VK_NULL_HANDLE;

    // --- Descriptors + UBOs ---
    VkDescriptorPool             descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;
    std::vector<VkBuffer>        ubo_buffers_;
    std::vector<VkDeviceMemory>  ubo_memory_;
    std::vector<void*>           ubo_mapped_;   // persistently mapped

    // --- Commands ---
    VkCommandPool                command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    // --- Mesh buffers ---
    VkBuffer       vertex_buffer_        = VK_NULL_HANDLE;
    VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;
    VkBuffer       index_buffer_         = VK_NULL_HANDLE;
    VkDeviceMemory index_buffer_memory_  = VK_NULL_HANDLE;
    uint32_t       index_count_          = 0;

    // --- Sync ---
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> image_available_;
    std::vector<VkSemaphore> render_finished_;
    std::vector<VkFence>     in_flight_;
    uint32_t current_frame_ = 0;

    bool resize_requested_ = false;

    // --- Init steps ---
    bool create_instance();
    bool setup_debug_messenger();
    bool create_surface();
    bool pick_physical_device();
    bool create_device();
    bool create_swapchain();
    bool create_image_views();
    bool create_depth_resources();
    bool create_render_pass();
    bool create_descriptor_set_layout();
    bool create_pipeline();
    bool create_framebuffers();
    bool create_command_pool();
    bool create_mesh_buffers(const Mesh& mesh);
    bool create_uniform_buffers();
    bool create_descriptor_pool();
    bool create_descriptor_sets();
    bool create_command_buffers();
    bool create_sync_objects();

    void cleanup_swapchain();
    void recreate_swapchain();

    // --- Helpers ---
    VkShaderModule    load_shader(const char* path);
    uint32_t          find_memory_type(uint32_t filter, VkMemoryPropertyFlags props);
    std::vector<char> read_file(const char* path);
    bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags props,
                       VkBuffer& buffer, VkDeviceMemory& memory);
};
