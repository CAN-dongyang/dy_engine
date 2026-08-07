# 04. GPU resource allocation counters

## 목표

GPU에 생성되어 살아 있는 RHI resource 수를 종류별로 추적한다. 일반 메모리 추적이나
CPU allocation hook은 구현하지 않는다.

## 추적 대상

- live/created/destroyed buffer count
- live/created/destroyed texture count
- live/created/destroyed pipeline count
- 이후 실제 RHI 객체가 추가되면 sampler, resource set, query pool count 확장

요청 byte나 native heap residency는 이 단계의 범위가 아니다. 따라서 이 값은 memory
usage가 아니라 resource object allocation counter다.

## 예정 RHI 계약

- `ResourceAllocationCounters` snapshot
- `IDevice::GetResourceAllocationCounters()` read-only query
- create 성공 후 증가, destroy 시 감소
- null destroy, 이중 destroy, counter underflow를 Null Backend test에서 검증
- 필요하면 Tracy plot에 live count를 전송하되 Tracy memory tracking은 사용하지 않음

## 예정 변경 폴더

| 폴더 | 변경 내용 |
| --- | --- |
| `src/RHI/` | 공통 counter snapshot과 조회 계약 |
| `src/Backends/Null/` | 증감·underflow 계약 테스트 |
| `src/Backends/Metal/` | native resource 생성 성공 기준 counter |
| `src/Backends/Vulkan/` | Vulkan resource 생성 성공 기준 counter |
| `src/Backends/D3D12/` | D3D12 resource 생성 성공 기준 counter |
| `src/Graphics/` | 진단 표시 또는 Tracy plot 소비 지점 |

## 완료 조건

- [ ] 공통 counter snapshot
- [ ] Null 증감·underflow 검증
- [ ] Metal counter 구현·검증
- [ ] Vulkan counter 구현·검증
- [ ] D3D12 counter 구현·검증
- [ ] device shutdown 시 live resource가 0인지 진단
- [ ] memory tracking API가 추가되지 않았는지 확인
