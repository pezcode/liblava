// file      : liblava-demo/lamp.cpp
// copyright : Copyright (c) 2018-present, Lava Block OÜ and contributors
// license   : MIT; see accompanying LICENSE file

#include <imgui.h>
#include <demo.hpp>

using namespace lava;

int main(int argc, char* argv[]) {
    app app("lava lamp", { argc, argv });

    setup_imgui_font_icons(app.config.imgui_font);

    if (!app.setup())
        return error::not_ready;

    r32 lamp_depth = .03f;
    v4 lamp_color{ .3f, .15f, .15f, 1.f };

    graphics_pipeline::ptr pipeline;
    pipeline_layout::ptr layout;

    app.on_create = [&]() {
        pipeline = make_graphics_pipeline(app.device);
        if (!pipeline->add_shader(file_data("lamp/vertex.spirv"), VK_SHADER_STAGE_VERTEX_BIT))
            return false;

        if (!pipeline->add_shader(file_data("lamp/fragment.spirv"), VK_SHADER_STAGE_FRAGMENT_BIT))
            return false;

        pipeline->add_color_blend_attachment();

        pipeline->set_rasterization_cull_mode(VK_CULL_MODE_FRONT_BIT);
        pipeline->set_rasterization_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);

        layout = make_pipeline_layout();
        layout->add_push_constant_range({ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(r32) * 8 });

        if (!layout->create(app.device))
            return false;

        pipeline->set_layout(layout);
        pipeline->set_auto_size(true);

        pipeline->on_process = [&](VkCommandBuffer cmd_buf) {
            VkViewport viewport = pipeline->get_viewport();

            r32 pc_resolution[2];
            pc_resolution[0] = viewport.width - viewport.x;
            pc_resolution[1] = viewport.height - viewport.y;
            app.device->call().vkCmdPushConstants(cmd_buf, layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT,
                                                  sizeof(r32) * 0, sizeof(r32) * 2, pc_resolution);

            r32 pc_time = to_r32(to_sec(app.run_time.current));
            app.device->call().vkCmdPushConstants(cmd_buf, layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT,
                                                  sizeof(r32) * 2, sizeof(r32), &pc_time);

            app.device->call().vkCmdPushConstants(cmd_buf, layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT,
                                                  sizeof(r32) * 3, sizeof(r32), &lamp_depth);

            app.device->call().vkCmdPushConstants(cmd_buf, layout->get(), VK_SHADER_STAGE_FRAGMENT_BIT,
                                                  sizeof(r32) * 4, sizeof(r32) * 4, glm::value_ptr(lamp_color));

            app.device->call().vkCmdDraw(cmd_buf, 3, 1, 0, 0);
        };

        render_pass::ptr render_pass = app.shading.get_pass();

        if (!pipeline->create(render_pass->get()))
            return false;

        render_pass->add_front(pipeline);

        return true;
    };

    app.on_destroy = [&]() {
        pipeline->destroy();
        layout->destroy();
    };

    bool auto_play = true;

    app.input.key.listeners.add([&](key_event::ref event) {
        if (app.imgui.capture_mouse())
            return false;

        if (event.pressed(key::enter)) {
            auto_play = !auto_play;
            return true;
        }

        return false;
    });

    app.imgui.on_draw = [&]() {
        ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(262, 262), ImGuiCond_FirstUseEver);

        ImGui::Begin(app.get_name());

        ImGui::Dummy({ 0.f, 2.f });
        ImGui::Dummy({ 0.f, 2.f });
        ImGui::SameLine(0.f, 5.f);

        ImGui::TextUnformatted(icon(ICON_FA_LIGHTBULB));

        ImGui::SameLine(0.f, 10.f);

        bool lamp_active = pipeline->activated();
        if (ImGui::Checkbox("active", &lamp_active))
            pipeline->toggle();

        ImGui::SameLine(0.f, 17.f);

        if (ImGui::Button(auto_play ? icon(ICON_FA_PLAY) : icon(ICON_FA_PAUSE)))
            auto_play = !auto_play;

        ImGui::SameLine();

        ImGui::TextUnformatted("auto play");

        ImGui::Separator();

        ImGui::DragFloat("depth", &lamp_depth, 0.0001f, 0.01f, 1.f, "%.4f");
        ImGui::ColorEdit4("color", (r32*) &lamp_color);

        v3 clear_color = app.shading.get_pass()->get_clear_color();
        if (ImGui::ColorEdit3("ground", (r32*) &clear_color))
            app.shading.get_pass()->set_clear_color(clear_color);

        ImGui::DragFloat("speed", &app.run_time.speed, 0.001f, -10.f, 10.f, "x %.3f");

        app.draw_about();

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("enter = auto play");

        ImGui::End();
    };

    struct dimmer {
        explicit dimmer() {
            next_factor();
        }

        r32 update(delta dt, r32 value) {
            r32 next = factor * dt;

            if (add)
                value += next;
            else
                value -= next;

            if (value > max) {
                add = false;
                next_factor();
            } else if (value < min) {
                add = true;
                next_factor();
            }

            return value;
        }

        void next_factor() {
            factor = random(factor_min, factor_max);
        }

        r32 factor = 0.f;
        r32 factor_min = 0.00001f;
        r32 factor_max = 0.0001f;

        bool add = false;
        r32 min = 0.01f;
        r32 max = 0.03f;
    };

    dimmer depth_dimmer;
    dimmer color_dimmer;
    color_dimmer.min = 0.f;
    color_dimmer.max = 1.f;
    color_dimmer.factor_min = 0.0005f;
    color_dimmer.factor_max = 0.02f;
    color_dimmer.next_factor();

    dimmer r_dimmer = color_dimmer;
    r_dimmer.add = true;

    dimmer g_dimmer = color_dimmer;
    g_dimmer.add = true;

    dimmer b_dimmer = color_dimmer;
    dimmer a_dimmer = color_dimmer;
    a_dimmer.min = 0.2f;

    app.on_update = [&](delta dt) {
        if (!auto_play || !pipeline->activated())
            return true;

        lamp_depth = depth_dimmer.update(dt, lamp_depth);

        lamp_color.r = r_dimmer.update(dt, lamp_color.r);
        lamp_color.g = g_dimmer.update(dt, lamp_color.g);
        lamp_color.b = b_dimmer.update(dt, lamp_color.b);
        lamp_color.a = a_dimmer.update(dt, lamp_color.a);

        return true;
    };

    return app.run();
}
