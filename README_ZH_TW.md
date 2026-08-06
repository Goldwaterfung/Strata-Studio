<div align="center">

# ![Strata](asset/strata.png) Strata Studio

### Strata Studio

<p align="center">
  <b>在幾秒鐘內完成 Session 混音、平衡與整理。<br>不再把時間浪費在繁瑣的 DAW 設定上—直接告訴你的 AI 助手，保持專注在音樂靈感中。</b>
</p>

[![License: CC BY 4.0](https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg)](http://creativecommons.org/licenses/by/4.0/)
[![Agent Skills](https://img.shields.io/badge/Agent%20Skills-Standard-green)](https://agentskills.io)
[![Multi-Runtime](https://img.shields.io/badge/Runtime-Claude%20Code%20·%20Codex%20·%20Cursor%20·%20Hermes%20·%20Gemini-blueviolet)](#快速開始--agentic-設定)
[![Plugin Host](https://img.shields.io/badge/Plugins-VST3%20%7C%20AU%20%7C%20CLAP-blue.svg)](#核心特性)

---

<p align="center">
  <a href="#快速開始--agentic-設定">快速開始</a> •
  <a href="#展示用例">展示用例</a> •
  <a href="#ai-agent-在-strata-studio-中能為你做些什麼">Agent 功能</a> •
  <a href="#核心特性">核心特性</a> •
  <a href="#開發者指南與原始碼編譯">開發者指南</a> •
  <a href="#許可證">許可證</a>
</p>

<p align="center">
  <b>其他語言:</b><br>
  <a href="README.md">English</a> •
  <a href="README_ZH.md">简体中文</a> •
  <a href="README_JA.md">日本語</a> •
  <a href="README_KO.md">한국어</a>
</p>
</div>

---

## 概述

你打開 DAW 是為了創作音樂——而不是把一半的腦力耗費在記憶快捷鍵、修飾鍵和複雜的控制介面上。**讓工具服務於你的創意，而非你的記憶。**

**Strata Studio** 讓你的 AI 助手直接掌控 DAW，讓你能夠純粹專注於音樂創作。無需再花費 30 分鐘手動調節串流音量、移除錄音背景噪音，或者為每條軌道掛載效果外掛，只需用自然語言向你的 AI 助手（**Claude Code**、**Cursor**、**Codex**、**Hermes** 或 **Gemini**）表達需求，你的工程即可在幾秒鐘內完成準備、平衡並進入製作狀態。

---

## 快速開始 & Agentic 設定

無需編寫任何終端命令，也不需要手動設定編譯環境。打開你常用的 AI Agent（**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode** 等 50+ 款工具），直接告訴它完成設定：

### 1. 複製程式碼庫
將專案程式碼庫複製到本地環境：

```bash
git clone https://github.com/Goldwaterfung/Strata-Studio.git
cd Strata-Studio
```

或者告訴你的 AI Agent：

```text
幫我複製 https://github.com/Goldwaterfung/Strata-Studio 並設定該專案
```

### 2. 編譯並安裝 Strata Studio（Agent 自動編譯應用）
告訴你的 Agent：

```text
幫我編譯並打包 Strata Studio
```

*(你的 Agent 會在背景自動執行 `./scripts/install_dependencies.sh` 和 `./scripts/build.sh release --package`)*。

### 3. 安裝 Skill（讓 Agent 學會控制 DAW）
告訴你的 Agent：

```text
從 https://github.com/Goldwaterfung/Strata-Studio 安裝 daw-cli skill
```

<details>
<summary><b>方案 2：手動設定 Skill 目錄</b></summary>
<br>

若需要手動將 Skill 安裝到特定 AI Agent 框架中，可複製或軟連結 `skills/daw-cli/` 目錄：

| Agent 框架 | 專案本地 Skill 路徑 | 全域使用者 Skill 路徑 |
| :--- | :--- | :--- |
| **Codex** | `.agents/skills/daw-cli` | `~/.agents/skills/daw-cli` |
| **Claude Code / Co-Work** | `.claude/skills/daw-cli` | `~/.claude/skills/daw-cli` |
| **Hermes** | `.hermes/skills/daw-cli` | `~/.hermes/skills/daw-cli` |
| **Antigravity** | `.agents/skills/daw-cli` | `~/.gemini/config/skills/daw-cli` |
| **Gemini CLI** | `.gemini/skills/daw-cli` | `~/.gemini/skills/daw-cli` |
| **OpenCode** | `.opencode/skills/daw-cli` | `~/.config/opencode/skills/daw-cli` |

安裝裝完成後，Agent 會將 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) 作為其操作手冊。

</details>

---

## 展示用例

以下是在 Strata Studio 中由 `daw-cli` 驅動 Agentic 自動化工作流程的實時演示：

### 🎬 用例 1：瞬間多音軌匯入、剪輯放置與自動著色

> **自然語言提示詞：**  
> *"匯入 `/Volumes/AudioDrives/Stems/Song_Session_A/` 中的所有音訊檔案。將所有剪輯對齊放置在時間線起始位置。自動清理軌道名稱中的多餘副檔名與底線，並根據樂器類型自動為軌道著色，讓 Session 保持整潔有序。"*

![用例 1：多音軌匯入與自動著色](asset/usecase-1.gif)

* **核心命令：** `daw-cli track create`, `daw-cli clip add-audio`, `daw-cli track sanitize-names`, `daw-cli track auto-color`

---

### 🎬 用例 2：多音軌 Session 編組編排與推桿佈局

> **自然語言提示詞：**  
> *"創建一個 124 BPM 的 Session，包含 8 件套鼓組：Kick, Snare Top, Snare Bottom, Hi-Hat, Tom 1, Tom 2, 以及立體聲 Overheads。自動進行顏色分類，將所有推桿音量預設為 -6 dB 留出動態餘量，將 overheads 左右聲道分別推至 100% 極左極右。"*

![用例 2：多音軌 Session 編排與推桿佈局](asset/usecase-2.gif)

* **核心命令：** `daw-cli transport set-tempo`, `daw-cli track create-batch`, `daw-cli track auto-color`, `daw-cli track set-gain`, `daw-cli track set-pan`

---

## Agentic 命令範例

以下是在 Strata Studio 中與 AI 助手協同工作的實際場景：

```text
User    ❯ 設定 Tempo 為 128 BPM，創建 Kick、Snare、HH 和 Tom 軌道，並平衡它們的音量層級。

Agent   ❯ [Strata Agentic Engine]
          ✓ 已將 Session 速度設定為 128.0 BPM (4/4 拍)
          ✓ 已創建 4 條音訊軌道: Kick, Snare, HH, Tom
          ✓ 已平衡軌道 1..4 的音量層級以防止過載剪切
          完成。隨時可以開始編曲。
```

```text
User    ❯ 在 Snare 軌道上掛載 FabFilter Pro-Q 3 等化器，將該外掛鏈複製到所有 Tom 軌道上。

Agent   ❯ [Strata Agentic Engine]
          ✓ 已掃描系統 VST3/AU 外掛庫
          ✓ 已在軌道 2 (Snare) 的 0 號插槽掛載 'FabFilter Pro-Q 3'
          ✓ 已將軌道 2 的外掛鏈複製到軌道 3..4
```

---

## 核心特性

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎛️ 完美音量平衡與乾淨錄音</h3>
      <p>自動平衡軌道音量，讓音樂清晰有勁並達到串流發行標準—同時自動切除錄音素材中的背景噪音、房間串音與靜音片段。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 快捷 FX 與外掛鏈設定</h3>
      <p>隨心載入你最愛的外掛（FabFilter, Waves, iZotope 等），只需一句提示詞即可在多條軌道上批量掛載專屬的人聲或鼓組混音鏈。</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎙️ 靈感優先的快速編曲</h3>
      <p>透過與錄音室助手自然對話，即刻搭建軌道架構、編排 Rhythm 節奏與 Synth 旋律、調整音量與聲相，並快速編輯時間線 Clip。</p>
    </td>
    <td width="50%" valign="top">
      <h3>⚡ 絲滑無爆音的錄音室性能</h3>
      <p>在包含數十條軌道與重度外掛的高負載工程中實時流暢播放與錄製，提供水晶般清晰的音訊體驗，絕無爆音、卡頓或延遲。</p>
    </td>
  </tr>
</table>

## AI Agent 在 Strata Studio 中能為你做些什麼

無需繁瑣地點擊選單、記憶快捷鍵或手動調節每一個旋鈕，你只需用自然語言命令你的 AI 助手（**Claude Code**, **Cursor**, **Codex**, **Hermes**, **Gemini** 等）。以下是你的 AI 助手目前能為你完成的工作：

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎚️ 智慧增益平衡 & 專案聲標調配</h3>
      <p>讓 Agent 自動平衡各音軌音量、調整聲相 (Pan)，或為多軌素材進行自動增益預調 (Gain-Staging)，確保混音清晰、富有衝擊力且留有充足動態餘量。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 VST3 / AU 外掛程式掛載 & 效果器鏈複製</h3>
      <p>告訴 Agent 掃描已安裝的效果器外掛程式（FabFilter, iZotope, Waves 等），自動插入 EQ 或壓縮器，並將滿意的效果器鏈瞬間複製到其他音軌。</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎹 MIDI 編曲 & 時間軸剪輯</h3>
      <p>編寫鼓組節奏、生成合成器 MIDI 片段、轉調音高、排列時間軸上的音訊剪輯 (Audio Clips)，無需滑鼠點按即可完成編段修改。</p>
    </td>
    <td width="50%" valign="top">
      <h3>⏱️ 播控傳輸 & 專案狀態管理</h3>
      <p>隨時設定專案速度 (BPM)、拍號、控制播放與停止、跳轉播放指針，並即時整理音軌顏色與名稱。</p>
    </td>
  </tr>
</table>

### 🚀 已支援的 AI 音樂製作指令

* **專案與播放控制**：設定專案 BPM、修改拍號、跳轉播放指針、播放/暫停/停止。
* **音軌建立與平衡**：新建音訊/樂器音軌、設定音量與聲相、靜音/獨奏、自動增益平衡 (Gain Staging)。
* **外掛程式管理**：掛載 VST3/AU 外掛程式、微調歸一化參數、在不同音軌間一鍵複製效果器鏈。
* **時間軸與 MIDI 編輯**：放置音訊剪輯、設定起始偏移與時長、編寫 MIDI 音符、旋律轉調。

---

## 開發者指南與原始碼編譯

Strata Studio 是一款專為 AI 驅動音樂製作打造的開源高性能 C++20 DAW 引擎。AI Agent 透過 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) 技能規範與 DAW 進行互動。

<details>
<summary><b>從原始碼建置與編譯</b></summary>
<br>

### 前置條件

工程使用 **vcpkg** 清單模式 (manifest mode) 管理依賴。

#### 必要工具
- **CMake** 3.20 或更高版本
- **Git**
- **支援 C++20 的編譯器**: Clang 12+, GCC 11+, 或 MSVC 2022+

#### 依賴自動安裝
運行設定腳本部署構建工具與相關依賴庫 (RtAudio, RtMidi, libsndfile, spdlog, Catch2)：

```bash
./scripts/install_dependencies.sh
```

---

### 構建步驟

1. **克隆倉庫**:
   ```bash
   git clone https://github.com/Goldwaterfung/Strata-Studio.git
   cd Strata-Studio
   ```

2. **配置與構建**:
   ```bash
   mkdir -p build/debug && cd build/debug
   cmake -DCMAKE_BUILD_TYPE=Debug ../../
   cmake --build . --parallel
   ```

3. **運行應用**:
   ```bash
   ./bin/strata_studio
   ```

---

### 構建選項

<table width="100%">
  <thead>
    <tr>
      <th align="left">選項</th>
      <th align="center">預設值</th>
      <th align="left">說明</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>BUILD_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>構建單元測試</td>
    </tr>
    <tr>
      <td><code>BUILD_PERFORMANCE_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>構建性能基準測試</td>
    </tr>
    <tr>
      <td><code>ENABLE_SIMD</code></td>
      <td align="center"><code>ON</code></td>
      <td>開啟 SIMD 指令集優化 (AVX2)</td>
    </tr>
    <tr>
      <td><code>USE_ASAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>開啟 Address Sanitizer (記憶體診斷)</td>
    </tr>
    <tr>
      <td><code>USE_TSAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>開啟 Thread Sanitizer (執行緒診斷)</td>
    </tr>
    <tr>
      <td><code>BUILD_PLUGINS</code></td>
      <td align="center"><code>ON</code></td>
      <td>構建外掛宿主支援</td>
    </tr>
  </tbody>
</table>

---

### 測試與 Release 構建

```bash
# 構建並運行單元測試
./scripts/build.sh debug --test

# 構建 Release 編譯產物
./scripts/build.sh release

# 構建打包版本
./scripts/build.sh release --package
```

</details>

---

## 開發路線圖

### Agentic Layer (`daw-cli`) 功能狀態

- [x] **Session 狀態與 Transport** (`status`, `transport`) - 已完全實現
- [x] **軌道管理與增益調配** (`track`, `prep`) - 已完全實現
- [x] **VST3 / AU 外掛宿主管理** (`plugin`) - 已完全實現
- [x] **Clip 與時間線編輯** (`clip`, `midi`) - 已完全實現
- [ ] **Bus 子混音與 Aux 特效路由** (`route`) - *開發中*
- [ ] **非視覺 DSP 分析與音訊智能** (`analyze`) - *開發中*
- [ ] **Stem 分軌匯出與非同步渲染任務** (`export`, `job`) - *開發中*

---

## 開源協議

<div align="center">

[![CC BY 4.0][cc-by-shield]][cc-by]

本專案基於 [Creative Commons Attribution 4.0 International License][cc-by] 協議開源。

[![CC BY 4.0][cc-by-image]][cc-by]

[cc-by]: http://creativecommons.org/licenses/by/4.0/
[cc-by-image]: https://i.creativecommons.org/l/by/4.0/88x31.png
[cc-by-shield]: https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg

</div>

---

## 致謝

<div align="center">

架構設計靈感來源於：  
**[Ardour](https://ardour.org/)** (libardour) • **[Bitwig Studio](https://www.bitwig.com/)** • **[Reaper](https://www.reaper.fm/)** • **[JUCE Framework](https://juce.com/)**

</div>
