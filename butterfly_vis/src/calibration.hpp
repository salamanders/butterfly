#pragma once
#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>
#include <algorithm>
#include <cmath>
#include <concepts>
#include <span>
#include <vector>

#include "vision.hpp"
#include "overlay_gl.hpp"

namespace bf {

// Place the 4 standard AprilTag (tag36h11, IDs 0..3) fiducial tiles at the far corners
// of the 1920x1080 projectable area.
inline constexpr float ZOOM_SAFE_BORDER_NORM = 0.045f; // 4.5% camera sensor border safety margin when zooming in
inline constexpr double WIDE_DETECT_BUDGET_SEC = 5.0;  // Max seconds spent looking for tags at widest zoom
inline constexpr double STEP_DETECT_BUDGET_SEC = 1.2;  // Max seconds spent verifying tags after each zoom step
inline constexpr double MAX_TOTAL_CALIB_SEC    = 15.0; // Hard cap so calibration NEVER blocks startup (>15s impossible)

inline Homography3x3 makeDefaultHomography(int screenW, int screenH) noexcept {
    Homography3x3 H{};
    H.m[0][0] = float(screenW); H.m[0][1] = 0.0f;           H.m[0][2] = 0.0f;
    H.m[1][0] = 0.0f;           H.m[1][1] = float(screenH); H.m[1][2] = 0.0f;
    H.m[2][0] = 0.0f;           H.m[2][1] = 0.0f;           H.m[2][2] = 1.0f;
    return H;
}

// Compute 3x3 perspective homography mapping normalized camera (u,v) -> projector screen (x,y)
// from the 4 detected AprilTag centers (IDs 0:TL, 1:TR, 2:BL, 3:BR)
inline bool computeHomographyFromTagCenters(
    std::span<const float, 4> srcU, std::span<const float, 4> srcV,
    std::span<const float, 4> dstX, std::span<const float, 4> dstY,
    Homography3x3& outH) noexcept
{
    double A[8][9] = {{0.0}};
    for (int i = 0; i < 4; ++i) {
        double u = srcU[i], v = srcV[i];
        double x = dstX[i], y = dstY[i];
        int r0 = i * 2;
        int r1 = i * 2 + 1;
        A[r0][0] = u; A[r0][1] = v; A[r0][2] = 1.0;
        A[r0][6] = -u * x; A[r0][7] = -v * x; A[r0][8] = x;

        A[r1][3] = u; A[r1][4] = v; A[r1][5] = 1.0;
        A[r1][6] = -u * y; A[r1][7] = -v * y; A[r1][8] = y;
    }

    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row) {
            if (std::fabs(A[row][col]) > std::fabs(A[pivot][col])) pivot = row;
        }
        if (std::fabs(A[pivot][col]) < 1e-11) return false;
        if (pivot != col) {
            for (int k = col; k <= 8; ++k) std::swap(A[col][k], A[pivot][k]);
        }
        double div = A[col][col];
        for (int k = col; k <= 8; ++k) A[col][k] /= div;
        for (int row = 0; row < 8; ++row) {
            if (row != col) {
                double factor = A[row][col];
                for (int k = col; k <= 8; ++k) A[row][k] -= factor * A[col][k];
            }
        }
    }

    outH.m[0][0] = float(A[0][8]); outH.m[0][1] = float(A[1][8]); outH.m[0][2] = float(A[2][8]);
    outH.m[1][0] = float(A[3][8]); outH.m[1][1] = float(A[4][8]); outH.m[1][2] = float(A[5][8]);
    outH.m[2][0] = float(A[6][8]); outH.m[2][1] = float(A[7][8]); outH.m[2][2] = 1.0f;
    return true;
}

// Non-blocking Standard AprilTag (tag36h11, IDs 0, 1, 2, 3) Startup Calibrator
// - Projects 4 standard AprilTag fiducial tiles at the far corners of the 1920x1080 screen.
// - Starts at widest possible hardware zoom (minHwZoom) if camera supports V4L2_CID_ZOOM_ABSOLUTE.
// - Steps hardware zoom inward until any of the 4 tags clips or touches the camera edge, then backs up one setting.
// - Strictly bounded to <= 15 seconds total and never blocks completion if no camera or tags are visible.
template <typename FlipPresentFn, std::invocable ShouldCloseFn>
inline Homography3x3 runStartupCalibration(
    int screenW, int screenH,
    VisionWorker& vision,
    OverlayRenderer& overlay,
    FlipPresentFn&& flipPresent,
    ShouldCloseFn&& shouldClose)
{
    if (vision.demoMode) {
        std::println("[Calib] Demo mode active; using default 1:1 full-frame homography ({}x{})", screenW, screenH);
        return makeDefaultHomography(screenW, screenH);
    }

    const double calibStartSec = monotonicSec();

    // Wait briefly (up to 2.5s) in case the USB webcam thread is still opening /dev/v4l/by-id at boot
    while (!vision.cameraOpen.load() && !shouldClose() && (monotonicSec() - calibStartSec) < 2.5) {
        flipPresent([&]() {});
    }

    if (!vision.cameraOpen.load()) {
        std::println("[Calib] No USB webcam open; proceeding immediately with default {}x{} homography.",
                     screenW, screenH);
        return makeDefaultHomography(screenW, screenH);
    }

    // 1. Always start at the widest possible zoom so all 4 far-edge AprilTags are inside the camera view
    vision.pausePose.store(true);
    vision.resetZoomToWideForCalibration();

    // 2. Build official tag36h11 family and pre-render the 4 corner tag bitmaps (IDs 0:TL, 1:TR, 2:BL, 3:BR)
    apriltag_family_t* tf = tag36h11_create();
    apriltag_detector_t* td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = 1.0f; // Full 640x480 resolution for maximum range on corner tags
    td->quad_sigma = 0.0f;
    td->decode_sharpening = 0.5;
    td->nthreads = 2;
    td->refine_edges = true;

    image_u8_t* tagImgs[4] = {
        apriltag_to_image(tf, 0),
        apriltag_to_image(tf, 1),
        apriltag_to_image(tf, 2),
        apriltag_to_image(tf, 3)
    };

    // Place the 4 AprilTag tiles right at the far corners of the projectable area
    const float tagSizePx = std::round(264.0f * (float(screenH) / 1080.0f));
    const float marginPx  = std::round(24.0f  * (float(screenH) / 1080.0f));
    const float halfTag   = 0.5f * tagSizePx;

    const float dstX[4] = {
        marginPx + halfTag,
        float(screenW) - marginPx - halfTag,
        marginPx + halfTag,
        float(screenW) - marginPx - halfTag
    };
    const float dstY[4] = {
        marginPx + halfTag,
        marginPx + halfTag,
        float(screenH) - marginPx - halfTag,
        float(screenH) - marginPx - halfTag
    };

    auto drawFourCornerTags = [&]() {
        for (int c = 0; c < 4; ++c) {
            overlay.drawAprilTag(tagImgs[c], dstX[c], dstY[c], tagSizePx, screenW, screenH);
        }
    };

    auto isInsideSafeBorder = [](float u, float v) noexcept -> bool {
        return (u >= ZOOM_SAFE_BORDER_NORM && u <= (1.0f - ZOOM_SAFE_BORDER_NORM) &&
                v >= ZOOM_SAFE_BORDER_NORM && v <= (1.0f - ZOOM_SAFE_BORDER_NORM));
    };

    // Present the 4 AprilTags at 60Hz while checking fresh camera frames until all 4 IDs (0..3) are detected
    // or the per-pass time budget expires.
    auto detectFourTagsWithTimeout = [&](double settleSec, double timeoutSec,
                                         bool enforceBorder,
                                         float outU[4], float outV[4]) -> bool {
        double passStart = monotonicSec();
        double minFrameTs = passStart + settleSec;
        double lastCheckedTs = 0.0;

        std::vector<uint8_t> gray;
        int grayW = 0, grayH = 0;

        while (!shouldClose()) {
            double now = monotonicSec();
            if ((now - passStart) > timeoutSec || (now - calibStartSec) > MAX_TOTAL_CALIB_SEC) {
                break;
            }

            // Keep DRM/KMS page-flipping active at 60Hz so the projector continuously displays the 4 tags
            flipPresent(drawFourCornerTags);

            double frameTs = 0.0;
            if (!vision.getLatestGrayAfter( std::max(minFrameTs, lastCheckedTs + 0.01), gray, grayW, grayH, frameTs )) {
                continue;
            }
            lastCheckedTs = frameTs;

            image_u8_t im{
                .width = grayW,
                .height = grayH,
                .stride = grayW,
                .buf = gray.data()
            };

            zarray_t* detections = apriltag_detector_detect(td, &im);
            bool seen[4] = {false, false, false, false};
            float foundU[4] = {0.0f}, foundV[4] = {0.0f};

            for (int i = 0; i < zarray_size(detections); ++i) {
                apriltag_detection_t* det = nullptr;
                zarray_get(detections, i, static_cast<void*>(&det));
                if (det && det->id >= 0 && det->id < 4 && det->hamming <= 1) {
                    float u = float(det->c[0]) / float(grayW);
                    float v = float(det->c[1]) / float(grayH);
                    if (!enforceBorder || isInsideSafeBorder(u, v)) {
                        seen[det->id] = true;
                        foundU[det->id] = u;
                        foundV[det->id] = v;
                    }
                }
            }
            apriltag_detections_destroy(detections);

            if (seen[0] && seen[1] && seen[2] && seen[3]) {
                for (int c = 0; c < 4; ++c) {
                    outU[c] = foundU[c];
                    outV[c] = foundV[c];
                }
                std::println("[Calib] Zoom={} detected all 4 AprilTags (0..3): TL=({:.3f},{:.3f}) TR=({:.3f},{:.3f}) BL=({:.3f},{:.3f}) BR=({:.3f},{:.3f})",
                             vision.currentHwZoom(),
                             outU[0], outV[0], outU[1], outV[1], outU[2], outV[2], outU[3], outV[3]);
                return true;
            }
        }
        return false;
    };

    std::println("[Calib] Projecting 4 corner AprilTags (tag36h11 IDs 0..3) at widest zoom...");
    float bestU[4] = {0.0f}, bestV[4] = {0.0f};
    int bestZoom = vision.supportsHwZoom() ? vision.minHwZoom() : 0;

    // Allow 0.6s after initial KMS modeset for projector + camera auto-exposure to settle, up to 5.0s budget
    bool calibrated = detectFourTagsWithTimeout(0.60, WIDE_DETECT_BUDGET_SEC, /*enforceBorder=*/false, bestU, bestV);

    // 3. If widest zoom sees all 4 tags AND camera supports V4L2_CID_ZOOM_ABSOLUTE:
    //    Step hardware zoom inward until a tag clips or touches the border, then back up one setting!
    if (calibrated && vision.supportsHwZoom() && !shouldClose()) {
        bool wideInsideBorder = true;
        for (int c = 0; c < 4; ++c) {
            if (!isInsideSafeBorder(bestU[c], bestV[c])) {
                wideInsideBorder = false;
                break;
            }
        }

        if (wideInsideBorder) {
            int zMin = vision.minHwZoom();
            int zMax = vision.maxHwZoom();
            int zHwStep = vision.stepHwZoom();
            int totalSteps = (zMax - zMin) / zHwStep;
            int stepDelta = (totalSteps <= 12)
                ? zHwStep
                : std::max(zHwStep, (totalSteps / 10) * zHwStep);

            while (!shouldClose() && (monotonicSec() - calibStartSec) < (MAX_TOTAL_CALIB_SEC - 1.5)) {
                int candidateZoom = bestZoom + stepDelta;
                if (candidateZoom > zMax) break;

                std::println("[Calib] Stepping V4L2_CID_ZOOM_ABSOLUTE to {} (+{})...", candidateZoom, stepDelta);
                if (!vision.setHardwareZoom(candidateZoom)) break;

                float testU[4] = {0.0f}, testV[4] = {0.0f};
                if (detectFourTagsWithTimeout(0.25, STEP_DETECT_BUDGET_SEC, /*enforceBorder=*/true, testU, testV)) {
                    bestZoom = vision.currentHwZoom();
                    for (int c = 0; c < 4; ++c) {
                        bestU[c] = testU[c];
                        bestV[c] = testV[c];
                    }
                    if (bestZoom >= zMax) break;
                } else {
                    std::println("[Calib] Zoom={} clipped an AprilTag -> backing up one setting to V4L2_CID_ZOOM_ABSOLUTE={}.",
                                 candidateZoom, bestZoom);
                    vision.setHardwareZoom(bestZoom);
                    break;
                }
            }
        }
    }

    for (image_u8_t* img : tagImgs) {
        if (img) image_u8_destroy(img);
    }
    apriltag_detector_destroy(td);
    tag36h11_destroy(tf);
    vision.pausePose.store(false);

    if (calibrated) {
        Homography3x3 H;
        if (computeHomographyFromTagCenters(bestU, bestV, dstX, dstY, H)) {
            std::println("[Calib] AprilTag calibration succeeded in {:.1f}s (hw_zoom={})!",
                         monotonicSec() - calibStartSec, bestZoom);
            return H;
        }
    }

    std::println("[Calib] AprilTag calibration did not see all 4 tags within {:.1f}s; starting butterflies with default {}x{} homography.",
                 monotonicSec() - calibStartSec, screenW, screenH);
    return makeDefaultHomography(screenW, screenH);
}

} // namespace bf
