# 04. GPU resource object counters

## 개요

RHI를 통해 생성되는 GPU resource 객체의 lifecycle을 종류별로 추적한다.

## 추적 항목

- Buffer: live, created, destroyed
- Texture: live, created, destroyed
- Pipeline: live, created, destroyed

이 값은 객체 개수이며 allocation byte, CPU heap, native GPU heap 또는 residency를
측정하지 않는다.

## RHI API

- `ResourceAllocationCounter`
- `ResourceAllocationCounters`
- `IDevice::GetResourceAllocationCounters()`

각 Backend는 public RHI resource 생성 성공 후 객체를 등록하고 destroy 시 등록을
해제한다. 공통 tracker는 null destroy, 중복 destroy 및 counter underflow를 방지한다.
Device 종료 시 live 객체가 남아 있으면 종류별 개수를 진단 로그로 출력한다.

## 출력

- In-app HUD: Buffer, Texture, Pipeline의 live/created/destroyed count
- Tracy plots:
  - `GPU.Resources.Buffers.Live`
  - `GPU.Resources.Textures.Live`
  - `GPU.Resources.Pipelines.Live`

## 범위

공개 `IDevice::CreateBuffer`, `CreateTexture`, `CreateGraphicsPipeline`으로 생성된 RHI 객체를
추적한다. Swapchain back buffer, Backend 내부 임시 객체 및 memory byte 사용량은 포함하지
않는다.

## 검증

- Null: 생성·해제, null destroy, 중복 destroy, underflow 자동 테스트 통과
- Metal: Makefile/Xcode 빌드 및 Cube 실행 확인
- Vulkan: Ubuntu CI 빌드 통과
- D3D12: Windows CI 빌드 통과
