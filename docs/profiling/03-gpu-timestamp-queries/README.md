# 03. GPU timestamp queries

## 목표

CPU 시간과 분리된 GPU 실행 시간을 pass 단위로 측정한다. query의 생성, command 기록,
resolve, 이전 frame 결과 읽기를 명시적인 RHI 계약으로 제공한다.

## 예정 RHI 계약

- timestamp query pool 또는 frame-local query allocator
- command list의 timestamp write
- begin/end timestamp를 자동으로 기록하는 RAII GPU timing scope
- submit 완료 전 결과를 읽지 않는 frame-latency 계약
- tick을 nanosecond로 변환하는 Backend별 frequency/period 정보
- 지원 여부와 최대 query 수를 명시하는 최소 query

## Backend 번역

| Backend | Native API |
| --- | --- |
| D3D12 | timestamp query heap, `EndQuery`, `ResolveQueryData`, queue timestamp frequency |
| Vulkan | query pool, command timestamp write, `timestampPeriod`와 valid bits |
| Metal | counter sample buffer 지원 시 stage boundary sample과 resolve |
| Null | deterministic fake tick으로 lifecycle과 resolve 순서만 검증 |

## 예정 변경 폴더

| 폴더 | 변경 내용 |
| --- | --- |
| `src/RHI/` | timestamp query와 result 계약 |
| `src/Backends/Null/` | query lifecycle 검증 |
| `src/Backends/Metal/` | counter sampling과 availability 처리 |
| `src/Backends/Vulkan/` | query pool, reset, timestamp resolve |
| `src/Backends/D3D12/` | query heap, readback buffer, frequency 변환 |
| `src/Graphics/` | Shadow/MainForward GPU timing scope |

## 완료 조건

- [ ] RHI timestamp query 계약
- [ ] RAII GPU timing scope
- [ ] frame retirement와 비동기 result 수명 검증
- [ ] Null 구현·테스트
- [ ] Metal 구현·실행 검증
- [ ] Vulkan 구현·실행 검증
- [ ] D3D12 구현·실행 검증
