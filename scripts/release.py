"""Version, changelog and release asset checks; uses only the Python standard library."""

import argparse
from datetime import date
import hashlib
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
PROJECT = re.compile(r"(\bproject\(\s*HLaunch\s+VERSION\s+)([0-9]+\.[0-9]+\.[0-9]+)")
SECTION = re.compile(r"^## \[([^\]]+)\][^\n]*\n", re.MULTILINE)
MACHINES = {"x64": 0x8664, "x86": 0x014C, "arm64": 0xAA64}


def checked_version(value):
    if not re.fullmatch(VERSION_PATTERN, value):
        raise ValueError(f"Invalid version: {value!r}; expected X.Y.Z without leading zeroes")
    if any(int(part) > 65535 for part in value.split(".")):
        raise ValueError("Version components must fit Windows VERSIONINFO (0..65535)")
    return value


def project_version(root):
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    matches = list(PROJECT.finditer(text))
    if len(matches) != 1:
        raise ValueError("Expected exactly one project(HLaunch VERSION X.Y.Z)")
    return checked_version(matches[0][2])


def changelog_section(text, name):
    sections = list(SECTION.finditer(text))
    matches = [(index, match) for index, match in enumerate(sections) if match[1] == name]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one changelog section [{name}]")
    index, match = matches[0]
    end = sections[index + 1].start() if index + 1 < len(sections) else len(text)
    body = text[match.end():end].strip()
    if not re.search(r"^- \S", body, re.MULTILINE):
        raise ValueError(f"Changelog section [{name}] needs release notes")
    return match.start(), end, body


def validate(root, tag):
    if not re.fullmatch("v" + VERSION_PATTERN, tag):
        raise ValueError("Release tag must be exactly vX.Y.Z")
    version = checked_version(tag[1:])
    if version != project_version(root):
        raise ValueError(f"Tag {tag} does not match the CMake project version")
    text = (root / "CHANGELOG.md").read_text(encoding="utf-8-sig")
    return version, changelog_section(text, version)[2] + "\n"


def prepare(root, today=None):
    version = project_version(root)
    major, minor, patch = map(int, version.split("."))
    next_version = checked_version(f"{major}.{minor}.{patch + 1}")
    changelog_path = root / "CHANGELOG.md"
    changelog = changelog_path.read_text(encoding="utf-8-sig")
    if next_version in [match[1] for match in SECTION.finditer(changelog)]:
        raise ValueError(f"Changelog already contains [{next_version}]")
    start, end, notes = changelog_section(changelog, "Unreleased")
    stamp = today or date.today().isoformat()
    replacement = f"## [Unreleased]\n\n## [{next_version}] - {stamp}\n\n{notes}\n\n"
    updated_changelog = changelog[:start] + replacement + changelog[end:]
    cmake_path = root / "CMakeLists.txt"
    cmake = cmake_path.read_text(encoding="utf-8-sig")
    updated_cmake = PROJECT.sub(lambda match: match[1] + next_version, cmake, count=1)
    # Complete validation before touching either tracked file. No Git mutations here.
    changelog_section(updated_changelog, next_version)
    cmake_path.write_text(updated_cmake, encoding="utf-8", newline="\n")
    changelog_path.write_text(updated_changelog, encoding="utf-8", newline="\n")
    return next_version


def pe_machine(path):
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise ValueError(f"Not a Windows executable: {path.name}")
        stream.seek(0x3C)
        offset = struct.unpack("<I", stream.read(4))[0]
        stream.seek(offset)
        if stream.read(4) != b"PE\0\0":
            raise ValueError(f"Invalid PE header: {path.name}")
        return struct.unpack("<H", stream.read(2))[0]


def checksums(directory, version):
    expected = {f"HLaunch-{version}-{arch}.exe" for arch in MACHINES}
    actual = {path.name for path in directory.glob("*.exe")}
    if actual != expected:
        raise ValueError(f"Expected all three executables; missing={expected - actual}, extra={actual - expected}")
    lines = []
    for arch, machine in MACHINES.items():
        path = directory / f"HLaunch-{version}-{arch}.exe"
        if pe_machine(path) != machine:
            raise ValueError(f"PE architecture does not match the asset name: {path.name}")
        lines.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n")
    (directory / "SHA256SUMS.txt").write_text("".join(lines), encoding="ascii", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("prepare", help="Increment the patch version and archive Unreleased notes")
    check = commands.add_parser("validate", help="Check tag, CMake version and release notes")
    check.add_argument("--tag", required=True)
    check.add_argument("--notes-output", type=Path)
    check.add_argument("--github-output", type=Path)
    assets = commands.add_parser("checksums", help="Verify the three PE architectures and write SHA256SUMS.txt")
    assets.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            print(f"Prepared v{prepare(ROOT)}; review and commit before tagging.")
        elif args.command == "validate":
            version, notes = validate(ROOT, args.tag)
            if args.notes_output:
                args.notes_output.parent.mkdir(parents=True, exist_ok=True)
                args.notes_output.write_text(notes, encoding="utf-8", newline="\n")
            if args.github_output:
                with args.github_output.open("a", encoding="utf-8") as stream:
                    stream.write(f"version={version}\n")
            print(f"Validated v{version}")
        else:
            checksums(args.directory, project_version(ROOT))
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"Release check failed: {error}\n")


if __name__ == "__main__":
    main()
