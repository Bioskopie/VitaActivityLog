# 3DS Activity Log for PS Vita

A custom homebrew application for the PS Vita that replicates the beloved **Nintendo 3DS Activity Log** experience. It tracks your playtime down to the exact day, week, month, year, and all-time, allowing you to browse your gaming habits with a clean 3DS-inspired grid interface.

![Activity Log Banner](https://img.shields.io/badge/PS%20Vita-Homebrew-blue.svg) ![Version](https://img.shields.io/badge/version-1.0-green.svg)

---

## ✨ Features

* **Complete Time Range Analytics:** Switch between Daily Records, 7-Day Weekly views, Monthly charts, Yearly summaries, and All-Time Rankings using the L/R triggers and D-Pad.
* **Seamless Background Tracking:** Automatically syncs with **BetterTrackPlug** / **TrackPlug** to log new sessions and daily playtimes without background lag or battery drain.
* **Manual Playtime Editor:** Highlight any game, press **Confirm** to open its 3DS Info Card, and press **Triangle (`△`)** to manually adjust your pre-app baseline hours and minutes.
* **Automatic Region Button Detection:** Automatically detects if you own a Western or Japanese PS Vita (`SCE_SYSTEM_PARAM_ENTER_BUTTON`) and configures `O` vs `X` button prompts accordingly.
* **OLED Dark Mode Toggle:** Press **Square (`[]`)** at any time to switch between the classic 3DS Light Grid theme and an OLED-friendly Dark Theme.
* **Custom LiveArea Presentation:** Features a 3DS-styled bubble icon (`icon0.png`), startup gateway, and custom background pamphlet.

---

## 🛠️ Prerequisites & Requirements

To make this app work properly on your PS Vita, your console must meet the following requirements:

1. **A Jailbroken PS Vita** running HENkaku/Enso (Firmware 3.60 – 3.74).
2. **VitaShell** installed for managing files and installing `.vpk` files.
3. **BetterTrackPlug** (or TrackPlug) installed and configured in your `config.txt` so your Vita tracks game playtimes in the background.

---

## 🚀 Installation Guide

### Step 1: Install the Background Plugin (If you haven't already)
1. Ensure you have **BetterTrackPlug** active in your taiHEN configuration file (`ur0:tai/config.txt` under `*KERNEL`).
2. This allows the app to automatically read your game playtimes from `ux0:/data/TrackPlug/Records/`.

### Step 2: Install the App
1. Download the latest **`VitaActivityLog.vpk`** from the [Releases page](../../releases).
2. Transfer `VitaActivityLog.vpk` to your PS Vita via USB or FTP using **VitaShell**.
3. Open **VitaShell**, navigate to the `.vpk`, and press your confirm button to install it.
4. *(Recommended)* Delete the installation bubble and reboot your Vita to ensure the LiveArea icon and background cache refresh properly.

---

## 🎮 How to Use

* **Switch Views:** Press **`SELECT`** to flip between **Daily Records** and **Software Library & Rankings**.
* **Change Time Filters:** Press **`L` or `R`** to cycle between Day, Week, Month, Year, and All-Time.
* **Change Dates:** Use **`D-Pad Left / Right`** to browse previous days, weeks, months, or years.
* **Edit Past / Baseline Hours:** Highlight a game and press your confirm button to open its **Game Info Card**, then press **`Triangle`** to edit your hours/minutes.
* **Refresh Playtime Live:** Press **`START`** inside the app to instantly reload the latest stats from your records.
* **Toggle Dark Mode:** Press **`Square`** anytime.

---

## 📜 Credits & Built With

* Built using **VitaSDK** and **vita2d**.
* Inspired by the official Nintendo 3DS Activity Log app.
