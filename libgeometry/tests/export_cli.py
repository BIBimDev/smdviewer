from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def run(command, directory, diagnostic=None):
    result = subprocess.run(command, cwd=directory, capture_output=True, text=True, errors="replace")
    if diagnostic is None:
        passed = result.returncode == 0
    else:
        passed = result.returncode > 0 and diagnostic in result.stderr
    if not passed:
        raise AssertionError(
            f"{command!r} exited {result.returncode}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    if result.stdout:
        print(result.stdout, end="")
    if diagnostic is not None:
        print(result.stderr, end="")


def verify_export(directory, cli, fixture, qualifier, additions, reductions):
    input_file = directory / "input specimen.smd"
    shutil.copyfile(fixture, input_file)
    output_dir = directory / "exports"
    output_dir.mkdir()
    output = output_dir / "specimen export.json"
    run([cli, input_file.name, output.relative_to(directory)], directory)
    if set(directory.rglob("*")) != {input_file, output_dir, output} or not output.is_file():
        raise AssertionError("expected exactly one standalone JSON/BREP export")
    run([qualifier, fixture, additions, reductions, output], directory)


def verify_failures(directory, cli, fixture):
    output = directory / "new.json"
    existing = directory / "existing.json"
    sentinel = b"existing export must be preserved\n"
    existing.write_bytes(sentinel)
    malformed = directory / "malformed.smd"
    malformed.write_bytes(b'"DICAD ASC"\n')
    invalid_face = directory / "invalid face.smd"
    # Zero the top plane's coefficients. Its points still pass the parser's plane residual check.
    source = Path(fixture).read_bytes().replace(b"\r\n", b"\n")
    top_plane = b"\n6\n0\n0\n1\n-6000\n"
    if source.count(top_plane) != 1:
        raise AssertionError("plain fixture no longer matches the invalid face setup")
    invalid_face.write_bytes(source.replace(top_plane, b"\n6\n0\n0\n0\n0\n"))
    initial_files = {existing, malformed, invalid_face}

    cases = [
        ([], "Usage:"),
        ([fixture], "Usage:"),
        ([fixture, output, "extra"], "Usage:"),
        ([directory / "missing.smd", output], "Cannot open input file"),
        ([directory, output], "expected a regular file"),
        ([malformed, output], "#10125 main geometry block was not found."),
        ([invalid_face, output], "SourceFace[0]: degenerate source plane normal"),
        ([fixture, existing], "GEO-EXP-01 [output]"),
        ([fixture, directory / "missing" / "new.json"], "GEO-EXP-01 [output]"),
    ]
    for arguments, diagnostic in cases:
        run([cli, *arguments], directory, diagnostic)
        if set(directory.rglob("*")) != initial_files or existing.read_bytes() != sentinel:
            raise AssertionError("failed command created an artifact or changed the existing destination")


if __name__ == "__main__":
    cli, fixture, *qualification = sys.argv[1:]
    with TemporaryDirectory(prefix="smd-geo-export-") as name:
        directory = Path(name)
        if qualification:
            verify_export(directory, cli, fixture, *qualification)
        else:
            verify_failures(directory, cli, fixture)
