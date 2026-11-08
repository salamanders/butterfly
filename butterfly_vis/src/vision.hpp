#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "cpp26.hpp"

#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/interpreter_builder.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model_builder.h>

namespace bf {

// Embed the 2.8 MB quantized MoveNet SinglePose Lightning INT8 model directly into .rodata
BF_EMBED_BINARY(movenet_tflite, "assets/movenet_lightning_int8.tflite")

inline constexpr int CAM_WIDTH = 320;
inline constexpr int CAM_HEIGHT = 240;

inline double monotonicSec() noexcept {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct Homography3x3 {
    float m[3][3] = {
        {1920.0f, 0.0f,    0.0f},
        {0.0f,    1080.0f, 0.0f},
        {0.0f,    0.0f,    1.0f}
    };
};

// Guardrail B: Unified normalized camera coordinate (u, v) in [0..1] -> screen pixel (sx, sy)
constexpr void mapCamToScreen(const Homography3x3& H, float u, float v, float& sx, float& sy) noexcept {
    float denom = H.m[2][0] * u + H.m[2][1] * v + H.m[2][2];
    if (denom > -1e-6f && denom < 1e-6f) {
        sx = -9999.0f;
        sy = -9999.0f;
        return;
    }
    sx = (H.m[0][0] * u + H.m[0][1] * v + H.m[0][2]) / denom;
    sy = (H.m[1][0] * u + H.m[1][1] * v + H.m[1][2]) / denom;
}

struct PalmTarget {
    float u = 0.0f; // Normalized [0..1] camera x
    float v = 0.0f; // Normalized [0..1] camera y
};

struct HeadZone {
    float u = 0.5f;          // Normalized [0..1] camera x
    float v = 0.35f;         // Normalized [0..1] camera y
    float radius_u = 0.06f;  // Normalized camera radius
};

struct VisionState {
    std::vector<PalmTarget> palms; // 0, 1, or 2 palms
    bool has_head = false;
    HeadZone head;
};

class VisionWorker {
public:
    std::atomic<bool> pausePose{false};
    std::atomic<bool> cameraOpen{false};
    std::atomic<bool> tfliteReady{false};
    std::atomic<float> lastVisionMs{0.0f};
    bool demoMode = false;

    void start(bool enableDemoMode = false) {
        demoMode = enableDemoMode;
        workerThread_ = std::jthread([this](const std::stop_token& st) {
            threadLoop(st);
        });
    }

    void stop() {
        if (workerThread_.joinable()) {
            workerThread_.request_stop();
            workerThread_.join();
        }
    }

    VisionState getState() {
        std::scoped_lock lock(mtx_);
        return state_;
    }

    // Reset webcam to widest angle (minHwZoom) before running calibration test pattern
    void resetZoomToWideForCalibration() {
        if (v4l2Fd_ >= 0 && hasHwZoom_) {
            v4l2_control ctrl{};
            ctrl.id = V4L2_CID_ZOOM_ABSOLUTE;
            ctrl.value = hwZoomMin_;
            if (ioctl(v4l2Fd_, VIDIOC_S_CTRL, &ctrl) == 0) {
                hwZoomCurrent_ = hwZoomMin_;
                std::println("[Vision] Reset V4L2_CID_ZOOM_ABSOLUTE to widest angle ({}) for calibration.", hwZoomMin_);
            }
        }
    }

    [[nodiscard]] int currentHwZoom() const noexcept { return hwZoomCurrent_; }
    [[nodiscard]] int minHwZoom() const noexcept { return hwZoomMin_; }
    [[nodiscard]] int maxHwZoom() const noexcept { return hwZoomMax_; }
    [[nodiscard]] int stepHwZoom() const noexcept { return std::max(1, hwZoomStep_); }
    [[nodiscard]] bool supportsHwZoom() const noexcept { return v4l2Fd_ >= 0 && hasHwZoom_ && hwZoomMax_ > hwZoomMin_; }

    // Set V4L2_CID_ZOOM_ABSOLUTE directly and read back the exact setting accepted by the camera
    bool setHardwareZoom(int targetZoom) {
        if (v4l2Fd_ < 0 || !hasHwZoom_) return false;
        int clamped = std::clamp(targetZoom, hwZoomMin_, hwZoomMax_);
        v4l2_control ctrl{};
        ctrl.id = V4L2_CID_ZOOM_ABSOLUTE;
        ctrl.value = clamped;
        if (ioctl(v4l2Fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
            return false;
        }
        v4l2_control readCtrl{};
        readCtrl.id = V4L2_CID_ZOOM_ABSOLUTE;
        if (ioctl(v4l2Fd_, VIDIOC_G_CTRL, &readCtrl) == 0) {
            hwZoomCurrent_ = readCtrl.value;
        } else {
            hwZoomCurrent_ = clamped;
        }
        return true;
    }

    // Retrieve latest full-resolution grayscale (Y) frame captured strictly after minTimestamp
    bool getLatestGrayAfter(double minTimestamp, std::vector<uint8_t>& outGray, int& outW, int& outH, double& outTs) {
        std::scoped_lock lock(mtx_);
        if (!latestGray_.empty() && latestGrayW_ > 0 && latestGrayH_ > 0 && latestGrayTs_ >= minTimestamp) {
            outGray = latestGray_;
            outW = latestGrayW_;
            outH = latestGrayH_;
            outTs = latestGrayTs_;
            return true;
        }
        return false;
    }

private:
    struct MmapBuf {
        void* start = nullptr;
        size_t length = 0;
    };

    std::jthread workerThread_;
    std::mutex mtx_;
    VisionState state_;

    std::vector<uint8_t> latestGray_;
    int latestGrayW_ = 0;
    int latestGrayH_ = 0;
    double latestGrayTs_ = 0.0;

    int v4l2Fd_ = -1;
    int capW_ = 640;
    int capH_ = 480;
    bool hasHwZoom_ = false;
    int hwZoomMin_ = 100;
    int hwZoomMax_ = 100;
    int hwZoomStep_ = 1;
    int hwZoomCurrent_ = 100;
    std::vector<MmapBuf> v4l2Bufs_;

    std::unique_ptr<tflite::FlatBufferModel> tfliteModel_;
    std::unique_ptr<tflite::Interpreter> tfliteInterpreter_;
    int tfliteInW_ = 192;
    int tfliteInH_ = 192;

    bool initMoveNetTFLite() {
        auto emb = embedded_movenet_tflite();
        if (!emb.empty()) {
            tfliteModel_ = tflite::FlatBufferModel::BuildFromBuffer(
                reinterpret_cast<const char*>(emb.data()), emb.size());
        }
        if (!tfliteModel_) {
            std::println(stderr, "[Vision] Embedded TFLite model failed to load.");
            return false;
        }
        tflite::ops::builtin::BuiltinOpResolver resolver;
        tflite::InterpreterBuilder builder(*tfliteModel_, resolver);
        builder(&tfliteInterpreter_);
        if (!tfliteInterpreter_) return false;

        tfliteInterpreter_->SetNumThreads(1);
        if (tfliteInterpreter_->AllocateTensors() != kTfLiteOk) return false;

        const TfLiteTensor* inTensor = tfliteInterpreter_->input_tensor(0);
        if (inTensor && inTensor->dims && inTensor->dims->size >= 3) {
            tfliteInH_ = inTensor->dims->data[1];
            tfliteInW_ = inTensor->dims->data[2];
        }
        tfliteReady.store(true);
        std::println("[Vision] Initialized native C++26 TFLite MoveNet Pose ({}x{}, type={}) from embedded .rodata",
                     tfliteInW_, tfliteInH_, inTensor ? int(inTensor->type) : -1);
        return true;
    }

    bool analyzeYuyvMoveNet(std::span<const uint8_t> yuyv, int srcW, int srcH) {
        if (!tfliteInterpreter_) return false;
        TfLiteTensor* inTensor = tfliteInterpreter_->input_tensor(0);
        if (!inTensor || inTensor->type != kTfLiteUInt8) return false;

        const int inW = tfliteInW_;
        const int inH = tfliteInH_;

        uint8_t* dst = inTensor->data.uint8;
        for (int ty = 0; ty < inH; ++ty) {
            int sy = (ty * srcH) / inH;
            for (int tx = 0; tx < inW; ++tx) {
                int sx = (tx * srcW) / inW;
                int pairBase = (sy * srcW + (sx & ~1)) * 2;
                int y = int(yuyv[pairBase + ((sx & 1) ? 2 : 0)]) - 16;
                int u = int(yuyv[pairBase + 1]) - 128;
                int v = int(yuyv[pairBase + 3]) - 128;
                uint8_t* px = &dst[static_cast<ptrdiff_t>(ty * inW + tx) * 3];
                px[0] = uint8_t(std::clamp((298 * y + 409 * v + 128) >> 8, 0, 255));
                px[1] = uint8_t(std::clamp((298 * y - 100 * u - 208 * v + 128) >> 8, 0, 255));
                px[2] = uint8_t(std::clamp((298 * y + 516 * u + 128) >> 8, 0, 255));
            }
        }

        if (tfliteInterpreter_->Invoke() != kTfLiteOk) return false;

        const TfLiteTensor* outTensor = tfliteInterpreter_->output_tensor(0);
        if (!outTensor || outTensor->type != kTfLiteUInt8) return false;

        struct Kp {
            float v = 0.0f;
            float u = 0.0f;
            float score = 0.0f;
        };
        Kp kps[17]{};

        const uint8_t* outU = outTensor->data.uint8;
        const float scale = outTensor->params.scale;
        const int32_t zp = outTensor->params.zero_point;
        for (int k = 0; k < 17; ++k) {
            kps[k] = {
                .v = float(int32_t(outU[k * 3 + 0]) - zp) * scale,
                .u = float(int32_t(outU[k * 3 + 1]) - zp) * scale,
                .score = float(int32_t(outU[k * 3 + 2]) - zp) * scale
            };
        }

        int confidentCount = 0;
        for (int k = 0; k <= 12; ++k) {
            if (kps[k].score >= 0.25f) confidentCount++;
        }
        if (confidentCount < 3) {
            std::scoped_lock lock(mtx_);
            state_.has_head = false;
            state_.palms.clear();
            return false;
        }

        bool detectedHead = false;
        HeadZone headNow{};
        std::vector<PalmTarget> palmsNow;

        // 1. Head center + radius from face keypoints (0..4) with shoulder fallback (5, 6)
        float sumFaceU = 0.0f, sumFaceV = 0.0f;
        int faceCnt = 0;
        for (int k = 0; k <= 4; ++k) {
            if (kps[k].score >= 0.30f) {
                sumFaceU += kps[k].u;
                sumFaceV += kps[k].v;
                faceCnt++;
            }
        }

        const bool shOk = (kps[5].score >= 0.30f && kps[6].score >= 0.30f);
        const float shWidth = shOk ? std::hypot(kps[5].u - kps[6].u, kps[5].v - kps[6].v) : 0.14f;

        if (faceCnt >= 2) {
            headNow.u = sumFaceU / float(faceCnt);
            headNow.v = sumFaceV / float(faceCnt);
            float maxD = 0.0f;
            for (int k = 0; k <= 4; ++k) {
                if (kps[k].score >= 0.30f) {
                    maxD = std::max(maxD, std::hypot(kps[k].u - headNow.u, kps[k].v - headNow.v));
                }
            }
            headNow.radius_u = std::max({0.045f, maxD * 1.45f, shWidth * 0.42f});
            detectedHead = true;
        } else if (shOk) {
            headNow.u = 0.5f * (kps[5].u + kps[6].u);
            headNow.v = 0.5f * (kps[5].v + kps[6].v) - 0.65f * shWidth;
            headNow.radius_u = std::max(0.045f, 0.55f * shWidth);
            detectedHead = true;
        }

        // 2. Waist threshold from hips (11, 12) and shoulders (5, 6)
        float waistV = 0.78f;
        if (shOk && kps[11].score >= 0.25f && kps[12].score >= 0.25f) {
            float hipV = 0.5f * (kps[11].v + kps[12].v);
            float shV  = 0.5f * (kps[5].v + kps[6].v);
            waistV = hipV - 0.08f * std::max(0.06f, hipV - shV);
        }

        // 3. Left (elbow=7, wrist=9) & Right (elbow=8, wrist=10) palm projection
        constexpr int kArmPairs[2][2] = {{7, 9}, {8, 10}};
        for (const auto& pair : kArmPairs) {
            const Kp& elb = kps[pair[0]];
            const Kp& wr  = kps[pair[1]];
            if (wr.score >= 0.30f) {
                float pu = wr.u;
                float pv = wr.v;
                if (elb.score >= 0.25f) {
                    pu += 0.28f * (wr.u - elb.u);
                    pv += 0.28f * (wr.v - elb.v);
                }
                if (pv < waistV) {
                    palmsNow.push_back({.u = pu, .v = pv});
                }
            }
        }

        std::scoped_lock lock(mtx_);
        state_.has_head = detectedHead;
        if (detectedHead) {
            state_.head = headNow;
        }
        state_.palms = std::move(palmsNow);
        return true;
    }

    // Modern Linux udev USB webcam discovery via /dev/v4l/by-id and /dev/v4l/by-path
    // Filters strictly for USB devices ("usb") so internal Raspberry Pi platform/ISP nodes are never opened.
    static std::vector<std::string> discoverUsbWebcamNodes() {
        namespace fs = std::filesystem;
        std::vector<std::string> candidates;
        std::set<std::string> seenCanonical;

        for (const char* dirPath : {"/dev/v4l/by-id", "/dev/v4l/by-path"}) {
            std::error_code ec;
            if (!fs::exists(dirPath, ec)) continue;
            for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
                std::string fname = entry.path().filename().string();
                if (fname.find("usb") == std::string::npos) continue;
                std::error_code targetEc;
                fs::path resolved = fs::canonical(entry.path(), targetEc);
                std::string key = targetEc ? entry.path().string() : resolved.string();
                if (seenCanonical.insert(key).second) {
                    candidates.push_back(entry.path().string());
                }
            }
        }
        return candidates;
    }

    bool initV4L2Camera() {
        for (const std::string& devPath : discoverUsbWebcamNodes()) {
            int fd = open(devPath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;

            v4l2_capability cap{};
            if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
                close(fd);
                continue;
            }
            std::string_view busInfo(reinterpret_cast<const char*>(cap.bus_info));
            if (!busInfo.starts_with("usb-")) {
                close(fd);
                continue;
            }
            const uint32_t devCaps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                ? cap.device_caps
                : cap.capabilities;
            if (!(devCaps & V4L2_CAP_VIDEO_CAPTURE) || !(devCaps & V4L2_CAP_STREAMING)) {
                close(fd);
                continue;
            }

            // Prefer 640x480 YUYV for crisp AprilTag detection and MoveNet pose; fall back to 320x240
            int chosenW = 0, chosenH = 0;
            constexpr int kResolutions[2][2] = {{640, 480}, {CAM_WIDTH, CAM_HEIGHT}};
            for (const auto& res : kResolutions) {
                v4l2_format fmt{};
                fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                fmt.fmt.pix.width = res[0];
                fmt.fmt.pix.height = res[1];
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
                fmt.fmt.pix.field = V4L2_FIELD_NONE;
                if (ioctl(fd, VIDIOC_S_FMT, &fmt) == 0 &&
                    fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV &&
                    fmt.fmt.pix.width > 0 &&
                    fmt.fmt.pix.height > 0) {
                    chosenW = int(fmt.fmt.pix.width);
                    chosenH = int(fmt.fmt.pix.height);
                    break;
                }
            }
            if (chosenW == 0) {
                close(fd);
                continue;
            }

            // Check if the webcam supports UVC hardware zoom (V4L2_CID_ZOOM_ABSOLUTE)
            v4l2_queryctrl qctrl{};
            qctrl.id = V4L2_CID_ZOOM_ABSOLUTE;
            if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0 && !(qctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {
                hasHwZoom_ = true;
                hwZoomMin_ = qctrl.minimum;
                hwZoomMax_ = qctrl.maximum;
                hwZoomStep_ = std::max(1, int(qctrl.step));
                hwZoomCurrent_ = hwZoomMin_;
                v4l2_control ctrl{};
                ctrl.id = V4L2_CID_ZOOM_ABSOLUTE;
                ctrl.value = hwZoomMin_;
                ioctl(fd, VIDIOC_S_CTRL, &ctrl);
            }

            v4l2_requestbuffers req{};
            req.count = 2;
            req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            req.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 1) {
                close(fd);
                continue;
            }

            v4l2Bufs_.resize(req.count);
            bool mmapOk = true;
            for (uint32_t i = 0; i < req.count; ++i) {
                v4l2_buffer b{};
                b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                b.memory = V4L2_MEMORY_MMAP;
                b.index = i;
                if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { mmapOk = false; break; }
                v4l2Bufs_[i].length = b.length;
                v4l2Bufs_[i].start = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
                if (v4l2Bufs_[i].start == MAP_FAILED) { mmapOk = false; break; }
                ioctl(fd, VIDIOC_QBUF, &b);
            }
            if (!mmapOk) {
                close(fd);
                v4l2Bufs_.clear();
                continue;
            }

            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
                close(fd);
                v4l2Bufs_.clear();
                continue;
            }

            v4l2Fd_ = fd;
            capW_ = chosenW;
            capH_ = chosenH;
            cameraOpen.store(true);
            std::println("[Vision] Opened V4L2 USB camera {} ({}) at {}x{} YUYV (HW Zoom: {})",
                         devPath, reinterpret_cast<const char*>(cap.card), capW_, capH_,
                         hasHwZoom_ ? "supported" : "fixed");
            return true;
        }
        return false;
    }

    void closeV4L2Camera() {
        if (v4l2Fd_ >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(v4l2Fd_, VIDIOC_STREAMOFF, &type);
            for (auto& b : v4l2Bufs_) {
                if (b.start && b.start != MAP_FAILED) munmap(b.start, b.length);
            }
            v4l2Bufs_.clear();
            close(v4l2Fd_);
            v4l2Fd_ = -1;
            cameraOpen.store(false);
        }
    }

    void updateDemoState(double nowSec, double startSec) {
        double t = nowSec - startSec;
        std::scoped_lock lock(mtx_);

        state_.has_head = true;
        state_.head.u = 0.50f + 0.025f * float(std::sin(t * 0.45));
        state_.head.v = 0.34f + 0.012f * float(std::cos(t * 0.60));
        state_.head.radius_u = 0.052f;

        state_.palms.clear();
        double cycle = std::fmod(t, 10.0);
        float p0u = 0.26f + (cycle > 8.8 ? 0.09f * float(std::sin((cycle - 8.8) * 5.0)) : 0.0f);
        float p0v = 0.52f + (cycle > 8.8 ? -0.06f * float(std::cos((cycle - 8.8) * 5.0)) : 0.0f);
        state_.palms.push_back({.u = p0u, .v = p0v});

        float p1u = 0.74f + 0.075f * float(std::cos(t * 0.85));
        float p1v = 0.48f + 0.065f * float(std::sin(t * 1.35));
        state_.palms.push_back({.u = p1u, .v = p1v});
    }

    void threadLoop(const std::stop_token& st) {
        double startSec = monotonicSec();
        if (!demoMode) {
            initMoveNetTFLite();
            initV4L2Camera();
        }

        std::vector<uint8_t> grayBuf;
        double lastCamRetrySec = startSec;

        while (!st.stop_requested()) {
            double nowSec = monotonicSec();

            if (demoMode) {
                updateDemoState(nowSec, startSec);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            if (v4l2Fd_ < 0) {
                // Retry opening USB webcam every 1.0s in case it is still enumerating at boot
                if (nowSec - lastCamRetrySec >= 1.0) {
                    lastCamRetrySec = nowSec;
                    initV4L2Camera();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (v4l2Fd_ >= 0) {
                v4l2_buffer buf{};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                if (ioctl(v4l2Fd_, VIDIOC_DQBUF, &buf) == 0) {
                    const size_t minFrameBytes = static_cast<size_t>(capW_) * static_cast<size_t>(capH_) * 2;
                    if (buf.index >= v4l2Bufs_.size() ||
                        v4l2Bufs_[buf.index].length < minFrameBytes ||
                        (buf.bytesused > 0 && buf.bytesused < minFrameBytes)) {
                        ioctl(v4l2Fd_, VIDIOC_QBUF, &buf);
                        continue;
                    }
                    double tVis0 = monotonicSec();
                    std::span<const uint8_t> yuyv(
                        static_cast<const uint8_t*>(v4l2Bufs_[buf.index].start),
                        v4l2Bufs_[buf.index].length
                    );
                    // Extract full-resolution luminance (Y) channel for AprilTag calibration
                    const size_t numPixels = static_cast<size_t>(capW_) * static_cast<size_t>(capH_);
                    grayBuf.resize(numPixels);
                    for (size_t i = 0; i < numPixels; ++i) {
                        grayBuf[i] = yuyv[i * 2];
                    }
                    double captureTs = monotonicSec();
                    {
                        std::scoped_lock lock(mtx_);
                        latestGray_ = grayBuf;
                        latestGrayW_ = capW_;
                        latestGrayH_ = capH_;
                        latestGrayTs_ = captureTs;
                    }
                    if (!pausePose.load() && tfliteReady.load()) {
                        analyzeYuyvMoveNet(yuyv, capW_, capH_);
                    }
                    double tVis1 = monotonicSec();
                    lastVisionMs.store(float((tVis1 - tVis0) * 1000.0));
                    ioctl(v4l2Fd_, VIDIOC_QBUF, &buf);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        closeV4L2Camera();
    }
};

} // namespace bf
