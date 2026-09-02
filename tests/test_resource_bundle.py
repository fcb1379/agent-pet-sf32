import argparse
import hashlib
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = REPOSITORY_ROOT / "work/watch_bt_audio_template/tools/resource_bundle.py"
SPEC = importlib.util.spec_from_file_location("resource_bundle", TOOL_PATH)
RESOURCE_BUNDLE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RESOURCE_BUNDLE)


def make_tree(root: Path, version: str, files: dict[str, bytes]) -> Path:
    records = []
    for relative, data in sorted(files.items()):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        records.append({
            "path": relative,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    manifest_path = root / "manifest.json"
    manifest_path.write_text(json.dumps({
        "format": 1,
        "version": version,
        "files": records,
    }), encoding="utf-8")
    return manifest_path


class ResourceBundleTest(unittest.TestCase):
    def test_diff_contains_only_changed_file_and_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base = make_tree(root / "base", "1", {
                "resource/a.bin": b"old-a",
                "resource/b.bin": b"same-b",
            })
            target = make_tree(root / "target", "2", {
                "resource/a.bin": b"new-a",
                "resource/b.bin": b"same-b",
            })
            outputs = [root / "first.apres", root / "second.apres"]
            for output in outputs:
                arguments = argparse.Namespace(
                    base=base,
                    target=target,
                    output=output,
                    max_bytes=RESOURCE_BUNDLE.MAX_DIFF_BYTES,
                )
                self.assertEqual(0, RESOURCE_BUNDLE.build_diff(arguments))
            self.assertEqual(outputs[0].read_bytes(), outputs[1].read_bytes())
            with zipfile.ZipFile(outputs[0]) as archive:
                self.assertEqual(["manifest.json", "resource/a.bin"], archive.namelist())
                manifest = json.loads(archive.read("manifest.json"))
                self.assertEqual(9, manifest["payloadBytes"])
                self.assertEqual("1", manifest["baseVersion"])
                self.assertEqual("2", manifest["targetVersion"])

    def test_diff_rejects_deleted_resources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base = make_tree(root / "base", "1", {
                "resource/a.bin": b"a",
                "resource/deleted.bin": b"delete",
            })
            target = make_tree(root / "target", "2", {
                "resource/a.bin": b"changed",
            })
            arguments = argparse.Namespace(
                base=base,
                target=target,
                output=root / "invalid.apres",
                max_bytes=RESOURCE_BUNDLE.MAX_DIFF_BYTES,
            )
            with self.assertRaisesRegex(ValueError, "deletion is not supported"):
                RESOURCE_BUNDLE.build_diff(arguments)

    def test_manifest_excludes_transaction_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "resource").mkdir()
            (root / "resource/a.bin").write_bytes(b"a")
            (root / "resource.version").write_text("1\n", encoding="ascii")
            manifest = RESOURCE_BUNDLE.manifest_for(root, "1")
            self.assertEqual(["resource/a.bin"],
                             [record["path"] for record in manifest["files"]])


if __name__ == "__main__":
    unittest.main()
