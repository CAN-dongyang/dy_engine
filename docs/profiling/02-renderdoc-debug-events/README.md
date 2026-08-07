# 02. GPU debug events

## 개요

GPU capture 도구에서 render pass와 draw 구간을 식별할 수 있도록 API 중립적인 debug
event와 marker를 제공한다.

## RHI API

- `ICommandList::BeginDebugEvent(name, color)`
- `ICommandList::EndDebugEvent()`
- `ICommandList::InsertDebugMarker(name, color)`
- `CommandDebugEventScope` RAII wrapper

RAII wrapper는 정상 종료와 조기 반환 모두에서 열린 event를 자동으로 닫는다.

## Backend 매핑

| Backend | Native API |
| --- | --- |
| D3D12 | `PIXBeginEvent`, `PIXEndEvent`, `PIXSetMarker` |
| Vulkan | `VK_EXT_debug_utils` command buffer labels |
| Metal | `pushDebugGroup`, `popDebugGroup`, `insertDebugSignpost` |
| Null | event nesting 및 begin/end 계약 검증 |

Metal은 RenderDoc 대신 Xcode GPU Capture에서 label을 확인한다. Vulkan과 D3D12의 native
annotation은 RenderDoc 및 PIX capture에서 사용할 수 있다.

## 기록 범위

- scope: `Shadow`, `MainForward`, `ProfilerHUD`
- marker: `Shadow.Draws`, `MainForward.Draws`
- pass 종류별 debug color

Vulkan은 지연 기록되는 draw index와 함께 label event를 저장한 뒤 실제 command buffer
기록 시 재생한다. `VK_EXT_debug_utils`가 없는 장치에서는 label 기록만 비활성화된다.

D3D12는 WinPixEventRuntime을 고정 버전으로 가져와 link하고 runtime DLL을 실행 폴더에
배치한다.

## 검증

- Null: nesting 및 unbalanced event 검증
- Metal: macOS 빌드·실행 및 Xcode GPU Capture label 확인
- Vulkan: Ubuntu CI 빌드 통과, RenderDoc capture 검증 대기
- D3D12: Windows CI 빌드 통과, PIX/RenderDoc capture 검증 대기
