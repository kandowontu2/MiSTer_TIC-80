"""Build an offline Frontier-style install ZIP, never including MiSTer Main.

An installer package is separate from an earlier matched prototype package.
Repackaging does not transfer its Main-dependent hardware qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

REPO = Path(__file__).resolve().parents[1]
RUNTIME_PATHS = (
    "games/TIC-80/TIC-80", "games/TIC-80/TIC-80-Studio",
    "games/TIC-80/_handler.sh", "games/TIC-80/cacert.pem",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(package, output):
    manifest = json.loads((package / "manifest.json").read_text())
    cores = [p for p in manifest["files"] if p.startswith("_Other/TIC80_") and p.endswith(".rbf")]
    if len(cores) != 1:
        raise ValueError("Expected one TIC-80 core")
    core = cores[0]
    # Never accept a path supplied by the input manifest outside this allowlist.
    import re
    if not re.fullmatch(r"_Other/TIC80_[0-9]{8}\.rbf", core):
        raise ValueError("Invalid core name")
    paths = (core, *RUNTIME_PATHS)
    for path in paths:
        source = package / "sd-card" / path
        if digest(source) != manifest["files"][path]["sha256"]:
            raise ValueError(f"Input checksum failed: {path}")
    qualification = None
    if "qualification_sha256" in manifest:
        qualification = package / "qualification.json"
        if digest(qualification) != manifest["qualification_sha256"]:
            raise ValueError("Qualification checksum failed")
        record = json.loads(qualification.read_text(encoding="utf-8"))
        if record["payload_sha256"] != {path: manifest["files"][path]["sha256"] for path in paths}:
            raise ValueError("Qualification describes different payloads")
    archive = Path(str(output) + ".zip")
    if output.exists() or archive.exists():
        raise FileExistsError("Use a fresh output path; existing packages are immutable")
    output.mkdir(parents=True)
    scripts = output / "Scripts"
    bundle = scripts / "TIC80-install"
    bundle.mkdir(parents=True)
    shutil.copyfile(REPO / "Scripts/Install_TIC80.sh", scripts / "Install_TIC80.sh")
    for path in paths:
        dest = bundle / "payload" / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(package / "sd-card" / path, dest)
    (bundle / "files.sha256").write_text("".join(
        f"{digest(bundle / 'payload' / path)}  {path}\n" for path in paths), newline="\n")
    shutil.copytree(REPO / "vendor/MiSTer_Frontier", bundle / "frontier")
    (bundle / "frontier.sha256").write_text(
        f"{digest(bundle / 'frontier/Master_Daemon.sh')}  Master_Daemon.sh\n", newline="\n")
    # Public packages get the complete reviewed notice collection, independent
    # of an older prototype's smaller license directory.
    license_root = REPO / "licenses"
    shutil.copytree(license_root if license_root.exists() else package / "licenses", bundle / "licenses")
    documentation = output / "docs/TIC-80"
    documentation.mkdir(parents=True)
    for source in [REPO / name for name in ("README.md", "CREDITS.md", "NOTICE", "LICENSE", "DEVELOPMENT.md")] + [
            REPO / "docs" / name for name in ("install.md", "usage.md", "building.md", "stock-main.md", "release-status.md", "validation.md")]:
        content = source.read_text(encoding="utf-8")
        content = content.replace('(docs/', '(').replace('src="docs/images/', 'src="images/')
        for name in ("README.md", "DEVELOPMENT.md", "CREDITS.md", "NOTICE", "LICENSE"):
            content = content.replace('(../' + name + ')', '(' + name + ')')
        content = content.replace('(licenses/)', '(../../Scripts/TIC80-install/licenses/)')
        (documentation / source.name).write_text(content, encoding="utf-8", newline="\n")
    shutil.copytree(REPO / "docs/images", documentation / "images")
    if qualification is not None:
        shutil.copyfile(qualification, documentation / "qualification.json")
    status = {
        "kind": "development-installer", "shared_main_included": False,
        "shared_main_replaced": False, "stock_main_hardware_qualified": False,
        "release_accepted": False, "input_manifest_sha256": digest(package / "manifest.json"),
        "bounded_runtime_evidence_included": qualification is not None,
        "note": "Installer checks are separate from stock-Main runtime qualification, which remains pending.",
        "files": {p.relative_to(output).as_posix(): digest(p)
                  for p in sorted(output.rglob("*")) if p.is_file()},
    }
    (output / "manifest.json").write_text(json.dumps(status, indent=2) + "\n", newline="\n")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as z:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(output).as_posix())
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert "MiSTer" not in z.namelist()
        for path, expected in status["files"].items():
            assert hashlib.sha256(z.read(path)).hexdigest() == expected
    print(json.dumps({"archive": str(archive.resolve()), "sha256": digest(archive),
                      "files": len(status["files"]) + 1}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    build(args.package, args.output)
