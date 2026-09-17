"""Load a sample .pcr file and print a few points and targets.

    python tools/pcr/example.py
    python tools/pcr/example.py path/to/file.pcr
"""

from __future__ import annotations

import sys
from pathlib import Path

from pcr_loader import PcrError, Recording, load

REPO_ROOT = Path(__file__).resolve().parents[2]
SAMPLE_DIR = REPO_ROOT / "asset" / "dataset" / "sample"
DEFAULT_SAMPLE = SAMPLE_DIR / "1.pcr"


def sample_path() -> Path:
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    if DEFAULT_SAMPLE.is_file():
        return DEFAULT_SAMPLE
    matches = sorted(SAMPLE_DIR.glob("*.pcr"))
    if matches:
        return matches[0]
    raise FileNotFoundError(
        f"no sample .pcr found in {SAMPLE_DIR}. Pass a file path."
    )


def print_samples(path: Path, recording: Recording, max_frames: int = 2, max_items: int = 3) -> None:
    header = recording.header
    spec = header.sensor_spec
    print(f"file          : {path}")
    print(f"version       : {header.version.decode()}")
    print(f"sessions      : {header.session_count}")
    print(f"total frames  : {header.total_frame_count}")
    print(
        "sensor        : "
        f"hfov={spec.hfov_deg:.1f} vfov={spec.vfov_deg:.1f} "
        f"range=({spec.range_min_x:.2f},{spec.range_min_y:.2f},{spec.range_min_z:.2f})-"
        f"({spec.range_max_x:.2f},{spec.range_max_y:.2f},{spec.range_max_z:.2f})"
    )
    print()

    for session in recording.sessions:
        print(
            f"session {session.id}: {session.name!r}  "
            f"frames={len(session.frames)}  "
            f"length_us={session.length_us}  "
            f"bookmarks={len(session.bookmarks)}"
        )
        for i, frame in enumerate(session.frames[:max_frames]):
            print(
                f"  frame {i}: points={len(frame.points)} "
                f"targets={len(frame.targets)} delta_us={frame.delta_us}"
            )
            for point in frame.points[:max_items]:
                print(
                    f"    point: x={point.x:.3f} y={point.y:.3f} z={point.z:.3f} "
                    f"doppler={point.doppler:.3f} power={point.power:.3f} "
                    f"target_id={point.target_id}"
                )
            if len(frame.points) > max_items:
                print(f"    ... {len(frame.points) - max_items} more points")
            for target in frame.targets[:max_items]:
                print(
                    f"    target: id={target.target_id} x={target.x:.3f} y={target.y:.3f} "
                    f"status={target.status} "
                    f"box=({target.minx:.3f},{target.miny:.3f},{target.minz:.3f})-"
                    f"({target.maxx:.3f},{target.maxy:.3f},{target.maxz:.3f})"
                )
            if len(frame.targets) > max_items:
                print(f"    ... {len(frame.targets) - max_items} more targets")
        if len(session.frames) > max_frames:
            print(f"  ... {len(session.frames) - max_frames} more frames")


def main() -> int:
    path = sample_path()
    try:
        recording = load(path)
    except PcrError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print_samples(path, recording)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
