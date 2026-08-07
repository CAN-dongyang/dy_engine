# 02. RenderDoc debug events

## 목표

한 쌍의 공통 RHI scope API로 RenderDoc 캡처에서 pass와 draw 구간의 이름을 확인한다.
RenderDoc SDK에 직접 의존하지 않고 graphics API의 native debug event 기능을 사용한다.

## 예정 RHI 계약

- `ICommandList::BeginDebugEvent(name, color)`
- `ICommandList::EndDebugEvent()`
- `ICommandList::InsertDebugMarker(name, color)`
- begin/end를 자동으로 짝짓는 RAII command debug scope
- Null Backend에서 scope nesting과 unbalanced end 검증

이름은 PIX, Vulkan, Metal 중 하나에 종속되지 않는 `DebugEvent`와 `DebugMarker`를 사용한다.

## Backend 번역

| Backend | Native API |
| --- | --- |
| D3D12 | PIX event/marker 또는 `ID3D12GraphicsCommandList::BeginEvent`, `EndEvent`, `SetMarker` |
| Vulkan | `VK_EXT_debug_utils`의 command buffer label begin/end/insert |
| Metal | `pushDebugGroup`, `popDebugGroup`, `insertDebugSignpost` |
| Null | 문자열과 nesting 상태를 검증하고 GPU 호출은 하지 않음 |

## 예정 변경 폴더

| 폴더 | 변경 내용 |
| --- | --- |
| `src/RHI/` | API 중립 debug event 계약과 RAII scope |
| `src/Backends/Null/` | nesting 계약 검증 |
| `src/Backends/Metal/` | Metal debug group/signpost 번역 |
| `src/Backends/Vulkan/` | `VK_EXT_debug_utils` label 번역과 extension 로딩 |
| `src/Backends/D3D12/` | PIX-compatible event/marker 번역 |
| `src/Graphics/` | Shadow, MainForward 등 실제 pass 이름 기록 |

## 완료 조건

- [ ] 공통 RHI debug event 계약
- [ ] RAII scope wrapper
- [ ] Null nesting 검증
- [ ] Metal 구현·캡처 확인
- [ ] Vulkan 구현·RenderDoc 캡처 확인
- [ ] D3D12 구현·RenderDoc 캡처 확인
