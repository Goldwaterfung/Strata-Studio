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
  <a href="#使用範例">使用範例</a> •
  <a href="#核心特性">核心特性</a> •
  <a href="#開發者與架構指南">開發者指南</a> •
  <a href="#開源協議">開源協議</a>
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

## 使用範例

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

---

## 開發者與架構指南

作為音樂創作者，你只需用自然語言與 AI 交流。Agent 會自動讀取 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) 並將其轉換為底層的 IPC 操作——你無需記憶任何終端命令、語法或參數標誌。

<details>
<summary><b>IPC 協定與 CLI 命令參考 (daw-cli)</b></summary>
<br>

Strata Studio 包含專為 AI Agent 和自動化工程控制設計的 Agentic IPC 守護進程與命令列工具 (`daw-cli`)。

完整 CLI 命令標誌、JSON Schema、錯誤碼及 IPC 協定文件，請參閱 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md)。

</details>

<br>

<details>
<summary><b>引擎架構 (8 層分層模型)</b></summary>
<br>

Strata Studio 基於嚴謹的 C++20 8 層架構構建，保障實時音訊安全性、模組化可維護性與確定性 DSP 執行。

</details>

<details>
<summary><b>從原始碼構建與編譯</b></summary>
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
