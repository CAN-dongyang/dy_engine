# 03. GPU timestamp queries

## 개요

CPU 시간과 분리된 GPU 실행 시간을 render pass 단위로 측정한다. 완료되지 않은 frame의
값은 공개하지 않고 GPU 작업이 끝난 이전 frame의 결과만 제공한다.

## RHI API

- `ICommandList::BeginGpuTimestamp(name)`
- `ICommandList::EndGpuTimestamp()`
- `CommandGpuTimestampScope` RAII wrapper
- `IDevice::SupportsGpuTimestamps()`
- `IDevice::GetMaxGpuTimestampScopes()`
- `IDevice::TryGetLastGpuTimestamp(name, result)`

결과 단위는 nanosecond이며 `frameSerial`로 결과 frame을 구분한다. frame당 최대 64개
sample, 32개 scope를 사용한다.

## Backend 매핑

| Backend | 구현 |
| --- | --- |
| D3D12 | timestamp query heap, resolve buffer, queue frequency 변환 |
| Vulkan | query pool, command timestamp, `timestampPeriod` 변환 |
| Metal | counter sample buffer, command buffer completion resolve |
| Null | deterministic tick 기반 lifecycle 검증 |

## 계측 및 출력

- RenderPath가 `Shadow`, `MainForward` 범위를 자동 기록한다.
- Tracy plot: `GPU.Shadow.ms`, `GPU.MainForward.ms`
- In-app HUD: `MainForward` GPU 시간과 frame history
- timestamp를 지원하지 않는 장치에서는 해당 값만 비활성화된다.

## 검증

- Null: query lifecycle과 완료 결과 검증
- Metal: 빌드·실행 및 완료된 timestamp result 확인
- Vulkan: Ubuntu CI 빌드 통과, GPU runtime 검증 대기
- D3D12: Windows CI 빌드 통과, GPU runtime 검증 대기
