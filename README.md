<p align="center">
  <img src="logo.svg" width="120" alt="Deskwarp Logo">
</p>

<h1 align="center">Deskwarp</h1>

<p align="center">
  <strong>Bring the legendary Wobbly Windows effect to your desktop.</strong>
</p>

<p align="center">
  <img src="win10.svg" width="22" alt="Windows 10" valign="middle">
  &nbsp;
  <img src="win11.svg" width="22" alt="Windows 11" valign="middle">
  <br>
  <strong>Supported OS:</strong> Windows 10 & 11
</p>

<p align="center">
  <a href="https://github.com/doebalov/Deskwarp/releases">
    <img src="https://img.shields.io/badge/version-1.0-6C63FF?style=for-the-badge" alt="Version">
  </a>
  <a href="https://github.com/doebalov/Deskwarp">
    <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white" alt="Platform">
  </a>
  <a href="#-built-with">
    <img src="https://img.shields.io/badge/built%20with-Qt%206-41CD52?style=for-the-badge&logo=qt&logoColor=white" alt="Qt 6">
  </a>
  <a href="https://dalink.to/doebalov">
    <img src="https://img.shields.io/badge/♥%20Support-Donate-FF4B7E?style=for-the-badge" alt="Donate">
  </a>
</p>

<p align="center">
  <a href="#-features">Features</a> •
  <a href="#-realism-levels">Realism</a> •
  <a href="#-installation">Installation</a> •
  <a href="#-building-from-source">Build</a> •
  <a href="#-languages">Languages</a> •
  <a href="#-support-the-author">Support</a>
</p>

---

## ✨ Overview

**Deskwarp** recreates the iconic **Wobbly Windows** effect — that soft, springy, gelatinous motion your windows follow as you drag them across the screen.

Powered by a real physics simulation and rendered on the GPU through **Direct3D 11** and **DirectComposition**, Deskwarp delivers buttery-smooth deformation with virtually no impact on your workflow. It lives quietly in your system tray, adapts to your Windows theme and accent color, and just works.

<br>

## 🎯 Features

| | |
|---|---|
| 🌊 **Wobbly Windows** | Physics-based soft-body deformation applied to any window when dragged by its title bar. |
| 🎚️ **Adjustable Realism** | Four distinct realism levels — from heavy and grounded to wildly floppy. |
| ⚡ **GPU Accelerated** | Rendered via Direct3D 11 + DirectComposition for a fluid, high-framerate experience. |
| 🎨 **Native Theming** | Automatically follows your Windows light/dark mode and system accent color. |
| 🖥️ **System Tray** | Runs unobtrusively in the tray with quick **Open** and **Exit** controls. |
| 🚀 **Auto-Start** | Optional launch on boot in a silent `--background` mode. |
| 🔒 **Single Instance** | Guarantees one clean instance at all times, bringing the running app to focus on relaunch. |
| 🛡️ **Hardened Runtime** | DEP, ASLR, Control Flow Guard, strict handle checks and DLL-search hardening. |
| 🌍 **15 Languages** | Fully localized, including full right-to-left support for Arabic. |
| 💾 **Persistent Settings** | Your preferences are saved automatically and restored on every launch. |
| 📐 **HiDPI Ready** | Crisp, DPI-aware rendering across every display. |

<br>

## 🎚️ Realism Levels

Deskwarp exposes a single, intuitive slider that reshapes the entire physics model in real time:

| Level | Character | Feel |
|:-----:|-----------|------|
| **1** | **Grounded** | Heavy, tightly damped — subtle and professional. |
| **2** | **Balanced** *(default)* | The classic wobble, perfectly tuned. |
| **3** | **Soft** | Springier, more playful motion. |
| **4** | **Extreme** | Maximum floppiness — pure, joyful chaos. |

Each level fine-tunes friction, stiffness, mass, restitution and rest thresholds to craft a distinct personality of movement.

<br>

## 📦 Installation

1. Download the latest release from the [**Releases**](https://github.com/doebalov/Deskwarp/releases) page.
2. Run **`Deskwarp.exe`**.
3. Toggle **Wobbly Windows** on, pick your realism level, and start dragging.

> Deskwarp is fully portable and self-contained — no installer required.

<br>

## 🛠️ Building from Source

Deskwarp is built with **CMake** and **Qt 6**, and links statically for a single, dependency-free executable.

### Prerequisites
- **CMake** ≥ 3.16
- **Qt 6** (Core, Gui, Widgets, Svg, Network)
- A C++ toolchain for Windows (MinGW or MSVC)

### Build
```bash
git clone https://github.com/doebalov/Deskwarp.git
cd Deskwarp

cmake -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build --config Release
