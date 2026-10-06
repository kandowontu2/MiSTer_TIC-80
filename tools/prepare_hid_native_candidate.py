"""Freeze local native-test payloads and a published-release rollback copy.

Does not access MiSTer, run installation, publish a release or claim native
qualification. The prepared installer destinations exclude shared Main.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RELEASE = "v0.1.0-dev.20261005"
RELEASE_SHA = "4682b6309ae2da74cd2357986d0abf17453549fe5e938d70342172be30263327"
MAIN_SHA = "9f6e5a237c36be6404ab4823d804821491db4bf125827f84aca2a1ca31f0a8a6"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe-build", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir()  # Fresh immutable evidence; never replace a prior plan.
    archive = ROOT / "releases" / f"TIC80-Frontier-{RELEASE}.zip"
    assert sha(archive) == RELEASE_SHA
    frontends = ROOT / "build/hid-frontends-v1-20261005"
    frontend_receipt = read(frontends / "result.json")
    assert frontend_receipt["passed"] and frontend_receipt["backend_and_input_ABI_match"]
    for name, expected in frontend_receipt["source_sha256"].items():
        assert sha(ROOT / name) == expected, name
    probe = read(args.probe_build / "result.json")
    assert probe["passed"]
    binary = args.probe_build / "arm/tic80-hid-pan-probe"
    assert sha(binary) == probe["ARM_probe_sha256"]
    for name in ("tools/hid_pan_probe.c", "src/hid_wheel.c", "src/hid_pan.c",
                 "include/tic80_mister/hid_wheel.h", "include/tic80_mister/hid_pan.h"):
        assert sha(ROOT / name) == probe["source_sha256"][name], name
    fit_dir = ROOT / "build/fpga-linux-wheel-seed22-20261005"
    fit = read(fit_dir / "fpga-build.json")
    assert read(fit_dir / "original-terminal.json")["exit_code"] == 0
    rbf = fit_dir / "fpga/output_files/TIC80.rbf"
    assert sha(rbf) == fit["rbf_sha256"]
    for name, expected in fit["post_fit_audits"].items():
        assert sha(fit_dir / name) == expected, name
    with zipfile.ZipFile(archive) as published:
        prefix = "Scripts/TIC80-install/"
        expected = {line.split()[1]: line.split()[0] for line in
                    published.read(prefix + "files.sha256").decode().splitlines()}
        assert len(expected) == 5 and "MiSTer" not in expected
        files = {}
        replacements = {
            "_Other/TIC80_20261003.rbf": rbf,
            "games/TIC-80/TIC-80": frontends / "tic80-live",
            "games/TIC-80/TIC-80-Studio": frontends / "tic80-studio-live",
        }
        for name, original_sha in expected.items():
            original = published.read(prefix + "payload/" + name)
            assert hashlib.sha256(original).hexdigest() == original_sha
            rollback = args.output / "rollback" / name
            rollback.parent.mkdir(parents=True, exist_ok=True)
            rollback.write_bytes(original)
            candidate = args.output / "candidate" / name
            candidate.parent.mkdir(parents=True, exist_ok=True)
            if name in replacements:
                shutil.copyfile(replacements[name], candidate)
            else:
                candidate.write_bytes(original)
            files[name] = {"candidate_sha256": sha(candidate),
                           "rollback_sha256": sha(rollback),
                           "candidate_bytes": candidate.stat().st_size,
                           "rollback_bytes": rollback.stat().st_size}
    tool = args.output / "tools/hid-pan-probe"
    tool.parent.mkdir()
    shutil.copyfile(binary, tool)
    manifest = {
        "kind": "local-native-test-candidate",
        "native_qualified": False,
        "published_release_changed": False,
        "hardware_accessed": False,
        "shared_Main_payload_included": False,
        "expected_stock_Main_sha256": MAIN_SHA,
        "rollback_release": RELEASE,
        "rollback_archive_sha256": RELEASE_SHA,
        "files": files,
        "probe_sha256": sha(tool),
        "local_frontend_receipt": str(frontends / "result.json"),
        "local_probe_receipt": str(args.probe_build / "result.json"),
        "local_fpga_receipt": str(fit_dir / "fpga-build.json"),
        "required_native_gates": [
            "UHID/HIDraw reports while actual stock Main owns evdev grab",
            "End-to-end player and Studio mouse API, real OSD epoch boundaries",
            "Stock-Main startup/MGL/reset/reload/source-save",
            "Sustained audio/HDMI and bounded helper/resource lifecycle",
        ],
        "test_sequence": [
            "Obtain a free MiSTer test window before changing the selected core",
            "Verify stock Main, installed published payloads, frontend selection and current project",
            "Run reader-only probe first on the published core; no installed payload replacement",
            "Stage candidate and exact backups privately, then apply only at MENU",
            "Run full candidate tests with original launch/journal handles",
            "At MENU restore matching backups, verify stock Main and every installed payload",
        ],
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"prepared": True, "output": str(args.output), "files": len(files),
                      "native_qualified": False, "hardware_accessed": False}))


if __name__ == "__main__":
    main()
