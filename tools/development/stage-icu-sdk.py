"""Stage the genuine ICU 78 DLLs required by this Qt SDK without changing their source."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-bin", type=Path, required=True)
    parser.add_argument("--qt-root", type=Path,
                        default=Path(__file__).resolve().parents[3] / "qt-autocomplete-sdk/ucrt64")
    args = parser.parse_args()
    qt_root = args.qt_root.resolve()
    source_bin = args.source_bin.resolve()
    destination = qt_root / "bin"
    if not destination.is_dir() or destination.resolve() != destination:
        raise SystemExit("Qt SDK bin must exist and must not be redirected")
    manifest = {"created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "qt_root": str(qt_root), "files": []}
    for name in ("libicudt78.dll", "libicuin78.dll", "libicuuc78.dll"):
        source = source_bin / name
        target = destination / name
        source_hash = digest(source)
        if target.exists() and digest(target) != source_hash:
            raise SystemExit(f"Refusing to replace a different SDK runtime: {target}")
        if not target.exists():
            shutil.copy2(source, target)
        if digest(target) != source_hash:
            raise SystemExit(f"ICU staging hash mismatch: {target}")
        manifest["files"].append({"source": str(source), "destination": str(target),
                                  "sha256": source_hash, "bytes": target.stat().st_size})
    path = qt_root / "icu-runtime-provenance.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
