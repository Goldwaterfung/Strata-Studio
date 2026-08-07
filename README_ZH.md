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
  <a href="#为什么选择-agentic-daw">为什么选择 Agentic DAW</a> •
  <a href="#快速开始--agentic-配置">快速开始</a> •
  <a href="#展示用例">展示用例</a> •
  <a href="#ai-agent-在-strata-studio-中能为你做些什么">Agent 功能</a> •
  <a href="#核心特性">核心特性</a> •
  <a href="#开发者指南与源码编译">开发者指南</a> •
  <a href="#许可证">许可证</a>
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

你打开 DAW 是为了创作音乐——而不是把一半的脑力耗费在记忆快捷键、修饰键和复杂的控制界面上。**让工具服务于你的创意，而非你的记忆。**

**Strata Studio** 是一款支持 AI 原生掌控的 Agentic DAW，让你能够纯粹专注于音乐创作。无需再花费 30 分钟手动调节流媒体音量、移除录音背景噪音，或者为每条轨道挂载效果插件，只需用自然语言向你的 AI 代理（**Claude Code**、**Cursor**、**Codex**、**Hermes** 或 **Gemini**）表达需求，你的工程即可在几秒钟内完成准备、平衡并进入制作状态。

---

## 为什么选择 Agentic DAW？

传统的 DAW 是为键盘与鼠标时代设计的，每一个细节操作都需要手动完成。当工程庞大至 100+ 轨道时，你的创作精力会被大量重复的机械劳动消耗殆尽。

| 传统 DAW 工作流 | Strata Studio Agentic 工作流 |
| :--- | :--- |
| **数百次点击与手动编辑**：逐一选择、调整增益、着色并路由 100+ 轨道。 | **一条简单命令**：一句自然语言指令，瞬间精准控制 100+ 轨道。 |
| **快捷键负担**：上网查找被遗忘的键盘快捷键与复杂修饰键。 | **自然语言交互**：直接用日常语言告诉 AI Agent 你的需求。 |
| **无语意的轨道标签**：传统 DAW 将轨道名视为死板的文本，需要手动分组编组。 | **语义智能识别**：AI Agent 识别乐器上下文（如 `GTR_L_57` → 吉他 Bus）并自动分轨分组。 |
| **创作被打断**：为了技术配置、增益打理和文件预处理而被迫退出灵感状态。 | **纯粹灵感流动**：AI Agent 在后台作为你不知疲倦的录音室助手高效运转。 |

---

## 快速开始 & Agentic 配置

无需编写任何终端命令，也不需要手动配置编译环境。打开你常用的 AI Agent（**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode** 等 50+ 款工具），直接告诉它完成配置：

### 1. 克隆代码库
将项目代码库克隆到本地环境：

```bash
git clone https://github.com/Goldwaterfung/Strata-Studio.git
cd Strata-Studio
```

或者告诉你的 AI Agent：

```text
帮我克隆 https://github.com/Goldwaterfung/Strata-Studio 并配置该项目
```

### 2. 编译并安装 Strata Studio（Agent 自动编译应用）
告诉你的 Agent：

```text
帮我编译并打包 Strata Studio
```

*(你的 Agent 会在后台自动运行 `./scripts/install_dependencies.sh` 和 `./scripts/build.sh release --package`)*。

### 3. 安装 Skill（让 Agent 学会控制 DAW）
告诉你的 Agent：

```text
从 https://github.com/Goldwaterfung/Strata-Studio 安装 daw-cli skill
```

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

## 展示用例

以下是在 Strata Studio 中由 `daw-cli` 驱动 Agentic 自动化工作流的实时演示：

### 🎬 用例 1：瞬间多音轨导入、剪辑放置与自动着色

> **自然语言提示词：**  
> *"导入 `/Volumes/AudioDrives/Stems/Song_Session_A/` 中的所有音频文件。将所有剪辑对齐放置在时间线起始位置。自动清理轨道名称中的多余扩展名与下划线，并根据乐器类型自动为轨道着色，让 Session 保持整洁有序。"*

![用例 1：多音轨导入与自动着色](asset/usecase-1.gif)

* **核心命令：** `daw-cli track create`, `daw-cli clip add-audio`, `daw-cli track sanitize-names`, `daw-cli track auto-color`

---

### 🎬 用例 2：多音轨 Session 编组编排与推子布局

> **自然语言提示词：**  
> *"创建一个 124 BPM 的 Session，包含 8 件套鼓组：Kick, Snare Top, Snare Bottom, Hi-Hat, Tom 1, Tom 2, 以及立体声 Overheads。自动进行颜色分类，将所有推子音量预设为 -6 dB 留出动态余量，并将 overheads 左右声道分别推至 100% 极左极右。"*

![用例 2：多音轨 Session 编排与推子布局](asset/usecase-2.gif)

* **核心命令：** `daw-cli transport set-tempo`, `daw-cli track create-batch`, `daw-cli track auto-color`, `daw-cli track set-gain`, `daw-cli track set-pan`

---

### 🎬 用例 3：选择性 Stem 导入、合成器插件挂载与子混音总线路由

> **自然语言提示词：**  
> *"检查 `/Volumes/AudioDrives/Stems/Song_Session_A/stems` 中的 stems 以及我已安装的插件库。帮我搭建一个专业的 Synth Rock 混音工程。仅导入鼓组与吉他 stems，为 'Sub Bass' 与 'Synth Bass' 挂载 Serum 2 插件，并构建专业的子混音总线编组。"*

![用例 3：选择性 Stem 导入、合成器插件挂载与子混音总线路由](asset/usecase-3.gif)

* **核心命令：** `daw-cli plugin scan`, `daw-cli plugin add`, `daw-cli route folder`, `daw-cli prep gain-stage`

---

## Agentic 命令示例

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

## AI Agent 在 Strata Studio 中能为你做些什么

无需繁琐地点击菜单、记忆快捷键或手动调节每一个旋钮，你只需用自然语言命令你的 AI 助手（**Claude Code**, **Cursor**, **Codex**, **Hermes**, **Gemini** 等）。以下是你的 AI 助手目前能为你完成的工作：

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎚️ 智能增益平衡 & 工程声标调配</h3>
      <p>让 Agent 自动平衡各轨道音量、调整声相 (Pan)，或为多轨素材进行自动增益预调 (Gain-Staging)，确保混音清晰、富有冲击力且留有充足动态余量。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 VST3 / AU 插件挂载 & 效果器链复制</h3>
      <p>告诉 Agent 扫描已安装的效果器插件（FabFilter, iZotope, Waves 等），自动插入 EQ 或压缩器，并将满意的效果器链瞬间复制到其他轨道。</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎹 MIDI 编曲 & 时间线剪辑</h3>
      <p>编写鼓组节奏、生成合成器 MIDI 片段、转调音高、排列时间线上的音频剪辑 (Audio Clips)，无需鼠标点按即可完成编段修改。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔀 总线子混音 & 辅助效果器路由</h3>
      <p>将轨道归类编组至文件夹子混音总线，配置推子前/后辅助发送，并跨效果器链设置动态侧链压缩触发。</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>⏱️ 播控传输 & 工程状态管理</h3>
      <p>随时设置工程速度 (BPM)、拍号、控制播放与停止、跳转播放指针，并实时整理轨道颜色与名称。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔬 非视觉 DSP 分析与音频智能</h3>
      <p>分析轨道间的 Bark 频段遮蔽与能量碰撞、精确定位窄 Q 谐振峰与对应音高、计算多轨 Pearson 相位相关矩阵、校准多麦克风相位，并实时监测 LUFS 与 True-Peak 遥测指标。</p>
    </td>
  </tr>
</table>

### 🚀 已支持的 AI 音乐制作指令

* **工程与播放控制**：设定项目 BPM、修改拍号、跳转播放指针、播放/暂停/停止。
* **轨道建立与平衡**：新建音频/乐器轨道、设置音量与声相、静音/独奏、自动增益平衡 (Gain Staging)。
* **插件管理**：挂载 VST3/AU 插件、微调归一化参数、在不同轨道间一键复制效果器链。
* **时间线与 MIDI 编辑**：放置音频剪辑、设置起始偏移与时长、编写 MIDI 音符、旋律转调。
* **总线子混音与辅助效果器路由**：将轨道输出重定向至文件夹编组总线、配置推子前/后辅助效果器发送、关联插件侧链压缩触发器。
* **非视觉 DSP 分析与音频智能**：检测 Bark 标度频段遮蔽、捕捉谐振峰、评估多轨相位相关矩阵与样本级对齐偏移、流式获取实时 LUFS/dBTP 遥测、检查 7 频段频谱能量分布以及立体声宽度与单声道兼容性。

---

## 开发者指南与源码编译

Strata Studio 是一款专为 AI 驱动音乐制作打造的开源高性能 C++20 DAW 引擎。AI Agent 通过 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) 技能规范与 DAW 进行交互。

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
- [x] **Bus 子混音与 Aux 特效路由** (`route`) - 已完全实现
- [x] **非视觉 DSP 分析与音频智能** (`analyze`) - 已完全实现
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
