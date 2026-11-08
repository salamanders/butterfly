CXX      := g++
RAYLIB   := $(CURDIR)/third_party/raylib/src
TFLITE   := $(CURDIR)/third_party/tflite/usr
APRILTAG := $(CURDIR)/third_party/apriltag/usr
CXXFLAGS := -std=c++26 -O3 -mcpu=cortex-a72 -mtune=cortex-a72 -fopenmp \
            -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wnull-dereference -Wdouble-promotion -Wformat=2 \
            -isystem $(RAYLIB) -isystem $(TFLITE)/include \
            -isystem $(APRILTAG)/include -isystem $(APRILTAG)/include/apriltag \
            -I/usr/include/libdrm
LDFLAGS  := $(RAYLIB)/libraylib.a \
            -L$(TFLITE)/lib/aarch64-linux-gnu -Wl,-rpath,$(TFLITE)/lib/aarch64-linux-gnu -ltensorflow-lite \
            -L$(APRILTAG)/lib/aarch64-linux-gnu -Wl,-rpath,$(APRILTAG)/lib/aarch64-linux-gnu -lapriltag -lapriltag-utils \
            -lGLESv2 -lEGL -ldrm -lgbm -lpthread -ldl -lm -fopenmp

TARGET   := butterfly_vis

$(TARGET): $(RAYLIB)/libraylib.a src/main.cpp src/cpp26.hpp src/mesh.hpp src/textures.hpp src/sim.hpp src/overlay_gl.hpp src/vision.hpp src/calibration.hpp shaders/butterfly.vert shaders/butterfly.frag
	$(CXX) $(CXXFLAGS) src/main.cpp -o $(TARGET) $(LDFLAGS)

$(RAYLIB)/libraylib.a:
	@if [ ! -f "$(RAYLIB)/raylib.h" ]; then \
		git -C .. submodule update --init --depth 1 butterfly_vis/third_party/raylib; \
	fi
	@if git -C third_party/raylib apply --check ../../patches/raylib-drm-gles3.patch 2>/dev/null; then \
		git -C third_party/raylib apply ../../patches/raylib-drm-gles3.patch; \
	fi
	$(MAKE) -C $(RAYLIB) PLATFORM=PLATFORM_DRM GRAPHICS=GRAPHICS_API_OPENGL_ES3 -j4

clean:
	rm -f $(TARGET)

lint:
	clang-tidy -checks='bugprone-*,performance-*,modernize-*,-modernize-use-trailing-return-type,-modernize-avoid-c-arrays,-modernize-use-designated-initializers,-modernize-use-nodiscard,-performance-no-int-to-ptr,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,-bugprone-exception-escape' \
		-header-filter='.*/src/.*' src/main.cpp -- -std=c++2c -O3 -isystem $(RAYLIB) -isystem $(TFLITE)/include -isystem $(APRILTAG)/include -isystem $(APRILTAG)/include/apriltag -I/usr/include/libdrm
