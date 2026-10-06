"""Archive complete pinned runtime source and the candidate's staged FPGA input."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def candidate_identity(path):
    """Bind a new source archive to an existing immutable per-core candidate."""
    raw = path.read_bytes()
    candidate = json.loads(raw)
    assert candidate["shared_Main_payload_included"] is False
    files = candidate["files"]
    cores = [name for name in files if re.fullmatch(r"_Other/TIC80_[0-9]{8}\.rbf", name)]
    assert len(cores) == 1, "Expected one TIC-80 core"
    core = cores[0]
    assert set(files) == {core, "games/TIC-80/TIC-80", "games/TIC-80/TIC-80-Studio",
                          "games/TIC-80/_handler.sh", "games/TIC-80/cacert.pem"}
    hashes = {}
    for name, item in files.items():
        digest = item["candidate_sha256"]
        assert re.fullmatch(r"[0-9a-f]{64}", digest), name
        assert hashlib.sha256((path.parent / "candidate" / name).read_bytes()).hexdigest() == digest, name
        hashes[name] = digest
    return hashes[core], {"manifest_sha256": hashlib.sha256(raw).hexdigest(),
                         "files": hashes,
                         "note": "Payload identity only; source packaging does not confer runtime qualification."}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--fpga-project", type=Path, required=True)
    p.add_argument("--candidate-manifest", type=Path,
                   help="Bind to a prepared per-core candidate; omit for the first preview")
    a = p.parse_args()
    assert not a.output.exists(), "Use a fresh output path"
    integration_commit = git(ROOT, "rev-parse", "HEAD").decode().strip()
    assert not git(ROOT, "status", "--porcelain").strip(), "Commit source changes before packaging"
    runtime = ROOT / "reference/tic80"
    runtime_pin = "4dba5bc2640d9cde650fb0b427c9be6aab598de9"
    assert git(runtime, "rev-parse", "HEAD").decode().strip() == runtime_pin
    expected_rbf = "fd673c65c94b32ab5700870340337bde8adbcf5f7f8addc94a3fe9dcde4963d2"
    candidate = None
    if a.candidate_manifest:
        expected_rbf, candidate = candidate_identity(a.candidate_manifest)
    assert hashlib.sha256((a.fpga_project / "output_files/TIC80.rbf").read_bytes()).hexdigest() == expected_rbf
    entries = {}
    for rel in git(ROOT, "ls-files", "-z").decode().split("\0"):
        if rel:
            source = ROOT / rel
            if source.is_file():
                entries[rel] = source
    for rel in git(runtime, "ls-files", "--recurse-submodules", "-z").decode().split("\0"):
        if rel:
            source = runtime / rel
            if source.is_file():
                entries["upstream/tic80/" + rel] = source
    # Include source inputs, never Quartus databases, reports or compiled IP.
    extensions = {".v", ".sv", ".vhd", ".vhdl", ".svh", ".vh", ".tcl", ".qip",
                  ".qpf", ".qsf", ".sdc", ".mif", ".hex", ".inc", ".f", ".bsf", ".qsys", ".ip"}
    excluded = {"db", "incremental_db", "output_files", "simulation", "tmp", ".git"}
    for directory, subdirs, filenames in os.walk(a.fpga_project):
        subdirs[:] = [name for name in subdirs if name not in excluded]
        for filename in filenames:
            source = Path(directory) / filename
            rel = source.relative_to(a.fpga_project)
            if source.suffix.lower() in extensions or source.name == "LICENSE":
                entries["fpga-project/" + rel.as_posix()] = source
    manifest = {"integration_commit": integration_commit,
                "runtime_commit": runtime_pin, "runtime_submodules": git(runtime, "submodule", "status", "--recursive").decode().splitlines(),
                "platform_commit": "72cb0405417d506c33e59ab51c1a374fc4db649e",
                "Frontier_commit": "a7c61e0a000d9dfd40235e638229e06a603534d5",
                "candidate_RBF_sha256": expected_rbf,
                "note": "Original source snapshots with notices. Uninitialized optional desktop-only submodules are not linked by this port.",
                "files": {name: hashlib.sha256(source.read_bytes()).hexdigest() for name, source in sorted(entries.items())}}
    if candidate is not None:
        manifest["per_core_candidate"] = candidate
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.output, "x", compression=zipfile.ZIP_DEFLATED) as z:
        for name, source in sorted(entries.items()):
            z.write(source, name)
        z.writestr("source-manifest.json", json.dumps(manifest, indent=2) + "\n")
    with zipfile.ZipFile(a.output) as z:
        assert z.testzip() is None
        for name, expected in manifest["files"].items():
            assert hashlib.sha256(z.read(name)).hexdigest() == expected, name
    assert git(ROOT, "rev-parse", "HEAD").decode().strip() == integration_commit, "Source revision changed during packaging"
    print(json.dumps({"source_archive": str(a.output.resolve()), "files": len(entries),
                      "sha256": hashlib.sha256(a.output.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
