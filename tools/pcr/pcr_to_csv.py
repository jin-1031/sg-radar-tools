"""Convert one session from a Radar Studio .pcr recording to CSV."""

from __future__ import annotations

import argparse
import csv
import sys
from collections.abc import Iterable, Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path

from pcr_loader import Frame, Recording, Session, load

CSV_HEADER = (
    "frame",
    "timestamp_us",
    "x",
    "y",
    "z",
    "doppler",
    "power",
    "target_id",
)


class SessionSelectionError(ValueError):
    """Raised when a recording session cannot be selected unambiguously."""


@dataclass(frozen=True)
class ConversionSummary:
    session_index: int
    session_id: int
    frame_count: int
    empty_frame_count: int
    row_count: int
    output_path: Path


def select_session(
    recording: Recording, session_index: int | None
) -> tuple[int, Session]:
    sessions = recording.sessions
    if not sessions:
        raise SessionSelectionError("PCR recording contains no sessions")

    if session_index is None:
        if len(sessions) != 1:
            raise SessionSelectionError(
                f"PCR recording contains {len(sessions)} sessions; "
                "select one with --session INDEX"
            )
        return 0, sessions[0]

    if session_index < 0 or session_index >= len(sessions):
        raise SessionSelectionError(
            f"session index {session_index} is out of range "
            f"(valid range: 0..{len(sessions) - 1})"
        )

    return session_index, sessions[session_index]


def iter_frames_with_elapsed_us(
    frames: Iterable[Frame],
) -> Iterator[tuple[Frame, int]]:
    """Yield each frame with elapsed microseconds from the session's first frame."""

    elapsed_us = 0
    for index, frame in enumerate(frames):
        if index > 0:
            elapsed_us += frame.delta_us
        yield frame, elapsed_us


def format_point_value(value: float) -> str:
    return format(value, ".9g")


def write_session_csv(
    session: Session, output_path: str | Path, session_index: int = 0
) -> ConversionSummary:
    output_path = Path(output_path)
    empty_frame_count = 0
    row_count = 0

    with output_path.open("w", encoding="utf-8", newline="") as output_file:
        writer = csv.writer(output_file, lineterminator="\n")
        writer.writerow(CSV_HEADER)

        for frame, elapsed_us in iter_frames_with_elapsed_us(session.frames):
            if not frame.points:
                empty_frame_count += 1
                continue

            for point in frame.points:
                writer.writerow(
                    (
                        frame.frame_count,
                        elapsed_us,
                        format_point_value(point.x),
                        format_point_value(point.y),
                        format_point_value(point.z),
                        format_point_value(point.doppler),
                        format_point_value(point.power),
                        point.target_id,
                    )
                )
                row_count += 1

    return ConversionSummary(
        session_index=session_index,
        session_id=session.id,
        frame_count=len(session.frames),
        empty_frame_count=empty_frame_count,
        row_count=row_count,
        output_path=output_path,
    )


def convert_recording(
    recording: Recording,
    output_path: str | Path,
    session_index: int | None = None,
) -> ConversionSummary:
    selected_index, session = select_session(recording, session_index)
    return write_session_csv(session, output_path, selected_index)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert one session from a Radar Studio PCR file to CSV."
    )
    parser.add_argument("input_pcr", type=Path, help="input .pcr file")
    parser.add_argument("output_csv", type=Path, help="output .csv file")
    parser.add_argument(
        "--session",
        type=int,
        help="zero-based session index (required when the PCR has multiple sessions)",
    )
    return parser


def print_summary(summary: ConversionSummary) -> None:
    print(
        f"Selected session: index={summary.session_index}, "
        f"id={summary.session_id}"
    )
    print(f"Frames processed: {summary.frame_count}")
    print(f"Empty frames: {summary.empty_frame_count}")
    print(f"CSV data rows: {summary.row_count}")
    print(f"Output: {summary.output_path}")


def main(argv: Sequence[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)

    if not args.input_pcr.is_file():
        print(f"error: input PCR file not found: {args.input_pcr}", file=sys.stderr)
        return 1

    try:
        recording = load(args.input_pcr)
    except Exception as exc:
        print(
            f"error: failed to load PCR file '{args.input_pcr}': {exc}",
            file=sys.stderr,
        )
        return 1

    try:
        summary = convert_recording(recording, args.output_csv, args.session)
    except SessionSelectionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except OSError as exc:
        print(
            f"error: failed to write CSV file '{args.output_csv}': {exc}",
            file=sys.stderr,
        )
        return 1

    print_summary(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
