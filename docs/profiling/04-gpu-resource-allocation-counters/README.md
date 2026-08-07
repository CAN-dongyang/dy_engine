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

## 구현된 RHI 계약

- `ResourceAllocationCounters` snapshot
- `IDevice::GetResourceAllocationCounters()` read-only query
- create 성공 후 증가, destroy 시 감소
- null destroy, 이중 destroy, counter underflow를 Null Backend test에서 검증
- `IDevice`가 공개 RHI API로 만든 객체 주소를 공통으로 등록해 모든 Backend가 같은 규칙 사용
- Tracy plot에 종류별 live count를 전송하되 Tracy memory tracking은 사용하지 않음
- device 종료 시 live 객체가 남아 있으면 종류별 개수를 표준 오류에 출력

## 화면 표시

- 작은 HUD: `RES B <live> T <live> P <live>`
- F11 상세 HUD: Buffer/Texture/Pipeline의 `L`(live), `C`(created), `D`(destroyed)
- 외부 Tracy viewer를 연결한 경우:
  - `GPU.Resources.Buffers.Live`
  - `GPU.Resources.Textures.Live`
  - `GPU.Resources.Pipelines.Live`

카운터는 엔진의 `IDevice`에 있으므로 Cube에만 붙은 기능이 아니다. 같은 Renderer/RHI를
사용하는 다른 실행 파일과 새 프로그램에서도 별도 등록 없이 동작한다.

## 변경 폴더

| 폴더 | 변경 내용 |
| --- | --- |
| `src/RHI/` | 공통 counter snapshot과 조회 계약 |
| `src/Backends/Null/` | 생성·해제 시 공통 tracker 호출 |
| `src/Backends/Metal/` | Metal RHI 객체 생성·해제 시 공통 tracker 호출 |
| `src/Backends/Vulkan/` | Vulkan RHI 객체 생성·해제 시 공통 tracker 호출 |
| `src/Backends/D3D12/` | D3D12 RHI 객체 생성·해제 시 공통 tracker 호출 |
| `src/Graphics/` | HUD 표시와 Tracy live-count plot 전송 |
| `tests/` | null·중복 해제·underflow 자동 검증 |

## 범위 주의사항

현재 값은 공개 `IDevice::CreateBuffer/CreateTexture/CreateGraphicsPipeline`을 통해 만든
RHI 객체 수다. Swapchain back buffer나 Backend 내부 임시 객체, 할당 byte, native heap
residency는 세지 않는다. 따라서 이 기능은 요청된 GPU resource object 추적이며 memory
tracking이 아니다.

## 검증 결과 (2026-08-07)

- Metal + Tracy ON: Makefile·Xcode `Cube` 빌드 및 실행 완료
- Null + Tracy ON: `Cube` 및 `ResourceAllocationCountersTest` 빌드 완료
- Null 자동 테스트: 생성/해제, null 해제, 중복 해제, underflow 방지 통과
- Vulkan: 코드 적용 완료, Vulkan SDK 환경 빌드 대기
- D3D12: 코드 적용 완료, Windows 환경 빌드 대기

## 완료 조건

- [x] 공통 counter snapshot
- [x] Null 증감·underflow 검증
- [x] Metal counter 구현·검증
- [x] Vulkan counter 구현 (SDK 환경 검증 대기)
- [x] D3D12 counter 구현 (Windows 환경 검증 대기)
- [x] device shutdown 시 live resource가 0인지 진단
- [x] memory tracking API가 추가되지 않았는지 확인
