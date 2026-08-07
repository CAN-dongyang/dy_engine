# Profiling integration roadmap

프로파일링 기능은 아래 순서대로 한 단계씩 구현한다. 각 단계는 자기 폴더의
`README.md`에 변경 파일, Backend별 상태, 빌드·실행 검증 결과를 기록하고 승인 후
다음 단계로 넘어간다.

현재 작업 기준은 GitHub `main`의 `fbdbe44`이며, 로컬
`codex/profiling-latest` 브랜치에 단계별 커밋으로 적용한다.

| 단계 | 기능 | 상태 |
| --- | --- | --- |
| [01](01-tracy-cpu-zones/README.md) | Tracy CPU zones | Metal 검증 완료, Vulkan·D3D12 검증 대기 |
| [02](02-renderdoc-debug-events/README.md) | RenderDoc debug event/marker/label | 구현 완료, 플랫폼별 캡처 검증 대기 |
| [03](03-gpu-timestamp-queries/README.md) | GPU timestamp query | 구현 대기 |
| [04](04-gpu-resource-allocation-counters/README.md) | GPU resource allocation counter | 구현 대기 |

## 공통 원칙

- RHI 공개 계약은 API 중립적인 이름을 사용한다.
- Vulkan, D3D12, Metal은 같은 RHI 계약을 각 native API로 번역한다.
- Backend가 지원하지 않는 기능은 조용히 흉내 내지 않고 지원 여부 또는 실패를 명시한다.
- scope 기반 기능은 RAII wrapper로 정상 종료와 조기 반환 모두에서 닫히게 한다.
- memory allocation/free 추적과 일반 CPU heap memory tracking은 범위에서 제외한다.
- 한 단계가 끝날 때마다 diff, 빌드, 실행 결과를 이 문서와 단계 문서에 반영한다.

## 플랫폼 검증 행렬

| 기능 | Null | Metal | Vulkan | D3D12 |
| --- | --- | --- | --- | --- |
| Tracy CPU zones | Tracy OFF 빌드·실행 완료 | Tracy ON 빌드·실행 완료 | 빌드·실행 대기 | 빌드·실행 대기 |
| Debug events | 빌드·실행 완료 | 빌드·실행 완료, Xcode 캡처 대기 | 구현 완료, SDK 환경 검증 대기 | 구현 완료, Windows 검증 대기 |
| Timestamp queries | 대기 | 대기 | 대기 | 대기 |
| Resource counters | 대기 | 대기 | 대기 | 대기 |
