# Graphics · RHI 문제 목록

2026-08-19 현재 작업 트리와 `origin/shadow&light` 비교에서 확인한 항목이다. 이 문서는 해결책이 아니라 검토 대상을 고정하며, 각 항목은 한 문제만 한 줄로 기록한다. `[확정]`은 코드상 현상이 확인됐다는 뜻이며 최종 제품 판단이나 수정 우선순위까지 확정됐다는 뜻은 아니다.

- `GRI-001` `[확정·기능 제한]` `Scene`은 방향광과 점광원을 여러 개 보관하지만 stock Renderer는 각각 index 0만 읽으므로 두 번째 이후 광원은 화면에 기여하지 않는다 (`src/Public/Graphics/Scene.h`, `src/Graphics/Renderer.cpp`).
- `GRI-002` `[확정·기능 제한]` 유효한 점광원이 있으면 stock fragment shader가 방향광 radiance를 점광원 값으로 덮어쓰므로 방향광과 점광원이 동시에 조명에 기여하지 못한다 (`mesh_ps.glsl:171-182`).
- `GRI-003` `[확정·기능 제한]` 유효한 점광원 하나가 존재하면 첫 방향광의 shadow pass와 shadow sampling이 모두 비활성화된다 (`Renderer.cpp:447-450`, `Renderer.cpp:801-802`, `Renderer.cpp:879-881`).
- `GRI-004` `[결정 필요·브랜치 통합]` `origin/shadow&light`에 있던 다중 광원 개수·우선순위·누적 조명 경로가 현재 브랜치에는 없으므로 의도적 폐기인지 누락된 회귀인지 결정되지 않았다.
- `GRI-005` `[결정 필요·그림자 범위]` 현재 브랜치는 첫 방향광의 단일 shadow view만 지원하지만 `origin/shadow&light`는 선택된 한 광원에 방향광 CSM 4개·spot 1개·point 6개 view를 사용하므로 유지할 shadow 기능 범위가 결정되지 않았다.
- `GRI-006` `[확정·설계 부채]` `ShadowMapDesc`가 texture 할당값인 `resolution`과 방향광 orthographic projection·scene fitting 값을 한 타입에 섞어 map 자원 정책과 light-view 계산 정책의 경계가 불명확하다 (`src/Public/Graphics/RendererDesc.h`).
- `GRI-007` `[확정·설계 부채]` shadow format·enable·bias·PCF·auto-fit 값은 `RendererDesc`에, resolution·projection 값은 `ShadowMapDesc`에 흩어져 하나의 shadow 정책을 한 곳에서 검토하거나 검증하기 어렵다 (`src/Public/Graphics/RendererDesc.h`).
- `GRI-008` `[확정·화질 제한]` `autoFitShadowMap`은 외부 Camera frustum이 아니라 Scene의 모든 entity world bounds에 shadow 영역을 맞추므로 화면 밖의 크거나 먼 물체가 shadow texel 밀도를 떨어뜨릴 수 있다 (`Renderer.cpp:180-200`, `Renderer.cpp:885-898`).
- `GRI-009` `[해결·이번 변경]` stock shader의 Backend별 binary·entry point 선택을 `Graphics/Private/StockShaderAssets`로 격리해 `Renderer.cpp`의 Metal 포장 지식을 제거했다.
- `GRI-010` `[확정·자원 부채]` 같은 Metal metallib 전체 byte가 vertex·fragment·shadow `Shader`마다 복사되고 각 Metal shader 생성에서 별도 `MTLLibrary`를 만들기 때문에 불필요한 메모리 복제와 library 생성이 발생한다 (`Shader.h:38-46`, `MetalShader.mm:14-42`).
- `GRI-011` `[수용된 위험·수명 안전성]` 모든 RHI handle이 Backend 객체 raw pointer이고 generation·owner 검증·`IsValid`가 계획상 금지되어 `Destroy*` 뒤 stale handle 사용을 탐지하거나 차단할 수 없다 (`src/Public/RHI/ResourceHandles.h`, `graphics-rhi-commit-gates.md`).
- `GRI-012` `[수용된 위험·의존 수명]` `ResourceSet`은 pipeline·buffer·texture의 borrowed raw handle을 보관하지만 의존 자원의 선행 파괴나 다른 Device 자원 혼입을 타입 수준에서 막지 못한다 (`src/Public/RHI/ResourceSet.h`).
- `GRI-013` `[확정·성능 부채]` Renderer가 매 frame lighting·shadow constant buffer handle을 비운 뒤 새 buffer를 만들고 직전 buffer를 파괴하므로 안정 상태에서도 GPU 자원 할당·회수 churn이 발생한다 (`Renderer.cpp:416-443`, `Renderer.cpp:781-790`, `Renderer.cpp:867-877`).
- `GRI-014` `[확정·성능 부채]` DrawData가 매 frame 모든 material의 `ResourceSet`과 shadow `ResourceSet`을 새로 만들고 submit 직후 파괴하므로 material 수에 비례한 descriptor 할당·회수 churn이 발생한다 (`DrawData.cpp:515-591`).
- `GRI-015` `[확정·데이터 정확성 위험]` FBX texture 탐색 실패 시 material 이름·관용 이름·첫 번째 이미지 순으로 추측하는 fallback이 잘못된 texture를 성공처럼 조용히 연결할 수 있다 (`src/Graphics/Model.cpp`).
- `GRI-016` `[결정 필요·Swapchain 계약]` 계획은 구체 format의 정확한 지원과 native 실제 format 보고를 함께 요구하지만 Renderer는 요청 format과 실제 format이 다르면 즉시 실패하므로 “실제 format으로 pipeline을 만든다”는 정책의 적용 범위가 불명확하다 (`graphics-rhi-refactor-steps.md` 5번, `Renderer.cpp:289-298`).
- `GRI-017` `[확정·관측성 부족]` `Renderer::Render`가 `void`이고 frame·upload·resource 생성·submit 실패 경로가 대부분 조용히 `return`하여 일시적 frame 미준비와 영구 오류를 호출자가 구분할 수 없다 (`Renderer.cpp:387-460`).
- `GRI-018` `[해결·이번 변경]` README의 삭제된 `IBuffer`·`ITexture`·`IPipelineState` 설명을 현재 resource handle 구조로 교체했다.
- `GRI-019` `[해결·이번 변경]` README에서 존재하지 않는 `RenderPath` 계층·`07_RenderPath` 예제·실행 인자를 제거하고 `07_RendererStress`로 바로잡았다.
- `GRI-020` `[확정·프로세스 위험]` `graphics-rhi-refactor-steps.md`와 `graphics-rhi-commit-gates.md`가 현재 untracked라서 refactor의 의도와 검수 기준이 브랜치의 재현 가능한 계약에 포함되지 않는다 (`git status --short`).
- `GRI-021` `[확정·검증 공백]` CMake에 `enable_testing`·`add_test`가 없어 RHI 수명·실패·barrier·binding·shadow·lighting 계약을 자동으로 회귀 검증하는 test target이 없다 (`CMakeLists.txt`).
- `GRI-022` `[확정·CI 공백]` CI는 D3D12·Vulkan·Metal의 `dy_engine` library만 compile하고 examples와 Null Backend를 build하지 않아 public Graphics 사용 경로와 Null 계약 구현의 compile 회귀를 잡지 못한다 (`.github/workflows/ci.yml:14-42`).
- `GRI-023` `[확정·실행 검증 공백]` CI에 window 생성·frame submit·화면 결과·shadow/material 비교 실행이 없어 세 native Backend가 compile되어도 같은 장면을 같은 의미로 그리는지는 검증되지 않는다 (`.github/workflows/ci.yml`).
- `GRI-024` `[확정·검수 기준 부족]` refactor steps는 없어질 구조와 목표 구조를 상세히 적지만 변경 전 기능 중 무엇을 보존·제거·연기하는지 항목별 acceptance matrix로 고정하지 않아 구조 완료가 기능 보존 완료로 오인될 수 있다 (`graphics-rhi-refactor-steps.md`).
- `GRI-025` `[확정·검수 이행 공백]` commit gate는 동작 변경을 test·example로 검증하라고 요구하지만 그 결과를 재현할 자동 test나 커밋별 검증 기록이 repository에 없다 (`graphics-rhi-commit-gates.md:39-46`).
- `GRI-026` `[확정·Backend 계약 불일치]` binding array가 점유하는 범위의 중첩을 D3D12·Metal은 거부하지만 Null·Vulkan은 허용해 같은 `PipelineLayoutDesc`의 유효성이 Backend마다 다르다.
- `GRI-027` `[확정·Backend 계약 불일치]` `inlineConstantBinding`이 D3D12·Metal에서는 실제 binding index지만 Vulkan에서는 push constant에 binding이 없어 같은 필드의 의미가 Backend마다 다르다.
- `GRI-028` `[확정·명명 불일치]` `TextureDesc::depthOrArraySize`라는 이름은 3D texture도 표현하는 것처럼 보이지만 현재 구현은 2D 또는 2D-array texture만 지원한다 (`src/Public/RHI/Texture.h`).
- `GRI-029` `[확정·죽은 계약]` `BufferUsage::Indirect`가 공개되어 있지만 `ICommandList`에는 indirect draw command가 없어 이 usage를 완결된 경로로 사용할 수 없다 (`src/Public/RHI/Buffer.h`, `src/Public/RHI/ICommandList.h`).
- `GRI-030` `[확정·잘못된 상태 표현 가능]` `ResourceBarrierDesc`는 buffer와 texture가 동시에 있거나 둘 다 없는 값을 타입상 허용한다 (`src/Public/RHI/Barrier.h`).
- `GRI-031` `[확정·잘못된 상태 표현 가능]` `ResourceBinding`은 buffer와 texture가 동시에 있거나 둘 다 없는 값을 타입상 허용하며 실제 요구 자원 종류는 별도 pipeline layout에서만 알 수 있다 (`src/Public/RHI/Binding.h`).
- `GRI-032` `[확정·기능 제한]` pipeline당 flat `ResourceSet` 하나만 bind할 수 있어 frame·material·draw 빈도별 set 분리가 공개 계약에 없다 (`src/Public/RHI/ICommandList.h`, `src/Public/RHI/ResourceSet.h`).
- `GRI-033` `[확정·Null 동작 결함]` Null Backend의 backbuffer 크기가 0×0이라 기본 depth target을 쓰는 `Renderer::Create`가 실패해 Null은 현재 stock Renderer 실행 검증에 사용할 수 없다.
- `GRI-034` `[확정·계약 공백]` 01~07 예제가 모두 Graphics만 사용하고 RHI 공개 헤더를 직접 사용하는 예제가 없어 Direct RHI 사용 경로의 최소 계약이 고정되지 않았다 (`examples`).
