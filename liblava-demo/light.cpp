// file      : liblava-demo/light.cpp
// copyright : Copyright (c) 2018-present, Lava Block OÜ and contributors
// license   : MIT; see accompanying LICENSE file

#include <imgui.h>
#include <demo.hpp>

using namespace lava;

// structs for interfacing with shaders
namespace glsl {
    using namespace glm;
    using uint = uint32_t;
#include "res/light/data.inc"
} // namespace glsl

glsl::UboData g_ubo;

struct gbuffer_attachment {
    enum type : uint32_t {
        albedo = 0,
        normal,
        metallic_roughness,
        depth,
        count
    };

    VkFormats requested_formats;
    VkImageUsageFlags usage;
    image::ptr image_handle;
    attachment::ptr renderpass_attachment;
    VkAttachmentReference subpass_reference;

    bool create(uint32_t index);
};

using attachment_array = std::array<gbuffer_attachment, gbuffer_attachment::count>;
attachment_array g_attachments = {
    gbuffer_attachment{ { VK_FORMAT_R8G8B8A8_UNORM }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT },
    gbuffer_attachment{ { VK_FORMAT_R16G16B16A16_SFLOAT }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT },
    gbuffer_attachment{ { VK_FORMAT_R16G16_SFLOAT }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT },
    gbuffer_attachment{ { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT },
};

using light_array = std::array<glsl::LightData, 3>;
const light_array g_lights = {
    glsl::LightData{ { 2.0f, 2.0f, -0.5f }, 5.0f, { 1000.0f, 100.0f, 100.0f } },
    glsl::LightData{ { -2.0f, -2.0f, -0.5f }, 5.0f, { 100.0f, 1000.0f, 100.0f } },
    glsl::LightData{ { 0.0f, 2.0f, -0.5f }, 5.0f, { 100.0f, 100.0f, 1000.0f } }
};

app* g_app = nullptr;

render_pass::ptr create_gbuffer_renderpass(attachment_array& attachments);

int main(int argc, char* argv[]) {
    app app("lava light", { argc, argv });
    if (!app.setup())
        return error::not_ready;

    target_callback resize_callback;
    app.target->add_callback(&resize_callback);

    g_app = &app;

    // create global immutable resources
    // destroyed in app.add_run_end

    mesh::ptr quad = create_mesh(app.device, mesh_type::quad);
    if (!quad)
        return error::create_failed;

    std::array<mat4, 3> quad_instances;

    texture::ptr tex_normal = load_texture(app.device, "light/normal.png");
    texture::ptr tex_roughness = load_texture(app.device, "light/roughness.png");
    if (!tex_normal || !tex_roughness)
        return error::create_failed;

    app.staging.add(tex_normal);
    app.staging.add(tex_roughness);

    buffer ubo_buffer;
    if (!ubo_buffer.create_mapped(app.device, nullptr, sizeof(g_ubo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
        return error::create_failed;

    buffer light_buffer;
    if (!light_buffer.create_mapped(app.device, g_lights.data(), sizeof(g_lights), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        return error::create_failed;

    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
    };
    VkSampler sampler;
    if (!app.device->vkCreateSampler(&sampler_info, &sampler))
        return error::create_failed;

    // pipeline-specific resources
    // created in app.on_create, destroyed in app.on_destroy

    descriptor::pool descriptor_pool;

    render_pass::ptr gbuffer_renderpass = make_render_pass(app.device);
    descriptor::ptr gbuffer_set_layout = make_descriptor();
    pipeline_layout::ptr gbuffer_pipeline_layout = make_pipeline_layout();
    graphics_pipeline::ptr gbuffer_pipeline = make_graphics_pipeline(app.device);
    VkDescriptorSet gbuffer_set = VK_NULL_HANDLE;

    descriptor::ptr lighting_set_layout = make_descriptor();
    pipeline_layout::ptr lighting_pipeline_layout = make_pipeline_layout();
    graphics_pipeline::ptr lighting_pipeline = make_graphics_pipeline(app.device);
    VkDescriptorSet lighting_set = VK_NULL_HANDLE;

    app.on_create = [&]() {
        const VkDescriptorPoolSizes pool_sizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * 2 }, // one uniform buffer for each pass (gbuffer + lighting)
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }, // light buffer
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 /* normal + roughness texture */ + g_attachments.size() },
        };
        constexpr ui32 max_sets = 2; // one for each pass
        if (!descriptor_pool.create(app.device, pool_sizes, max_sets))
            return false;

        // gbuffer pass

        gbuffer_set_layout->add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
        gbuffer_set_layout->add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        gbuffer_set_layout->add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        if (!gbuffer_set_layout->create(app.device))
            return false;
        gbuffer_set = gbuffer_set_layout->allocate(descriptor_pool.get());
        if (!gbuffer_set)
            return false;

        std::vector<VkWriteDescriptorSet> gbuffer_write_sets;
        for (const descriptor::binding::ptr& binding : gbuffer_set_layout->get_bindings()) {
            const VkDescriptorSetLayoutBinding& info = binding->get();
            gbuffer_write_sets.push_back({ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                           .dstSet = gbuffer_set,
                                           .dstBinding = info.binding,
                                           .descriptorCount = info.descriptorCount,
                                           .descriptorType = info.descriptorType });
        }

        gbuffer_write_sets[0].pBufferInfo = ubo_buffer.get_descriptor_info();
        gbuffer_write_sets[1].pImageInfo = tex_normal->get_descriptor_info();
        gbuffer_write_sets[2].pImageInfo = tex_roughness->get_descriptor_info();

        app.device->vkUpdateDescriptorSets(gbuffer_write_sets.size(), gbuffer_write_sets.data());

        gbuffer_pipeline_layout->add(gbuffer_set_layout);
        gbuffer_pipeline_layout->add_push_constant_range({ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glsl::PushConstantData) });
        if (!gbuffer_pipeline_layout->create(app.device))
            return false;

        const VkPipelineColorBlendAttachmentState gbuffer_blend_state = {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        if (!gbuffer_pipeline->add_shader(file_data("light/gbuffer.vertex.spirv"), VK_SHADER_STAGE_VERTEX_BIT))
            return false;
        if (!gbuffer_pipeline->add_shader(file_data("light/gbuffer.fragment.spirv"), VK_SHADER_STAGE_FRAGMENT_BIT))
            return false;
        for (size_t i = 0; i < g_attachments.size() - 1; i++) {
            gbuffer_pipeline->add_color_blend_attachment(gbuffer_blend_state);
        }
        gbuffer_pipeline->set_depth_test_and_write(true, true);
        gbuffer_pipeline->set_depth_compare_op(VK_COMPARE_OP_LESS);
        gbuffer_pipeline->set_rasterization_cull_mode(VK_CULL_MODE_NONE);
        gbuffer_pipeline->set_vertex_input_binding({ 0, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX });
        gbuffer_pipeline->set_vertex_input_attributes({
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, to_ui32(offsetof(vertex, position)) },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT, to_ui32(offsetof(vertex, uv)) },
            { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, to_ui32(offsetof(vertex, normal)) },
        });
        gbuffer_pipeline->set_layout(gbuffer_pipeline_layout);
        gbuffer_pipeline->set_auto_size(true);

        gbuffer_pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
            scoped_label label(cmd_buf, "gbuffer");

            gbuffer_pipeline_layout->bind(cmd_buf, gbuffer_set);
            quad->bind(cmd_buf);

            for (size_t i = 0; i < quad_instances.size(); i++) {
                const glsl::PushConstantData pc = {
                    .model = quad_instances[i],
                    .color = v3(1.0f),
                    .metallic = float(i % 2)
                };
                app.device->call().vkCmdPushConstants(cmd_buf, gbuffer_pipeline_layout->get(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                      0, sizeof(pc), &pc);
                quad->draw(cmd_buf);
            }
        };

        gbuffer_renderpass = create_gbuffer_renderpass(g_attachments);
        gbuffer_renderpass->add_front(gbuffer_pipeline);

        // lighting pass

        for (size_t i = 0; i < g_attachments.size(); i++) {
            lighting_set_layout->add_binding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
        }
        lighting_set_layout->add_binding(g_attachments.size() + 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
        lighting_set_layout->add_binding(g_attachments.size() + 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
        if (!lighting_set_layout->create(app.device))
            return false;
        lighting_set = lighting_set_layout->allocate(descriptor_pool.get());
        if (!lighting_set)
            return false;

        lighting_pipeline_layout->add(lighting_set_layout);
        if (!lighting_pipeline_layout->create(app.device))
            return false;

        const VkPipelineColorBlendAttachmentState lighting_blend_state = {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        if (!lighting_pipeline->add_shader(file_data("light/lighting.vertex.spirv"), VK_SHADER_STAGE_VERTEX_BIT))
            return false;
        if (!lighting_pipeline->add_shader(file_data("light/lighting.fragment.spirv"), VK_SHADER_STAGE_FRAGMENT_BIT))
            return false;
        lighting_pipeline->add_color_blend_attachment(lighting_blend_state);
        lighting_pipeline->set_rasterization_cull_mode(VK_CULL_MODE_NONE);
        lighting_pipeline->set_layout(lighting_pipeline_layout);
        lighting_pipeline->set_auto_size(true);

        lighting_pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
            scoped_label label(cmd_buf, "lighting");

            lighting_pipeline_layout->bind(cmd_buf, lighting_set);
            app.device->call().vkCmdDraw(cmd_buf, 3, 1, 0, 0);
        };

        // use lava's default backbuffer renderpass
        render_pass::ptr lighting_renderpass = app.shading.get_pass();
        lighting_renderpass->add_front(lighting_pipeline);

        // the resize callback creates the gbuffer images and renderpass, call it once manually
        if (!resize_callback.on_created({}, { { 0, 0 }, app.target->get_size() }))
            return false;

        // renderpasses have been created at this point, actually create the pipelines
        if (!gbuffer_pipeline->create(gbuffer_renderpass->get()))
            return false;
        if (!lighting_pipeline->create(lighting_renderpass->get()))
            return false;

        return true;
    };

    app.on_process = [&](VkCommandBuffer cmd_buf, lava::index frame) {
        scoped_label label(cmd_buf, "on_process");

        // start custom renderpass, run on_process() for each pipeline added to the renderpass
        gbuffer_renderpass->process(cmd_buf, 0);
    };

    app.on_update = [&](delta dt) {
        float seconds = to_delta(app.get_running_time());
        float left = -1.5f;
        for (size_t i = 0; i < quad_instances.size(); i++) {
            float x = left + 1.5 * i;
            mat4 model = mat4(1.0f);
            model = glm::translate(model, { x, 0.0f, 0.0f });
            model = glm::rotate(model, glm::radians(std::fmod(seconds * 45.0f, 360.0f)), { 0.0f, 1.0f, 0.0f });
            model = glm::scale(model, { 0.75f, 0.75f, 0.75f });
            quad_instances[i] = model;
        }

        return true;
    };

    // handle backbuffer resize

    resize_callback.on_created = [&](VkAttachmentsRef, rect area) {
        // update uniform buffer
        g_ubo.camPos = { 0.0f, 0.0f, -3.0f };
        g_ubo.view = glm::lookAtLH(g_ubo.camPos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
        g_ubo.projection = perspective_matrix(area.get_size(), 90.0f, 10.0f);
        g_ubo.invProjection = glm::inverse(g_ubo.projection);
        g_ubo.resolution = area.get_size();
        g_ubo.lightCount = g_lights.size();
        *(decltype(g_ubo)*) ubo_buffer.get_mapped_data() = g_ubo;

        // recreate gbuffer attachments and collect views for framebuffer creation
        VkImageViews views;
        for (gbuffer_attachment& att : g_attachments) {
            if (!att.image_handle->create(app.device, area.get_size()))
                return false;
            views.push_back(att.image_handle->get_view());
        }

        // update lighting descriptor set with updated gbuffer image handles
        std::vector<VkWriteDescriptorSet> lighting_write_sets;
        for (const descriptor::binding::ptr& binding : lighting_set_layout->get_bindings()) {
            const VkDescriptorSetLayoutBinding& info = binding->get();
            lighting_write_sets.push_back({ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                            .dstSet = lighting_set,
                                            .dstBinding = info.binding,
                                            .descriptorCount = info.descriptorCount,
                                            .descriptorType = info.descriptorType });
        }

        std::array<VkDescriptorImageInfo, g_attachments.size()> lighting_images;
        for (size_t i = 0; i < g_attachments.size(); i++) {
            lighting_images[i] = {
                .sampler = sampler,
                .imageView = g_attachments[i].image_handle->get_view(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            lighting_write_sets[i].pImageInfo = &lighting_images[i];
        }
        lighting_write_sets[g_attachments.size() + 0].pBufferInfo = ubo_buffer.get_descriptor_info();
        lighting_write_sets[g_attachments.size() + 1].pBufferInfo = light_buffer.get_descriptor_info();

        app.device->vkUpdateDescriptorSets(lighting_write_sets.size(), lighting_write_sets.data());

        if (gbuffer_renderpass->get() == VK_NULL_HANDLE)
            return gbuffer_renderpass->create({ views }, area);
        else
            return gbuffer_renderpass->on_created({ views }, area); // creates VkFramebuffer
    };

    resize_callback.on_destroyed = [&]() {
        app.device->wait_for_idle();
        gbuffer_renderpass->on_destroyed(); // destroys VkFramebuffer
        for (gbuffer_attachment& att : g_attachments) {
            att.image_handle->destroy();
        }
    };

    app.imgui.on_draw = [&]() {
        ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(262, 262), ImGuiCond_FirstUseEver);

        ImGui::Begin(app.get_name());

        app.draw_about();

        ImGui::End();
    };

    app.on_destroy = [&]() {
        app.target->remove_callback(&resize_callback);
        resize_callback.on_destroyed();

        lighting_pipeline->destroy();
        lighting_pipeline_layout->destroy();
        lighting_set_layout->destroy();

        gbuffer_pipeline->destroy();
        gbuffer_pipeline_layout->destroy();
        gbuffer_set_layout->destroy();
        gbuffer_renderpass->destroy();

        descriptor_pool.destroy();
    };

    app.add_run_end([&]() {
        app.device->vkDestroySampler(sampler);
        sampler = VK_NULL_HANDLE;

        light_buffer.destroy();
        ubo_buffer.destroy();

        tex_roughness->destroy();
        tex_normal->destroy();

        quad->destroy();
    });

    return app.run();
}

bool gbuffer_attachment::create(uint32_t index) {
    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    std::optional<VkFormat> format = get_supported_format(g_app->device->get_vk_physical_device(), requested_formats, usage);
    if (!format.has_value())
        return false;

    image_handle = make_image(*format);
    image_handle->set_usage(usage);

    renderpass_attachment = make_attachment(*format);
    renderpass_attachment->set_op(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
    renderpass_attachment->set_stencil_op(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE);
    renderpass_attachment->set_layouts(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    subpass_reference.attachment = index;
    subpass_reference.layout = (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
                                   ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                   : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    return true;
}

render_pass::ptr create_gbuffer_renderpass(attachment_array& attachments) {
    VkClearValues clear_values(attachments.size(), { .color = { 1.0f, 0.0f, 0.0f, 1.0f } });
    clear_values[gbuffer_attachment::depth] = { .depthStencil = { 1.0f, 0 } };

    render_pass::ptr pass = make_render_pass(g_app->device);
    pass->set_clear_values(clear_values);

    VkAttachmentReferences color_attachments;
    for (uint32_t i = 0; i < gbuffer_attachment::count; i++) {
        if (!attachments[i].create(i))
            return nullptr;
        pass->add(attachments[i].renderpass_attachment);
        if (i != gbuffer_attachment::depth)
            color_attachments.push_back(attachments[i].subpass_reference);
    }

    subpass::ptr sub = make_subpass();
    sub->set_color_attachments(color_attachments);
    sub->set_depth_stencil_attachment(attachments[gbuffer_attachment::depth].subpass_reference);
    pass->add(sub);

    subpass_dependency::ptr dependency = make_subpass_dependency(VK_SUBPASS_EXTERNAL, 0);
    // wait for previous fragment shader to finish reading before clearing attachments
    dependency->set_stage_mask(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    // we need a memory barrier because this isn't a standard write-after-read hazard
    // subpass deps have an implicit attachment layout transition, so the dst access mask must be correct
    dependency->set_access_mask(0,
                                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    pass->add(dependency);

    dependency = make_subpass_dependency(pass->get_subpass_count() - 1, VK_SUBPASS_EXTERNAL);
    // don't run any fragment shader (sample attachments) before we're done writing to attachments
    dependency->set_stage_mask(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    // make attachment writes visible to subsequent reads
    dependency->set_access_mask(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT);
    pass->add(dependency);

    return pass;
}
