/*
 * SPDX-FileCopyrightText: 2026 AlphaDroid
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "BlurFilter.h"

#include "RuntimeEffectManager.h"

namespace android {
namespace renderengine {
namespace skia {

/**
 * Pyramid blur: repeated half-res downsamples with bilinear filtering, then a single upscale
 * back to the working resolution (same kInputScale workspace as other BlurFilter implementations).
 * Extra half-res steps scale with blur radius (gentle mapping: small radii add none).
 */
class PyramidBlurFilter : public BlurFilter {
public:
    /** Extra half-size steps after the initial kInputScale image (not counting final upscale). */
    static constexpr uint32_t kMaxExtraDownLevels = 6u;

    explicit PyramidBlurFilter(RuntimeEffectManager& effectManager);
    ~PyramidBlurFilter() override = default;

    sk_sp<SkImage> generate(SkiaGpuContext* context, uint32_t radius, const sk_sp<SkImage> blurInput,
                            const SkRect& blurRect) const override;
};

} // namespace skia
} // namespace renderengine
} // namespace android
