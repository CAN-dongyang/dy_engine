# Profiling integration

`dy_engine`의 공통 Graphics/RHI 계층에 CPU·GPU 프로파일링 기능을 제공한다. 계측은
특정 예제가 아니라 엔진에 포함되므로 동일한 Renderer를 사용하는 애플리케이션에
자동으로 적용된다.

| 단계 | 기능 | 상태 |
| --- | --- | --- |
| [01](01-tracy-cpu-zones/README.md) | Tracy CPU zones | 구현 완료 |
| [02](02-renderdoc-debug-events/README.md) | GPU debug event/marker/label | 구현 완료 |
| [03](03-gpu-timestamp-queries/README.md) | GPU timestamp query | 구현 완료 |
| [03B](03b-in-app-profiler-hud/README.md) | In-app profiler HUD | 구현 완료 |
| [04](04-gpu-resource-allocation-counters/README.md) | GPU resource object counters | 구현 완료 |

## 플랫폼 상태

| 플랫폼 | 빌드 | 실행·도구 검증 |
| --- | --- | --- |
| Null | 로컬 빌드 및 contract test 통과 | 리소스 카운터 lifecycle 검증 |
| Metal | macOS CI 및 Xcode 빌드 통과 | Cube 실행, Xcode GPU Capture label 확인 |
| Vulkan | Ubuntu CI 빌드 통과 | RenderDoc 캡처 검증 대기 |
| D3D12 | Windows CI 빌드 통과 | PIX/RenderDoc 캡처 검증 대기 |

## 설계 원칙

- RHI 공개 API는 graphics API에 독립적인 이름을 사용한다.
- Backend가 공통 API를 D3D12, Vulkan, Metal native 기능으로 변환한다.
- begin/end 형태의 계측은 RAII scope로 수명을 관리한다.
- 지원하지 않는 기능은 렌더링을 중단하지 않고 비활성화된다.
- 일반 CPU heap 및 GPU memory byte tracking은 포함하지 않는다.
