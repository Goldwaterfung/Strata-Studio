<div align="center">

# ![Strata](asset/strata.png) Strata Studio

### Strata Studio

<p align="center">
  <b>몇 초 만에 세션 믹싱, 밸런스 조절, 정돈을 완료하세요.<br>번거로운 DAW 설정으로 시간을 허비하지 마세요. AI 어시스턴트에게 필요한 것을 말하고 창작의 흐름을 유지하세요.</b>
</p>

[![License: CC BY 4.0](https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg)](http://creativecommons.org/licenses/by/4.0/)
[![Agent Skills](https://img.shields.io/badge/Agent%20Skills-Standard-green)](https://agentskills.io)
[![Multi-Runtime](https://img.shields.io/badge/Runtime-Claude%20Code%20·%20Codex%20·%20Cursor%20·%20Hermes%20·%20Gemini-blueviolet)](#빠른-시작--에이전트-설정)
[![Plugin Host](https://img.shields.io/badge/Plugins-VST3%20%7C%20AU%20%7C%20CLAP-blue.svg)](#핵심-기능)

---

<p align="center">
  <a href="#빠른-시작--에이전트-설정">빠른 시작</a> •
  <a href="#사용-예시">사용 예시</a> •
  <a href="#핵심-기능">핵심 기능</a> •
  <a href="#개발자-가이드--아키텍처">개발자 가이드</a> •
  <a href="#라이선스">라이선스</a>
</p>

<p align="center">
  <b>다른 언어:</b><br>
  <a href="README.md">English</a> •
  <a href="README_ZH.md">简体中文</a> •
  <a href="README_ZH_TW.md">繁體中文</a> •
  <a href="README_JA.md">日本語</a>
</p>
</div>

---

## 개요

여러분은 음악을 만들기 위해 DAW를 열었습니다. 단축키, 조합키, 복잡한 제어 인터페이스를 외우느라 뇌의 절반을 소모하기 위해서가 아닙니다. **도구가 여러분의 기억이 아닌, 창의성을 돕도록 하세요.**

**Strata Studio**는 AI 어시스턴트가 DAW를 직접 제어하게 하여 여러분이 순수하게 음악 제작에만 집중할 수 있도록 합니다. 스트리밍용 트랙 볼륨 조절, 녹음 음원의 배경 노이즈 제거, 모든 트랙에 이펙트 플러그인 라우팅을 위해 30분씩 수동 작업을 하는 대신, AI 어시스턴트(**Claude Code**, **Cursor**, **Codex**, **Hermes**, **Gemini**)에게 자연어로 원하는 내용을 말하세요. 몇 초 만에 세션 준비와 밸런스 조절이 완료되어 프로덕션에 들어갈 수 있습니다.

---

## 빠른 시작 & 에이전트 설정

터미널 명령어나 수동 컴파일이 필요하지 않습니다. 사용 중인 AI 에이전트(**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode** 등 50개 이상의 도구)를 열고 설정을 요청하세요:

### 1. 스킬 설치 (에이전트에게 DAW 제어법 학습)
에이전트에게 입력:

```text
https://github.com/Goldwaterfung/Strata-Studio 에서 daw-cli 스킬을 설치해 줘
```

### 2. Strata Studio 빌드 및 설정 (에이전트가 앱 컴파일)
에이전트에게 입력:

```text
Strata Studio를 빌드하고 패키징해 줘
```

*(에이전트가 백그라운드에서 `./scripts/install_dependencies.sh` 및 `./scripts/build.sh release --package`를 자동으로 실행합니다)*.

<details>
<summary><b>옵션 2: 수동 스킬 디렉터리 설정</b></summary>
<br>

선호하는 AI 에이전트 프레임워크에 수동으로 스킬을 배치하려면 `skills/daw-cli/` 디렉터리를 복사하거나 심볼릭 링크로 연결하세요:

| 에이전트 프레임워크 | 프로젝트 로컬 스킬 경로 | 글로벌 사용자 스킬 경로 |
| :--- | :--- | :--- |
| **Codex** | `.agents/skills/daw-cli` | `~/.agents/skills/daw-cli` |
| **Claude Code / Co-Work** | `.claude/skills/daw-cli` | `~/.claude/skills/daw-cli` |
| **Hermes** | `.hermes/skills/daw-cli` | `~/.hermes/skills/daw-cli` |
| **Antigravity** | `.agents/skills/daw-cli` | `~/.gemini/config/skills/daw-cli` |
| **Gemini CLI** | `.gemini/skills/daw-cli` | `~/.gemini/skills/daw-cli` |
| **OpenCode** | `.opencode/skills/daw-cli` | `~/.config/opencode/skills/daw-cli` |

설치가 완료되면 에이전트는 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md)를 매뉴얼로 참조합니다.

</details>

---

## 사용 예시

Strata Studio에서 AI 어시스턴트와 함께 작업하는 실제 모습입니다:

```text
User    ❯ 템포를 128 BPM으로 설정하고 Kick, Snare, HH, Tom 트랙을 생성한 다음 볼륨 레벨 밸런스를 맞춰 줘.

Agent   ❯ [Strata Agentic Engine]
          ✓ 세션 템포를 128.0 BPM (4/4 박자)으로 설정했습니다.
          ✓ 오디오 트랙 4개를 생성했습니다: Kick, Snare, HH, Tom
          ✓ 피크 클리핑을 방지하도록 트랙 1..4의 볼륨 레벨 밸런스를 맞췄습니다.
          완료되었습니다. 편곡 작업을 진행하세요.
```

```text
User    ❯ Snare 트랙에 FabFilter Pro-Q 3 이퀄라이저를 추가하고, 그 플러그인 체인을 모든 Tom 트랙에 복사해 줘.

Agent   ❯ [Strata Agentic Engine]
          ✓ 시스템 VST3/AU 플러그인을 스캔했습니다.
          ✓ 트랙 2 (Snare)의 슬롯 0에 'FabFilter Pro-Q 3'를 삽입했습니다.
          ✓ 트랙 2의 플러그인 체인을 트랙 3..4에 복사했습니다.
```

---

## 핵심 기능

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎛️ 완벽한 볼륨 밸런스 & 깨끗한 녹음</h3>
      <p>트랙 레벨을 자동으로 조절하여 음악을 뚜렷하고 펀치감 있게 스트리밍 표준으로 맞추며, 녹음 음원 내부 배경 노이즈, 룸 블리드, 무음 구간을 자동으로 제거합니다.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 신속한 FX & 플러그인 설정</h3>
      <p>선호하는 플러그인(FabFilter, Waves, iZotope 등)을 불러오고, 한 문장의 자연어 명령으로 여러 트랙에 보컬이나 드럼 믹싱 체인을 일괄 적용하세요.</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎙️ 신속한 아이디어 우선 작곡</h3>
      <p>세션 어시스턴트와의 자연스러운 대화를 통해 트랙 레이아웃을 즉시 구성하고, 비트와 신시사이저 멜로디를 시퀀싱하며, 볼륨/팬 조정 및 타임라인 클립 편집을 실행하세요.</p>
    </td>
    <td width="50%" valign="top">
      <h3>⚡ 부드럽고 노이즈 없는 스튜디오 성능</h3>
      <p>수십 개의 트랙과 무거운 플러그인이 포함된 대형 프로젝트에서도 노이즈, 팝음, 지연 없이 선명한 오디오로 재생 및 녹음할 수 있습니다.</p>
    </td>
  </tr>
</table>

---

## 개발자 가이드 & 아키텍처

음악 크리에이터는 AI 어시스턴트와 자연어로 대화하기만 하면 됩니다. 에이전트가 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md)를 참조하여 내부 IPC 명령으로 자동 변환하므로 터미널 명령어나 구문, 플래그를 암기할 필요가 없습니다.

<details>
<summary><b>IPC 프로토콜 & CLI 명령어 참조 (daw-cli)</b></summary>
<br>

Strata Studio에는 AI 에이전트 및 자동 세션 제어를 위해 설계된 전용 Agentic IPC 데몬과 CLI 유틸리티(`daw-cli`)가 포함되어 있습니다.

전체 CLI 명령어 플래그, JSON 스키마, 에러 코드 및 IPC 프로토콜 문서는 [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md)를 참조하세요.

</details>

<br>

<details>
<summary><b>엔진 아키텍처 (8계층 구조)</b></summary>
<br>

Strata Studio는 엄격한 C++20 8계층 아키텍처로 설계되어 실시간 오디오 안정성, 모듈화된 유지보수성, 결정론적 DSP 실행을 보장합니다.

</details>

<details>
<summary><b>소스 코드 빌드 및 컴파일</b></summary>
<br>

### 사전 요구 사항

본 프로젝트는 의존성 관리를 위해 **vcpkg**의 매니페스트 모드(manifest mode)를 사용합니다.

#### 필수 도구
- **CMake** 3.20 이상
- **Git**
- **C++20 지원 컴파일러**: Clang 12+, GCC 11+, MSVC 2022+

#### 자동 설정
설정 스크립트를 실행하여 의존성 라이브러리(RtAudio, RtMidi, libsndfile, nlohmann_json, spdlog, Catch2)를 설치하세요:

```bash
./scripts/install_dependencies.sh
```

---

### 빌드 단계

1. **리포지토리 클론**:
   ```bash
   git clone https://github.com/Goldwaterfung/Strata-Studio.git
   cd Strata-Studio
   ```

2. **설정 및 빌드**:
   ```bash
   mkdir -p build/debug && cd build/debug
   cmake -DCMAKE_BUILD_TYPE=Debug ../../
   cmake --build . --parallel
   ```

3. **애플리케이션 실행**:
   ```bash
   ./bin/strata_studio
   ```

---

### 빌드 옵션

<table width="100%">
  <thead>
    <tr>
      <th align="left">옵션</th>
      <th align="center">기본값</th>
      <th align="left">설명</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>BUILD_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>유닛 테스트 빌드</td>
    </tr>
    <tr>
      <td><code>BUILD_PERFORMANCE_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>성능 벤치마크 빌드</td>
    </tr>
    <tr>
      <td><code>ENABLE_SIMD</code></td>
      <td align="center"><code>ON</code></td>
      <td>SIMD 최적화 활성화 (AVX2)</td>
    </tr>
    <tr>
      <td><code>USE_ASAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Address Sanitizer 활성화</td>
    </tr>
    <tr>
      <td><code>USE_TSAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Thread Sanitizer 활성화</td>
    </tr>
    <tr>
      <td><code>BUILD_PLUGINS</code></td>
      <td align="center"><code>ON</code></td>
      <td>플러그인 호스트 지원 빌드</td>
    </tr>
  </tbody>
</table>

---

### 테스트 & 릴리스 빌드

```bash
# 유닛 테스트 빌드 및 실행
./scripts/build.sh debug --test

# 릴리스 바이너리 빌드
./scripts/build.sh release

# 패키징 릴리스
./scripts/build.sh release --package
```

</details>

---

## 개발 로드맵

### Agentic Layer (`daw-cli`) 기능 상태

- [x] **세션 상태 및 트랜스포트** (`status`, `transport`) - 구현 완료
- [x] **트랙 관리 및 게인 스테이징** (`track`, `prep`) - 구현 완료
- [x] **VST3 / AU 플러그인 호스트 관리** (`plugin`) - 구현 완료
- [x] **클립 및 타임라인 편집** (`clip`, `midi`) - 구현 완료
- [ ] **버스 서브믹싱 및 Aux 이펙트 루팅** (`route`) - *개발 중*
- [ ] **비시각적 DSP 분석 및 오디오 지능** (`analyze`) - *개발 중*
- [ ] **Stem 내보내기 및 비동기 렌더링 작업** (`export`, `job`) - *개발 중*

---

## 라이선스

<div align="center">

[![CC BY 4.0][cc-by-shield]][cc-by]

본 프로젝트는 [Creative Commons Attribution 4.0 International License][cc-by] 라이선스를 따릅니다.

[![CC BY 4.0][cc-by-image]][cc-by]

[cc-by]: http://creativecommons.org/licenses/by/4.0/
[cc-by-image]: https://i.creativecommons.org/l/by/4.0/88x31.png
[cc-by-shield]: https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg

</div>

---

## 감사의 말

<div align="center">

아키텍처 영감 출처:  
**[Ardour](https://ardour.org/)** (libardour) • **[Bitwig Studio](https://www.bitwig.com/)** • **[Reaper](https://www.reaper.fm/)** • **[JUCE Framework](https://juce.com/)**

</div>
