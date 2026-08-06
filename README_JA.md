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
  <a href="#ユースケースの実演">ユースケースの実演</a> •
  <a href="#strata-studio-で-ai-エージェントができること">エージェント機能</a> •
  <a href="#主な機能">主な機能</a> •
  <a href="#開発者ガイド--ソースからのビルド">開発者ガイド</a> •
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

あなたがDAWを開いたのは音楽を制作するためであり、ショートカットキーや修飾キー、複雑な操作画面の記憶に脳の半分を使い果たすためではありません。**ツールを記憶のためではなく、あなたの創造性のために役立てましょう。**

**Strata Studio**は、AIアシスタントにDAWの操作権限を与えることで、あなたが純粋に音楽制作だけに集中できるようにします。ストリーミング用の音量調整、録音素材の背景ノイズ除去、全トラックへのエフェクトプラグイン設定に30分も費やす代わりに、AIアシスタント（**Claude Code**、**Cursor**、**Codex**、**Hermes**、**Gemini**）に自然言語で希望を伝えるだけです。セッションの準備とバランス調整が数秒で完了し、すぐに楽曲制作を開始できます。

---

## クイックスタート & Agenticセットアップ

ターミナルコマンドや手動コンパイルは不要です。お使いのAIエージェント（**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode** など 50 以上のツール）を開き、セットアップを指示してください：

### 1. リポジトリのクローン
プロジェクトのリポジトリをローカル環境にクローンします：

```bash
git clone https://github.com/Goldwaterfung/Strata-Studio.git
cd Strata-Studio
```

または AI エージェントに伝える：

```text
https://github.com/Goldwaterfung/Strata-Studio をクローンしてプロジェクトをセットアップしてください
```

### 2. Strata Studioのビルド＆セットアップ（エージェントがアプリをコンパイル）
エージェントに伝える：

```text
Strata Studio をビルドしてパッケージ化してください
```

*(エージェントがバックグラウンドで `./scripts/install_dependencies.sh` と `./scripts/build.sh release --package` を自動実行します)*。

### 3. スキルのインストール（エージェントにDAW操作を学習させる）
エージェントに伝える：

```text
https://github.com/Goldwaterfung/Strata-Studio から daw-cli スキルをインストールしてください
```

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

## ユースケースの実演

Strata Studioで `daw-cli` を使用してエージェントワークフローを実行するリアルタイムのデモンストレーションです。

### 🎬 ユースケース 1: マルチトラックの即時取り込み、クリップ配置、自動カラーリング

> **自然言語プロンプト:**  
> *" `/Volumes/AudioDrives/Stems/Song_Session_A/` 内のすべてのオーディオファイルをインポートして。すべてのクリップをタイムラインの開始位置に配置し、トラック名から余計な拡張子やアンダースコアをクリーンアップして、楽器タイプに基づいて自動でカラーリングしてセッションを整理して。"*

![ユースケース 1: マルチトラック取り込みと自動カラーリング](asset/usecase-1.gif)

* **主要コマンド:** `daw-cli track create`, `daw-cli clip add-audio`, `daw-cli track sanitize-names`, `daw-cli track auto-color`

---

### 🎬 ユースケース 2: マルチトラックセッションの構成とフェーダーレイアウト

> **自然言語プロンプト:**  
> *"124 BPMのセッションを設定し、Kick、Snare Top、Snare Bottom、Hi-Hat、Tom 1、Tom 2、ステレオOverheadの8ピースドラムキットを作成して。自動でカラー分けし、ヘッドルーム確保のためにすべてのフェーダーを -6 dB に設定し、Overheadを左右100%にパンニングして。"*

![ユースケース 2: セッション構成とフェーダーレイアウト](asset/usecase-2.gif)

* **主要コマンド:** `daw-cli transport set-tempo`, `daw-cli track create-batch`, `daw-cli track auto-color`, `daw-cli track set-gain`, `daw-cli track set-pan`

---

## エージェントコマンドの使用例

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

## Strata Studio で AI エージェントができること

複雑なメニューの移動やショートカットキーの記憶、個別ノブの手動調整を行う代わりに、自然言語でお使いの AI アシスタント（**Claude Code**, **Cursor**, **Codex**, **Hermes**, **Gemini** など）に指示を出すだけです：

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎚️ スマートゲインステージング & トラックレベリング</h3>
      <p>各トラックの音量バランスやパンの調整、マルチトラックのゲインステージング（Gain-Staging）を自動で行い、クリッピングを防ぎつつ明瞭なミックスを実現します。</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 VST3 / AU プラグインホスティング & FX チェーン</h3>
      <p>インストール済みエフェクトプラグイン（FabFilter, iZotope, Waves 等）をスキャンして EQ やコンプレッサーを自動挿入し、設定したエフェクトチェーンを他のトラックへ一瞬で複製できます。</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎹 MIDI シーケンス & タイムラインクリップ編集</h3>
      <p>ドラムパターンの構築、シンセリードの MIDI クリップ作成、トランスポーズ（移調）、タイムライン上のオーディオクリップ配置・分割を会話形式で素早く指示できます。</p>
    </td>
    <td width="50%" valign="top">
      <h3>⏱️ トランスポート再生 & セッション管理</h3>
      <p>テンポ（BPM）や拍号の設定、再生・停止・プレイヘッドの移動、トラックのカラー整理やリネームをリアルタイムで実行します。</p>
    </td>
  </tr>
</table>

### 🚀 対応している AI 音楽制作コマンド

* **セッション & トランスポート制御**: BPM 設定、拍号変更、プレイヘッド移動、再生/停止。
* **トラック作成 & レベリング**: オーディオ/インストゥルメントトラック追加、音量・パン設定、ミュート/ソロ、自動ゲインステージング。
* **プラグイン管理**: VST3/AU プラグインの挿入、パラメータ微調整、別トラックへのエフェクトチェーン複製。
* **タイムライン & MIDI 編集**: オーディオクリップ配置、開始オフセット・長さの調整、MIDI ノート入力、メロディのトランスポーズ。

---

## 開発者ガイド & ソースからのビルド

Strata Studio は、AI 駆動の音楽制作のために構築されたオープンソースの高性能 C++20 DAW エンジンです。AI エージェントは [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) スキル定義を介して DAW と通信します。

<details>
<summary><b>ソースからのビルド & コンパイル</b></summary>
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
