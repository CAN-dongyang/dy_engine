# 03B. In-app profiler HUD

## 요구사항 정정

MethaneKit Asteroids 화면 왼쪽 위에 보이는 패널은 Tracy 화면을 게임 창에 삽입한 것이
아니라 MethaneKit의 자체 `UserInterface HUD`다. MethaneKit도 Tracy instrumentation은
별도 기능으로 제공한다. dy_engine도 같은 구분을 따른다.

- Tracy: CPU zone과 상세 timeline을 수집하는 선택적 외부 분석 도구
- Profiler HUD: 프로그램 창 안에서 항상 볼 수 있는 엔진 자체 진단 UI

따라서 외부 Tracy Profiler를 실행하지 않아도 평소 성능 수치는 프로그램 자체에서
확인할 수 있고, 더 깊은 분석이 필요할 때만 Tracy를 연결한다.

## 화면 동작

기본 상태에는 장면 왼쪽 위에 작은 요약 패널이 자동 표시된다.

- FPS
- CPU render time
- GPU `MainForward` time 또는 지원하지 않을 때 `N/A`
- `F11` 안내

`F11`을 누르면 상세 패널이 열리고 다시 누르면 접힌다. 상세 패널에는 최근 120 frame의
다음 그래프를 동시에 표시한다.

- 전체 frame time: cyan
- CPU render time: green
- GPU main pass time: orange
- 60 FPS 예산 16.67 ms: red
- 큰 spike도 잘리지 않도록 자동으로 늘어나는 graph scale

macOS가 F11을 데스크톱 단축키로 사용하는 설정이면 `fn + F11`을 누르거나 시스템 설정의
표준 기능 키 옵션을 켠다.

## 자동 적용 구조

HUD는 예제의 `main.cpp`에 넣지 않았다.

1. 공통 `Window`가 GLFW의 F11 press를 한 번만 기록한다.
2. 모든 프로그램이 사용하는 `Renderer`가 그 입력을 자동 소비하고 CPU/GPU 측정값을 모은다.
3. `ProfilerHud`가 내장 5x7 글꼴과 그래프를 RHI triangle geometry로 만든다.
4. PerDraw, Batched, Bindless `RenderPath`가 장면 마지막에 같은 HUD를 합성한다.

그래서 `RendererDesc`를 평소처럼 만들면 추가 설정 없이 켜지며, 필요할 때만
`enableProfilerHud = false`로 끌 수 있다. 초기부터 상세 화면을 원하면
`profilerHudStartsExpanded = true`를 지정할 수 있다.

## 플랫폼 구조

HUD 자체에는 Metal, Vulkan, D3D12 native 호출이 없다. 현재 프로그램의 표준 renderer
shader contract와 공통 RHI buffer/pipeline/draw 계약만 사용한다. backend별 차이는 기존
pipeline과 GPU timestamp 구현이 처리한다.

| Backend | 상태 |
| --- | --- |
| Metal | Makefile 전체 예제 빌드, Xcode Cube 빌드, Cube 실행 완료 |
| Null | Cube 빌드 완료 |
| Vulkan | 공통 RHI 경로 적용, Vulkan SDK 환경 실행 검증 대기 |
| D3D12 | 공통 RHI 경로 적용, Windows 환경 실행 검증 대기 |

## 변경 파일

| 위치 | 내용 |
| --- | --- |
| `src/Graphics/ProfilerHud.*` | 내장 글꼴, 패널, 120 frame graph, frame별 GPU buffer |
| `src/Graphics/Renderer.*` | 자동 측정, GPU timestamp 소비, HUD pipeline과 생명주기 |
| `src/Graphics/RenderPath.*` | 세 binding 전략의 main pass 마지막에 HUD 기록 |
| `src/Platform/Window.*` | 공통 F11 press 이벤트 |
| `src/Backends/Metal/` | HUD alpha blend와 depth-disabled pipeline 상태 보장 |

## 확인 방법

Xcode에서 `Cube > My Mac`을 선택하고 실행한다.

1. 큐브 왼쪽 위에 `DY PROFILER` 요약 패널이 보이는지 확인한다.
2. FPS, CPU, GPU 값이 갱신되는지 확인한다.
3. `F11` 또는 `fn + F11`을 눌러 상세 graph가 열리는지 확인한다.
4. 다시 눌러 요약 panel로 돌아오는지 확인한다.
5. 외부 Tracy Profiler가 꺼져 있어도 위 기능이 그대로 동작하는지 확인한다.

## 범위에서 제외

- Tracy 전체 timeline UI를 프로그램 안에 복제하는 일
- 일반 CPU heap memory tracking 또는 Tracy memory allocation tracking
- GPU resource allocation counter: 다음 04 단계에서 HUD 수치로 연결
