# dy_engine

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C?style=flat-square&logo=cmake)
![Compiler](https://img.shields.io/badge/Compiler-MSVC%20%7C%20GCC%20%7C%20Clang-555555?style=flat-square)
![Graphics](https://img.shields.io/badge/Graphics-Vulkan%20%7C%20D3D12%20%7C%20Metal-4B5563?style=flat-square)

`dy_engine`은 현대 그래픽스 API의 설계 철학에 맞추어, 하드웨어 캐시 구조와 명시적 자원 모델에 맞춘 얇은 데이터 지향 렌더링 프레임워크입니다.

## 목차

- [프로젝트 개요](#프로젝트-개요)
- [프로젝트 자료](#프로젝트-자료)
- [팀원 소개](#팀원소개)
- [주요 기능](#주요-기능)
- [설계 구조](#설계-구조)
- [요구 사항](#요구-사항)
- [CMake 옵션](#cmake-옵션)
- [빌드 방법](#빌드-방법)
- [실행 방법](#실행-방법)


## 프로젝트 개요

DirectX 12, Vulkan, Metal은 드라이버가 감추던 리소스 상태, 동기화, 파이프라인 구성을 애플리케이션이 직접 설계하도록 요구합니다. 이 방식은 런타임 유효성 검사와 암묵적 상태 추적 비용을 줄일 수 있지만, 두꺼운 범용 래퍼나 구형 상태 기계식 추상화를 얹으면 명시적 API의 장점이 상위 렌더링 단계까지 충분히 드러나지 않습니다.

`dy_engine`은 두 개의 사용 경로를 둡니다. 일반적인 장면은 `Graphics::Renderer`와 `Graphics::Scene`으로 구성하고, 그 추상화가 맞지 않는 렌더링은 `RHI::IDevice`와 `RHI::ICommandList`로 직접 작성합니다. Backends는 이 RHI 계약을 D3D12, Vulkan, Metal 또는 Null로 번역합니다.

목표는 UI·네트워크·패키지 시스템을 갖춘 범용 게임 엔진이 아니라, 명시적 그래픽스 API의 자원과 커맨드 모델을 가리지 않으면서 반복 작업만 줄이는 작은 렌더링 프레임워크입니다. 현재 01~07 예제는 Graphics 경로를 검증하며, Direct RHI 경로는 전용 예제로 계약을 확정해야 하는 상태입니다.

## 프로젝트 자료

| 자료 | 링크 |
| --- | --- |
| 프로젝트 계획서 PPT | [docs/dy_engine_plan.pptx](docs/dy_engine_plan.pptx) |
| 프로젝트 발표 자료 | [docs/dy_engine_presentation.pdf](docs/dy_engine_presentation.pdf) |
| 프로젝트 보고서 | [docs/dy_engine_보고서_DyD팀.docx](docs/dy_engine_보고서_DyD팀.docx) |

## 팀원소개

| 이름 |  [![GitHub](https://img.shields.io/badge/한재승-181717?style=flat-square&logo=github)](https://github.com/suhanjin17)| [![GitHub](https://img.shields.io/badge/윤훈-181717?style=flat-square&logo=github)](https://github.com/yunhoon0206) | [![GitHub](https://img.shields.io/badge/정준혁-181717?style=flat-square&logo=github)](https://github.com/Wnsgur7318) |  [![GitHub](https://img.shields.io/badge/정현진-181717?style=flat-square&logo=github)](https://github.com/junghj0724)|
| :---: | :---: | :---: | :---: | :---: |
|역할 |Core/RHI|Vulkan|Metal|DirectX12|

## 주요 기능

- 얇은 RHI: `IDevice`, `ICommandList`와 `Buffer`·`Texture`·`Pipeline` handle로 상위 렌더러와 백엔드 구현을 분리합니다.
- 백엔드 선택: CMake 옵션으로 Vulkan, Direct3D 12, Metal, Null 백엔드를 선택합니다.
- 두 사용 경로: 기본 Graphics 프런트엔드와 직접 RHI 작성을 분리합니다.
- 렌더 데이터 구성: 장면, 메시, 재질, 텍스처, 조명 데이터를 GPU 제출 형태로 변환합니다.
- 명시적 그래픽스 API 실험: descriptor heap/table, push constant, pipeline state, command list 제출 흐름을 백엔드별로 연결합니다.
- 모델과 텍스처 로딩: OBJ, glTF, FBX 모델을 렌더링 경로에 올리고 재질/텍스처 연결을 검증합니다.
- 조명과 그림자: 현재 stock Renderer는 첫 방향광 또는 첫 점광원을 사용하며, 첫 방향광에 단일 shadow map을 지원합니다.
- 데이터 지향 수학 경로: `DY_ENABLE_SIMD`로 SIMD 행렬 연산 경로를 켜고 끌 수 있습니다.

## 설계 구조

```text
examples/
└─ 공개 API만 사용하는 실행 가능한 계약

src/Public/
├─ Graphics/   Renderer · Scene · Camera · Entity · Mesh · Material · Texture · Light · Model
├─ RHI/        Device · command · resource · binding · pipeline · rendering 계약
├─ Math/       CPU 수학 타입과 연산
└─ Platform/   예제용 Window

src/Graphics/  stock Renderer · GPU 제출 데이터 · model/texture 변환 구현
src/RHI/       선택한 Backend의 Device 생성 진입점
src/Backends/  D3D12 · Vulkan · Metal · Null 번역 구현
src/Platform/  Window 구현
```

### RHI 계층

`RHI::IDevice`가 자원을 생성·파괴하고 frame 제출을 소유하며, `ICommandList`가 barrier, rendering, binding, draw command를 기록합니다. 자원 handle은 `ResourceHandles.h`에 모은 Backend 객체의 비소유 pointer이며, 반드시 생성한 Device의 `Destroy*`로 파괴해야 합니다. `ResourceSet`보다 참조하는 pipeline·buffer·texture가 오래 살아 있어야 합니다.

### Graphics 계층

`Renderer`, `Scene`, `Camera`, `Mesh`, `Material`, `Texture`, `Light`, `Model`을 사용자 인터페이스로 둡니다. `EntityID` 같은 ID와 `Transform`은 Graphics 장면에서만 의미가 있으므로 별도 Core 계층을 만들지 않습니다. importer의 중간 데이터, stock shader 포장, GPU 제출용 구조체는 `src/Graphics/Private`에 숨깁니다.

### API 구현 계층

그래픽스 API 구현은 [CMake 옵션](#cmake-옵션)으로 선택합니다.

## 요구 사항

먼저 빌드할 그래픽스 백엔드를 하나 고릅니다. 공통으로는 CMake와 C++17 컴파일러가 필요하고, 선택한 백엔드에 따라 플랫폼 SDK나 개발 패키지가 추가로 필요합니다.

- CMake 3.20 이상
- C++17 지원 컴파일러
- Git

### Dependencies

GLFW, stb, fastgltf, ufbx는 CMake `FetchContent`로 가져옵니다. 처음 configure할 때는 네트워크 연결이 필요할 수 있습니다.

### Backend Requirements

| 백엔드 | 대상 환경 | 추가 요구 사항 |
| --- | --- | --- |
| `DirectX 12` | Windows | MSVC C++ 빌드 도구, Windows SDK |
| `Vulkan` | Windows 또는 Linux | Vulkan SDK 또는 Vulkan 개발 패키지, `glslc` |
| `Metal` | macOS | Xcode 또는 Command Line Tools |
| 옵션 없음 | 모든 플랫폼 | Null 백엔드. 렌더링 API 없이 인터페이스 빌드 확인용 |

Ubuntu/Debian에서 Vulkan을 빌드할 때는 보통 다음 패키지가 필요합니다.

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git pkg-config
sudo apt-get install -y libvulkan-dev vulkan-tools vulkan-validationlayers-dev glslc
```

GLFW 창 시스템은 `DY_LINUX_WINDOW_SYSTEM`으로 고를 수 있습니다. X11을 사용할 때는 `xorg-dev`, Wayland를 사용할 때는 Wayland 개발 패키지가 필요합니다.

```bash
sudo apt-get install -y xorg-dev
sudo apt-get install -y libwayland-dev wayland-protocols libxkbcommon-dev
```

## CMake 옵션

| 옵션 | 기본값 | 설명 |
| --- | --- | --- |
| `DY_BACKEND` | `Null` | `Null`, `D3D12`, `Vulkan`, `Metal` 중 하나 |
| `DY_ENABLE_SIMD` | `ON` | 지원 CPU에서 SIMD 수학 경로 사용 |
| `DY_LINUX_WINDOW_SYSTEM` | `AUTO` | Linux GLFW window system 선택. `AUTO`, `X11`, `WAYLAND` |

백엔드를 바꿔 실험할 때는 빌드 폴더를 분리하면 CMake cache 충돌을 피할 수 있습니다.

## 빌드 방법

아래 예시는 generator를 지정하지 않습니다. CMake가 환경에 맞는 기본 generator를 고르게 두는 쪽이 보편적입니다. Visual Studio, Xcode, Ninja처럼 특정 generator가 필요하면 `-G` 옵션을, 특정 아키텍처를 지정하려면 `-A` 옵션을 사용하십시오.

### Vulkan

```bash
cmake -S . -B build/vulkan -DDY_BACKEND=Vulkan
cmake --build build/vulkan --config Release
```

### DirectX 12

```powershell
cmake -S . -B build/d3d12 -DDY_BACKEND=D3D12
cmake --build build/d3d12 --config Release
```

### Metal

```bash
cmake -S . -B build/metal -DDY_BACKEND=Metal
cmake --build build/metal --config Release
```

### Null

```bash
cmake -S . -B build/null -DDY_BACKEND=Null
cmake --build build/null --config Release
```

SIMD 비교가 필요하면 같은 백엔드 설정에 `-DDY_ENABLE_SIMD=OFF`를 추가하고 별도 빌드 폴더를 사용합니다.

```bash
cmake -S . -B build/vulkan-nosimd -DDY_BACKEND=Vulkan -DDY_ENABLE_SIMD=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/vulkan-nosimd --config Release
```

아래는 generator 지정 예시입니다.

```bash
cmake -S . -B build/directx -DDY_BACKEND=D3D12 -G "Visual Studio <your_version>" -A "<x64|Win32>"
cmake --build build/directx --config Release
```

## 실행 방법

예제는 빌드 후 생성된 실행 파일 위치에서 실행합니다. 셰이더와 모델 파일이 실행 파일 폴더로 복사되므로, 다른 작업 디렉터리에서 직접 실행하면 상대 경로가 맞지 않을 수 있습니다.

먼저 필요한 예제 타깃을 빌드합니다.

```bash
cmake --build build/vulkan --config Release --target Cube
```

실행 파일 위치는 사용하는 generator에 따라 조금 다릅니다.

| 빌드 방식 | 실행 위치 예시 |
| --- | --- |
| Windows multi-config generator | `build/vulkan/examples/03_Cube/Release/Cube.exe` |
| macOS multi-config generator | `build/metal/examples/03_Cube/Release/Cube` |
| Ninja, Makefile 같은 single-config generator | `build/vulkan/examples/03_Cube/Cube` |

대표 예제:

| 예제 | CMake 타깃 | 예제 폴더 |
| --- | --- |
| Hello Window | `HelloWindow` | `examples/01_HelloWindow` |
| Hello Renderer | `HelloRenderer` | `examples/02_HelloRenderer` |
| Cube | `Cube` | `examples/03_Cube` |
| Textured Cube | `TexturedCube` | `examples/04_TexturedCube` |
| Load Model | `LoadModel` | `examples/05_LoadModel` |
| Shadow Cube | `ShadowCube` | `examples/06_ShadowCube` |
| Renderer Stress | `07_RendererStress` | `examples/07_RendererStress` |
