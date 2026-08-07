# 03. GPU timestamp queries

## 목표

CPU 시간과 분리된 GPU 실행 시간을 pass 단위로 측정한다. query의 생성, command 기록,
resolve, 이전 frame 결과 읽기를 명시적인 RHI 계약으로 제공한다.

## 구현한 RHI 계약

- `ICommandList::BeginGpuTimestamp(name)` / `EndGpuTimestamp()`
- begin/end를 자동으로 짝짓는 `CommandGpuTimestampScope` RAII wrapper
- `IDevice::SupportsGpuTimestamps()` 지원 여부 조회
- `IDevice::GetMaxGpuTimestampScopes()` frame당 최대 scope 수 조회
- `IDevice::TryGetLastGpuTimestamp(name, result)` 완료 결과 조회
- 결과 단위는 공통으로 nanosecond이며 `frameSerial`로 최신 완료 frame을 구분
- submit 중인 frame은 읽지 않고 fence/completion handler로 완료된 frame만 공개
- Backend 내부 frame-local query allocator의 상한은 64 samples(32 scopes)

## 프로그램 내부 자동 동작

GPU 계측은 예제 Cube가 아니라 `src/Graphics/RenderPath.cpp`, `Renderer.cpp`와 각 RHI
Backend에 있다. 따라서 `dy_engine`으로 만든 다른 프로그램도 별도 계측 코드를 넣지
않아도 현재 Backend에 맞는 timestamp 구현을 자동 사용한다.

- `Shadow`, `MainForward` 범위를 RenderPath가 자동 기록한다.
- 완료된 결과는 Tracy plot `GPU.Shadow.ms`, `GPU.MainForward.ms`로 자동 전송한다.
- Tracy는 기본 활성화되어 실행 파일 내부에서 시작한다.
- 지원하지 않는 GPU에서는 timestamp만 조용히 비활성화되고 렌더링은 계속된다.
- 일반 memory allocation/free tracking은 사용하지 않는다.

## Backend 번역

| Backend | Native API |
| --- | --- |
| D3D12 | timestamp query heap, `EndQuery`, `ResolveQueryData`, queue timestamp frequency |
| Vulkan | query pool, command timestamp write, `timestampPeriod`와 valid bits |
| Metal | timestamp counter sample buffer, draw-boundary sample, command-buffer 완료 후 resolve |
| Null | deterministic fake tick으로 lifecycle과 resolve 순서만 검증 |

## 변경 폴더

| 폴더 | 변경 내용 |
| --- | --- |
| `src/RHI/` | timestamp query와 result 계약 |
| `src/Backends/Null/` | query lifecycle 검증 |
| `src/Backends/Metal/` | counter sampling과 availability 처리 |
| `src/Backends/Vulkan/` | query pool, reset, timestamp resolve |
| `src/Backends/D3D12/` | query heap, readback buffer, frequency 변환 |
| `src/Graphics/` | Shadow/MainForward GPU timing scope |

## Backend별 완료 방식

- D3D12: frame별 query heap 구간과 readback buffer를 사용하고, 해당 backbuffer fence를
  기다린 다음 queue frequency로 nanosecond를 계산한다.
- Vulkan: frame별 query pool을 command buffer 안에서 reset하고 top/bottom timestamp를
  기록한다. frame fence 완료 뒤 `timestampPeriod`와 queue family의 valid bits를 적용한다.
- Metal: GPU가 draw-boundary counter sampling을 지원하는지 검사하고 frame마다 sample
  buffer를 만든다. command buffer completion handler에서 resolve한다.
- Null: 가짜 tick을 사용해 빈 이름, begin/end 불일치, Close 시 열린 scope를 검증한다.

## 검증 결과 (2026-08-07)

| 대상 | 결과 |
| --- | --- |
| Metal + Tracy | 전체 엔진 및 7개 예제 빌드 성공 |
| Metal 실행 | `03_Cube` 창 실행 성공, GPU 완료 후 `TryGetLastGpuTimestamp` 성공 경로 도달 확인 |
| Null + Tracy | 전체 엔진 및 7개 예제 빌드 성공 |
| Vulkan | 구현 완료, 현재 Mac에 Vulkan SDK가 없어 플랫폼 빌드·실행 대기 |
| D3D12 | 구현 완료, Windows 환경 빌드·실행 대기 |

### 실행

기본값으로 Tracy가 켜지므로 별도 Tracy option이 필요 없다.

```sh
cmake -S . -B build-tracy -DUSE_METAL=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-tracy -j4
cd build-tracy/examples/03_Cube
./Cube
```

Tracy Profiler에서 실행 중인 프로그램에 연결한 뒤 `GPU.MainForward.ms` plot을 확인한다.
그림자가 활성화된 Backend에서는 `GPU.Shadow.ms`도 함께 나타난다.

## 완료 조건

- [x] RHI timestamp query 계약
- [x] RAII GPU timing scope
- [x] frame retirement와 비동기 result 수명 구현
- [x] Null 구현·빌드 검증
- [x] Metal 구현·빌드·실행 검증
- [x] Vulkan 구현
- [ ] Vulkan SDK 빌드·실행 검증
- [x] D3D12 구현
- [ ] D3D12 Windows 빌드·실행 검증
