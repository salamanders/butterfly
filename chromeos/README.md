# A Kaleidoscope of Butterflies — ChromeOS Screensaver Extension

A 60 FPS **WebGL 2.0 (`#version 300 es`)** port of *A Kaleidoscope of Butterflies* packaged as a **Manifest V3 ChromeOS Idle Screensaver Extension**.

It runs the exact same 3D instanced GLSL ES 3.00 shaders, 26-vertex 4-wing articulated mesh, 8 real museum butterfly specimens (`512x512` `TEXTURE_2D_ARRAY`), biomechanical + cel-animation flight physics, and 200 bioluminescent wingtip fairy-dust motes as the Raspberry Pi 4B projector installation—automatically launching in borderless fullscreen whenever your Chromebook goes idle.

---

## How to Install on ChromeOS

1. Copy this `chromeos/` folder onto your Chromebook (into **Files -> My files**).
2. Open Chrome and navigate to:
   ```text
   chrome://extensions
   ```
3. Toggle **Developer mode** ON in the top-right corner.
4. Click **Load unpacked** (top-left) and select the `chromeos` folder.

---

## How It Works

- **Automatic Idle Launch (`chrome.idle` + `chrome.windows`):** When you don't touch the keyboard or trackpad for the configured timeout (default **2 minutes**, adjustable from **15 seconds** to **15 minutes** in the extension popup), the background service worker automatically opens the screensaver in a borderless **`state: "fullscreen"`** window.
- **Keeps Display Awake (`chrome.power`):** While the screensaver window is open, it calls `chrome.power.requestKeepAwake("display")` so your Chromebook display doesn't dim or sleep while the butterflies are flying.
- **Instant Passive Dismissal:** Moving the mouse/trackpad, touching the screen, or pressing any key immediately closes the screensaver window and releases the display wake lock.
- **On-Demand Preview:** Click the butterfly icon in the Chrome toolbar (puzzle-piece menu -> pin **A Kaleidoscope of Butterflies**) to adjust the idle timeout, change the butterfly count (`20` to `200`, default `75`), or click **Launch Screensaver Now**.
