"""Restore only firmware assets from a prior workflow's setup-site artifact."""
import pathlib
import sys
import zipfile

destination = pathlib.Path(sys.argv[2]).resolve()
with zipfile.ZipFile(sys.argv[1]) as archive:
    for entry in archive.infolist():
        path = pathlib.PurePosixPath(entry.filename)
        if not path.parts or path.parts[0] != 'firmware':
            continue
        target = destination.joinpath(*path.parts).resolve()
        if not target.is_relative_to(destination / 'firmware'):
            raise SystemExit('Unsafe firmware archive path')
        if entry.is_dir():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(archive.read(entry))
