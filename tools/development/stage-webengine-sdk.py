"""Fill an incomplete workspace Qt SDK with matching WebEngine runtime assets."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--toolchain-prefix", type=Path, required=True)
    args = parser.parse_args()
    sdk = args.sdk.resolve(strict=True)
    toolchain = args.toolchain_prefix.resolve(strict=True)
    if sdk == toolchain or "1.27" in str(toolchain):
        raise SystemExit("Select the independent workspace toolchain as the asset source")
    if not (sdk / "bin/Qt6WebEngineCore.dll").is_file():
        raise SystemExit("The selected SDK does not contain Qt WebEngine")
    files = [Path("share/qt6/bin/QtWebEngineProcess.exe")]
    for relative in ("share/qt6/resources", "share/qt6/translations/qtwebengine_locales"):
        source = toolchain / relative
        if not source.is_dir():
            raise SystemExit("Missing WebEngine asset directory: " + str(source))
        files.extend(path.relative_to(toolchain) for path in sorted(source.rglob("*")) if path.is_file())
    entries = []
    for relative in files:
        source, destination = toolchain / relative, sdk / relative
        if not source.is_file():
            raise SystemExit("Missing WebEngine asset: " + str(source))
        if not source.resolve().is_relative_to(toolchain) or not destination.resolve().is_relative_to(sdk):
            raise SystemExit("Asset path escapes the selected source or destination")
        sha256 = digest(source)
        if destination.exists() and (not destination.is_file() or digest(destination) != sha256):
            raise SystemExit("Refusing to replace a different existing SDK asset: " + str(destination))
        entries.append({"source": str(source), "destination": str(destination),
                        "bytes": source.stat().st_size, "sha256": sha256})
    for entry in entries:
        destination = Path(entry["destination"])
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists():
            shutil.copy2(entry["source"], destination)
        if digest(destination) != entry["sha256"]:
            raise SystemExit("SDK asset verification failed: " + str(destination))
    manifest = sdk / "webengine-runtime-provenance.json"
    manifest.write_text(json.dumps({"sdk": str(sdk), "toolchain": str(toolchain),
                                   "files": entries}, indent=2) + "\n", encoding="utf-8")
    print(f"Staged and verified {len(entries)} WebEngine assets; provenance: {manifest}")


if __name__ == "__main__":
    main()
