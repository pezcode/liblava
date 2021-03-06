// file      : liblava/core/version.hpp
// copyright : Copyright (c) 2018-present, Lava Block OÜ and contributors
// license   : MIT; see accompanying LICENSE file

#pragma once

#include <liblava/core/types.hpp>

namespace lava {

    struct internal_version {
        i32 major = LIBLAVA_VERSION_MAJOR;
        i32 minor = LIBLAVA_VERSION_MINOR;
        i32 patch = LIBLAVA_VERSION_PATCH;

        auto operator<=>(internal_version const&) const = default;
    };

    enum class version_stage {
        preview,
        alpha,
        beta,
        rc,
        release
    };

    struct version {
        i32 year = 2021;
        i32 release = 0;
        version_stage stage = version_stage::preview;
        i32 rev = 0;
    };

    constexpr name _build_date = LIBLAVA_BUILD_DATE;
    constexpr name _build_time = LIBLAVA_BUILD_TIME;

} // namespace lava
