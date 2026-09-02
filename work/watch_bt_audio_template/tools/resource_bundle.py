#!/usr/bin/env python3
"""Serialize SiFli LVGL resources and create file-level differential archives."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath


MANIFEST_FORMAT = 1
# Keep enough free FAT space for metadata, the transaction journal and the
# previous files retained during atomic replacement. A factory font change is
# intentionally excluded from BLE differential updates because the 1 MiB font
# cannot be staged safely in the current 4 MiB resource partition.
MAX_DIFF_BYTES = 512 * 1024
MAX_DIFF_FILES = 16
RESOURCE_VERSION_FILE = "resource.version"
COLOR_FORMATS = {
    "LV_IMG_CF_UNKNOWN": 0, "LV_IMG_CF_RAW": 1,
    "LV_IMG_CF_RAW_ALPHA": 2, "LV_IMG_CF_RAW_CHROMA_KEYED": 3,
    "LV_IMG_CF_TRUE_COLOR": 4, "LV_IMG_CF_TRUE_COLOR_ALPHA": 5,
    "LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED": 6, "LV_IMG_CF_INDEXED_1BIT": 7,
    "LV_IMG_CF_INDEXED_2BIT": 8, "LV_IMG_CF_INDEXED_4BIT": 9,
    "LV_IMG_CF_INDEXED_8BIT": 10, "LV_IMG_CF_ALPHA_1BIT": 11,
    "LV_IMG_CF_ALPHA_2BIT": 12, "LV_IMG_CF_ALPHA_4BIT": 13,
    "LV_IMG_CF_ALPHA_8BIT": 14, "LV_IMG_CF_RGB888": 15,
    "LV_IMG_CF_RGBA8888": 16, "LV_IMG_CF_RGBX8888": 17,
    "LV_IMG_CF_RGB565": 18, "LV_IMG_CF_RGBA5658": 19,
    "LV_IMG_CF_RGB565A8": 20,
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\r\n]*", "", text)


def referenced_images(source_root: Path) -> set[str]:
    names: set[str] = set()
    macro_values: dict[str, str] = {}
    for path in source_root.rglob("*"):
        if path.suffix.lower() not in {".c", ".h"}:
            continue
        text = strip_c_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for macro, value in re.findall(
                r"^\s*#\s*define\s+([A-Z][A-Z0-9_]*)\s+([a-z][A-Za-z0-9_]*)\s*$",
                text, flags=re.MULTILINE):
            macro_values[macro] = value
        names.update(re.findall(r"\bLV_IMG_DECLARE\s*\(\s*([A-Za-z_]\w*)\s*\)", text))
        names.update(re.findall(r"\bLV_EXT_IMG_GET\s*\(\s*([A-Za-z_]\w*)\s*\)", text))
    return {macro_values.get(name, name) for name in names
            if name in macro_values or not name.isupper()}


def parse_generated_image(path: Path, name: str) -> bytes:
    text = path.read_text(encoding="utf-8", errors="strict")
    array_match = re.search(
        rf"\b(?:const\s+)?(?:uint8_t|unsigned\s+char)\s+{re.escape(name)}_map\s*"
        rf"\[\s*\].*?=\s*\{{(.*?)\}}\s*;", text, flags=re.DOTALL)
    descriptor_match = re.search(
        rf"\blv_img_dsc_t\s+{re.escape(name)}\b.*?=\s*\{{(.*?)\}}\s*;",
        text, flags=re.DOTALL)
    if array_match is None or descriptor_match is None:
        raise ValueError(f"cannot parse generated image {name}: {path}")
    body = descriptor_match.group(1)
    cf_match = re.search(r"\.header\.cf\s*=\s*(LV_IMG_CF_[A-Z0-9_]+)", body)
    width_match = re.search(r"\.header\.w\s*=\s*(\d+)", body)
    height_match = re.search(r"\.header\.h\s*=\s*(\d+)", body)
    size_match = re.search(r"\.data_size\s*=\s*(\d+)", body)
    if None in (cf_match, width_match, height_match, size_match):
        raise ValueError(f"incomplete image descriptor {name}: {path}")
    cf_name = cf_match.group(1)
    if cf_name not in COLOR_FORMATS:
        raise ValueError(f"unsupported LVGL color format {cf_name}: {name}")
    width = int(width_match.group(1))
    height = int(height_match.group(1))
    declared_size = int(size_match.group(1))
    if width > 0x7FF or height > 0x7FF:
        raise ValueError(f"image dimensions exceed LVGL header: {name}")
    data = bytes(int(value, 16) for value in re.findall(
        r"\b0x([0-9A-Fa-f]{1,2})\b", array_match.group(1)))
    if len(data) != declared_size:
        raise ValueError(f"byte count mismatch for {name}: {len(data)} != {declared_size}")
    header = COLOR_FORMATS[cf_name] | (width << 10) | (height << 21)
    return struct.pack("<I", header) + data


def manifest_for(root: Path, version: str) -> dict:
    records = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        if path.name in {"manifest.json", RESOURCE_VERSION_FILE}:
            continue
        records.append({"path": path.relative_to(root).as_posix(),
                        "size": path.stat().st_size, "sha256": sha256_file(path)})
    return {"format": MANIFEST_FORMAT, "version": version, "files": records}


def write_manifest(path: Path, manifest: dict) -> None:
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def generate_missing_images(args: argparse.Namespace, names: set[str]) -> dict[str, Path]:
    source_root = args.image_sources.resolve()
    executable = args.ezip.resolve()
    source_files: dict[str, Path] = {}
    for path in source_root.rglob("*"):
        if path.suffix.lower() not in {".png", ".gif"}:
            continue
        if path.stem in source_files:
            raise ValueError(f"duplicate image source name: {path.stem}")
        source_files[path.stem] = path
    missing_sources = sorted(names - source_files.keys())
    if missing_sources:
        raise ValueError("image source files missing: " + ", ".join(missing_sources))
    temporary = tempfile.TemporaryDirectory(prefix="agent_pet_resources_")
    args.generated_temporary = temporary
    output_root = Path(temporary.name)
    generated: dict[str, Path] = {}
    color_flag = "-rgb565" if args.color_depth == 16 else "-rgb888"
    for name in sorted(names):
        source = source_files[name]
        category = source.parent.name
        flags = [color_flag, "-section", "ROM3_IMG"]
        if category == "no_ezip":
            flags.extend(["-cfile", "1", "-dpt", "1"])
        elif category == "ezip":
            flags.extend(["-cfile", "2", "-dpt", "1"])
        elif category == "large_ezip":
            flags.extend(["-cfile", "2", "-dpt", "4", "-ers", str(args.ers)])
        else:
            raise ValueError(f"unsupported image resource category: {source}")
        if source.suffix.lower() == ".gif":
            gif_input = output_root / source.name
            shutil.copy2(source, gif_input)
            command = [str(executable), "-gif", str(gif_input), *flags]
        else:
            command = [str(executable), "-convert", str(source), *flags,
                       "-outdir", str(output_root)]
        subprocess.run(command, cwd=output_root, check=True,
                       stdout=subprocess.DEVNULL)
        generated_path = output_root / f"{name}.c"
        if not generated_path.is_file():
            raise ValueError(f"image converter did not create {generated_path.name}")
        generated[name] = generated_path
    return generated


def build_resources(args: argparse.Namespace) -> int:
    output_root = args.output.resolve()
    image_output = output_root / "resource"
    font_output = output_root / "font"
    image_output.mkdir(parents=True, exist_ok=True)
    font_output.mkdir(parents=True, exist_ok=True)
    required = referenced_images(args.sources.resolve())
    generated = {}
    if args.generated is not None:
        generated_root = args.generated.resolve()
        generated = {path.stem: path for path in generated_root.rglob("*.c")}
    missing = sorted(required - generated.keys())
    if missing and (args.image_sources is not None) and (args.ezip is not None):
        generated.update(generate_missing_images(args, set(missing)))
        missing = sorted(required - generated.keys())
    if missing:
        raise ValueError("generated images missing: " + ", ".join(missing))
    expected_files: set[Path] = set()
    for name in sorted(required):
        destination = image_output / f"{name}.bin"
        destination.write_bytes(parse_generated_image(generated[name], name))
        expected_files.add(destination.resolve())
    for stale in image_output.glob("*.bin"):
        if stale.resolve() not in expected_files:
            stale.unlink()
    shutil.copy2(args.font.resolve(), font_output / args.font.name)
    (output_root / RESOURCE_VERSION_FILE).write_text(args.version + "\n", encoding="ascii")
    manifest = manifest_for(output_root, args.version)
    write_manifest(output_root / "manifest.json", manifest)
    total = sum(record["size"] for record in manifest["files"])
    print(f"resources={len(required)} files={len(manifest['files'])} bytes={total}")
    return 0


def load_manifest(path: Path) -> dict:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("format") != MANIFEST_FORMAT or not isinstance(manifest.get("files"), list):
        raise ValueError(f"unsupported resource manifest: {path}")
    return manifest


def validate_relative_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or not path.parts \
            or path.parts[0] not in {"resource", "font"}:
        raise ValueError(f"unsafe resource path: {value}")
    return path


def deterministic_zip_write(archive: zipfile.ZipFile, name: str, data: bytes) -> None:
    info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


def build_diff(args: argparse.Namespace) -> int:
    base = load_manifest(args.base.resolve())
    target_path = args.target.resolve()
    target = load_manifest(target_path)
    target_root = target_path.parent
    base_files = {item["path"]: item for item in base["files"]}
    target_files = {item["path"]: item for item in target["files"]}
    changed = [item for path, item in sorted(target_files.items())
               if base_files.get(path, {}).get("sha256") != item["sha256"]]
    deleted = sorted(set(base_files) - set(target_files))
    version_pattern = re.compile(r"^[A-Za-z0-9_.-]{1,15}$")
    if not version_pattern.fullmatch(str(base.get("version", ""))) \
            or not version_pattern.fullmatch(str(target.get("version", ""))) \
            or base["version"] == target["version"]:
        raise ValueError("base and target must have different valid versions")
    if not changed:
        raise ValueError("target contains no changed resource files")
    if len(changed) > MAX_DIFF_FILES:
        raise ValueError(f"changed file count {len(changed)} exceeds limit {MAX_DIFF_FILES}")
    if deleted:
        raise ValueError("resource deletion is not supported: " + ", ".join(deleted))
    for item in changed:
        relative = validate_relative_path(item["path"])
        path = target_root / Path(*relative.parts)
        if path.stat().st_size != item["size"] or sha256_file(path) != item["sha256"]:
            raise ValueError(f"target resource does not match manifest: {item['path']}")
    payload_bytes = sum(item["size"] + 4 for item in changed)
    if payload_bytes > args.max_bytes:
        raise ValueError(f"differential payload {payload_bytes} exceeds limit {args.max_bytes}")
    package_manifest = {
        "format": MANIFEST_FORMAT, "baseVersion": base["version"],
        "targetVersion": target["version"], "payloadBytes": payload_bytes,
        "files": changed, "deleted": deleted,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w") as archive:
        deterministic_zip_write(archive, "manifest.json",
                                (json.dumps(package_manifest, ensure_ascii=False, indent=2)
                                 + "\n").encode("utf-8"))
        for item in changed:
            relative = validate_relative_path(item["path"])
            deterministic_zip_write(archive, item["path"],
                                    (target_root / Path(*relative.parts)).read_bytes())
    print(f"changed={len(changed)} deleted={len(deleted)} transfer_bytes={payload_bytes} "
          f"archive_bytes={args.output.stat().st_size}")
    return 0


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="serialize generated C images")
    build.add_argument("--generated", type=Path,
                       help="existing SiFli-generated image C directory")
    build.add_argument("--image-sources", type=Path,
                       help="image source root, used to generate missing C files")
    build.add_argument("--ezip", type=Path,
                       help="SiFli png2ezip executable for a clean resource build")
    build.add_argument("--color-depth", type=int, choices=(16, 24), default=16)
    build.add_argument("--ers", type=int, default=128,
                       help="large image decoder row size")
    build.add_argument("--sources", type=Path, required=True)
    build.add_argument("--font", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--version", required=True)
    build.set_defaults(handler=build_resources)
    diff = commands.add_parser("diff", help="package changed resource files")
    diff.add_argument("--base", type=Path, required=True)
    diff.add_argument("--target", type=Path, required=True)
    diff.add_argument("--output", type=Path, required=True)
    diff.add_argument("--max-bytes", type=lambda value: int(value, 0), default=MAX_DIFF_BYTES)
    diff.set_defaults(handler=build_diff)
    return parser


def main() -> int:
    args = create_parser().parse_args()
    try:
        return args.handler(args)
    except (OSError, ValueError, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        print(f"resource_bundle: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
