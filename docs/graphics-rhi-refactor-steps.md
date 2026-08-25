# Graphics · RHI 재구성

## 목표

기본 사용자는 Graphics만 사용하고, 직접 GPU 명령을 다루는 사용자만 RHI를 사용한다.

- Graphics: `Renderer::Create(windowHandle, RendererDesc) → Render(Scene, Camera)`
- Direct RHI: `IDevice::Create(windowHandle, DeviceDesc) → CreateSwapchain(SwapchainDesc) → resource handle → RHI command`

| 계층 | 역할 |
| --- | --- |
| Graphics | `Scene`과 `Camera`를 해석하고 `RendererDesc`의 화면 정책으로 렌더링한다. |
| RHI | 자원, pipeline, binding, rendering과 동기화를 표현한다. |
| Backend | RHI 요청을 native API로 옮기고 GPU 객체의 완료 시점을 추적한다. |

화면 결과를 정하는 값은 `RendererDesc`, `Scene`과 `Camera`에 둔다. 일반 GPU 동작은 RHI descriptor와 command로 요청한다. Backend가 native capability에서 선택한 실제 값은 RHI 객체가 다시 보고한다.

## 사용법 변화

### Graphics

| 구간 | 변경 전 | 완료 후 |
| --- | --- | --- |
| 생성 | 사용자가 `IDevice`와 `Renderer`를 각각 생성·초기화 | `Renderer::Create(windowHandle, RendererDesc)` |
| Shader | Backend별 파일과 경로를 사용자가 선택 | Build에 포함된 기본 shader를 Renderer가 사용 |
| Camera | Renderer setter로 누적 | 사용자가 RH·depth `[0, 1]` 기준의 view·projection을 구성해 `Render(Scene, Camera)`에 전달 |
| Light·material | Renderer setter와 `Scene`에 중복 | 방향광·점광원·material은 `Scene`, ambient·PBR·environment는 `RendererDesc`에서 설정 |
| Frame | `BeginFrame → Render → Present`를 사용자가 호출 | `Renderer::Render` 한 번 호출 |
| 종료 | `Renderer::Shutdown(device)` | `Renderer` 소멸자가 내부 자원을 회수 |

```cpp
auto renderer = Graphics::Renderer::Create(window.GetHandle(), rendererDesc);
Graphics::Camera camera{ view, projection, position };

while(window.IsRunning())
    renderer->Render(scene, camera);
```

Graphics 사용자는 `RendererDesc`, `Scene`과 `Camera`로 화면 결과를 정한다. Device, Swapchain, command, shader와 GPU 자원 수명은 Renderer의 private implementation이 맡는다.

### Direct RHI

| 구간 | 변경 전 | 완료 후 |
| --- | --- | --- |
| 출력 | Device 생성이 Swapchain까지 결정 | Device 생성 뒤 `CreateSwapchain(SwapchainDesc)`로 요청 |
| 자원 | `I*` 객체 포인터와 public `Map/Unmap` | Device가 소유하는 typed resource handle과 `Update*` command |
| Shader·binding | Raw shader byte와 개별 bind 함수 | `ShaderHandle`, pipeline layout과 `ResourceSetHandle` |
| Rendering | Target 설정과 clear를 따로 호출 | attachment·load/store·clear를 `BeginRendering`에 전달 |
| 상태 | Backend가 transition을 추측 | Caller가 command barrier를 기록 |
| 제출 | 실패를 보고하지 않는 submit | `Submit` 성공 뒤에만 present |
| 회수 | 사용자 `delete`와 Device 삭제가 혼재 | 생성 Device의 `Destroy*`에 handle을 반환 |

`BufferHandle`, `TextureHandle`, `ShaderHandle`, `PipelineHandle`과 `ResourceSetHandle`은 Backend 구현 객체를 가리키는 typed pointer handle이다. 생성 Device가 객체를 소유하고 caller는 handle을 빌려 쓴다. `nullptr`은 빈 handle이며 `Destroy*` 뒤 caller의 handle은 더 이상 사용하지 않는다. Backend 구현 객체는 native 객체와 필요한 metadata를 보관한다. `GetBackBuffer()`가 반환한 `TextureHandle`은 Swapchain 소유이므로 `DestroyTexture()`에 전달하지 않고, 매 frame 다시 받아 그 frame의 command에서 사용한다.

초기 resource upload와 backbuffer frame은 다음 두 제출로 이어진다. `Update*` submission은 frame과 독립적이므로 초기화와 frame data 준비에서 필요한 시점에 기록한다.

`Create Device → Create Swapchain → Create owned handles → [AcquireCommandList → barrier → Update → barrier → Close → Submit] → [BeginFrame → GetBackBuffer → AcquireCommandList → Present→RenderTarget barrier → BeginRendering → bind·draw → EndRendering → RenderTarget→Present barrier → Close → Submit → Present] → Destroy owned handles`

## 커밋 순서

| 커밋 | 번호 | 오류 수정 | 없어지는 것 |
| --- | --- | --- | --- |
| `6920883` 스왑체인과 제출 수명 분리 | 1, 3~5, 20(제출), 21 | Device 실패를 factory 결과에 반영하고 Swapchain 요청, frame slot, present image와 native command 수명을 submission 완료 기준으로 분리한다. | 거짓 Device 생성 성공, Device 생성 중 Swapchain, `GetCurrentFrameIndex`, 고정 2-frame과 image index 결합, frame 경로의 blocking wait와 native recording 객체 조기 재사용 |
| `464523a` 렌더링 계약과 소유권 명시 | 2, 6~16, 18~21, 26(shader) | Typed handle, Device의 Create·Update·Destroy, shader·pipeline·resource set, rendering·barrier 계약을 모든 Backend에 구현하고 기본 shader와 일반 shadow·material 경로를 연결한다. | Public `Map/Unmap`·resource destructor, raw shader 수명, Backend 고정 상태·binding·render pass·transition, Vulkan 전용 shadow·fallback과 runtime shader 경로 |
| `f14f04e` 그림자와 광원 계약 정리 | 6(shadow), 17, 23(light) | 그림자 계산과 설정을 Renderer private implementation, `RendererDesc`와 `Scene`으로 모은다. | `ShadowMath`, 방향광 setter와 중복 설정, shadow projection Y-flip, 구현되지 않은 shadow 종류와 계산 |
| `79c0dda` 렌더러 사용 경로 단일화 | 22~27 | `Renderer::Create(windowHandle, RendererDesc) → Render(Scene, Camera)`와 하나의 draw·texture 경로로 Graphics를 정리하고 그림자 설정에 필요한 shader·자원·binding만 만든다. | `ISceneRenderer`, public lifecycle·setter, 중복 Camera·loader·Scene view, `RendererBindingMode`·`IRenderPath`·`RenderPass`, 공개 `GpuScene`·`ImageFile`와 예제의 RHI·Backend 관리 |

## 1. 현재 오류 수정

| 번호 | 문제 | 수정 | 없어지는 것 |
| --- | --- | --- | --- |
| 1 | `IDevice::Create()`가 Backend `Initialize()`의 기존 실패 결과를 무시한다. | factory가 그 결과를 생성 성공 조건으로 사용하고, 실패한 부분 객체를 파괴한 뒤 `nullptr`을 반환한다. | 사용할 수 없는 Device와 거짓 성공 |
| 2 | 자원 갱신·삭제가 public `Map/Unmap`, 사용자 `delete`, GPU 전체 대기와 조기 해제로 나뉜다. | 자원을 만든 Device의 `Create/Update/Destroy`가 handle 하나의 수명 계약을 이룬다. `Update*(ICommandList&, handle, data)`는 caller command에 upload copy를 기록하고 성공 여부를 반환한다. Caller는 handle을 참조한 command를 모두 submit한 뒤 `void Destroy*(handle)`을 호출한다. Staging과 삭제 대상은 마지막 사용 submission 완료값까지 보관한다. | 일반 경로의 GPU idle 대기, public `Map/Unmap`, public resource destructor와 사용자 `delete` |
| 3 | Frame slot과 Swapchain image가 같은 index와 고정 배열을 공유한다. | frame slot에는 completion과 frame-local RHI 상태만 두고 `maxFramesInFlight`만큼 순환한다. Native recording 객체는 21의 submission record가 소유한다. Native image index는 image별 backbuffer와 in-flight 완료 상태에만 사용한다. | 고정 2-frame 배열과 image index로 frame 자원을 재사용하는 코드 |
| 4 | Frame이나 image를 얻지 못해도 command 기록과 present가 계속된다. | `BeginFrame()`은 completion을 기다리지 않고 조회하며, 재사용 가능한 slot과 present image가 모두 준비된 경우에만 `true`다. `false`이면 같은 slot을 다시 시도한다. 그 image를 사용하는 `Submit()`이 성공해 완료값을 연결한 뒤에만 slot을 전진시키며 frame-independent submission은 slot에 영향을 주지 않는다. | 실패 뒤의 command 기록·submit·present, frame completion busy/blocking wait와 제출되지 않은 slot 대기 |
| 5 | Device 생성과 Swapchain 생성이 묶여 Graphics의 화면 요청과 native 실제 결과가 분리되지 않는다. | `IDevice::Create(windowHandle, DeviceDesc)`는 Device만 초기화하고 non-owning window handle을 보관한다. `CreateSwapchain(SwapchainDesc)`는 `format`, `minimumImageCount`, `presentMode`를 받고 `bool`을 반환한다. Backend의 private swapchain 구현은 요청을 보관해 resize·out-of-date 때 재사용한다. 구체 format·present mode는 정확히 지원할 때만 성공하고 `Unknown` format은 Backend 선택을 받아들이는 요청이다. Native가 조정한 image count는 실제 image 목록에, format·extent는 `GetBackBuffer()`의 `TextureHandle` metadata에 반영한다. Renderer는 구체 기본 format과 `RendererDesc`의 buffer count·vsync를 전달하고 실제 backbuffer format으로 pipeline을 만든다. | Device 생성 중의 Swapchain과 출력 설정, Backend 강제 format·image count·present mode와 조용한 format·present mode 대체 |

## 2. RHI 표현 완성

| 번호 | 문제 | 수정 | 없어지는 것 |
| --- | --- | --- | --- |
| 6 | 화면 방향 보정이 Graphics, shader와 Backend에 흩어져 있다. | RHI NDC를 Y-up·depth `[0, 1]`로 고정하고, Vulkan Backend가 viewport를 뒤집어 native 차이를 처리한다. | `RequiresClipSpaceYFlip()`, projection·shadow flip 분기 |
| 7 | Shader 탐색과 compile 방식이 실행 환경마다 다르다. | Build target이 Backend별 shader binary를 만들고 Graphics library의 generated byte array로 포함한다. Renderer는 포함된 binary를 직접 사용한다. | Runtime compile·file I/O, 실행 경로 탐색과 shader fallback path |
| 8 | Pipeline이 shader byte pointer와 외부 수명에 의존한다. | 생성 Device가 `ShaderDesc(stage, entryPoint, binary)`의 binary를 복사·소유하는 `ShaderHandle`을 만들고 회수한다. Pipeline descriptor는 그 handle을 참조한다. | Raw shader pointer·size와 shadow 전용 shader field |
| 9 | Vertex 입력과 기본 Renderer의 vertex-pulling 경로가 중복된다. | Pipeline은 topology와 vertex attribute 목록을 받고 command는 vertex/index buffer를 bind한다. | `GeometryBinding`, 고정 input layout과 vertex-pulling 특례 |
| 10 | Depth-only pass가 shadow 전용 기능으로 구현된다. | Color attachment와 fragment shader가 없는 pipeline을 일반 depth pipeline으로 만들고 일반 rendering command로 실행한다. | Shadow 전용 pipeline 생성 조건 |
| 11 | Raster와 depth/stencil 상태가 Backend 상수다. | Graphics나 Direct RHI 호출자가 pipeline descriptor의 cull, fill, depth, stencil과 bias를 채우고 Backend가 그대로 번역한다. | Backend의 고정 raster·depth 상태와 RHI에 옮겨 적은 숨은 정책 기본값 |
| 12 | Blend와 color write가 attachment 0에 고정된다. | Pipeline descriptor는 실제 color attachment 목록마다 format, blend와 write mask를 받는다. | 단일 `renderTargetFormat`, 고정 attachment 배열과 attachment 0 특례 |
| 13 | Binding layout과 descriptor 용량이 Backend에 고정된다. | Pipeline layout이 binding 목록을 선언하고 `ResourceSetHandle`이 그 layout의 실제 resource handle을 묶는다. Backend allocator 용량은 선언과 생성 요청에서 계산하고 native limit을 넘으면 생성에 실패한다. | 고정 slot·root·set, 개별 bind API, `maxDrawsPerFrame`과 `maxBindlessTextures` |
| 14 | Target, clear와 render pass 시작이 따로 움직인다. | `BeginRendering()`이 color/depth attachment 목록, load/store와 clear를 받고 `EndRendering()`으로 닫힌다. | `SetRenderTargets`, 개별 Clear와 숨은 pass 시작 |
| 15 | Backend가 resource transition을 임의로 넣는다. | Buffer descriptor는 initial state를 표현한다. 새 texture는 `Undefined`, Backend의 private swapchain image만 `Present`에서 시작한다. Command barrier는 before/after state와 subresource를 표현한다. Caller는 2의 `Update*`와 같은 command list에 `CopyDestination` 전환, copy와 사용 상태 전환을 순서대로 기록한다. Backend는 barrier와 present 전이를 native 명령으로 번역한다. | 숨은 transition, command close의 암묵적 present 전이와 Backend별 state 추측 |

## 3. Backend의 고정 렌더링 제거

| 번호 | 문제 | 수정 | 없어지는 것 |
| --- | --- | --- | --- |
| 16 | Vulkan만 shadow pass를 내부에서 실행한다. | Graphics가 일반 depth pass를 기록하고 그 결과를 main pass의 resource set에서 sampling한다. | Vulkan shadow renderer와 shadow Backend 질의 |
| 17 | Shadow 해상도, 포맷, bias, PCF와 view 계산이 여러 계층에 흩어져 있다. | `ShadowMapDesc`와 화면 설정은 `RendererDesc`, directional light 상태는 `Scene`에 둔다. Renderer는 그림자를 사용할 때 해당 포맷·해상도의 depth target과 shader binding을 만들고 설정한 bias·PCF 반경으로 shadow view와 sampling을 구성한다. | `ShadowMath` 타입과 파일, `DeviceDesc` shadow 값, 중복 setter와 구현되지 않은 point·spot·planar shadow field·계산 |
| 18 | Backend별 기본 material texture가 화면 결과를 바꾼다. | `MaterialDesc`가 texture가 없을 때의 sample 의미를 곱셈 항등값, neutral normal과 덧셈 영으로 정하고, Graphics가 그 값의 기본 texture를 일반 resource set에 넣는다. | Backend가 고르는 흰색·검은색 fallback texture |
| 19 | Backend Device가 native resource, pipeline, command와 기본 Renderer 상태를 함께 소유한다. | 각 Backend의 Buffer, Texture, Shader, Pipeline과 ResourceSet 구현 객체가 자신의 native 객체와 필요한 metadata를 소유한다. Public RHI는 이 객체를 `BufferHandle`, `TextureHandle`, `ShaderHandle`, `PipelineHandle`과 `ResourceSetHandle`로 노출하고 생성 Device가 회수한다. Device에는 객체 생성, queue submission, private swapchain 조정과 완료 회수만 남긴다. | `IBuffer`, `ITexture`, `IShader`, `IPipelineState`, `IResourceSet`이라는 가짜 동작 인터페이스, Device의 stock Renderer 상태, 평행 native 객체 배열과 서로 무관한 객체 사이의 부작용 |
| 20 | Submit 실패 뒤 fence, image와 command 상태가 다시 쓰이지 못한다. | `AcquireCommandList()`는 present frame과 독립된 일반 command를 반환해 초기 upload와 frame 작업이 같은 `Submit()` 경로를 쓴다. `Submit()`은 성공 여부를 반환하고 성공한 submission에만 completion 값과 사용한 present image를 연결한다. 실패한 command·descriptor와 frame slot은 즉시 다시 쓸 수 있게 한다. | 별도 upload submission helper, 제출되지 않은 fence 대기와 고착된 image·frame slot |
| 21 | D3D12 allocator와 Metal encoder·drawable가 GPU 완료 전에 재사용된다. | command 기록에 사용한 native 객체를 submission 단위로 묶고 해당 completion 뒤 pool로 반환한다. | 조기 allocator reset, 열린 encoder 제출과 drawable 조기 해제 |

## 4. Graphics 사용 경로 정리

| 번호 | 문제 | 수정 | 없어지는 것 |
| --- | --- | --- | --- |
| 22 | 사용자가 Renderer와 별도로 Device, Swapchain과 frame을 관리한다. | `Renderer::Create(windowHandle, RendererDesc)`는 완성된 `std::unique_ptr<Renderer>` 또는 `nullptr`을 반환한다. `void Render(const Scene&, const Camera&)`는 `BeginFrame`이나 `Submit` 실패 시 present를 생략하고 다음 호출에서 재시도한다. Renderer는 build가 선택한 Backend의 Device, Swapchain, frame과 기본 GPU 자원을 private implementation으로 소유한다. | `ISceneRenderer`, public `Initialize/Shutdown`, RHI field·인자와 반쯤 초기화된 Renderer |
| 23 | Camera, light와 material 설정이 setter와 `Scene`에 중복된다. | Graphics의 `Camera` 하나를 `Render(Scene, Camera)`에 전달한다. Material과 light는 `Scene`, 지속 설정은 `RendererDesc`에서 한 번만 읽고 light 생성·조회·수정에는 같은 typed ID를 사용한다. | `CameraDesc`와 Core의 중복 Camera, raw light index, light·PBR·environment setter와 중복 field |
| 24 | Binding 방식별로 같은 draw가 복제된다. | Renderer는 pipeline layout과 resource set을 사용하는 하나의 draw 경로만 기록하고 Backend가 native allocator를 선택한다. | `RendererBindingMode`, `IRenderPath`와 binding 방식별 draw |
| 25 | Texture cache가 공개되고 다른 Scene의 같은 index를 잘못 재사용한다. | Renderer의 private texture cache는 현재 `Scene` 객체와 그 Scene이 생성한 불변 `TextureID`를 cache identity로 쓴다. Scene 객체가 바뀌면 기존 GPU texture를 2의 완료값으로 회수하고 새 Scene을 다시 upload한다. | 공개 `GpuScene`·`ImageFile`, Scene을 무시한 index cache와 별도 삭제 경로 |
| 26 | 기본 Renderer 전용 shader 경로와 payload layout이 public API에 노출된다. | 7의 bundled shader, C++ payload와 binding·flag ABI를 Renderer private implementation으로 옮기고 `StockShaderLayout.inc`를 shader 언어와 C++의 단일 값 출처로 사용한다. Custom GPU 작업은 일반 `ShaderHandle`, pipeline layout과 `ResourceSetHandle` 계약을 쓴다. | 공개 shader path와 `RendererShaderLayout` |
| 27 | 같은 Scene과 model data에 개별 API, 전체 vector view와 helper class가 겹쳐 있다. | Scene은 typed ID 기반의 생성·조회·수정만 제공하고 model loading은 기존 free 함수 경로 하나를 사용한다. | 사용처 없는 Scene vector view와 `ModelLoader` wrapper class |
