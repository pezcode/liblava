// file      : liblava/frame/render_target.hpp
// copyright : Copyright (c) 2018-present, Lava Block OÜ
// license   : MIT; see accompanying LICENSE file

#pragma once

#include <liblava/frame/swapchain.hpp>
#include <liblava/fwd.hpp>

namespace lava {

struct render_target {

    using ptr = std::shared_ptr<render_target>;

    struct callback {

        using list = std::vector<callback*>;

        using created_func = std::function<bool(VkImageViews const&, uv2)>;
        created_func on_created;

        using destroyed_func = std::function<void()>;
        destroyed_func on_destroyed;
    };

    bool create(device* device, VkSurfaceKHR surface, uv2 size);
    void destroy();

    void set_clear_color(v3 value = default_color);
    VkClearColorValue get_clear_color() const { return clear_color; }

    uv2 get_size() const { return _swapchain.get_size(); }
    bool resize(uv2 new_size) { return _swapchain.resize(new_size); }

    ui32 get_frame_count() const { return _swapchain.get_backbuffer_count(); }

    bool must_reload() const { return _swapchain.must_reload(); }
    void reload() { _swapchain.resize(_swapchain.get_size()); }

    device* get_device() { return _swapchain.get_device(); }
    swapchain* get_swapchain() { return &_swapchain; }

    VkFormat get_format() const { return _swapchain.get_format(); }

    image::list const& get_backbuffers() const { return _swapchain.get_backbuffers(); }
    inline image::ptr get_backbuffer(index index) { 

        auto& backbuffers = get_backbuffers();
        if (index >= backbuffers.size())
            return nullptr;

        return backbuffers.at(index);
    }

    inline VkImage get_backbuffer_image(index index) {

        auto result = get_backbuffer(index);
        return result ? result->get() : nullptr;
    }

    void add_target_callback(callback* callback) { target_callbacks.push_back(callback); }

    using swapchain__start_func = std::function<bool()>;
    swapchain__start_func on_swapchain_start;

    using swapchain_stop_func = std::function<void()>;
    swapchain_stop_func on_swapchain_stop;

    using create_attachments_func = std::function<VkImageViews()>;
    create_attachments_func on_create_attachments;

    using destroy_attachments_func = std::function<void()>;
    destroy_attachments_func on_destroy_attachments;

private:
    swapchain _swapchain;
    swapchain::callback _swapchain_callback;
    VkClearColorValue clear_color = {};

    callback::list target_callbacks;
};

render_target::ptr create_target(window* window, device* device);

} // lava
