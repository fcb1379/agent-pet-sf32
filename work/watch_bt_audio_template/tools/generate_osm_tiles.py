#!/usr/bin/env python3
"""Generate small LVGL RGB565 offline-map tile sets from OpenStreetMap data."""

from __future__ import annotations

import argparse
import json
import math
import struct
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw


TILE_SIZE = 256
LVGL_TRUE_COLOR = 4
GCJ_AXIS = 6378245.0
GCJ_ECCENTRICITY = 0.00669342162296594323
OVERPASS_URL = "https://overpass-api.de/api/interpreter"


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


def gcj02_to_wgs84(latitude: float, longitude: float) -> tuple[float, float]:
    wgs_latitude = latitude
    wgs_longitude = longitude
    for _ in range(6):
        converted_latitude, converted_longitude = wgs84_to_gcj02(
            wgs_latitude, wgs_longitude
        )
        wgs_latitude -= converted_latitude - latitude
        wgs_longitude -= converted_longitude - longitude
    return wgs_latitude, wgs_longitude


def lonlat_to_pixel(latitude: float, longitude: float, zoom: int) -> tuple[float, float]:
    map_size = float(TILE_SIZE << zoom)
    latitude = max(-85.0511288, min(85.0511288, latitude))
    x = (longitude + 180.0) / 360.0 * map_size
    sine = math.sin(math.radians(latitude))
    y = (0.5 - math.log((1.0 + sine) / (1.0 - sine)) / (4.0 * math.pi))
    return x, y * map_size


def pixel_to_lonlat(pixel_x: float, pixel_y: float, zoom: int) -> tuple[float, float]:
    map_size = float(TILE_SIZE << zoom)
    longitude = pixel_x / map_size * 360.0 - 180.0
    mercator = math.pi * (1.0 - 2.0 * pixel_y / map_size)
    latitude = math.degrees(math.atan(math.sinh(mercator)))
    return latitude, longitude


def tile_range(latitude: float, longitude: float, zoom: int, radius: int) -> tuple[range, range]:
    gcj_latitude, gcj_longitude = wgs84_to_gcj02(latitude, longitude)
    pixel_x, pixel_y = lonlat_to_pixel(gcj_latitude, gcj_longitude, zoom)
    tile_x = int(pixel_x) // TILE_SIZE
    tile_y = int(pixel_y) // TILE_SIZE
    return (
        range(tile_x - radius, tile_x + radius + 1),
        range(tile_y - radius, tile_y + radius + 1),
    )


def query_bounds(latitude: float, longitude: float, zooms: Iterable[int], radius: int) -> tuple[float, float, float, float]:
    gcj_bounds: list[tuple[float, float]] = []
    for zoom in zooms:
        x_range, y_range = tile_range(latitude, longitude, zoom, radius)
        for pixel_x, pixel_y in (
            (x_range.start * TILE_SIZE, y_range.start * TILE_SIZE),
            (x_range.stop * TILE_SIZE, y_range.stop * TILE_SIZE),
        ):
            gcj_bounds.append(pixel_to_lonlat(pixel_x, pixel_y, zoom))
    wgs_bounds = [gcj02_to_wgs84(lat, lon) for lat, lon in gcj_bounds]
    south = min(point[0] for point in wgs_bounds)
    north = max(point[0] for point in wgs_bounds)
    west = min(point[1] for point in wgs_bounds)
    east = max(point[1] for point in wgs_bounds)
    return south, west, north, east


def fetch_osm(bounds: tuple[float, float, float, float]) -> dict:
    south, west, north, east = bounds
    bbox = f"{south:.7f},{west:.7f},{north:.7f},{east:.7f}"
    selectors = (
        "way[highway]",
        "way[building]",
        "way[landuse]",
        "way[leisure]",
        "way[natural]",
        "way[waterway]",
        "way[railway]",
        "way[amenity]",
    )
    body = "[out:json][timeout:60];(" + "".join(
        f"{selector}({bbox});" for selector in selectors
    ) + ");out tags geom;"
    request = urllib.request.Request(
        OVERPASS_URL,
        data=urllib.parse.urlencode({"data": body}).encode("utf-8"),
        headers={"User-Agent": "agent-pet-sf32-offline-map-generator/1.0"},
    )
    with urllib.request.urlopen(request, timeout=90) as response:
        return json.load(response)


def geometry_pixels(element: dict, zoom: int, origin_x: int, origin_y: int) -> list[tuple[float, float]]:
    points: list[tuple[float, float]] = []
    for point in element.get("geometry", []):
        latitude, longitude = wgs84_to_gcj02(point["lat"], point["lon"])
        pixel_x, pixel_y = lonlat_to_pixel(latitude, longitude, zoom)
        points.append((pixel_x - origin_x, pixel_y - origin_y))
    return points


def polygon_style(tags: dict) -> tuple[str, str] | None:
    if tags.get("natural") in {"water", "bay"} or "water" in tags:
        return "#a8d7e8", "#83bfd5"
    if tags.get("landuse") in {"grass", "forest", "meadow", "recreation_ground"}:
        return "#cfe5bd", "#bad6a4"
    if tags.get("leisure") in {"park", "garden", "pitch"}:
        return "#c9e4b5", "#afd295"
    if "building" in tags:
        return "#d8d2c9", "#c2b9ad"
    if tags.get("amenity") in {"parking", "school", "hospital", "university"}:
        return "#e4dfd4", "#cfc6b7"
    return None


def road_style(tags: dict) -> tuple[int, str, str] | None:
    highway = tags.get("highway")
    styles = {
        "motorway": (9, "#f2b36f", "#c98b4d"),
        "trunk": (8, "#f4c079", "#ce9b55"),
        "primary": (7, "#f4d27d", "#c9a857"),
        "secondary": (6, "#f3df9d", "#c9b97d"),
        "tertiary": (5, "#fff2bf", "#d6cda9"),
        "residential": (4, "#ffffff", "#c9c9c5"),
        "unclassified": (4, "#ffffff", "#c9c9c5"),
        "service": (3, "#f7f7f4", "#d0d0cb"),
        "living_street": (3, "#f7f7f4", "#d0d0cb"),
        "cycleway": (2, "#8fc7a0", "#6da97e"),
        "footway": (2, "#e3b0a7", "#c58f86"),
        "path": (2, "#d8b99b", "#b99675"),
        "pedestrian": (3, "#eee6dd", "#cabeb2"),
    }
    return styles.get(highway)


def render_tile(elements: list[dict], zoom: int, tile_x: int, tile_y: int) -> Image.Image:
    scale = 2
    image = Image.new("RGB", (TILE_SIZE * scale, TILE_SIZE * scale), "#eef1ed")
    draw = ImageDraw.Draw(image)
    origin_x = tile_x * TILE_SIZE
    origin_y = tile_y * TILE_SIZE

    for element in elements:
        tags = element.get("tags", {})
        style = polygon_style(tags)
        points = geometry_pixels(element, zoom, origin_x, origin_y)
        if style is None or len(points) < 3 or points[0] != points[-1]:
            continue
        scaled = [(x * scale, y * scale) for x, y in points]
        draw.polygon(scaled, fill=style[0], outline=style[1])

    for element in elements:
        tags = element.get("tags", {})
        points = geometry_pixels(element, zoom, origin_x, origin_y)
        if len(points) < 2:
            continue
        scaled = [(x * scale, y * scale) for x, y in points]
        if "waterway" in tags:
            draw.line(scaled, fill="#77b8d1", width=3 * scale, joint="curve")
        elif "railway" in tags:
            draw.line(scaled, fill="#8f8c88", width=2 * scale, joint="curve")

    road_elements = sorted(
        elements,
        key=lambda element: (road_style(element.get("tags", {})) or (0, "", ""))[0],
    )
    for element in road_elements:
        style = road_style(element.get("tags", {}))
        points = geometry_pixels(element, zoom, origin_x, origin_y)
        if style is None or len(points) < 2:
            continue
        width, fill, casing = style
        scaled = [(x * scale, y * scale) for x, y in points]
        draw.line(scaled, fill=casing, width=(width + 2) * scale, joint="curve")
        draw.line(scaled, fill=fill, width=width * scale, joint="curve")

    return image.resize((TILE_SIZE, TILE_SIZE), Image.Resampling.LANCZOS)


def write_lvgl_rgb565(image: Image.Image, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    header = LVGL_TRUE_COLOR | (TILE_SIZE << 10) | (TILE_SIZE << 21)
    pixels = bytearray()
    for red, green, blue in image.convert("RGB").getdata():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        pixels.extend(struct.pack("<H", value))
    output_path.write_bytes(struct.pack("<I", header) + pixels)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--latitude", type=float, default=22.543096)
    parser.add_argument("--longitude", type=float, default=114.057865)
    parser.add_argument("--zoom", type=int, nargs="+", default=[15, 16])
    parser.add_argument("--radius", type=int, default=1)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--input", type=Path, help="Use cached Overpass JSON")
    parser.add_argument("--preview", type=Path, help="Optional PNG preview directory")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    bounds = query_bounds(
        arguments.latitude, arguments.longitude, arguments.zoom, arguments.radius
    )
    if arguments.input is None:
        osm_data = fetch_osm(bounds)
    else:
        osm_data = json.loads(arguments.input.read_text(encoding="utf-8"))
    elements = osm_data.get("elements", [])
    if not elements:
        raise RuntimeError("OpenStreetMap query returned no drawable elements")

    tile_count = 0
    for zoom in arguments.zoom:
        x_range, y_range = tile_range(
            arguments.latitude, arguments.longitude, zoom, arguments.radius
        )
        for tile_x in x_range:
            for tile_y in y_range:
                image = render_tile(elements, zoom, tile_x, tile_y)
                write_lvgl_rgb565(
                    image, arguments.output / str(zoom) / str(tile_x) / f"{tile_y}.bin"
                )
                if arguments.preview is not None:
                    preview_path = (
                        arguments.preview / str(zoom) / str(tile_x) / f"{tile_y}.png"
                    )
                    preview_path.parent.mkdir(parents=True, exist_ok=True)
                    image.save(preview_path)
                tile_count += 1
    print(f"generated {tile_count} tiles from {len(elements)} OSM ways")
    print(f"query bounds WGS84: {bounds}")


if __name__ == "__main__":
    main()
