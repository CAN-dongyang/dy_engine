# 01. Tracy CPU zones

## 목표

렌더링의 주요 CPU 작업을 Tracy timeline에서 계층적으로 확인한다. 계측이 꺼진
빌드는 Tracy 의존성과 runtime 비용 없이 기존 동작을 유지한다.

## 현재 구현

- `DY_ENABLE_TRACY` CMake option을 제공하며 기본값은 `ON`이다. 필요할 때만
  `-DDY_ENABLE_TRACY=OFF`로 명시적으로 끈다.
- Tracy `v0.13.1`을 선택적으로 연결한다.
- `DY_PROFILE_CPU_ZONE_NAMED`가 Tracy의 RAII scoped zone을 감싼다.
- `Renderer::Render` 아래에 texture sync, material/light/shadow update와 선택된
  RenderPath의 resource preparation, shadow draw, main draw zone이 중첩된다.
- Tracy memory allocation/free macro는 사용하지 않는다.
- GitHub 최신 `main` 기준 커밋 `fbdbe44`에 이식했다.
- 계측은 Cube가 아니라 `dy_engine`과 public `Engine_Options`에 연결된다. 따라서
  엔진을 링크하는 어떤 실행 파일이든 자기 프로세스 안에서 Tracy client가 자동으로
  시작되고, Profiler에는 그 실행 파일의 이름으로 나타난다.

## 변경 폴더

| 폴더 | 변경 내용 |
| --- | --- |
| `cmake/` | Tracy option, FetchContent, compile definition과 link 설정 |
| `src/Platform/` | Tracy가 꺼졌을 때 no-op이 되는 CPU zone wrapper |
| `src/Graphics/` | Renderer, GpuScene, RenderPath CPU zone |
| `examples/03_Cube/` | Metal에서 실제 zone을 검증하기 위한 MSL shader |
| `src/Backends/Metal/` | 길이가 명시된 MSL source 로딩 |

## Backend 상태

Tracy CPU zone은 Graphics와 Platform 코드에 있으므로 graphics API와 독립적이다.
Backend마다 별도의 Tracy 코드를 추가하지 않고 동일 zone을 사용한다. 다만 빌드와
실행은 각 운영체제에서 별도로 검증해야 한다.

| Backend | 상태 | 남은 검증 |
| --- | --- | --- |
| Metal | 완료 | 없음 |
| Vulkan | 대기 | Tracy ON/OFF 빌드, 예제 실행, timeline 확인 |
| D3D12 | 대기 | Tracy ON/OFF 빌드, 예제 실행, timeline 확인 |
| Null | 부분 완료 | Tracy OFF 빌드·실행 완료, Tracy ON 확인 대기 |

## Metal 검증 기록

- `DY_ENABLE_TRACY=OFF`: 전체 빌드 성공
- `DY_ENABLE_TRACY=ON`: 전체 빌드 성공
- `03_Cube`: Metal shader 로드 후 창과 렌더 루프 실행 확인
- Tracy Profiler `0.13.1`: Cube client 연결 확인
- 테스트용 Cube는 확인 후 `Ctrl-C`로 종료했으므로 종료 코드 `130`은 의도된 값이다.
- 최신 `main` 기반 `/private/tmp/dy_engine_latest_metal` 신규 구성에서도 전체 빌드와 `03_Cube` 실행을 다시 확인했다.

## 실행

```bash
cmake -S . -B build-tracy \
  -DUSE_METAL=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-tracy -j4
```

Tracy에서 `127.0.0.1` 연결을 열고 다음을 실행한다.

```bash
cd build-tracy/examples/03_Cube
./Cube
```

## 완료 조건

- [x] 공통 RAII CPU zone wrapper
- [x] 엔진을 사용하는 모든 실행 파일에 기본 포함
- [x] 주요 Graphics CPU zone
- [x] Tracy OFF 빌드
- [x] Metal Tracy ON 빌드·실행
- [ ] Null Tracy ON 빌드
- [x] Null Tracy OFF 빌드·실행
- [ ] Vulkan Tracy ON/OFF 빌드·실행
- [ ] D3D12 Tracy ON/OFF 빌드·실행
