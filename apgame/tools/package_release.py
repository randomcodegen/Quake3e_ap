"""Package existing Release builds; never read a player's game directory."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


def archive(path, files):
    # Exclusive creation keeps earlier release artifacts intact.
    with zipfile.ZipFile(path, "x", zipfile.ZIP_DEFLATED) as output:
        for name, source in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, source.read_bytes())
    with zipfile.ZipFile(path) as result:
        assert result.testzip() is None
        assert set(result.namelist()) == set(files)
        for name, source in files.items():
            assert result.read(name) == source.read_bytes(), name


def tracked(repo, *pathspecs):
    output = subprocess.check_output([
        "git", "-c", f"safe.directory={repo.as_posix()}", "-C", str(repo),
        "ls-files", "-z", "--", *(pathspecs or (".",)),
    ])
    return [Path(name.decode()) for name in output.split(b"\0") if name]


def main():
    engine = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archipelago", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    world = args.archipelago / "worlds/quake3"
    cache = dict(line.split("=", 1) for line in
                 (engine / "build-ap-reuse/CMakeCache.txt").read_text().splitlines()
                 if "=" in line and not line.startswith(("//", "#")))
    apcc = Path(cache["APCC_ROOT:PATH"])
    share = Path(cache["VCPKG_INSTALLED_DIR:PATH"]) / "x64-windows/share"
    binaries = engine / "build-ap-reuse/Release"
    modules = engine / "apgame/build-reuse/Release"
    names = (
        "quake3e.x64.exe", "quake3e_vulkan_x86_64.dll", "quake3e_opengl_x86_64.dll",
        "glib-2.0-0.dll", "iconv-2.dll", "intl-8.dll", "jansson.dll",
        "libcrypto-3-x64.dll", "libssl-3-x64.dll", "pcre2-8.dll", "uv.dll",
        "websockets.dll", "z.dll",
    )
    client = {name: binaries / name for name in names}
    client["q3ap/qagamex86_64.dll"] = modules / "qagamex86_64.dll"
    for mod in ("q3ap", "cpma-ap"):
        client[f"{mod}/uix86_64.dll"] = modules / "uix86_64.dll"
        assets = engine / "apgame/assets"
        for source in assets.rglob("*"):
            if source.is_file():
                client[f"{mod}/{source.relative_to(assets).as_posix()}"] = source
    for name in ("launch-client.ps1", "launch-cpma-ap.ps1", "q3ap-launch.cmd", "cpma-ap-launch.cmd"):
        client[name] = engine / "apgame/release" / name
    client["README.md"] = engine / "README.md"
    client["docs/respawn_timers.md"] = engine / "docs/respawn_timers.md"
    for source in (world / "docs").glob("*.md"):
        client[f"docs/{source.name}"] = source
    client["licenses/Quake3e-GPL-2.0.txt"] = engine / "COPYING.txt"
    client["licenses/q3ap-upstream.md"] = engine / "apgame/UPSTREAM.md"
    client["licenses/APCc-README.md"] = apcc / "README.md"
    client["licenses/APCc-LGPL-2.1.txt"] = apcc / "LICENSE"
    for package in ("glib", "jansson", "libwebsockets", "openssl", "pcre2",
                    "libuv", "zlib", "libiconv", "gettext"):
        client[f"licenses/{package}.txt"] = share / package / "copyright"

    apworld = {f"quake3/{p.name}": p for p in world.glob("*.py")}
    apworld["quake3/archipelago.json"] = world / "archipelago.json"
    for folder in ("data", "docs"):
        for source in (world / folder).iterdir():
            if source.is_file() and source.suffix in (".py", ".json", ".md"):
                apworld[f"quake3/{folder}/{source.name}"] = source

    source = {}
    for relative in tracked(engine):
        path = engine / relative
        if path.is_file():
            source[f"Quake3e_ap/{relative.as_posix()}"] = path
    for relative in tracked(apcc):
        source[f"APCc/{relative.as_posix()}"] = apcc / relative
    for relative in tracked(args.archipelago, "LICENSE", "worlds/quake3"):
        source[f"Archipelago_q3/{relative.as_posix()}"] = args.archipelago / relative

    for name, input_file in (client | apworld | source).items():
        if not input_file.is_file():
            raise FileNotFoundError(input_file)
        assert Path(name).suffix not in (".pk3", ".bsp", ".aas", ".qvm", ".cfg", ".log", ".cache"), name
        assert "__pycache__" not in name and "q3key" not in name
    manifest = json.loads((world / "archipelago.json").read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    paths = [args.output / f"Quake3-AP-{manifest['world_version']}-windows-x64.zip",
             args.output / "quake3.apworld",
             args.output / f"Quake3-AP-{manifest['world_version']}-source.zip"]
    checksums = args.output / "SHA256SUMS.txt"
    for path in [*paths, checksums]:
        if path.exists():
            raise FileExistsError(path)
    archive(paths[0], client)
    archive(paths[1], apworld)
    archive(paths[2], source)
    with checksums.open("x", encoding="utf-8") as output:
        for path in paths:
            output.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n")
            print(f"{path}: {path.stat().st_size:,} bytes")
    print("Archives verified; SHA-256 checksums written. Nothing uploaded.")


if __name__ == "__main__":
    main()
