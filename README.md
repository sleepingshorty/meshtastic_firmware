

# 📡 Meshtastic Firmware Mod – Debug & Stationary Network Extensions

> ⚠️ **Experimental Build – Use at Your Own Risk**  
> This firmware modification is intended for **debugging** and **experimentation in stationary mesh networks**.  
> It is **not optimized for robustness, mobile use, or long-term deployment**.  
> The implementation is quick & dirty, tested only in the field, and may include hardcoded logic or unstable behavior.

This fork introduces several bot-style commands for radio diagnostics, routing transparency, and manual tuning of forwarding behavior. It is meant to help with understanding network dynamics in complex setups (e.g. fixed nodes on rooftops, multi-hop routing, etc.).

---

## ✅ Available Bot Commands

### `/status_info`
Displays radio and routing statistics:  
- Sent / received packets  
- Duplicate packets  
- Dropped forwards  

### `/neighbor_info`
Lists currently detected **0-hop neighbors** (directly reachable nodes):  
- Includes Node ID  
- Shows last recorded SNR (Signal-to-Noise Ratio)

### `"ping"`
Responds to `"ping"` messages with:  
- Hop count  
- RSSI (signal strength)  
- SNR (signal-to-noise ratio)

### `/set_priority <3–8>`
Only as node admin!

Manually sets the **priority** for this client:  
- Lower value = higher priority (faster sending)  
- Example: `3 = high priority`, `8 = lowest priority`  
- Use `-1` to revert to default SNR-based behavior

### `/get_priority`
Displays the currently active **manual priority**, or reports fallback to the SNR-based logic if unset.

### `/set_rtm_count <n>`
Only as node admin!

Configures the **repeat-to-mute threshold**:  
- A node will retransmit a packet only if it has heard it from fewer than `n` other nodes  
- Default is `1` (classic repeat-to-mute)  
- Higher values (e.g., `2` or `3`) allow increased redundancy

### `/get_rtm_count`
Returns the current **RTM threshold value**:  
- Defines how many times a packet must be overheard before it is suppressed

### `/enable_tx`
Only as node admin!

Enables TX. **RTM threshold value**:  
- Enable TX. You can disable tx for energy saving or recover from a misconfiguration state

---

---

## 📝 Attribution & License Notice

This firmware mod includes code from the project [VilemR/meshtstic_modules_mod](https://github.com/VilemR/meshtstic_modules_mod), licensed under the GNU General Public License v3.0.

The following components are directly or partially derived from that project:

- `RangeTestModule.py` (included without modification)
- Parts of the `ping` command logic (copied and adapted)

All modifications made in this repository are also subject to the terms of the [GPL-3.0 License](https://www.gnu.org/licenses/gpl-3.0.html).



<div align="center" markdown="1">
<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

<a href="https://trendshift.io/repositories/5524" target="_blank"><img src="https://trendshift.io/api/badge/repositories/5524" alt="meshtastic%2Ffirmware | Trendshift" style="width: 250px; height: 55px;" width="250" height="55"/></a>

</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>

## Overview

This repository contains the official device firmware for Meshtastic, an open-source LoRa mesh networking project designed for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware supports various hardware platforms, including ESP32, nRF52, RP2040/RP2350, and Linux-based devices.

Meshtastic enables text messaging, location sharing, and telemetry over a decentralized mesh network, making it ideal for outdoor adventures, emergency preparedness, and remote operations.

### Get Started

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.

Join our community and help improve Meshtastic! 🚀

## Stats

![Alt](https://repobeats.axiom.co/api/embed/8025e56c482ec63541593cc5bd322c19d5c0bdcf.svg "Repobeats analytics image")
