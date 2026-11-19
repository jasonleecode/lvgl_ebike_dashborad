#!/usr/bin/env python3
"""Replay a GPX recording as timestamped NMEA on stdout."""
import argparse
from datetime import datetime, timezone
import math
from pathlib import Path
import sys
import time
import xml.etree.ElementTree as ET

DEFAULT_TRACK = Path(__file__).resolve().parents[1] / "data/Walking_2025-12-07_10-05-38_km.gpx"


def sentence(payload):
    checksum = 0
    for byte in payload.encode("ascii"):
        checksum ^= byte
    return f"${payload}*{checksum:02X}\r\n"


def coordinate(degrees, width):
    # Round before splitting so 59.99996 minutes carries into the next degree.
    whole, minutes = divmod(round(abs(degrees) * 600000), 600000)
    return f"{whole:0{width}d}{minutes / 10000:07.4f}"


def sample(step):
    # A small loop, about 5 m per second; RMC ground speed is in knots.
    phase = step / 60
    lat = coordinate(39.907 + 0.0027 * math.sin(phase), 2)
    lon = coordinate(116.397 + 0.0035 * math.cos(phase), 3)
    utc = time.gmtime()
    rmc = f"GNRMC,{time.strftime('%H%M%S', utc)},A,{lat},N,{lon},E,9.7,,{time.strftime('%d%m%y', utc)},,,A"
    return sentence(rmc) + sentence(f"PEBIK,{23 + math.sin(phase):.1f},78")


def track_points(path):
    """Read one segment at a time and keep the last fix for duplicate timestamps."""
    pending = None
    segment = None
    found = False
    for event, element in ET.iterparse(path, events=("start", "end")):
        tag = element.tag.rsplit("}", 1)[-1]
        if tag == "trkseg" and event == "start":
            segment = element
            first = True
        elif tag == "trkpt" and event == "end" and segment is not None:
            lat = float(element.attrib["lat"])
            lon = float(element.attrib["lon"])
            stamp = element.findtext("{*}time")
            if not stamp or not (-90 <= lat <= 90 and -180 <= lon <= 180):
                raise ValueError("GPX track point needs coordinates and a timestamp")
            when = datetime.fromisoformat(stamp.strip().replace("Z", "+00:00"))
            if when.tzinfo is None:
                raise ValueError("GPX timestamp needs a time zone")
            when = when.astimezone(timezone.utc)
            segment.remove(element)
            if pending and when < pending[0]:
                raise ValueError("GPX timestamps go backwards within a track segment")
            if pending and when != pending[0]:
                yield pending
                first = False
            pending = when, lat, lon, first
        elif tag == "trkseg" and event == "end":
            if pending:
                yield pending
                found = True
            pending = segment = None
            element.clear()
    if not found:
        raise ValueError("GPX contains no timestamped track points")


def distance_m(lat1, lon1, lat2, lon2):
    dy, dx = math.radians(lat2-lat1), math.radians(lon2-lon1)
    h = math.sin(dy/2)**2 + math.cos(math.radians(lat1))*math.cos(math.radians(lat2))*math.sin(dx/2)**2
    return 12742000 * math.asin(math.sqrt(min(1, h)))


def track_sentences(path):
    previous = None
    for when, lat, lon, first in track_points(path):
        delay = (when - previous[0]).total_seconds() if previous else 0
        speed = ""
        if not first and delay > 0:
            metres = distance_m(previous[1], previous[2], lat, lon)
            speed = f"{metres / delay * 3600 / 1852:.3f}"
        utc = when.strftime("%H%M%S")
        if when.microsecond:
            utc += f".{when.microsecond:06d}"
        ns, ew = ("N" if lat >= 0 else "S"), ("E" if lon >= 0 else "W")
        fields = f"{coordinate(lat, 2)},{ns},{coordinate(lon, 3)},{ew}"
        yield max(0, delay), sentence(f"GNRMC,{utc},A,{fields},{speed},,{when:%d%m%y},,,A")
        previous = when, lat, lon


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--gpx", type=Path, default=DEFAULT_TRACK, help="timestamped GPX track")
    source.add_argument("--synthetic", action="store_true", help="use the Beijing test loop with simulated sensors")
    parser.add_argument("--rate", type=float, default=1, help="playback speed multiplier (default 1)")
    args = parser.parse_args()
    if not math.isfinite(args.rate) or args.rate <= 0:
        parser.error("--rate must be positive and finite")
    deadline = time.monotonic()
    if args.synthetic:
        step = 0
        while True:
            sys.stdout.write(sample(step))
            sys.stdout.flush()
            time.sleep(1 / args.rate)
            step += 1
    try:
        for delay, message in track_sentences(args.gpx):
            deadline += delay / args.rate
            time.sleep(max(0, deadline - time.monotonic()))
            sys.stdout.write(message)
            sys.stdout.flush()
    except (OSError, ET.ParseError, KeyError, ValueError) as exc:
        if isinstance(exc, BrokenPipeError):
            return
        parser.exit(1, f"GPX replay: {exc}\n")


if __name__ == "__main__":
    try:
        main()
    except (BrokenPipeError, KeyboardInterrupt):
        pass
