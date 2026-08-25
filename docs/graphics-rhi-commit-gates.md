# Graphics · RHI 커밋 점검

AI용. 커밋마다 `docs/graphics-rhi-refactor-steps.md`의 해당 번호, parent, staged diff와 결과 tree를 함께 읽는다. 계획·gate·검토 log는 구현 커밋에 넣지 않는다.

## 범위

- diff의 모든 hunk를 해당 계획의 `문제`, `수정`, `없어지는 것` 중 하나에 직접 대응시킨다. 대응되지 않는 변경은 제거한다.
- 직접 필요한 호출부·Backend 대응만 함께 바꾼다. 관련 개선, 방어 코드, 일관성 정리와 추후 작업은 현재 변경의 근거가 아니다.
- 주석만 바뀐 파일과 서식·include 정리는 넣지 않는다. 새 public 계약을 설명하는 주석은 실제 계약 변경과 같은 hunk에서만 바꾼다. 같은 커밋에서 삭제한 심볼이나 파일을 가리키는 주석은 그 삭제와 함께 제거한다.
- Model, RenderGraph, Tracing과 SDK는 별도 계획이다. 현재 문서의 번호가 직접 요구하지 않는 파일은 건드리지 않는다.

## 구조

- 새 상수, 기본값, 분기, 배열, helper, type, 상태와 API는 최종 구조에 남는 불변식이나 사용자 선택을 표현해야 한다.
- 후속 커밋에서 삭제할 임시 구조를 만들지 않는다. 리터럴을 `constexpr`, enum, helper나 type으로 감싸도 값의 출처가 생기지는 않는다.
- 새 public·private 함수, 반환형, `Result`, `IsValid`, 예외 변환과 상태 머신은 해당 계획이 요구하는 호출자 선택이나 영구 불변식이 있을 때만 만든다. 기존 `nullptr`, `bool`, `void`와 예외 정책으로 충분하면 그대로 쓴다.
- helper는 접근 경로를 늘리거나 한 호출·리터럴·고정값을 숨기지 않는다.
- 같은 데이터에 wrapper class와 free 함수, typed ID와 raw index, 개별 접근과 전체 container view를 함께 노출하지 않는다.

## 계약

- 기본 Graphics 호출 경로를 변경 전후로 적어 새 RHI·Backend 개념이 사용자에게 새지 않는지 확인한다.
- `IDevice`와 `ICommandList`만 행동 interface다. Buffer, Texture, Shader, Pipeline과 ResourceSet은 생성 Device가 소유하고 caller가 빌려 쓰는 typed handle이다.
- Public RHI의 descriptor, command와 `Create/Update/Destroy`는 `BufferHandle`, `TextureHandle`, `ShaderHandle`, `PipelineHandle`과 `ResourceSetHandle`을 일관되게 사용한다.
- Handle은 기존 Backend 구현 객체를 가리키는 typed pointer다. 숫자 ID, registry, generation, refcount, owner 검증 상태와 `IsValid`를 추가하지 않는다.
- 화면 정책은 Graphics, 일반 GPU 계약은 RHI, native 변환과 GPU 완료 추적은 Backend에 둔다.
- 동작을 정하는 값은 사용자·RHI 요청 또는 native capability·제약에서 온다. Backend가 native 값을 선택하면 실제 결과를 RHI metadata로 보고한다.
- 값이나 format 전달을 고친다는 이유로 shader, 색 변환, Scene과 화면 결과를 함께 바꾸지 않는다. 화면 결과 변경은 해당 계획에 명시된 경우에만 한 계층에서 한 번 적용한다.
- 기능 설정에 따라 shader 선언, pipeline layout과 resource set이 같은 binding 집합을 사용한다. 비활성 기능을 가짜 resource binding으로 채우지 않는다.
- 공통 RHI 계약을 바꾸면 모든 Backend와 호출부가 같은 계약을 구현한다. Backend 고유 변경은 해당 Backend만 검증한다.
- Create·Update·Destroy 계열은 같은 소유권과 실패 규칙을 쓴다. 삭제한 동작의 영구 대체는 같은 커밋에 있어야 한다.

## 동기화

- 일반 update·destroy·frame 경로는 CPU를 기다리게 하지 않는다. 특정 submission 완료 여부를 조회하고 준비되지 않았으면 작업을 보관하거나 `false`를 반환한다.
- Queue submission 완료와 화면 표시 완료를 같은 시점으로 취급하지 않는다. 표시 완료 근거 없이 retired swapchain과 present semaphore를 재사용하거나 파괴하지 않는다.
- blocking wait는 사용자가 명시적으로 호출한 wait API와 종료 정리에만 둔다. 제거했던 blocking helper나 wait 호출을 다른 이름으로 되살리지 않는다.

## 커밋

- 각 커밋은 clean parent에서 만들며 실험 커밋을 cherry-pick·squash해 시작하지 않는다.
- 각 커밋의 결과 tree가 독립적으로 gate와 build를 통과한다. 후속 작업에서 결함을 찾으면 원래 커밋을 amend하고 다시 검증한다.
- 명시한 파일만 stage하고 `git status`, staged diff와 commit diff를 구분해 확인한다. `.md`와 review log가 stage되지 않았는지 확인한다.
- 동작 변경은 해당 결과를 직접 확인하는 test·example로 검증하고, 구조 변경은 public API compile 경로를 검증한다.
- 영향받는 현재 플랫폼의 Backend를 증분 build한 뒤 커밋한다.
- 전달할 때 계획 번호, 변경 파일별 이유, 검증 결과만 짧은 표로 남긴다.
