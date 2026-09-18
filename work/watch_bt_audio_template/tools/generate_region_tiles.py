#!/usr/bin/env python3
"""Build a resumable LVGL RGB565 regional map pack from an OSM PBF extract."""

from __future__ import annotations

import argparse
import math
import multiprocessing
import os
import sqlite3
import struct
import sys
import time
from pathlib import Path
from typing import Iterable, Iterator

from PIL import Image, ImageDraw


TILE_SIZE = 256
TILE_FILE_SIZE = 4 + TILE_SIZE * TILE_SIZE * 2
LVGL_TRUE_COLOR = 4
GCJ_AXIS = 6378245.0
GCJ_ECCENTRICITY = 0.00669342162296594323
LAYER_WATER_AREA = 0
LAYER_GREEN_AREA = 1
LAYER_BUILDING = 2
LAYER_WATERWAY = 3
LAYER_RAILWAY = 4
LAYER_ROAD = 5

WORKER_DATABASE: sqlite3.Connection | None = None
WORKER_OUTPUT: Path | None = None


def transform_latitude(longitude_offset: float, latitude_offset: float) -> float:
    result = (
        -100.0
        + 2.0 * longitude_offset
        + 3.0 * latitude_offset
        + 0.2 * latitude_offset * latitude_offset
        + 0.1 * longitude_offset * latitude_offset
        + 0.2 * math.sqrt(abs(longitude_offset))
    )
    result += (
        20.0 * math.sin(6.0 * longitude_offset * math.pi)
        + 20.0 * math.sin(2.0 * longitude_offset * math.pi)
    ) * 2.0 / 3.0
    result += (
        20.0 * math.sin(latitude_offset * math.pi)
        + 40.0 * math.sin(latitude_offset * math.pi / 3.0)
    ) * 2.0 / 3.0
    result += (
        160.0 * math.sin(latitude_offset * math.pi / 12.0)
        + 320.0 * math.sin(latitude_offset * math.pi / 30.0)
    ) * 2.0 / 3.0
    return result


def transform_longitude(longitude_offset: float, latitude_offset: float) -> float:
    result = (
        300.0
        + longitude_offset
        + 2.0 * latitude_offset
        + 0.1 * longitude_offset * longitude_offset
        + 0.1 * longitude_offset * latitude_offset
        + 0.1 * math.sqrt(abs(longitude_offset))
    )
    result += (
        20.0 * math.sin(6.0 * longitude_offset * math.pi)
        + 20.0 * math.sin(2.0 * longitude_offset * math.pi)
    ) * 2.0 / 3.0
    result += (
        20.0 * math.sin(longitude_offset * math.pi)
        + 40.0 * math.sin(longitude_offset * math.pi / 3.0)
    ) * 2.0 / 3.0
    result += (
        150.0 * math.sin(longitude_offset * math.pi / 12.0)
        + 300.0 * math.sin(longitude_offset * math.pi / 30.0)
    ) * 2.0 / 3.0
    return result


def wgs84_to_gcj02(latitude: float, longitude: float) -> tuple[float, float]:
    if not (0.8293 <= latitude <= 55.8271 and 72.004 <= longitude <= 137.8347):
        return latitude, longitude
    latitude_offset = transform_latitude(longitude - 105.0, latitude - 35.0)
    longitude_offset = transform_longitude(longitude - 105.0, latitude - 35.0)
    latitude_radians = math.radians(latitude)
    magic = math.sin(latitude_radians)
    magic = 1.0 - GCJ_ECCENTRICITY * magic * magic
    sqrt_magic = math.sqrt(magic)
    latitude_offset = latitude_offset * 180.0 / (
        (GCJ_AXIS * (1.0 - GCJ_ECCENTRICITY) / (magic * sqrt_magic))
        * math.pi
    )
    longitude_offset = longitude_offset * 180.0 / (
        (GCJ_AXIS / sqrt_magic) * math.cos(latitude_radians) * math.pi
    )
    return latitude + latitude_offset, longitude + longitude_offset


def lonlat_to_normalized(latitude: float, longitude: float) -> tuple[float, float]:
    latitude = max(-85.0511288, min(85.0511288, latitude))
    x = (longitude + 180.0) / 360.0
    sine = math.sin(math.radians(latitude))
    y = 0.5 - math.log((1.0 + sine) / (1.0 - sine)) / (4.0 * math.pi)
    return x, y


def wgs84_to_gcj_normalized(latitude: float, longitude: float) -> tuple[float, float]:
    gcj_latitude, gcj_longitude = wgs84_to_gcj02(latitude, longitude)
    return lonlat_to_normalized(gcj_latitude, gcj_longitude)


def parse_poly(poly_path: Path) -> list[tuple[bool, list[tuple[float, float]]]]:
    lines = poly_path.read_text(encoding="utf-8").splitlines()
    rings: list[tuple[bool, list[tuple[float, float]]]] = []
    index = 1
    while index < len(lines):
        name = lines[index].strip()
        index += 1
        if name == "END":
            break
        is_hole = name.startswith("!")
        points: list[tuple[float, float]] = []
        while index < len(lines) and lines[index].strip() != "END":
            longitude_text, latitude_text = lines[index].split()[:2]
            points.append(
                wgs84_to_gcj_normalized(float(latitude_text), float(longitude_text))
            )
            index += 1
        index += 1
        if len(points) >= 3:
            rings.append((is_hole, points))
    if not any(not is_hole for is_hole, _ in rings):
        raise ValueError(f"no outer ring found in {poly_path}")
    return rings


def point_in_ring(x: float, y: float, ring: list[tuple[float, float]]) -> bool:
    inside = False
    previous_x, previous_y = ring[-1]
    for current_x, current_y in ring:
        if ((current_y > y) != (previous_y > y)) and (
            x
            < (previous_x - current_x)
            * (y - current_y)
            / (previous_y - current_y)
            + current_x
        ):
            inside = not inside
        previous_x, previous_y = current_x, current_y
    return inside


def point_in_region(
    x: float, y: float, rings: list[tuple[bool, list[tuple[float, float]]]]
) -> bool:
    inside_outer = any(
        point_in_ring(x, y, ring) for is_hole, ring in rings if not is_hole
    )
    inside_hole = any(
        point_in_ring(x, y, ring) for is_hole, ring in rings if is_hole
    )
    return inside_outer and not inside_hole


def orientation(
    first: tuple[float, float],
    second: tuple[float, float],
    third: tuple[float, float],
) -> float:
    return (second[0] - first[0]) * (third[1] - first[1]) - (
        second[1] - first[1]
    ) * (third[0] - first[0])


def segments_intersect(
    first_start: tuple[float, float],
    first_end: tuple[float, float],
    second_start: tuple[float, float],
    second_end: tuple[float, float],
) -> bool:
    first_side = orientation(first_start, first_end, second_start)
    second_side = orientation(first_start, first_end, second_end)
    third_side = orientation(second_start, second_end, first_start)
    fourth_side = orientation(second_start, second_end, first_end)
    return ((first_side <= 0.0 <= second_side) or (second_side <= 0.0 <= first_side)) and (
        (third_side <= 0.0 <= fourth_side) or (fourth_side <= 0.0 <= third_side)
    )


def tile_intersects_region(
    tile_x: int,
    tile_y: int,
    zoom: int,
    rings: list[tuple[bool, list[tuple[float, float]]]],
) -> bool:
    tile_count = 1 << zoom
    minimum_x = tile_x / tile_count
    maximum_x = (tile_x + 1) / tile_count
    minimum_y = tile_y / tile_count
    maximum_y = (tile_y + 1) / tile_count
    test_points = (
        (minimum_x, minimum_y),
        (maximum_x, minimum_y),
        (minimum_x, maximum_y),
        (maximum_x, maximum_y),
        ((minimum_x + maximum_x) / 2.0, (minimum_y + maximum_y) / 2.0),
    )
    if any(point_in_region(x, y, rings) for x, y in test_points):
        return True
    rectangle_edges = (
        ((minimum_x, minimum_y), (maximum_x, minimum_y)),
        ((maximum_x, minimum_y), (maximum_x, maximum_y)),
        ((maximum_x, maximum_y), (minimum_x, maximum_y)),
        ((minimum_x, maximum_y), (minimum_x, minimum_y)),
    )
    for _, ring in rings:
        previous = ring[-1]
        for current in ring:
            if minimum_x <= current[0] <= maximum_x and minimum_y <= current[1] <= maximum_y:
                return True
            if any(segments_intersect(previous, current, start, end) for start, end in rectangle_edges):
                return True
            previous = current
    return False


def region_tiles(
    rings: list[tuple[bool, list[tuple[float, float]]]],
    minimum_zoom: int,
    maximum_zoom: int,
) -> Iterator[tuple[int, int, int]]:
    all_points = [point for _, ring in rings for point in ring]
    minimum_x = min(point[0] for point in all_points)
    maximum_x = max(point[0] for point in all_points)
    minimum_y = min(point[1] for point in all_points)
    maximum_y = max(point[1] for point in all_points)
    for zoom in range(minimum_zoom, maximum_zoom + 1):
        tile_count = 1 << zoom
        first_x = max(0, int(math.floor(minimum_x * tile_count)))
        last_x = min(tile_count - 1, int(math.floor(maximum_x * tile_count)))
        first_y = max(0, int(math.floor(minimum_y * tile_count)))
        last_y = min(tile_count - 1, int(math.floor(maximum_y * tile_count)))
        for tile_x in range(first_x, last_x + 1):
            for tile_y in range(first_y, last_y + 1):
                if tile_intersects_region(tile_x, tile_y, zoom, rings):
                    yield zoom, tile_x, tile_y


def detail_tiles(
    bounds: tuple[float, float, float, float],
    minimum_zoom: int,
    maximum_zoom: int,
) -> Iterator[tuple[int, int, int]]:
    west, south, east, north = bounds
    corners = [
        wgs84_to_gcj_normalized(latitude, longitude)
        for latitude in (south, north)
        for longitude in (west, east)
    ]
    minimum_x = min(point[0] for point in corners)
    maximum_x = max(point[0] for point in corners)
    minimum_y = min(point[1] for point in corners)
    maximum_y = max(point[1] for point in corners)
    for zoom in range(minimum_zoom, maximum_zoom + 1):
        tile_count = 1 << zoom
        for tile_x in range(
            int(math.floor(minimum_x * tile_count)),
            int(math.floor(maximum_x * tile_count)) + 1,
        ):
            for tile_y in range(
                int(math.floor(minimum_y * tile_count)),
                int(math.floor(maximum_y * tile_count)) + 1,
            ):
                yield zoom, tile_x, tile_y


def classify_feature(tags: dict[str, str], closed: bool) -> tuple[int, str, int] | None:
    highway = tags.get("highway")
    if highway:
        minimum_zoom = {
            "motorway": 5,
            "motorway_link": 7,
            "trunk": 6,
            "trunk_link": 8,
            "primary": 7,
            "primary_link": 9,
            "secondary": 8,
            "secondary_link": 10,
            "tertiary": 10,
            "tertiary_link": 11,
            "residential": 12,
            "unclassified": 12,
            "living_street": 13,
            "service": 14,
            "pedestrian": 14,
            "cycleway": 15,
            "footway": 15,
            "path": 15,
            "track": 15,
        }.get(highway)
        if minimum_zoom is not None:
            return LAYER_ROAD, highway, minimum_zoom
    if tags.get("railway"):
        return LAYER_RAILWAY, tags["railway"], 9
    if tags.get("waterway"):
        return LAYER_WATERWAY, tags["waterway"], 9
    if closed and (
        tags.get("natural") in {"water", "bay"}
        or tags.get("landuse") in {"reservoir", "basin"}
        or tags.get("water")
    ):
        return LAYER_WATER_AREA, "water", 8
    if closed and (
        tags.get("landuse")
        in {"forest", "grass", "meadow", "recreation_ground", "village_green"}
        or tags.get("leisure") in {"park", "garden", "pitch"}
        or tags.get("natural") in {"wood", "grassland"}
    ):
        return LAYER_GREEN_AREA, "green", 10
    if closed and tags.get("building"):
        return LAYER_BUILDING, "building", 14
    if closed and tags.get("amenity") in {
        "parking",
        "school",
        "hospital",
        "university",
    }:
        return LAYER_BUILDING, "amenity", 13
    return None


def create_database(database_path: Path) -> sqlite3.Connection:
    if database_path.exists():
        database_path.unlink()
    connection = sqlite3.connect(database_path)
    connection.executescript(
        """
        PRAGMA journal_mode=OFF;
        PRAGMA synchronous=OFF;
        PRAGMA temp_store=MEMORY;
        CREATE TABLE features (
            id INTEGER PRIMARY KEY,
            layer INTEGER NOT NULL,
            subtype TEXT NOT NULL,
            min_zoom INTEGER NOT NULL,
            points BLOB NOT NULL
        );
        CREATE VIRTUAL TABLE feature_index USING rtree(
            id, min_x, max_x, min_y, max_y
        );
        """
    )
    return connection


def build_index(pbf_path: Path, database_path: Path) -> None:
    try:
        import osmium
    except ImportError as error:
        raise RuntimeError("pyosmium is required to index PBF input") from error

    connection = create_database(database_path)

    class FeatureHandler(osmium.SimpleHandler):
        def __init__(self) -> None:
            super().__init__()
            self.next_id = 1
            self.feature_rows: list[tuple[int, int, str, int, bytes]] = []
            self.index_rows: list[tuple[int, float, float, float, float]] = []
            self.accepted = 0

        def way(self, way) -> None:
            tags = {tag.k: tag.v for tag in way.tags}
            if len(way.nodes) < 2:
                return
            closed = way.nodes[0].ref == way.nodes[-1].ref
            classification = classify_feature(tags, closed)
            if classification is None:
                return
            points: list[tuple[float, float]] = []
            try:
                for node in way.nodes:
                    if not node.location.valid():
                        return
                    point = wgs84_to_gcj_normalized(
                        node.location.lat, node.location.lon
                    )
                    if not points or point != points[-1]:
                        points.append(point)
            except osmium.InvalidLocationError:
                return
            if len(points) < 2:
                return
            layer, subtype, minimum_zoom = classification
            flat_points = [coordinate for point in points for coordinate in point]
            point_data = struct.pack(f"<{len(flat_points)}f", *flat_points)
            feature_id = self.next_id
            self.next_id += 1
            minimum_x = min(point[0] for point in points)
            maximum_x = max(point[0] for point in points)
            minimum_y = min(point[1] for point in points)
            maximum_y = max(point[1] for point in points)
            self.feature_rows.append(
                (feature_id, layer, subtype, minimum_zoom, point_data)
            )
            self.index_rows.append(
                (feature_id, minimum_x, maximum_x, minimum_y, maximum_y)
            )
            self.accepted += 1
            if len(self.feature_rows) >= 5000:
                self.flush()

        def flush(self) -> None:
            if not self.feature_rows:
                return
            connection.executemany(
                "INSERT INTO features VALUES (?, ?, ?, ?, ?)", self.feature_rows
            )
            connection.executemany(
                "INSERT INTO feature_index VALUES (?, ?, ?, ?, ?)", self.index_rows
            )
            connection.commit()
            self.feature_rows.clear()
            self.index_rows.clear()
            if self.accepted % 50000 == 0:
                print(f"indexed {self.accepted} drawable ways", flush=True)

    handler = FeatureHandler()
    start_time = time.monotonic()
    handler.apply_file(str(pbf_path), locations=True, idx="flex_mem")
    handler.flush()
    connection.execute("CREATE INDEX feature_zoom ON features(min_zoom, layer)")
    connection.commit()
    connection.close()
    elapsed = time.monotonic() - start_time
    print(
        f"index complete: {handler.accepted} drawable ways in {elapsed:.1f}s",
        flush=True,
    )


def road_style(subtype: str) -> tuple[int, str, str]:
    styles = {
        "motorway": (9, "#f2b36f", "#c98b4d"),
        "motorway_link": (5, "#f2b36f", "#c98b4d"),
        "trunk": (8, "#f4c079", "#ce9b55"),
        "trunk_link": (5, "#f4c079", "#ce9b55"),
        "primary": (7, "#f4d27d", "#c9a857"),
        "primary_link": (5, "#f4d27d", "#c9a857"),
        "secondary": (6, "#f3df9d", "#c9b97d"),
        "secondary_link": (4, "#f3df9d", "#c9b97d"),
        "tertiary": (5, "#fff2bf", "#d6cda9"),
        "tertiary_link": (4, "#fff2bf", "#d6cda9"),
        "residential": (4, "#ffffff", "#c9c9c5"),
        "unclassified": (4, "#ffffff", "#c9c9c5"),
        "service": (3, "#f7f7f4", "#d0d0cb"),
        "living_street": (3, "#f7f7f4", "#d0d0cb"),
        "cycleway": (2, "#8fc7a0", "#6da97e"),
        "footway": (2, "#e3b0a7", "#c58f86"),
        "path": (2, "#d8b99b", "#b99675"),
        "track": (2, "#d8b99b", "#b99675"),
        "pedestrian": (3, "#eee6dd", "#cabeb2"),
    }
    return styles.get(subtype, (3, "#ffffff", "#cccccc"))


def unpack_points(
    point_data: bytes, zoom: int, tile_x: int, tile_y: int, scale: int
) -> list[tuple[float, float]]:
    values = struct.unpack(f"<{len(point_data) // 4}f", point_data)
    map_size = float(TILE_SIZE << zoom)
    origin_x = tile_x * TILE_SIZE
    origin_y = tile_y * TILE_SIZE
    return [
        (
            (values[index] * map_size - origin_x) * scale,
            (values[index + 1] * map_size - origin_y) * scale,
        )
        for index in range(0, len(values), 2)
    ]


def write_lvgl_rgb565(image: Image.Image, output_path: Path) -> None:
    header = LVGL_TRUE_COLOR | (TILE_SIZE << 10) | (TILE_SIZE << 21)
    pixel_bytes = bytearray(TILE_SIZE * TILE_SIZE * 2)
    offset = 0
    for red, green, blue in image.convert("RGB").getdata():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        struct.pack_into("<H", pixel_bytes, offset, value)
        offset += 2
    temporary_path = output_path.with_suffix(".tmp")
    temporary_path.write_bytes(struct.pack("<I", header) + pixel_bytes)
    temporary_path.replace(output_path)


def initialize_worker(database_path: str, output_path: str) -> None:
    global WORKER_DATABASE, WORKER_OUTPUT
    WORKER_DATABASE = sqlite3.connect(
        f"file:{Path(database_path).as_posix()}?mode=ro", uri=True
    )
    WORKER_DATABASE.execute("PRAGMA query_only=ON")
    WORKER_OUTPUT = Path(output_path)


def render_tile(task: tuple[int, int, int]) -> tuple[str, int, int, int]:
    if WORKER_DATABASE is None or WORKER_OUTPUT is None:
        raise RuntimeError("worker is not initialized")
    zoom, tile_x, tile_y = task
    output_path = WORKER_OUTPUT / str(zoom) / str(tile_x) / f"{tile_y}.bin"
    if output_path.exists() and output_path.stat().st_size == TILE_FILE_SIZE:
        return "skipped", zoom, tile_x, tile_y
    output_path.parent.mkdir(parents=True, exist_ok=True)
    tile_count = 1 << zoom
    margin = 12.0 / (TILE_SIZE * tile_count)
    minimum_x = tile_x / tile_count - margin
    maximum_x = (tile_x + 1) / tile_count + margin
    minimum_y = tile_y / tile_count - margin
    maximum_y = (tile_y + 1) / tile_count + margin
    rows = WORKER_DATABASE.execute(
        """
        SELECT f.layer, f.subtype, f.points
        FROM feature_index AS i
        JOIN features AS f ON f.id = i.id
        WHERE i.min_x <= ? AND i.max_x >= ?
          AND i.min_y <= ? AND i.max_y >= ?
          AND f.min_zoom <= ?
        ORDER BY f.layer, f.id
        """,
        (maximum_x, minimum_x, maximum_y, minimum_y, zoom),
    ).fetchall()
    scale = 2
    image = Image.new("RGB", (TILE_SIZE * scale, TILE_SIZE * scale), "#eef1ed")
    draw = ImageDraw.Draw(image)
    for layer, subtype, point_data in rows:
        points = unpack_points(point_data, zoom, tile_x, tile_y, scale)
        if len(points) < 2:
            continue
        if layer == LAYER_WATER_AREA and len(points) >= 3:
            draw.polygon(points, fill="#a8d7e8", outline="#83bfd5")
        elif layer == LAYER_GREEN_AREA and len(points) >= 3:
            draw.polygon(points, fill="#cfe5bd", outline="#bad6a4")
        elif layer == LAYER_BUILDING and len(points) >= 3:
            draw.polygon(points, fill="#d8d2c9", outline="#c2b9ad")
        elif layer == LAYER_WATERWAY:
            draw.line(points, fill="#77b8d1", width=3 * scale, joint="curve")
        elif layer == LAYER_RAILWAY:
            draw.line(points, fill="#8f8c88", width=2 * scale, joint="curve")
        elif layer == LAYER_ROAD:
            width, fill, casing = road_style(subtype)
            draw.line(
                points, fill=casing, width=(width + 2) * scale, joint="curve"
            )
            draw.line(points, fill=fill, width=width * scale, joint="curve")
    image = image.resize((TILE_SIZE, TILE_SIZE), Image.Resampling.LANCZOS)
    write_lvgl_rgb565(image, output_path)
    return "rendered", zoom, tile_x, tile_y


def unique_tasks(
    rings: list[tuple[bool, list[tuple[float, float]]]], arguments: argparse.Namespace
) -> list[tuple[int, int, int]]:
    tasks = set(
        region_tiles(
            rings, arguments.region_zoom[0], arguments.region_zoom[1]
        )
    )
    tasks.update(
        detail_tiles(
            tuple(arguments.detail_bounds),
            arguments.detail_zoom[0],
            arguments.detail_zoom[1],
        )
    )
    return sorted(tasks)


def write_pack_readme(output_path: Path, tile_count: int, data_date: str) -> None:
    readme = f"""Guangdong offline map pack for agent-pet-sf32

Directory layout: MAP/<zoom>/<x>/<y>.bin
Coordinate system: GCJ-02 Web Mercator
Image format: LVGL v8 RGB565, 256 x 256, little-endian
Tile count: {tile_count}
OSM data snapshot: {data_date}

Coverage:
- Guangdong, Hong Kong and Macau: zoom 5 through 14
- Shenzhen detail rectangle: zoom 15 through 16

Copy this MAP directory to the root of a FAT32 TF card. Insert the card before
booting the device. Map data is derived from OpenStreetMap contributors under
the Open Database License 1.0: https://www.openstreetmap.org/copyright
"""
    (output_path / "README.txt").write_text(readme, encoding="utf-8")


def render_pack(arguments: argparse.Namespace) -> None:
    rings = parse_poly(arguments.poly)
    tasks = unique_tasks(rings, arguments)
    estimated_bytes = len(tasks) * TILE_FILE_SIZE
    print(
        f"tile plan: {len(tasks)} files, {estimated_bytes / (1024 ** 3):.2f} GiB raw",
        flush=True,
    )
    if arguments.estimate_only:
        return
    arguments.output.mkdir(parents=True, exist_ok=True)
    start_time = time.monotonic()
    rendered = 0
    skipped = 0
    worker_count = max(1, arguments.workers)
    with multiprocessing.Pool(
        worker_count,
        initializer=initialize_worker,
        initargs=(str(arguments.database), str(arguments.output)),
    ) as pool:
        for index, result in enumerate(
            pool.imap_unordered(render_tile, tasks, chunksize=8), start=1
        ):
            if result[0] == "rendered":
                rendered += 1
            else:
                skipped += 1
            if index % 250 == 0 or index == len(tasks):
                elapsed = time.monotonic() - start_time
                rate = index / elapsed if elapsed > 0.0 else 0.0
                remaining = (len(tasks) - index) / rate if rate > 0.0 else 0.0
                print(
                    f"tiles {index}/{len(tasks)} rendered={rendered} skipped={skipped} "
                    f"rate={rate:.1f}/s eta={remaining / 60.0:.1f}m",
                    flush=True,
                )
    write_pack_readme(arguments.output, len(tasks), arguments.data_date)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pbf", type=Path, required=True)
    parser.add_argument("--poly", type=Path, required=True)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--region-zoom", type=int, nargs=2, default=(5, 14))
    parser.add_argument("--detail-zoom", type=int, nargs=2, default=(15, 16))
    parser.add_argument(
        "--detail-bounds",
        type=float,
        nargs=4,
        metavar=("WEST", "SOUTH", "EAST", "NORTH"),
        default=(113.7, 22.4, 114.7, 22.9),
    )
    parser.add_argument("--workers", type=int, default=max(1, os.cpu_count() // 2))
    parser.add_argument("--data-date", default="2026-09-17")
    parser.add_argument("--skip-index", action="store_true")
    parser.add_argument("--estimate-only", action="store_true")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    if arguments.region_zoom[0] > arguments.region_zoom[1]:
        raise ValueError("invalid region zoom range")
    if arguments.detail_zoom[0] > arguments.detail_zoom[1]:
        raise ValueError("invalid detail zoom range")
    if not arguments.skip_index:
        build_index(arguments.pbf, arguments.database)
    elif not arguments.database.is_file():
        raise FileNotFoundError(arguments.database)
    render_pack(arguments)


if __name__ == "__main__":
    multiprocessing.freeze_support()
    try:
        main()
    except KeyboardInterrupt:
        print("interrupted; existing valid tiles will be reused on restart", file=sys.stderr)
        raise SystemExit(130)
