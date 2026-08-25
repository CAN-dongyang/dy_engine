# Examples

`examples`는 공개 API로 완성할 사용자 작업과 재현 가능한 성능 측정을 코드로 먼저
고정한다. 필드 하나나 내부 처리 단계 하나를 별도 예제로 세지 않는다. 현재 API로 실행할
수 없는 계약 코드는 CMake 대상에서 제외하고, 필요한 공개 API가 구현된 뒤 활성화한다.

## 목표 구조

```text
examples/
├─ Common/                 창과 예제 자산을 준비하는 보조 코드
├─ Graphics/
│  ├─ 01_Scene/
│  ├─ 02_Materials/
│  ├─ 03_Model/
│  ├─ 04_Lighting/
│  ├─ 05_Animation/
│  ├─ 06_Output/
│  └─ 07_LargeScene/
├─ RHI/
│  ├─ 01_TexturedCube/
│  ├─ 02_Compute/
│  ├─ 03_MeshShader/
│  └─ 04_RayTracing/
└─ Performance/
   ├─ 01_Math/
   ├─ 02_Scene/
   └─ 03_Rendering/
```

`Common`은 예제 전용 보조 코드이며 공개 렌더링 기능이 아니다.

## Graphics

Graphics 예제는 RHI나 Backend에 접근하지 않고 `Renderer`, `Scene`과 관련 데이터만
사용한다.

| 예제 | 사용자가 확인하는 결과 | 현재 예제와의 관계 |
| --- | --- | --- |
| `01_Scene` | Camera로 raw mesh가 있는 Scene을 그리고 entity를 움직인다. | 기존 Window·Renderer·Cube 흐름을 합친 코드가 현재 API로 작성되어 있다. |
| `02_Materials` | 공개된 PBR material 값과 texture 입력이 각각 화면에 어떻게 반영되는지 비교한다. | 모든 material 입력과 sRGB/linear texture 구분을 요구하는 목표 코드가 있다. 현재 `TextureAsset`에는 색공간 필드가 없다. |
| `03_Model` | 모델 파일 하나를 불러와 geometry, material과 texture가 보존된 장면을 그린다. | 현재 `AddModelToScene` 경로로 작성되어 있다. 포맷별 호환성은 importer test가 맡는다. |
| `04_Lighting` | 공개된 광원 종류가 함께 장면을 밝히고, 그림자를 지원하는 광원과 entity 설정이 실제 그림자를 결정한다. | 목표 코드는 작성됐지만 현재 Renderer는 첫 방향광 또는 첫 점광원만 사용하며 방향광 그림자 하나도 point light와 함께 처리하지 못한다. |
| `05_Animation` | 모델 instance의 skeletal·morph animation을 재생하고 제어한다. | 목표 코드는 작성됐지만 model instance와 animation 공개 API가 없다. |
| `06_Output` | HDR 장면 중간 결과를 공개 설정으로 tone mapping해 SDR backbuffer에 출력한다. | 목표 코드는 작성됐지만 HDR intermediate·post-process API가 없고 tone mapping과 gamma가 stock shader에 고정되어 있다. HDR display 출력은 별도 기능이다. |
| `07_LargeScene` | 많은 entity를 생성하고 매 frame transform을 갱신해 같은 장면 의미로 렌더링한다. | 현재 API 코드가 있다. 성능과 최적화 방식은 Performance와 test가 검증한다. |

## Direct RHI

RHI 예제는 Graphics를 사용하지 않는다. 선택한 Backend가 달라도 같은 RHI 소스가 같은
결과를 만들어야 한다.

| 예제 | 사용자가 확인하는 결과 | 현재 상태 |
| --- | --- | --- |
| `01_TexturedCube` | Texture와 depth가 적용된 cube를 backbuffer에 직접 그린다. | 현재 RHI로 C++ 흐름을 작성했다. 실행에는 backend별 shader source와 build 설정이 더 필요하다. |
| `02_Compute` | Buffer에 Compute 작업을 실행하고 결과를 CPU로 readback해 검증한다. | 목표 코드는 작성됐지만 headless device, Compute pipeline, dispatch와 readback API가 없다. |
| `03_MeshShader` | 지원 여부를 확인하고 meshlet geometry를 출력한다. | 목표 코드는 작성됐지만 capability와 Mesh Shader API가 없다. |
| `04_RayTracing` | Geometry로 acceleration structure를 만들고 ray tracing 결과를 출력한다. | 흐름 초안은 있지만 공통 pipeline 모델, AS build usage와 barrier가 미정이므로 아직 공개 계약으로 확정하지 않는다. |

## Performance

Performance 예제는 같은 입력을 반복 실행하고 build configuration, warm-up, 반복 횟수,
입력 크기와 측정 단위를 함께 출력한다. 화면 기능을 새로 정의하지 않고 이미 확정된
Graphics·RHI 경로의 비용을 측정한다.

| 예제 | 측정하는 것 | 현재 예제와의 관계 |
| --- | --- | --- |
| `01_Math` | `DY_ENABLE_SIMD` 설정에 따른 public batch transform 처리 시간 | 단순 loop reference와 공개 batch를 측정한다. 실제 Scalar/SIMD 비교는 옵션이 다른 두 build 결과를 대조한다. |
| `02_Scene` | Entity 수에 따른 Scene 생성·순회·transform update 시간과 메모리 사용량 | 생성·순회·갱신 시간 코드는 작성됐다. 공개 메모리 통계는 아직 없다. |
| `03_Rendering` | Draw 수에 따른 CPU 제출 시간과 GPU frame time, 병렬 기록과 graph 사용에 따른 차이 | 현재는 성공 여부를 알 수 없는 `Renderer::Render` 호출 시도 시간만 측정한다. GPU timestamp와 병렬 기록 API는 아직 없다. |

## 통합한 기존 계획

- `HelloWindow`, `HelloRenderer`, `Cube`는 장면 하나를 만드는 과정이므로 `01_Scene`에
  합친다. 창 생성 코드는 `Common`으로 옮긴다.
- `Lighting`, `Shadow`는 장면 조명이라는 한 결과이므로 `04_Lighting`에서 함께 검증한다.
- `PostProcess`는 `06_Output`에 포함한다. single/multi-thread RenderGraph,
  `FramePipeline`과 resize·병렬 기록은 사용자 화면 기능이 아니라 구현·견고성 문제이므로
  별도 예제로 세지 않는다.
- `CrowdScene`, `RendererStress`, `GPUDriven`은 `07_LargeScene` 하나로 합치되 예제는
  장면 결과만 검증한다. SIMD와 제출 성능은 `Performance`에서 측정하고,
  culling·indirect 같은 구현 정확성은 test로 검증한다.
- `ModelViewer`는 위 기능을 합친 showcase가 필요할 때 `samples`에 두며 공개 API 계약
  예제로 세지 않는다.

## 작성 순서

1. 기존 코드를 `Graphics/01_Scene`으로 합쳐 Graphics 공개 경계를 고정한다.
2. `RHI/01_TexturedCube`를 새로 작성해 RHI와 Backend가 반드시 만족할 최소 경계를
   고정한다.
3. 현재 기능을 `Graphics/02_Materials`, `03_Model`, `04_Lighting`, `07_LargeScene`으로
   이관한다.
4. `05_Animation`, `06_Output`과 나머지 RHI 예제의 코드가 요구하는 API를 차례로
   구현하고, 실행 가능해진 예제부터 CMake 대상에 추가한다.
5. 각 기능 경로가 확정된 뒤 대응하는 `Performance` 예제를 작성한다.
