"""Validate the API support contract without requiring simultaneous backend ports."""

import argparse
import copy
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile


APIS = ("vulkan", "d3d12", "metal")
CHECKS = {"build", "shader", "cpu"}
CONTRACT_PATH = ".github/ci-support.json"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def keys(value, expected, label):
    require(isinstance(value, dict) and set(value) == set(expected),
            f"{label}: expected fields {', '.join(expected)}")


def source_path(value, label):
    require(isinstance(value, str) and bool(value) and "\\" not in value,
            f"{label}: expected a relative POSIX source path")
    path = PurePosixPath(value)
    require(not path.is_absolute() and ".." not in path.parts
            and ":" not in value and str(path) == value,
            f"{label}: unsafe source path {value!r}")


def unique_strings(values, label):
    require(isinstance(values, list) and all(isinstance(x, str) for x in values),
            f"{label}: expected a string array")
    require(len(values) == len(set(values)), f"{label}: duplicate value")


def validate_contract(document):
    keys(document, ("version", "items"), "contract")
    require(type(document["version"]) is int and document["version"] == 1,
            "Unsupported contract version")
    items = document["items"]
    require(isinstance(items, dict), "Contract items must be an object")
    directories, targets = set(), set()
    for name, item in items.items():
        require(re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*", name),
                f"Invalid item ID: {name}")
        keys(item, ("kind", "target", "directory", "requires", "arguments",
                    "rendering", "apis"), name)
        require(item["kind"] in ("example", "feature"), f"{name}: unknown kind")
        require(isinstance(item["target"], str) and
                re.fullmatch(r"[A-Za-z0-9_]+", item["target"]),
                f"{name}: invalid CMake target")
        source_path(item["directory"], name)
        require(item["directory"].startswith("examples/"),
                f"{name}: directory must be under examples/")
        unique_strings(item["requires"], name + ".requires")
        require(isinstance(item["arguments"], list) and
                all(isinstance(a, str) and "\x00" not in a for a in item["arguments"]),
                f"{name}: arguments must be strings")
        require(type(item["rendering"]) is bool, f"{name}: rendering must be boolean")
        keys(item["apis"], APIS, name + ".apis")
        if item["kind"] == "example":
            require(item["target"] not in targets and item["directory"] not in directories,
                    f"{name}: duplicate example target or directory")
            targets.add(item["target"])
            directories.add(item["directory"])
        else:
            require(bool(item["requires"]), f"{name}: feature needs a dependency")
        for api, support in item["apis"].items():
            label = f"{name}/{api}"
            keys(support, ("status", "checks", "shaders", "gpu_required"), label)
            require(support["status"] in ("supported", "planned", "unsupported"),
                    f"{label}: unknown support state")
            unique_strings(support["checks"], label + ".checks")
            require(set(support["checks"]) <= CHECKS,
                    f"{label}: unknown/unavailable check (GPU CI is not integrated yet)")
            require(support["gpu_required"] is False,
                    f"{label}: GPU gating is unavailable until the GPU workflow is integrated")
            checks = support["checks"]
            require(not checks or "build" in checks, f"{label}: checks require build")
            require(support["status"] != "unsupported" or not checks,
                    f"{label}: use planned for an existing build-only implementation")
            if support["status"] == "supported":
                require("build" in checks, f"{label}: supported item requires build")
                require(not item["rendering"] or "shader" in checks,
                        f"{label}: rendering support requires shader checks")
            require(isinstance(support["shaders"], list), f"{label}: shaders must be an array")
            require(bool(support["shaders"]) == ("shader" in checks),
                    f"{label}: shader check and shader sources must be registered together")
            shader_paths = set()
            for shader in support["shaders"]:
                keys(shader, ("source", "stage"), label + ".shader")
                source_path(shader["source"], label)
                require(shader["source"].startswith("Shaders/") and
                        shader["source"] not in shader_paths, f"{label}: invalid/duplicate shader")
                shader_paths.add(shader["source"])
                require(shader["stage"] in ("vertex", "fragment", "compute"),
                        f"{label}: invalid shader stage")
                require(PurePosixPath(shader["source"]).suffix ==
                        {"vulkan": ".glsl", "d3d12": ".hlsl", "metal": ".metal"}[api],
                        f"{label}: shader extension does not match API")

    visited, active = set(), set()

    def visit(name):
        require(name in items, f"Unknown dependency {name}")
        require(name not in active, f"Dependency cycle at {name}")
        if name in visited:
            return
        active.add(name)
        for dependency in items[name]["requires"]:
            visit(dependency)
            for api in APIS:
                if items[name]["apis"][api]["status"] == "supported":
                    require(items[dependency]["apis"][api]["status"] == "supported",
                            f"{name}/{api}: dependency {dependency} is not supported")
        active.remove(name)
        visited.add(name)

    for name, item in items.items():
        visit(name)
        if item["kind"] == "feature":
            owners = [items[dep] for dep in item["requires"] if items[dep]["kind"] == "example"
                      and items[dep]["target"] == item["target"]
                      and items[dep]["directory"] == item["directory"]]
            require(len(owners) == 1, f"{name}: feature must require its example target")
            for api in APIS:
                if item["apis"][api]["checks"]:
                    require("build" in owners[0]["apis"][api]["checks"],
                            f"{name}/{api}: owning example has no build check")
    return document


def read_json(text):
    def no_duplicates(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=no_duplicates)


def policy():
    current = validate_contract(read_json(Path(CONTRACT_PATH).read_text(encoding="utf-8")))
    if not current["items"]:
        print("No examples or feature checks registered")
    for name, item in current["items"].items():
        for api in APIS:
            support = item["apis"][api]
            print(f"{name}/{api}: {support['status']}; checks={','.join(support['checks']) or 'none'}; GPU unverified")
    return current


def verify_outputs(document, build_dir, config, api):
    validate_contract(document)
    require(api in (*APIS, "null"), "Unknown build API")
    require(config in ("Debug", "Release"), "Unknown build configuration")
    build_dir = Path(build_dir).resolve()
    paths = read_json((build_dir / f"ci-targets-{config}.json").read_text(encoding="utf-8"))
    require(isinstance(paths, dict), "CMake target inventory must be an object")
    selected_api = "vulkan" if api == "null" else api
    selected = {name: item for name, item in document["items"].items()
                if (all("cpu" in item["apis"][backend]["checks"] for backend in APIS)
                    if api == "null" else "build" in item["apis"][api]["checks"])}
    expected = {item["target"] for item in selected.values()}
    require(set(paths) == expected,
            f"CMake target inventory differs: missing={sorted(expected - set(paths))}, "
            f"unexpected={sorted(set(paths) - expected)}")
    for target, value in paths.items():
        require(isinstance(value, str), f"{target}: invalid binary path")
        binary = Path(value).resolve()
        require(binary.is_relative_to(build_dir) and binary.is_file() and binary.stat().st_size > 0,
                f"{target}: missing or invalid built executable: {binary}")
    shader_count, cpu_count = 0, 0
    for name, item in selected.items():
        support = item["apis"][selected_api]
        binary = Path(paths[item["target"]]).resolve()
        if api != "null":
            extension = {"vulkan": "spv", "d3d12": "dxbc", "metal": "air"}[api]
            for shader in support["shaders"]:
                output = build_dir / "ci-shaders" / config / name / (shader["source"] + "." + extension)
                require(output.is_file() and output.stat().st_size > 0,
                        f"{name}/{api}: missing offline shader output: {output}")
                runtime_source = PurePosixPath(shader["source"])
                if api == "vulkan":
                    runtime_source = runtime_source.with_suffix(".spv")
                runtime_shader = binary.parent / str(runtime_source)
                require(runtime_shader.is_file() and runtime_shader.stat().st_size > 0,
                        f"{name}/{api}: shader was not deployed beside the executable: {runtime_shader}")
                shader_count += 1
        if "cpu" in support["checks"]:
            result = subprocess.run([str(binary), *item["arguments"]], cwd=binary.parent,
                                    timeout=60, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace")
            log = build_dir / "ci-checks" / config / f"{name}.log"
            log.parent.mkdir(parents=True, exist_ok=True)
            log.write_text(result.stdout + result.stderr, encoding="utf-8")
            print(result.stdout + result.stderr, end="")
            require(result.returncode == 0, f"{name}/{api}: CPU check exited {result.returncode}")
            cpu_count += 1
    print(f"Verified {len(paths)} example executables, {shader_count} shader outputs, {cpu_count} CPU checks")
    if not cpu_count:
        print("No CPU checks registered on this revision; no CPU runtime coverage is claimed")
    print("GPU execution is not part of this build verification")


def self_test():
    # Configuration changes are allowed; the current configuration must be valid.
    def api(status="unsupported", checks=None):
        return {"status": status, "checks": checks or [], "shaders": [],
                "gpu_required": False}

    original = {
        "version": 1,
        "items": {
            "Window": {
                "kind": "example", "target": "Window",
                "directory": "examples/01_Window", "requires": [],
                "arguments": [], "rendering": False,
                "apis": {"vulkan": api("supported", ["build"]),
                         "d3d12": api(), "metal": api()},
            }
        },
    }
    validate_contract(original)
    addition = copy.deepcopy(original)
    addition["items"]["Window"]["apis"]["d3d12"] = api("supported", ["build"])
    accepted = [("adding API support", addition)]
    removed = copy.deepcopy(original)
    removed["items"].clear()
    accepted.append(("removing all examples", removed))
    downgraded = copy.deepcopy(original)
    downgraded["items"]["Window"]["apis"]["vulkan"]["status"] = "planned"
    accepted.append(("returning to planned", downgraded))
    downgraded = copy.deepcopy(original)
    downgraded["items"]["Window"]["apis"]["vulkan"] = api()
    accepted.append(("withdrawing API support and checks", downgraded))
    renamed = copy.deepcopy(original)
    renamed["items"]["Renamed"] = renamed["items"].pop("Window")
    renamed["items"]["Renamed"].update(target="Renamed", directory="examples/02_Renamed",
                                      arguments=["--new-mode"])
    accepted.append(("renaming and changing an example", renamed))
    planned = copy.deepcopy(original)
    planned["items"]["Window"]["apis"]["vulkan"] = api("planned")
    accepted.append(("removing a build check", planned))
    shader_changed = copy.deepcopy(original)
    shader_changed["items"]["Window"]["rendering"] = True
    shader_changed["items"]["Window"]["apis"]["vulkan"].update(
        checks=["build", "shader"],
        shaders=[{"source": "Shaders/replacement.glsl", "stage": "vertex"}])
    accepted.append(("replacing shader checks", shader_changed))
    shader_removed = copy.deepcopy(shader_changed)
    shader_removed["items"]["Window"]["apis"]["vulkan"] = api("planned", ["build"])
    accepted.append(("removing shader checks", shader_removed))
    for name, candidate in accepted:
        try:
            validate_contract(candidate)
        except ValueError as error:
            raise AssertionError(f"Rejected {name}: {error}") from error

    cases = []
    missing_api = copy.deepcopy(original)
    del missing_api["items"]["Window"]["apis"]["metal"]
    cases.append(("omitting an API", missing_api))
    typo = copy.deepcopy(original)
    typo["items"]["Window"]["apis"]["vulkan"]["checks"] = ["biuld"]
    cases.append(("unknown check", typo))
    traversal = copy.deepcopy(original)
    traversal["items"]["Window"]["directory"] = "examples/../../outside"
    cases.append(("source path traversal", traversal))
    dependent = copy.deepcopy(original)
    feature = copy.deepcopy(original["items"]["Window"])
    feature.update(kind="feature", requires=["Window"])
    feature["apis"]["d3d12"] = api("supported", ["build"])
    dependent["items"]["Feature"] = feature
    cases.append(("supporting a feature before its dependency", dependent))
    implicit_build = copy.deepcopy(dependent)
    implicit_build["items"]["Feature"]["apis"]["d3d12"]["status"] = "planned"
    cases.append(("implicitly building an unsupported example through a feature", implicit_build))
    cycle = copy.deepcopy(original)
    cycle["items"]["Window"]["requires"] = ["Window"]
    cases.append(("dependency cycle", cycle))
    gpu = copy.deepcopy(original)
    gpu["items"]["Window"]["apis"]["vulkan"].update(
        checks=["build", "gpu"], gpu_required=True)
    cases.append(("requiring a GPU runner before integration", gpu))
    missing_shader = copy.deepcopy(original)
    missing_shader["items"]["Window"]["rendering"] = True
    cases.append(("rendering support without shader validation", missing_shader))

    for name, candidate in cases:
        try:
            validate_contract(candidate)
        except ValueError:
            continue
        raise AssertionError(f"Accepted {name}")

    with tempfile.TemporaryDirectory() as temporary:
        build = Path(temporary)
        binary = build / "Window.exe"
        binary.write_bytes(b"example binary")
        (build / "ci-targets-Debug.json").write_text(
            json.dumps({"Window": str(binary)}), encoding="utf-8")
        verify_outputs(original, build, "Debug", "vulkan")
        shader_contract = copy.deepcopy(original)
        shader_contract["items"]["Window"]["rendering"] = True
        shader_support = shader_contract["items"]["Window"]["apis"]["vulkan"]
        shader_support["checks"].append("shader")
        shader_support["shaders"] = [{"source": "Shaders/mesh_vs.glsl", "stage": "vertex"}]
        shader_output = build / "ci-shaders/Debug/Window/Shaders/mesh_vs.glsl.spv"
        shader_output.parent.mkdir(parents=True)
        shader_output.write_bytes(b"compiled shader")
        runtime_shader = build / "Shaders/mesh_vs.spv"
        runtime_shader.parent.mkdir()
        runtime_shader.write_bytes(b"compiled shader")
        verify_outputs(shader_contract, build, "Debug", "vulkan")
        runtime_shader.unlink()
        try:
            verify_outputs(shader_contract, build, "Debug", "vulkan")
        except ValueError:
            pass
        else:
            raise AssertionError("Missing runtime shader resource was accepted")
        runtime_shader.write_bytes(b"compiled shader")
        shader_output.unlink()
        try:
            verify_outputs(shader_contract, build, "Debug", "vulkan")
        except ValueError:
            pass
        else:
            raise AssertionError("Missing compiled shader output was accepted")
        binary.unlink()
        try:
            verify_outputs(original, build, "Debug", "vulkan")
        except ValueError:
            pass
        else:
            raise AssertionError("Missing example executable was accepted")
        cpu_contract = copy.deepcopy(original)
        cpu_contract["items"]["Window"]["apis"]["vulkan"]["checks"].append("cpu")
        cpu_binary = build / ("CpuCheck.cmd" if sys.platform == "win32" else "CpuCheck")
        (build / "ci-targets-Debug.json").write_text(
            json.dumps({"Window": str(cpu_binary)}), encoding="utf-8")
        for exit_code in (7, 0):
            text = f"@echo off\nexit /b {exit_code}\n" if sys.platform == "win32" else f"#!/bin/sh\nexit {exit_code}\n"
            cpu_binary.write_text(text, encoding="utf-8")
            if sys.platform != "win32":
                cpu_binary.chmod(0o700)
            try:
                verify_outputs(cpu_contract, build, "Debug", "vulkan")
            except ValueError:
                assert exit_code != 0, "Successful CPU process was rejected"
            else:
                assert exit_code == 0, "Failing CPU process was accepted"
    print(f"Support self-check: {len(accepted)} allowed changes, {len(cases)} invalid configurations and output checks passed")


def cmake_self_test():
    """Features share one example target; API and Null jobs select the right checks."""
    helper = Path(__file__).resolve().parents[2] / "cmake" / "ExampleSupport.cmake"
    support = {api: {"status": "supported", "checks": ["build"],
                     "shaders": [], "gpu_required": False} for api in APIS}
    example = {"kind": "example", "target": "Window", "directory": "examples/01_Window",
               "requires": [], "arguments": [], "rendering": False, "apis": support}
    feature = copy.deepcopy(example)
    feature.update(kind="feature", requires=["Window"])
    for api in APIS:
        feature["apis"][api]["checks"].append("cpu")
    ordinary = copy.deepcopy(example)
    ordinary.update(target="Ordinary", directory="examples/02_Ordinary")
    specific = copy.deepcopy(example)
    specific.update(target="VulkanOnly", directory="examples/03_VulkanOnly")
    specific["apis"]["vulkan"]["checks"].append("cpu")
    for api in ("d3d12", "metal"):
        specific["apis"][api].update(status="unsupported", checks=[])
    contract = {"version": 1,
                "items": {"Window": example, "CpuFeature": feature,
                          "Ordinary": ordinary, "VulkanOnly": specific}}
    validate_contract(contract)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / ".github").mkdir()
        (root / CONTRACT_PATH).write_text(json.dumps(contract), encoding="utf-8")
        for item in (example, ordinary, specific):
            directory = root / item["directory"]
            directory.mkdir(parents=True)
            target = item["target"]
            (directory / "CMakeLists.txt").write_text(
                f'add_executable({target} IMPORTED GLOBAL)\n'
                f'set_target_properties({target} PROPERTIES IMPORTED_LOCATION "${{CMAKE_BINARY_DIR}}/{target}.bin")\n',
                encoding="utf-8")
        (root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(CISupportFixture NONE)\n'
            f'include("{helper.as_posix()}")\n', encoding="utf-8")
        for api, enabled, expected in (
                ("null", "ON", {"Window"}),
                ("null", "OFF", {"Window", "Ordinary"}),
                ("vulkan", "ON", {"Window", "Ordinary", "VulkanOnly"}),
                ("d3d12", "ON", {"Window", "Ordinary"}),
                ("metal", "ON", {"Window", "Ordinary"})):
            build = root / ("build-" + api + "-" + enabled)
            result = subprocess.run(["cmake", "-S", str(root), "-B", str(build),
                                     "-DCMAKE_BUILD_TYPE=Debug", "-DDY_CI=" + enabled,
                                     "-DUSE_" + api.upper() + "=ON"],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace")
            if result.returncode:
                raise AssertionError(f"Example registration failed ({api}, CI={enabled}):\n"
                                     + result.stdout + result.stderr)
            inventory = read_json((build / "ci-targets-Debug.json").read_text(encoding="utf-8"))
            assert set(inventory) == expected, f"Example selection changed ({api}, CI={enabled})"
        for remaining, expected in (({"Window": example}, {"Window"}), ({}, set())):
            contract["items"] = remaining
            validate_contract(contract)
            (root / CONTRACT_PATH).write_text(json.dumps(contract), encoding="utf-8")
            result = subprocess.run(["cmake", "-S", str(root), "-B", str(build)],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace")
            assert result.returncode == 0, result.stdout + result.stderr
            inventory = read_json((build / "ci-targets-Debug.json").read_text(encoding="utf-8"))
            assert set(inventory) == expected, "Removed examples remain in the target inventory"
    print("CMake support self-check: 3 APIs, Null CI/local selection and partial/complete removal passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--self-test-cmake", action="store_true")
    parser.add_argument("--verify-build", type=Path)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--api", choices=(*APIS, "null"))
    args = parser.parse_args()
    try:
        if args.self_test_cmake:
            cmake_self_test()
        elif args.self_test:
            self_test()
        else:
            contract = policy()
            if args.verify_build:
                require(args.api is not None, "--verify-build requires --api")
                verify_outputs(contract, args.verify_build, args.config, args.api)
    except (ValueError, OSError, subprocess.SubprocessError, KeyError) as error:
        print(f"Support policy failed: {error}", file=sys.stderr)
        sys.exit(1)
