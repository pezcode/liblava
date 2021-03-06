// file      : liblava/frame/renderer.hpp
// copyright : Copyright (c) 2018-present, Lava Block OÜ and contributors
// license   : MIT; see accompanying LICENSE file

#pragma once

#include <liblava/frame/swapchain.hpp>
#include <optional>

namespace lava {

    using optional_index = std::optional<index>;

    struct renderer : id_obj {
        bool create(swapchain* target);
        void destroy();

        optional_index begin_frame();
        bool end_frame(VkCommandBuffers const& cmd_buffers);

        bool frame(VkCommandBuffers const& cmd_buffers) {
            if (!begin_frame())
                return false;

            return end_frame(cmd_buffers);
        }

        index get_frame() const {
            return current_frame;
        }

        device_ptr get_device() {
            return device;
        }

        using destroy_func = std::function<void()>;
        destroy_func on_destroy;

        bool active = true;

    private:
        device_ptr device = nullptr;
        queue graphics_queue;

        swapchain* target = nullptr;

        index current_frame = 0;
        ui32 queued_frames = 2;

        ui32 current_sync = 0;
        VkFences fences = {};
        VkFences fences_in_use = {};
        VkSemaphores image_acquired_semaphores = {};
        VkSemaphores render_complete_semaphores = {};
    };

} // namespace lava
