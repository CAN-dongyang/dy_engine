# 03B. In-app profiler HUD

## 개요

외부 profiler 없이 애플리케이션 화면에서 기본 성능 지표를 확인할 수 있는 공통 RHI
overlay를 제공한다. 상세 timeline 분석은 외부 Tracy Profiler가 담당한다.

## 표시 항목

기본 패널:

- FPS
- CPU render time
- GPU `MainForward` time 또는 `N/A`
- Buffer, Texture, Pipeline live count

F11 상세 패널:

- 최근 120 frame의 frame/CPU/GPU time graph
- 60 FPS 기준선(16.67 ms)
- graph scale 자동 조정
- 리소스 종류별 live/created/destroyed count

## 구현 구조

- `Window`가 F11 press event를 기록한다.
- `Renderer`가 CPU/GPU 측정값과 리소스 카운터를 수집한다.
- `ProfilerHud`가 내장 5x7 font와 graph geometry를 생성한다.
- 모든 RenderPath가 main pass 마지막에 동일한 HUD를 합성한다.

HUD는 공통 RHI buffer, pipeline, draw API만 사용하며 Backend native UI API에 의존하지
않는다. `RendererDesc::enableProfilerHud`로 비활성화하고
`profilerHudStartsExpanded`로 초기 상세 상태를 설정할 수 있다.

## 검증

- Metal: Makefile/Xcode 빌드 및 Cube 실행 확인
- Null: Cube 빌드 확인
- Vulkan: Ubuntu CI 빌드 통과
- D3D12: Windows CI 빌드 통과
