<div align="center">

# ![Strata](asset/strata.png) Strata Studio

### Strata Studio

<p align="center">
  <b>在几秒钟内完成 Session 混音、平衡与整理。<br>不再把时间浪费在繁琐的 DAW 配置上—直接告诉你的 AI 助手，保持专注在音乐灵感中。</b>
</p>

[![License: CC BY 4.0](https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg)](http://creativecommons.org/licenses/by/4.0/)
[![Agent Skills](https://img.shields.io/badge/Agent%20Skills-Standard-green)](https://agentskills.io)
[![Multi-Runtime](https://img.shields.io/badge/Runtime-Claude%20Code%20·%20Codex%20·%20Cursor%20·%20Hermes%20·%20Gemini-blueviolet)](#快速开始--agentic-配置)
[![Plugin Host](https://img.shields.io/badge/Plugins-VST3%20%7C%20AU%20%7C%20CLAP-blue.svg)](#核心特性)

---

<p align="center">
  <a href="#快速开始--agentic-配置">快速开始</a> •
  <a href="#使用示例">使用示例</a> •
  <a href="#核心特性">核心特性</a> •
  <a href="#开发者与架构指南">开发者指南</a> •
  <a href="#开源协议">开源协议</a>
</p>

<p align="center">
  <b>其他语言:</b><br>
  <a href="README.md">English</a> •
  <a href="README_ZH_TW.md">繁體中文</a> •
  <a href="README_JA.md">日本語</a> •
  <a href="README_KO.md">한국어</a>
</p>
</div>

---

## 概述

你打开 DAW 是为了创作音乐——而不是把一半的录音室时间浪费在平衡轨道音量、清理 Clip 杂音或配置效果器链上。**别再当自己 DAW 的打工人了。**

**Strata Studio** 让你的 AI 助手直接掌控 DAW，让你能够纯粹专注于音乐创作。无需再花费 30 分钟手动调节流媒体音量、移除录音背景噪音，或者为每条轨道挂载效果插件，只需用自然语言向你的 AI 助手（**Claude Code**、**Cursor**、**Codex**、**Hermes** 或 **Gemini**）表达需求，你的工程即可在几秒钟内完成准备、平衡并进入制作状态。

---

## 快速开始 & Agentic 配置

无需编写任何终端命令，也不需要手动配置编译环境。打开你常用的 AI Agent（**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode** 等 50+ 款工具），直接告诉它完成配置：

### 1. 安装 Skill（让 Agent 学会控制 DAW）
告诉你的 Agent：

```text
从 https://github.com/Goldwaterfung/Strata-Studio 安装 daw-cli skill
```

### 2. 编译并安装 Strata Studio（Agent 自动编译应用）
告诉你的 Agent：

```text
帮我编译并打包 Strata Studio
```

*(你的 Agent 会在后台自动运行 `./scripts/install_dependencies.sh` 和 `./scripts/build.sh release --package`)*。

<details>
<summary><b>方案 2：手动配置 Skill 目录</b></summary>
<br>

若需要手动将 Skill 安装到特定 AI Agent 框架中，可复制或软链接 `skills/daw-cli/` 目录：

| Agent 框架 | 项目本地 Skill 路径 | 全局用户 Skill 路径 |
| :--- | :--- | :--- |
| **Codex** | `.agents/skills/daw-cli` | `~/.agents/skills/daw-cli` |
| **Claude Code / Co-Work** | `.claude/skills/daw-cli` | `~/.claude/skills/daw-cli` |
| **Hermes** | `.hermes/skills/daw-cli` | `~/.hermes/skills/daw-cli` |
| **Antigravity** | `.agents/skills/daw-cli` | `~/.gemini/config/skills/daw-cli` |
| **Gemini CLI** | `.gemini/skills/daw-cli` | `~/.gemini/skills/daw-cli` |
| **OpenCode** | `.opencode/skills/daw-cli` | `~/.config/opencode/skills/daw-cli` |

安装完成后，Agent 会将 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) 作为其操作手册。

</details>

---

## 使用示例

以下是在 Strata Studio 中与 AI 助手协同工作的实际场景：

```text
User    ❯ 设置 Tempo 为 128 BPM，创建 Kick、Snare、HH 和 Tom 轨道，并平衡它们的音量层级。

Agent   ❯ [Strata Agentic Engine]
          ✓ 已将 Session 速度设置为 128.0 BPM (4/4 拍)
          ✓ 已创建 4 条音频轨道: Kick, Snare, HH, Tom
          ✓ 已平衡轨道 1..4 的音量层级以防止过载剪切
          完成。随时可以开始编曲。
```

```text
User    ❯ 在 Snare 轨道上挂载 FabFilter Pro-Q 3 均衡器，并将该插件链复制到所有 Tom 轨道上。

Agent   ❯ [Strata Agentic Engine]
          ✓ 已扫描系统 VST3/AU 插件库
          ✓ 已在轨道 2 (Snare) 的 0 号插槽挂载 'FabFilter Pro-Q 3'
          ✓ 已将轨道 2 的插件链复制到轨道 3..4
```

---

## 核心特性

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎛️ 完美音量平衡与干净录音</h3>
      <p>自动平衡轨道音量，让音乐清晰有劲并达到流媒体发布标准—同时自动切除录音素材中的背景噪音、房间串音与静音片段。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 快捷 FX 与插件链配置</h3>
      <p>随心加载你最爱的插件（FabFilter, Waves, iZotope 等），只需一句提示词即可在多条轨道上批量挂载专属的人声或鼓组混音链。</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎙️ 灵感优先的快速编曲</h3>
      <p>通过与录音室助手自然对话，即刻搭建轨道架构、编排 Rhythm 节奏与 Synth 旋律、调整音量与声相，并快速编辑时间线 Clip。</p>
    </td>
    <td width="50%" valign="top">
      <h3>⚡ 丝滑无爆音的录音室性能</h3>
      <p>在包含数十条轨道与重度插件的高负载工程中实时流畅播放与录制，提供水晶般清晰的音频体验，绝无爆音、卡顿或延迟。</p>
    </td>
  </tr>
</table>

---

## 开发者与架构指南

作为音乐创作者，你只需用自然语言与 AI 交流。Agent 会自动读取 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) 并将其转换为底层的 IPC 操作——你无需记忆任何终端命令、语法或参数标志。

<details>
<summary><b>IPC 协议与 CLI 命令参考 (daw-cli)</b></summary>
<br>

Strata Studio 包含专为 AI Agent 和自动化工程控制设计的 Agentic IPC 守护进程与命令行工具 (`daw-cli`)。

完整 CLI 命令标志、JSON Schema、错误码及 IPC 协议文档，请参阅 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md)。

</details>

<br>

<details>
<summary><b>引擎架构 (8 层分层模型)</b></summary>
<br>

Strata Studio 基于严谨的 C++20 8 层架构构建，保障实时音频安全性、模块化可维护性与确定性 DSP 执行。

</details>

<details>
<summary><b>从源码构建与编译</b></summary>
<br>

### 前置条件

工程使用 **vcpkg** 清单模式 (manifest mode) 管理依赖。

#### 必要工具
- **CMake** 3.20 或更高版本
- **Git**
- **支持 C++20 的编译器**: Clang 12+, GCC 11+, 或 MSVC 2022+

#### 依赖自动安装
运行配置脚本部署构建工具与相关依赖库 (RtAudio, RtMidi, libsndfile, spdlog, Catch2)：

```bash
./scripts/install_dependencies.sh
```

---

### 构建步骤

1. **克隆仓库**:
   ```bash
   git clone https://github.com/Goldwaterfung/Strata-Studio.git
   cd Strata-Studio
   ```

2. **配置与构建**:
   ```bash
   mkdir -p build/debug && cd build/debug
   cmake -DCMAKE_BUILD_TYPE=Debug ../../
   cmake --build . --parallel
   ```

3. **运行应用**:
   ```bash
   ./bin/strata_studio
   ```

---

### 构建选项

<table width="100%">
  <thead>
    <tr>
      <th align="left">选项</th>
      <th align="center">默认值</th>
      <th align="left">说明</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>BUILD_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>构建单元测试</td>
    </tr>
    <tr>
      <td><code>BUILD_PERFORMANCE_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>构建性能基准测试</td>
    </tr>
    <tr>
      <td><code>ENABLE_SIMD</code></td>
      <td align="center"><code>ON</code></td>
      <td>开启 SIMD 指令集优化 (AVX2)</td>
    </tr>
    <tr>
      <td><code>USE_ASAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>开启 Address Sanitizer (内存诊断)</td>
    </tr>
    <tr>
      <td><code>USE_TSAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>开启 Thread Sanitizer (线程诊断)</td>
    </tr>
    <tr>
      <td><code>BUILD_PLUGINS</code></td>
      <td align="center"><code>ON</code></td>
      <td>构建插件宿主支持</td>
    </tr>
  </tbody>
</table>

---

### 测试与 Release 构建

```bash
# 构建并运行单元测试
./scripts/build.sh debug --test

# 构建 Release 编译产物
./scripts/build.sh release

# 构建打包版本
./scripts/build.sh release --package
```

</details>

---

## 开发路线图

### Agentic Layer (`daw-cli`) 功能状态

- [x] **Session 状态与 Transport** (`status`, `transport`) - 已完全实现
- [x] **轨道管理与增益调配** (`track`, `prep`) - 已完全实现
- [x] **VST3 / AU 插件宿主管理** (`plugin`) - 已完全实现
- [x] **Clip 与时间线编辑** (`clip`, `midi`) - 已完全实现
- [ ] **Bus 子混音与 Aux 特效路由** (`route`) - *开发中*
- [ ] **非视觉 DSP 分析与音频智能** (`analyze`) - *开发中*
- [ ] **Stem 分轨导出与异步渲染任务** (`export`, `job`) - *开发中*

---

## 开源协议

<div align="center">

[![CC BY 4.0][cc-by-shield]][cc-by]

本项目基于 [Creative Commons Attribution 4.0 International License][cc-by] 协议开源。

[![CC BY 4.0][cc-by-image]][cc-by]

[cc-by]: http://creativecommons.org/licenses/by/4.0/
[cc-by-image]: https://i.creativecommons.org/l/by/4.0/88x31.png
[cc-by-shield]: https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg

</div>

---

## 致谢

<div align="center">

架构设计灵感来源于：  
**[Ardour](https://ardour.org/)** (libardour) • **[Bitwig Studio](https://www.bitwig.com/)** • **[Reaper](https://www.reaper.fm/)** • **[JUCE Framework](https://juce.com/)**

</div>
