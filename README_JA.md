<div align="center">

# ![Strata](asset/strata.png) Strata Studio

### Strata Studio

<p align="center">
  <b>セッションのミキシング、バランス調整、整理を数秒で完了。<br>面倒なDAWのセットアップに時間を費やすのはやめましょう。AIアシスタントに指示を出し、クリエイティブなフローを維持できます。</b>
</p>

[![License: CC BY 4.0](https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg)](http://creativecommons.org/licenses/by/4.0/)
[![Agent Skills](https://img.shields.io/badge/Agent%20Skills-Standard-green)](https://agentskills.io)
[![Multi-Runtime](https://img.shields.io/badge/Runtime-Claude%20Code%20·%20Codex%20·%20Cursor%20·%20Hermes%20·%20Gemini-blueviolet)](#クイックスタート--agenticセットアップ)
[![Plugin Host](https://img.shields.io/badge/Plugins-VST3%20%7C%20AU%20%7C%20CLAP-blue.svg)](#主な機能)

---

<p align="center">
  <a href="#クイックスタート--agenticセットアップ">クイックスタート</a> •
  <a href="#使用例">使用例</a> •
  <a href="#主な機能">主な機能</a> •
  <a href="#開発者ガイド--アーキテクチャ">開発者ガイド</a> •
  <a href="#ライセンス">ライセンス</a>
</p>

<p align="center">
  <b>他の言語:</b><br>
  <a href="README.md">English</a> •
  <a href="README_ZH.md">简体中文</a> •
  <a href="README_ZH_TW.md">繁體中文</a> •
  <a href="README_KO.md">한국어</a>
</p>
</div>

---

## 概要

あなたがDAWを開いたのは音楽を制作するためであり、スタジオ時間の半分をトラックの音量バランス調整、クリップのノイズ除去、エフェクトチェーンの設定に費やすためではありません。**DAWの「雑務係」になるのはもうやめましょう。**

**Strata Studio**は、AIアシスタントにDAWの操作権限を与えることで、あなたが純粋に音楽制作だけに集中できるようにします。ストリーミング用の音量調整、録音素材の背景ノイズ除去、全トラックへのエフェクトプラグイン設定に30分も費やす代わりに、AIアシスタント（**Claude Code**、**Cursor**、**Codex**、**Hermes**、**Gemini**）に自然言語で希望を伝えるだけです。セッションの準備とバランス調整が数秒で完了し、すぐに楽曲制作を開始できます。

---

## クイックスタート & Agenticセットアップ

ターミナルコマンドや手動コンパイルは不要です。お使いのAIエージェント（**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode** など 50 以上のツール）を開き、セットアップを指示してください：

### 1. スキルのインストール（エージェントにDAW操作を学習させる）
エージェントに伝える：

```text
https://github.com/Goldwaterfung/Strata-Studio から daw-cli スキルをインストールしてください
```

### 2. Strata Studioのビルド＆セットアップ（エージェントがアプリをコンパイル）
エージェントに伝える：

```text
Strata Studio をビルドしてパッケージ化してください
```

*(エージェントがバックグラウンドで `./scripts/install_dependencies.sh` と `./scripts/build.sh release --package` を自動実行します)*。

<details>
<summary><b>オプション2：スキルディレクトリの手動セットアップ</b></summary>
<br>

手動でお好みのAIエージェントフレームワークにスキルを導入する場合は、`skills/daw-cli/` ディレクトリをコピーまたはシンボリックリンク配置してください：

| エージェントフレームワーク | ローカルワークスペーススキルパス | グローバルユーザースキルパス |
| :--- | :--- | :--- |
| **Codex** | `.agents/skills/daw-cli` | `~/.agents/skills/daw-cli` |
| **Claude Code / Co-Work** | `.claude/skills/daw-cli` | `~/.claude/skills/daw-cli` |
| **Hermes** | `.hermes/skills/daw-cli` | `~/.hermes/skills/daw-cli` |
| **Antigravity** | `.agents/skills/daw-cli` | `~/.gemini/config/skills/daw-cli` |
| **Gemini CLI** | `.gemini/skills/daw-cli` | `~/.gemini/skills/daw-cli` |
| **OpenCode** | `.opencode/skills/daw-cli` | `~/.config/opencode/skills/daw-cli` |

導入後、エージェントは [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) を運用マニュアルとして使用します。

</details>

---

## 使用例

Strata StudioでAIアシスタントと共同作業する実際の例です：

```text
User    ❯ テンポを 128 BPM に設定し、Kick、Snare、HH、Tom のトラックを作成して、音量レベルのバランスを整えて。

Agent   ❯ [Strata Agentic Engine]
          ✓ セッションテンポを 128.0 BPM (4/4拍子) に設定しました
          ✓ 4つのオーディオトラック（Kick, Snare, HH, Tom）を作成しました
          ✓ クリッピングを防ぐためトラック 1..4 の音量レベルを調整しました
          完了しました。アレンジメント作業をどうぞ。
```

```text
User    ❯ Snare トラックに FabFilter Pro-Q 3 イコライザーを追加し、そのプラグインチェーンを Tom トラックすべてにコピーして。

Agent   ❯ [Strata Agentic Engine]
          ✓ システムの VST3/AU プラグインをスキャンしました
          ✓ トラック 2 (Snare) のスロット 0 に 'FabFilter Pro-Q 3' を挿入しました
          ✓ トラック 2 のプラグインチェーンをトラック 3..4 にコピーしました
```

---

## 主な機能

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎛️ 完璧な音量バランスとクリアな録音</h3>
      <p>トラックレベルを自動調整し、楽曲をクリアでパンチの効いたストリーミング対応の音質に仕上げます。録音素材の背景ノイズ、部屋の反射音、無音部分も自動カット。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 瞬時に完了するFX・プラグイン設定</h3>
      <p>お気に入りのプラグイン（FabFilter, Waves, iZotope など）を読み込み、一言伝えるだけでボーカルやドラム用のミキシングチェーンを複数トラックに一括適用。</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎙️ 迅速なアイデア優先の楽曲制作</h3>
      <p>アシスタントとの自然な会話により、トラック構成の構築、ビートやシンセメロディのシーケンス、音量/パンの調整、タイムラインのクリップ編集を即座に実行。</p>
    </td>
    <td width="50%" valign="top">
      <h3>⚡ スムーズでノイズのないスタジオパフォーマンス</h3>
      <p>多数のトラックと重いプラグインを重ねた大規模プロジェクトでも、ノイズ、ポップ音、遅延なしでクリアに再生・録音できます。</p>
    </td>
  </tr>
</table>

---

## 開発者ガイド & アーキテクチャ

クリエイターはAIアシスタントと自然言語で話すだけです。エージェントが [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) を読み込み、裏側でIPCアクションに自動変換するため、ターミナルコマンドや構文、フラグを覚える必要はありません。

<details>
<summary><b>IPC プロトコル & CLI コマンドリファレンス (daw-cli)</b></summary>
<br>

Strata Studio には、AIエージェントや自動セッション制御のために設計された Agentic IPC デーモンとCLIユーティリティ (`daw-cli`) が組み込まれています。

完全なCLIコマンドフラグ、JSONスキーマ、エラーコード、IPCプロトコルのドキュメントについては、[`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) を参照してください。

</details>

<br>

<details>
<summary><b>エンジン・アーキテクチャ (8層レイヤー構造)</b></summary>
<br>

Strata Studio は、厳格な C++20 8層アーキテクチャで構築されており、リアルタイムオーディオの安全性、モジュール化された保守性、決定論的なDSP実行を保証します。

</details>

<details>
<summary><b>ソースコードからのビルドとコンパイル</b></summary>
<br>

### 前提条件

本プロジェクトでは、依存関係の管理に **vcpkg** のマニフェストモードを使用しています。

#### 必須ツール
- **CMake** 3.20 以上
- **Git**
- **C++20 対応コンパイラ**: Clang 12+, GCC 11+, MSVC 2022+

#### 自動セットアップ
セットアップスクリプトを実行して、依存ライブラリ（RtAudio, RtMidi, libsndfile, nlohmann_json, spdlog, Catch2）をインストールします：

```bash
./scripts/install_dependencies.sh
```

---

### ビルド手順

1. **リポジトリのクローン**:
   ```bash
   git clone https://github.com/Goldwaterfung/Strata-Studio.git
   cd Strata-Studio
   ```

2. **設定とビルド**:
   ```bash
   mkdir -p build/debug && cd build/debug
   cmake -DCMAKE_BUILD_TYPE=Debug ../../
   cmake --build . --parallel
   ```

3. **アプリケーションの実行**:
   ```bash
   ./bin/strata_studio
   ```

---

### ビルドオプション

<table width="100%">
  <thead>
    <tr>
      <th align="left">オプション</th>
      <th align="center">デフォルト</th>
      <th align="left">説明</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>BUILD_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>ユニットテストのビルド</td>
    </tr>
    <tr>
      <td><code>BUILD_PERFORMANCE_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>パフォーマンスベンチマークのビルド</td>
    </tr>
    <tr>
      <td><code>ENABLE_SIMD</code></td>
      <td align="center"><code>ON</code></td>
      <td>SIMD最適化の有効化 (AVX2)</td>
    </tr>
    <tr>
      <td><code>USE_ASAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Address Sanitizer の有効化</td>
    </tr>
    <tr>
      <td><code>USE_TSAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Thread Sanitizer の有効化</td>
    </tr>
    <tr>
      <td><code>BUILD_PLUGINS</code></td>
      <td align="center"><code>ON</code></td>
      <td>プラグインホストサポートのビルド</td>
    </tr>
  </tbody>
</table>

---

### テスト＆リリースビルド

```bash
# ユニットテストのビルド＆実行
./scripts/build.sh debug --test

# リリリースバイナリのビルド
./scripts/build.sh release

# パッケージ版リリース
./scripts/build.sh release --package
```

</details>

---

## 開発ロードマップ

### Agentic Layer (`daw-cli`) 機能ステータス

- [x] **セッション状態＆トランスポート** (`status`, `transport`) - 実装済み
- [x] **トラック管理＆ゲインステージング** (`track`, `prep`) - 実装済み
- [x] **VST3 / AU プラグインホスト管理** (`plugin`) - 実装済み
- [x] **クリップ＆タイムライン編集** (`clip`, `midi`) - 実装済み
- [ ] **バス・サブミキシング＆Auxエフェクトルーティング** (`route`) - *開発中*
- [ ] **非視覚的DSP解析＆オーディオインテリジェンス** (`analyze`) - *開発中*
- [ ] **Stem出力＆非同期レンダリング** (`export`, `job`) - *開発中*

---

## ライセンス

<div align="center">

[![CC BY 4.0][cc-by-shield]][cc-by]

本プロジェクトは [Creative Commons Attribution 4.0 International License][cc-by] の下で公開されています。

[![CC BY 4.0][cc-by-image]][cc-by]

[cc-by]: http://creativecommons.org/licenses/by/4.0/
[cc-by-image]: https://i.creativecommons.org/l/by/4.0/88x31.png
[cc-by-shield]: https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg

</div>

---

## 謝辞

<div align="center">

アーキテクチャのインスピレーション：  
**[Ardour](https://ardour.org/)** (libardour) • **[Bitwig Studio](https://www.bitwig.com/)** • **[Reaper](https://www.reaper.fm/)** • **[JUCE Framework](https://juce.com/)**

</div>
