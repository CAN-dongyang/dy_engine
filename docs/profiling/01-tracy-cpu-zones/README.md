# 01. Tracy CPU zones

## 개요

렌더링의 주요 CPU 작업을 Tracy timeline에서 계층적으로 확인할 수 있도록 scoped
zone을 추가한다.

## 구성

- CMake option: `DY_ENABLE_TRACY` (기본값 `ON`)
- Tracy version: `v0.13.1`
- `TRACY_ON_DEMAND=ON`
- `TRACY_NO_FRAME_IMAGE=ON`
- 비활성화 시 profiling macro는 no-op으로 컴파일된다.

## 계측 범위

- `Renderer::Initialize`, `Renderer::Render`, `Renderer::Shutdown`
- pipeline 및 material/light/shadow 상태 갱신
- `GpuScene::SyncTextures`
- PerDraw, Batched, Bindless RenderPath의 resource 준비와 draw 기록

`DY_PROFILE_CPU_ZONE_NAMED`는 Tracy의 RAII zone을 감싸며 scope 종료 시 자동으로
zone을 닫는다.

## 적용 위치

| 위치 | 변경 내용 |
| --- | --- |
| `cmake/` | Tracy FetchContent, link 및 compile definition |
| `src/Platform/Profiler.h` | Tracy ON/OFF 공통 macro |
| `src/Graphics/` | Renderer, GpuScene, RenderPath CPU zones |

## 동작

Tracy client는 엔진을 링크하는 실행 파일에 포함된다. 외부 Tracy Profiler를 연결하면
실행 중인 애플리케이션 이름으로 CPU zone timeline이 표시된다. Tracy의 allocation/free
memory tracking macro는 사용하지 않는다.

## 검증

- Metal: Tracy ON/OFF 빌드 및 Cube 연결 확인
- Null: Tracy ON/OFF 빌드 확인
- Vulkan: Ubuntu CI 빌드 통과
- D3D12: Windows CI 빌드 통과
