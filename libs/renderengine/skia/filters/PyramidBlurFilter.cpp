/*
 * SPDX-FileCopyrightText: 2026 AlphaDroid
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "PyramidBlurFilter.h"

#include <SkCanvas.h>
#include <SkImage.h>
#include <SkRect.h>
#include <SkSurface.h>
#include <algorithm>
#include <cmath>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <log/log.h>

namespace android {
namespace renderengine {
namespace skia {

PyramidBlurFilter::PyramidBlurFilter(RuntimeEffectManager& effectManager) : BlurFilter(effectManager) {}

sk_sp<SkImage> PyramidBlurFilter::generate(SkiaGpuContext* context, const uint32_t blurRadius,
                                         const sk_sp<SkImage> input,
                                         const SkRect& blurRect) const {
    LOG_ALWAYS_FATAL_IF(context == nullptr, "%s: Needs GPU context", __func__);
    LOG_ALWAYS_FATAL_IF(input == nullptr, "%s: Invalid input image", __func__);

    if (blurRadius == 0) {
        return input;
    }

    const SkImageInfo scaledInfo = input->imageInfo().makeWH(
            std::ceil(blurRect.width() * kInputScale), std::ceil(blurRect.height() * kInputScale));

    const SkSamplingOptions linear(SkFilterMode::kLinear, SkMipmapMode::kNone);

    sk_sp<SkSurface> baseSurface = context->createRenderTarget(scaledInfo);
    LOG_ALWAYS_FATAL_IF(!baseSurface, "%s: Failed to create base pyramid surface", __func__);
    baseSurface->getCanvas()->drawImageRect(
            input, blurRect, SkRect::MakeWH(scaledInfo.width(), scaledInfo.height()), linear,
            nullptr, SkCanvas::SrcRectConstraint::kFast_SrcRectConstraint);
    sk_sp<SkImage> current = baseSurface->makeTemporaryImage();

    // Stronger radius -> more pyramid levels (cheap half-res steps), capped for stability.
    // Small radii use 0 extra levels (kInputScale only) for a crisper look; /8 is gentler than /6.
    const uint32_t extraDownLevels =
            std::min(kMaxExtraDownLevels, blurRadius / 8u);
    if (extraDownLevels == 0) {
        return current;
    }

    for (uint32_t i = 0; i < extraDownLevels; i++) {
        const int w = current->width();
        const int h = current->height();
        const int nw = std::max(1, w / 2);
        const int nh = std::max(1, h / 2);
        if (nw >= w && nh >= h) {
            break;
        }

        const SkImageInfo levelInfo = current->imageInfo().makeWH(nw, nh);
        sk_sp<SkSurface> levelSurface = context->createRenderTarget(levelInfo);
        LOG_ALWAYS_FATAL_IF(!levelSurface, "%s: Failed to create pyramid level surface", __func__);
        levelSurface->getCanvas()->drawImageRect(
                current.get(), SkRect::MakeIWH(w, h), SkRect::MakeIWH(nw, nh), linear, nullptr,
                SkCanvas::SrcRectConstraint::kFast_SrcRectConstraint);
        current = levelSurface->makeTemporaryImage();
    }

    // Upsample back to the standard blur working size (same as other filters).
    sk_sp<SkSurface> outSurface = context->createRenderTarget(scaledInfo);
    LOG_ALWAYS_FATAL_IF(!outSurface, "%s: Failed to create output surface", __func__);
    outSurface->getCanvas()->drawImageRect(
            current.get(), SkRect::MakeIWH(current->width(), current->height()),
            SkRect::MakeWH(scaledInfo.width(), scaledInfo.height()), linear, nullptr,
            SkCanvas::SrcRectConstraint::kFast_SrcRectConstraint);
    return outSurface->makeTemporaryImage();
}

} // namespace skia
} // namespace renderengine
} // namespace android
