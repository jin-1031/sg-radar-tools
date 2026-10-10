from __future__ import annotations

import csv
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from decimal import Decimal
from pathlib import Path
from unittest.mock import patch

PCR_TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PCR_TOOLS_DIR))

import pcr_to_csv
from pcr_loader import (
    FILE_MAGIC,
    FILE_VERSION,
    FileHeader,
    Frame,
    PcrError,
    Point,
    Recording,
    SensorSpec,
    Session,
)


def make_point(
    x: float = 1.0,
    y: float = 2.0,
    z: float = 3.0,
    doppler: float = 4.0,
    power: float = 5.0,
    target_id: int = 6,
) -> Point:
    return Point(x, y, z, doppler, power, target_id)


def make_frame(
    frame_count: int,
    delta_us: int = 0,
    points: list[Point] | None = None,
) -> Frame:
    return Frame(
        packet_size=0,
        frame_count=frame_count,
        delta_us=delta_us,
        points=[] if points is None else points,
        targets=[],
    )


def make_session(
    frames: list[Frame] | None = None,
    session_id: int = 10,
) -> Session:
    return Session(
        id=session_id,
        timestamp=0,
        length_us=0,
        bytes=0,
        name=f"session-{session_id}",
        description="",
        tag="",
        frames=[] if frames is None else frames,
    )


def make_recording(sessions: list[Session]) -> Recording:
    sensor_spec = SensorSpec(*([0.0] * 16))
    header = FileHeader(
        magic=FILE_MAGIC,
        version=FILE_VERSION,
        timestamp=0,
        sensor_spec=sensor_spec,
        session_count=len(sessions),
        total_frame_count=sum(len(session.frames) for session in sessions),
        uncompressed_size=0,
        compressed_size=0,
    )
    return Recording(header=header, sessions=sessions)


def convert_and_read(
    session: Session,
) -> tuple[pcr_to_csv.ConversionSummary, list[list[str]]]:
    with tempfile.TemporaryDirectory() as temp_dir:
        output_path = Path(temp_dir) / "output.csv"
        summary = pcr_to_csv.write_session_csv(session, output_path)
        with output_path.open(encoding="utf-8", newline="") as output_file:
            rows = list(csv.reader(output_file))
    return summary, rows


class SessionSelectionTests(unittest.TestCase):
    def test_single_session_is_selected_automatically(self) -> None:
        session = make_session(session_id=21)

        index, selected = pcr_to_csv.select_session(
            make_recording([session]), None
        )

        self.assertEqual(index, 0)
        self.assertIs(selected, session)

    def test_multiple_sessions_require_explicit_index(self) -> None:
        recording = make_recording([make_session(), make_session(session_id=11)])

        with self.assertRaisesRegex(
            pcr_to_csv.SessionSelectionError, "--session"
        ):
            pcr_to_csv.select_session(recording, None)

    def test_valid_session_index_is_selected(self) -> None:
        sessions = [make_session(session_id=10), make_session(session_id=20)]

        index, selected = pcr_to_csv.select_session(
            make_recording(sessions), 1
        )

        self.assertEqual(index, 1)
        self.assertIs(selected, sessions[1])

    def test_invalid_session_index_is_rejected(self) -> None:
        recording = make_recording([make_session()])

        for index in (-1, 1):
            with self.subTest(index=index):
                with self.assertRaisesRegex(
                    pcr_to_csv.SessionSelectionError, "out of range"
                ):
                    pcr_to_csv.select_session(recording, index)

    def test_recording_without_sessions_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            pcr_to_csv.SessionSelectionError, "no sessions"
        ):
            pcr_to_csv.select_session(make_recording([]), None)


class TimestampTests(unittest.TestCase):
    def test_first_delta_is_ignored_and_later_deltas_accumulate(self) -> None:
        frames = [
            make_frame(1, delta_us=999_999),
            make_frame(2, delta_us=50_000),
            make_frame(3, delta_us=51_234),
        ]

        elapsed = [
            elapsed_us
            for _, elapsed_us in pcr_to_csv.iter_frames_with_elapsed_us(frames)
        ]

        self.assertEqual(elapsed, [0, 50_000, 101_234])

        for frame in frames:
            frame.points.append(make_point())
        _, rows = convert_and_read(make_session(frames))
        self.assertEqual([row[1] for row in rows[1:]], ["0", "50000", "101234"])

    def test_integer_timestamps_match_legacy_milliseconds(self) -> None:
        frames = [
            make_frame(1, points=[make_point()]),
            make_frame(2, delta_us=1, points=[make_point()]),
            make_frame(3, delta_us=1_009, points=[make_point()]),
            make_frame(4, delta_us=990, points=[make_point()]),
            make_frame(5, delta_us=99_234, points=[make_point()]),
        ]
        _, rows = convert_and_read(make_session(frames))
        timestamps = [row[1] for row in rows[1:]]
        self.assertEqual(timestamps, ["0", "1", "1010", "2000", "101234"])
        self.assertEqual(
            [Decimal(value) for value in timestamps],
            [Decimal(value) * 1000 for value in ("0", "0.001", "1.01", "2", "101.234")],
        )

    def test_timestamps_reset_for_each_selected_session(self) -> None:
        sessions = [
            make_session([
                make_frame(1, delta_us=999, points=[make_point()]),
                make_frame(2, delta_us=50_000, points=[make_point()]),
            ]),
            make_session([
                make_frame(3, delta_us=888, points=[make_point()]),
                make_frame(4, delta_us=1, points=[make_point()]),
            ], session_id=11),
        ]
        recording = make_recording(sessions)
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "output.csv"
            for index, expected in enumerate((["0", "50000"], ["0", "1"])):
                pcr_to_csv.convert_recording(recording, output_path, index)
                with output_path.open(encoding="utf-8", newline="") as output_file:
                    rows = list(csv.reader(output_file))
                self.assertEqual([row[1] for row in rows[1:]], expected)


class CsvOutputTests(unittest.TestCase):
    def test_header_and_point_fields_are_preserved(self) -> None:
        points = [
            make_point(1.25, -2.5, 3.75, -4.5, 5.125, 255),
            make_point(-6.25, 7.5, -8.75, 9.5, -10.125, -1),
        ]

        summary, rows = convert_and_read(make_session([make_frame(42, points=points)]))

        self.assertEqual(
            rows[0],
            [
                "frame",
                "timestamp_us",
                "x",
                "y",
                "z",
                "doppler",
                "power",
                "target_id",
            ],
        )
        self.assertEqual(len(rows) - 1, len(points))
        self.assertEqual(summary.row_count, len(points))
        self.assertEqual(
            rows[1],
            ["42", "0", "1.25", "-2.5", "3.75", "-4.5", "5.125", "255"],
        )
        self.assertEqual(
            rows[2],
            ["42", "0", "-6.25", "7.5", "-8.75", "9.5", "-10.125", "-1"],
        )

    def test_float_values_use_float32_round_trip_precision(self) -> None:
        point = make_point(
            1.2345678806304932,
            -2.3456788063049316,
            3.456789016723633,
            -4.567890167236328,
            5.678901195526123,
            7,
        )

        _, rows = convert_and_read(make_session([make_frame(1, points=[point])]))

        self.assertEqual(
            rows[1][2:7],
            [format(value, ".9g") for value in (
                point.x,
                point.y,
                point.z,
                point.doppler,
                point.power,
            )],
        )

    def test_same_frame_points_share_timestamp(self) -> None:
        frames = [
            make_frame(1, points=[make_point()]),
            make_frame(2, delta_us=12_345, points=[make_point(), make_point()]),
        ]

        _, rows = convert_and_read(make_session(frames))

        self.assertEqual([rows[2][1], rows[3][1]], ["12345", "12345"])

    def test_duplicate_and_noncontiguous_frame_counts_are_preserved(self) -> None:
        frames = [
            make_frame(10, points=[make_point()]),
            make_frame(10, delta_us=1_000, points=[make_point()]),
            make_frame(99, delta_us=1_000, points=[make_point()]),
        ]

        _, rows = convert_and_read(make_session(frames))

        self.assertEqual([row[0] for row in rows[1:]], ["10", "10", "99"])

    def test_empty_frame_writes_no_row_and_is_counted(self) -> None:
        frames = [
            make_frame(1),
            make_frame(2, delta_us=5_000, points=[make_point()]),
        ]

        summary, rows = convert_and_read(make_session(frames))

        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1][1], "5000")
        self.assertEqual(summary.frame_count, 2)
        self.assertEqual(summary.empty_frame_count, 1)
        self.assertEqual(summary.row_count, 1)

    def test_empty_middle_frame_preserves_elapsed_time(self) -> None:
        frames = [
            make_frame(1, points=[make_point()]),
            make_frame(2, delta_us=50_000),
            make_frame(3, delta_us=51_234, points=[make_point()]),
        ]
        summary, rows = convert_and_read(make_session(frames))
        self.assertEqual([row[0] for row in rows[1:]], ["1", "3"])
        self.assertEqual([row[1] for row in rows[1:]], ["0", "101234"])
        self.assertEqual(summary.empty_frame_count, 1)
        self.assertEqual(summary.row_count, 2)

    def test_empty_session_writes_header_only(self) -> None:
        summary, rows = convert_and_read(make_session())

        self.assertEqual(rows, [list(pcr_to_csv.CSV_HEADER)])
        self.assertEqual(summary.frame_count, 0)
        self.assertEqual(summary.empty_frame_count, 0)
        self.assertEqual(summary.row_count, 0)

    def test_output_open_failure_is_reported_by_writer(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            with self.assertRaises(OSError):
                pcr_to_csv.write_session_csv(make_session(), Path(temp_dir))


class CliTests(unittest.TestCase):
    def test_missing_input_returns_nonzero(self) -> None:
        error_output = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir, redirect_stderr(error_output):
            result = pcr_to_csv.main(
                [
                    str(Path(temp_dir) / "missing.pcr"),
                    str(Path(temp_dir) / "output.csv"),
                ]
            )

        self.assertNotEqual(result, 0)
        self.assertIn("input PCR file not found", error_output.getvalue())

    def test_loader_failure_returns_nonzero(self) -> None:
        error_output = io.StringIO()

        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "input.pcr"
            input_path.touch()
            output_path = Path(temp_dir) / "output.csv"

            with patch.object(
                pcr_to_csv, "load", side_effect=PcrError("invalid recording")
            ), redirect_stderr(error_output):
                result = pcr_to_csv.main([str(input_path), str(output_path)])

        self.assertNotEqual(result, 0)
        self.assertIn("failed to load PCR file", error_output.getvalue())

    def test_multi_session_cli_requires_session_index(self) -> None:
        error_output = io.StringIO()
        recording = make_recording([make_session(), make_session(session_id=11)])

        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "input.pcr"
            input_path.touch()
            output_path = Path(temp_dir) / "output.csv"

            with patch.object(
                pcr_to_csv, "load", return_value=recording
            ), redirect_stderr(error_output):
                result = pcr_to_csv.main([str(input_path), str(output_path)])

        self.assertNotEqual(result, 0)
        self.assertIn("--session", error_output.getvalue())

    def test_invalid_session_cli_returns_nonzero(self) -> None:
        error_output = io.StringIO()
        recording = make_recording([make_session()])

        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "input.pcr"
            input_path.touch()
            output_path = Path(temp_dir) / "output.csv"

            with patch.object(
                pcr_to_csv, "load", return_value=recording
            ), redirect_stderr(error_output):
                result = pcr_to_csv.main(
                    [str(input_path), str(output_path), "--session", "1"]
                )

        self.assertNotEqual(result, 0)
        self.assertIn("out of range", error_output.getvalue())

    def test_output_open_failure_cli_returns_nonzero(self) -> None:
        error_output = io.StringIO()
        recording = make_recording([make_session()])

        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "input.pcr"
            input_path.touch()

            with patch.object(
                pcr_to_csv, "load", return_value=recording
            ), redirect_stderr(error_output):
                result = pcr_to_csv.main([str(input_path), temp_dir])

        self.assertNotEqual(result, 0)
        self.assertIn("failed to write CSV file", error_output.getvalue())

    def test_cli_selects_session_and_writes_summary(self) -> None:
        recording = make_recording(
            [make_session(session_id=10), make_session([make_frame(8)], 20)]
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "input.pcr"
            input_path.touch()
            output_path = Path(temp_dir) / "output.csv"

            with patch.object(pcr_to_csv, "load", return_value=recording):
                with patch("sys.stdout", new_callable=io.StringIO) as output:
                    result = pcr_to_csv.main(
                        [str(input_path), str(output_path), "--session", "1"]
                    )

            self.assertEqual(result, 0)
            self.assertTrue(output_path.is_file())
            self.assertIn("index=1, id=20", output.getvalue())
            self.assertIn("Frames processed: 1", output.getvalue())
            self.assertIn("Empty frames: 1", output.getvalue())
            self.assertIn("CSV data rows: 0", output.getvalue())


if __name__ == "__main__":
    unittest.main()
