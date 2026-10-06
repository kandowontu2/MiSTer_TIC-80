"""Archive complete pinned runtime source and the candidate's staged FPGA input."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--fpga-project", type=Path, required=True)
    a = p.parse_args()
    assert not a.output.exists(), "Use a fresh output path"
    runtime = ROOT / "reference/tic80"
    runtime_pin = "4dba5bc2640d9cde650fb0b427c9be6aab598de9"
    assert git(runtime, "rev-parse", "HEAD").decode().strip() == runtime_pin
    expected_rbf = "fd673c65c94b32ab5700870340337bde8adbcf5f7f8addc94a3fe9dcde4963d2"
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
    for source in a.fpga_project.rglob("*"):
        rel = source.relative_to(a.fpga_project)
        if source.is_file() and not excluded.intersection(rel.parts) and (source.suffix.lower() in extensions or source.name == "LICENSE"):
            entries["fpga-project/" + rel.as_posix()] = source
    manifest = {"integration_commit": git(ROOT, "rev-parse", "HEAD").decode().strip(),
                "runtime_commit": runtime_pin, "runtime_submodules": git(runtime, "submodule", "status", "--recursive").decode().splitlines(),
                "platform_commit": "72cb0405417d506c33e59ab51c1a374fc4db649e",
                "Frontier_commit": "a7c61e0a000d9dfd40235e638229e06a603534d5",
                "candidate_RBF_sha256": expected_rbf,
                "note": "Original source snapshots with notices. Uninitialized optional desktop-only submodules are not linked by this port.",
                "files": {name: hashlib.sha256(source.read_bytes()).hexdigest() for name, source in sorted(entries.items())}}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.output, "x", compression=zipfile.ZIP_DEFLATED) as z:
        for name, source in sorted(entries.items()):
            z.write(source, name)
        z.writestr("source-manifest.json", json.dumps(manifest, indent=2) + "\n")
    with zipfile.ZipFile(a.output) as z:
        assert z.testzip() is None
        for name, expected in manifest["files"].items():
            assert hashlib.sha256(z.read(name)).hexdigest() == expected, name
    print(json.dumps({"source_archive": str(a.output.resolve()), "files": len(entries),
                      "sha256": hashlib.sha256(a.output.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
