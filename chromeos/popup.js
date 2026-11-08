"use strict";

const DEFAULT_SETTINGS = {
  enabled: true,
  idleSeconds: 120,
  butterflyCount: 75
};

const enabledToggle = document.getElementById("enabledToggle");
const idleSelect = document.getElementById("idleSelect");
const countRange = document.getElementById("countRange");
const countLabel = document.getElementById("countLabel");
const launchBtn = document.getElementById("launchBtn");

async function initPopup() {
  if (typeof chrome !== "undefined" && chrome.storage && chrome.storage.local) {
    const stored = await chrome.storage.local.get(DEFAULT_SETTINGS);
    enabledToggle.checked = Boolean(stored.enabled);
    idleSelect.value = String(stored.idleSeconds || DEFAULT_SETTINGS.idleSeconds);
    countRange.value = String(stored.butterflyCount || DEFAULT_SETTINGS.butterflyCount);
    countLabel.textContent = String(countRange.value);
  }
}

function saveSettings() {
  const settings = {
    enabled: enabledToggle.checked,
    idleSeconds: Number(idleSelect.value) || 120,
    butterflyCount: Number(countRange.value) || 75
  };
  if (typeof chrome !== "undefined" && chrome.storage && chrome.storage.local) {
    chrome.storage.local.set(settings);
  }
}

enabledToggle.addEventListener("change", saveSettings);
idleSelect.addEventListener("change", saveSettings);
countRange.addEventListener("input", () => {
  countLabel.textContent = String(countRange.value);
  saveSettings();
});

launchBtn.addEventListener("click", () => {
  if (typeof chrome !== "undefined" && chrome.runtime && chrome.runtime.sendMessage) {
    chrome.runtime.sendMessage({ type: "LAUNCH_SCREENSAVER" }, () => {
      window.close();
    });
  } else {
    window.open(`screensaver.html?count=${encodeURIComponent(countRange.value)}`, "_blank");
  }
});

initPopup();
