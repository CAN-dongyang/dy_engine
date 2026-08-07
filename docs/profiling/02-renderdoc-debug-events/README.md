# 02. RenderDoc debug events

## 목표

한 쌍의 공통 RHI scope API로 RenderDoc 캡처에서 pass와 draw 구간의 이름을 확인한다.
RenderDoc SDK에 직접 의존하지 않고 graphics API의 native debug event 기능을 사용한다.

## 구현한 RHI 계약

- `ICommandList::BeginDebugEvent(name, color)`
- `ICommandList::EndDebugEvent()`
- `ICommandList::InsertDebugMarker(name, color)`
- begin/end를 자동으로 짝짓는 `CommandDebugEventScope` RAII wrapper
- Null Backend에서 scope nesting과 unbalanced end 검증

이름은 PIX, Vulkan, Metal 중 하나에 종속되지 않는 `DebugEvent`와 `DebugMarker`를 사용한다.

## Backend 번역

| Backend | Native API |
| --- | --- |
| D3D12 | WinPixEventRuntime의 `PIXBeginEvent`, `PIXEndEvent`, `PIXSetMarker` |
| Vulkan | `VK_EXT_debug_utils`의 command buffer label begin/end/insert |
| Metal | `pushDebugGroup`, `popDebugGroup`, `insertDebugSignpost` |
| Null | 문자열과 nesting 상태를 검증하고 GPU 호출은 하지 않음 |

Metal은 RenderDoc 지원 대상이 아니므로 Metal 결과는 Xcode GPU Capture에서 확인한다.
Vulkan과 D3D12 결과는 RenderDoc 캡처에서 확인한다.

## 변경 폴더

| 폴더 | 변경 내용 |
| --- | --- |
| `src/RHI/` | API 중립 debug event 계약과 RAII scope |
| `src/Backends/Null/` | nesting 계약 검증 |
| `src/Backends/Metal/` | Metal debug group/signpost 번역 |
| `src/Backends/Vulkan/` | `VK_EXT_debug_utils` label 번역과 extension 로딩 |
| `src/Backends/D3D12/` | PIX-compatible event/marker 번역 |
| `src/Graphics/` | Shadow, MainForward 등 실제 pass 이름 기록 |
| `cmake/`, `examples/` | WinPixEventRuntime 고정 버전 다운로드·링크와 DLL 배치 |

## 실제 기록 이름

- scope: `Shadow`, `MainForward`
- marker: `Shadow.Draws`, `MainForward.Draws`
- 색상은 Shadow와 MainForward를 서로 다르게 지정한다.

## 구현 메모

- Vulkan command list는 draw를 지연 기록하므로 label도 draw index와 pass 종류를 함께 저장한 뒤 실제 `VkCommandBuffer` 기록 시 재생한다.
- `VK_EXT_debug_utils`가 있는 경우에만 instance extension을 활성화하고 함수 포인터를 로드한다. 없는 환경에서는 GPU label 호출만 생략한다.
- D3D12는 WinPixEventRuntime `1.0.240308001`을 SHA-256으로 고정했으며, 예제 실행 폴더에 DLL을 자동 복사한다.
- Null Backend는 빈 이름, begin/end 불일치, 닫히지 않은 scope를 예외로 검출한다.
- RAII scope는 조기 반환에서도 자동으로 닫힌다. command list를 닫기 전 명시적으로 `End()`해야 하는 위치도 지원한다.

## 검증 결과 (2026-08-07)

GitHub 최신 `main`의 `fbdbe44`를 기준으로 새 브랜치에 이식한 뒤 다시 검증했다.
최신 Vulkan의 `VulkanDevice::Impl` 구조를 유지하고 label 기록 기능만 그 내부에
통합했다.

| 대상 | 결과 |
| --- | --- |
| 공통/Null | 별도 Null 구성에서 전체 예제 빌드 및 `03_Cube` 실행 성공 |
| Metal + Tracy | 신규 `/private/tmp/dy_engine_latest_metal` 구성에서 전체 빌드 성공 |
| Metal 실행 | `03_Cube` 창 실행 성공, shader/command 오류 없음 |
| Vulkan | 현재 Mac에 Vulkan SDK와 `glslc`가 없어 빌드·RenderDoc 검증 대기 |
| D3D12 | 현재 Mac에서 컴파일할 수 없어 Windows 빌드·RenderDoc 검증 대기 |
| Metal 캡처 | Xcode GPU Capture에서 `MainForward`, `pushDebugGroup`, `popDebugGroup` 확인 완료 |

Null 빌드에서 기존 `Mesh.cpp`의 `nodiscard` 경고 1개가 있었으며 이번 단계 변경과 무관하다.

### 사용한 검증 명령

```sh
cmake --build /private/tmp/dy_engine_latest_metal -j 6
cmake --build /private/tmp/dy_engine_latest_null -j 6
cd /private/tmp/dy_engine_latest_metal/examples/03_Cube
./Cube
```

## 완료 조건

- [x] 공통 RHI debug event 계약
- [x] RAII scope wrapper
- [x] Null nesting 검증
- [x] Metal 구현·빌드·실행 확인
- [x] Metal Xcode GPU Capture 확인
- [x] Vulkan 구현
- [ ] Vulkan SDK 빌드·RenderDoc 캡처 확인
- [x] D3D12 구현
- [ ] D3D12 Windows 빌드·RenderDoc 캡처 확인
