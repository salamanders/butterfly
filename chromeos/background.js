"use strict";

const DEFAULT_SETTINGS = {
  enabled: true,
  idleSeconds: 120,
  butterflyCount: 75
};

let screensaverWindowId = null;
let launchingWindow = false;

async function getSettings() {
  try {
    const stored = await chrome.storage.local.get(DEFAULT_SETTINGS);
    return {
      enabled: Boolean(stored.enabled),
      idleSeconds: Math.max(15, Number(stored.idleSeconds) || DEFAULT_SETTINGS.idleSeconds),
      butterflyCount: Math.min(300, Math.max(10, Number(stored.butterflyCount) || DEFAULT_SETTINGS.butterflyCount))
    };
  } catch (_) {
    return { ...DEFAULT_SETTINGS };
  }
}

async function applyIdleInterval() {
  const settings = await getSettings();
  chrome.idle.setDetectionInterval(settings.idleSeconds);
}

async function openScreensaverWindow() {
  if (launchingWindow) return;

  if (screensaverWindowId !== null) {
    try {
      await chrome.windows.get(screensaverWindowId);
      return; // Already open
    } catch (_) {
      screensaverWindowId = null;
    }
  }

  launchingWindow = true;
  try {
    const settings = await getSettings();
    chrome.power.requestKeepAwake("display");

    const url = chrome.runtime.getURL(`screensaver.html?count=${encodeURIComponent(settings.butterflyCount)}`);
    const win = await chrome.windows.create({
      url,
      type: "popup",
      state: "fullscreen",
      focused: true
    });
    if (win && typeof win.id === "number") {
      screensaverWindowId = win.id;
    }
  } catch (err) {
    console.error("[Butterfly Screensaver] Failed to open fullscreen window:", err);
    chrome.power.releaseKeepAwake();
  } finally {
    launchingWindow = false;
  }
}

async function closeScreensaverWindow() {
  const winId = screensaverWindowId;
  screensaverWindowId = null;
  chrome.power.releaseKeepAwake();

  if (winId !== null) {
    try {
      await chrome.windows.remove(winId);
    } catch (_) {
      // Window was already closed by the user orscreensaver script
    }
  }
}

chrome.runtime.onInstalled.addListener(async () => {
  const existing = await chrome.storage.local.get(null);
  const merged = { ...DEFAULT_SETTINGS, ...existing };
  await chrome.storage.local.set(merged);
  chrome.idle.setDetectionInterval(merged.idleSeconds);
});

chrome.runtime.onStartup.addListener(() => {
  applyIdleInterval();
});

chrome.storage.onChanged.addListener((changes, area) => {
  if (area === "local" && changes.idleSeconds) {
    const sec = Math.max(15, Number(changes.idleSeconds.newValue) || DEFAULT_SETTINGS.idleSeconds);
    chrome.idle.setDetectionInterval(sec);
  }
});

chrome.idle.onStateChanged.addListener(async (newState) => {
  if (newState === "idle") {
    const settings = await getSettings();
    if (settings.enabled) {
      await openScreensaverWindow();
    }
  } else if (newState === "active" || newState === "locked") {
    await closeScreensaverWindow();
  }
});

chrome.windows.onRemoved.addListener((windowId) => {
  if (windowId === screensaverWindowId) {
    screensaverWindowId = null;
    chrome.power.releaseKeepAwake();
  }
});

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (!msg || typeof msg.type !== "string") return false;

  if (msg.type === "LAUNCH_SCREENSAVER") {
    openScreensaverWindow().then(() => sendResponse({ ok: true }));
    return true;
  }
  if (msg.type === "DISMISS_SCREENSAVER") {
    closeScreensaverWindow().then(() => sendResponse({ ok: true }));
    return true;
  }
  return false;
});

applyIdleInterval();
