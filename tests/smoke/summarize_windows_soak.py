"""Summarize real-time Windows soak samples without turning short runs into passes."""
import csv
import json
from pathlib import Path
import statistics
import sys


def summarize(path, duration=7200):
    with path.open(newline="") as stream:
        rows = [{key: int(value) for key, value in row.items()}
                for row in csv.DictReader(stream)]
    assert rows and rows[-1]["seconds"] >= duration - 10, f"{path.name}: incomplete duration"
    assert all(a["seconds"] < b["seconds"] for a, b in zip(rows, rows[1:])), "Non-monotonic samples"
    largest_gap = max(b["seconds"] - a["seconds"] for a, b in zip(rows, rows[1:]))
    assert largest_gap <= 15, f"{path.name}: sampling stopped for {largest_gap}s"
    early = [row for row in rows if duration / 4 <= row["seconds"] < duration / 2]
    late = [row for row in rows if row["seconds"] >= duration * 3 / 4]
    assert early and late

    def median(data, key):
        return statistics.median(row[key] for row in data)

    growth = median(late, "private_bytes") - median(early, "private_bytes")
    handles = median(late, "handles") - median(early, "handles")
    result = {
        "file": str(path), "observed_seconds": rows[-1]["seconds"], "samples": len(rows),
        "largest_sample_gap_seconds": largest_gap,
        "early_private_mib_median": round(median(early, "private_bytes") / 2**20, 3),
        "late_private_mib_median": round(median(late, "private_bytes") / 2**20, 3),
        "private_mib_change": round(growth / 2**20, 3),
        "early_handles_median": median(early, "handles"),
        "late_handles_median": median(late, "handles"),
        "handles_change": handles,
    }
    # These are screening limits for this two-hour run, not a claim that every
    # smaller change is leak-free or that a longer run cannot expose a leak.
    result["resource_screen_passed"] = growth <= 8 * 2**20 and handles <= 8
    if "errors" in rows[-1]:
        result["enumerations"] = rows[-1]["enumerations"]
        result["errors"] = rows[-1]["errors"]
        assert result["errors"] == 0
        assert result["enumerations"] >= duration * 0.9, "Read-only enumeration loop stopped"
    if "connections" in rows[-1]:
        result["connections"] = rows[-1]["connections"]
        assert result["connections"] >= duration // 60 - 1, "Reconnect loop stopped"
        if "battery_queries" in rows[-1]:
            result["battery_queries"] = rows[-1]["battery_queries"]
            result["max_battery_gap_seconds"] = rows[-1]["max_battery_gap_seconds"]
            assert result["battery_queries"] >= duration // 30 - 2, "Battery polling stopped"
            assert result["max_battery_gap_seconds"] <= 45, "Healthy tray battery deadline stalled"
        for key in ("gdi_objects", "user_objects"):
            if key in rows[-1]:
                result[f"early_{key}_median"] = median(early, key)
                result[f"late_{key}_median"] = median(late, key)
                assert median(late, key) <= median(early, key) + 8, f"GUI resource growth: {key}"
    assert result["resource_screen_passed"], json.dumps(result)
    return result


if __name__ == "__main__":
    records = [summarize(Path(value).resolve()) for value in sys.argv[1:]]
    print(json.dumps(records, indent=2))
