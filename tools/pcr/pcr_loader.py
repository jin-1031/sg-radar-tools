"""Load Radar Studio .pcr recordings (PCR 1.0, zstd payload).

Example:

    from pathlib import Path
    from pcr_loader import load

    recording = load(Path("asset/dataset/sample/1.pcr"))
    for session in recording.sessions:
        for frame in session.frames:
            points = frame.points
            targets = frame.targets

Requires Python 3.14+ (compression.zstd) or: pip install zstandard
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

FILE_MAGIC = b"PCR1"
FILE_VERSION = b"1000"
PAYLOAD_MAGIC = 0x0807060504030201
HEADER_STRUCT = struct.Struct("<4s4sQ16fQQ32sQQ")
POINT_STRUCT = struct.Struct("<5fi")
TARGET_STRUCT = struct.Struct("<2fII6f")

assert HEADER_STRUCT.size == 144
assert POINT_STRUCT.size == 24
assert TARGET_STRUCT.size == 40


class PcrError(RuntimeError):
    pass


def zstd_decompress(data: bytes, uncompressed_size: int) -> bytes:
    try:
        from compression.zstd import decompress

        return decompress(data)
    except ImportError:
        pass

    try:
        import zstandard
    except ImportError as exc:
        raise PcrError(
            "Need Python 3.14+ (compression.zstd) or: pip install zstandard"
        ) from exc

    return zstandard.ZstdDecompressor().decompress(data, max_output_size=uncompressed_size)


@dataclass
class SensorSpec:
    hfov_deg: float
    vfov_deg: float
    sensor_width: float
    sensor_height: float
    pos_x: float
    pos_y: float
    pos_z: float
    yaw_deg: float
    pitch_deg: float
    range_min_x: float
    range_min_y: float
    range_min_z: float
    range_max_x: float
    range_max_y: float
    range_max_z: float
    range: float


@dataclass
class Point:
    x: float
    y: float
    z: float
    doppler: float
    power: float
    target_id: int


@dataclass
class Target:
    x: float
    y: float
    status: int
    target_id: int
    minx: float
    maxx: float
    miny: float
    maxy: float
    minz: float
    maxz: float


@dataclass
class Frame:
    packet_size: int
    frame_count: int
    delta_us: int
    points: list[Point]
    targets: list[Target]
    bytes: int = 0


@dataclass
class Bookmark:
    description: str
    frame_index: int


@dataclass
class Session:
    id: int
    timestamp: int
    length_us: int
    bytes: int
    name: str
    description: str
    tag: str
    frames: list[Frame] = field(default_factory=list)
    bookmarks: list[Bookmark] = field(default_factory=list)


@dataclass
class FileHeader:
    magic: bytes
    version: bytes
    timestamp: int
    sensor_spec: SensorSpec
    session_count: int
    total_frame_count: int
    uncompressed_size: int
    compressed_size: int


@dataclass
class Recording:
    header: FileHeader
    sessions: list[Session]


class Reader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._off = 0

    def remaining(self) -> int:
        return len(self._data) - self._off

    def take(self, fmt: struct.Struct) -> tuple:
        chunk = self._data[self._off : self._off + fmt.size]
        if len(chunk) != fmt.size:
            raise PcrError("unexpected end of payload")
        self._off += fmt.size
        return fmt.unpack(chunk)

    def u32(self) -> int:
        return self.take(struct.Struct("<I"))[0]

    def u64(self) -> int:
        return self.take(struct.Struct("<Q"))[0]

    def string(self) -> str:
        length = self.u64()
        raw = self._data[self._off : self._off + length]
        if len(raw) != length:
            raise PcrError("truncated string")
        self._off += length
        return raw.decode("utf-8", errors="replace")


def parse_header(raw: bytes) -> FileHeader:
    if len(raw) < HEADER_STRUCT.size:
        raise PcrError("file is shorter than the 144-byte PCR header")

    magic, version, timestamp, *rest = HEADER_STRUCT.unpack_from(raw)
    sensor = SensorSpec(*rest[:16])
    session_count, total_frame_count, _reserved, uncompressed_size, compressed_size = rest[16:]
    return FileHeader(
        magic=magic,
        version=version,
        timestamp=timestamp,
        sensor_spec=sensor,
        session_count=session_count,
        total_frame_count=total_frame_count,
        uncompressed_size=uncompressed_size,
        compressed_size=compressed_size,
    )


def parse_frame(reader: Reader) -> Frame:
    packet_size = reader.u32()
    frame_count = reader.u32()
    delta_us = reader.u64()

    points = [Point(*reader.take(POINT_STRUCT)) for _ in range(reader.u64())]
    targets = [Target(*reader.take(TARGET_STRUCT)) for _ in range(reader.u64())]
    nbytes = reader.u64()
    return Frame(packet_size, frame_count, delta_us, points, targets, nbytes)


def parse_payload(payload: bytes, session_count: int) -> list[Session]:
    reader = Reader(payload)
    magic = reader.u64()
    if magic != PAYLOAD_MAGIC:
        raise PcrError(f"bad payload magic: 0x{magic:016x}")

    sessions: list[Session] = []
    for _ in range(session_count):
        session = Session(
            id=reader.u32(),
            timestamp=reader.u64(),
            length_us=reader.u64(),
            bytes=reader.u64(),
            name=reader.string(),
            description=reader.string(),
            tag=reader.string(),
        )
        session.frames = [parse_frame(reader) for _ in range(reader.u64())]
        session.bookmarks = [
            Bookmark(reader.string(), reader.u64())
            for _ in range(reader.u64())
        ]
        sessions.append(session)

    return sessions


def load(path: str | Path) -> Recording:
    path = Path(path)
    data = path.read_bytes()
    header = parse_header(data)

    if header.magic != FILE_MAGIC:
        raise PcrError(f"not a PCR file (magic={header.magic!r})")
    if header.version != FILE_VERSION:
        raise PcrError(f"unsupported PCR version {header.version!r} (expected PCR 1.0)")

    compressed = data[HEADER_STRUCT.size : HEADER_STRUCT.size + header.compressed_size]
    if len(compressed) != header.compressed_size:
        raise PcrError("truncated compressed payload")

    payload = zstd_decompress(compressed, header.uncompressed_size)
    if len(payload) != header.uncompressed_size:
        raise PcrError(
            f"uncompressed size mismatch: got {len(payload)}, header {header.uncompressed_size}"
        )

    return Recording(header, parse_payload(payload, header.session_count))
