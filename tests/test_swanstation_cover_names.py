import importlib.util
import pathlib

script = pathlib.Path(__file__).resolve().parents[1] / "tools" / "swanstation_covers_sync.py"
spec = importlib.util.spec_from_file_location("swanstation_covers_sync", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

cases = {
    "Crash_[USA]_[SLUS-12345]": "Crash.png",
    "Mega___Man [U] (Disc 1)": "Mega_Man.png",
    "Crash_Bandicoot.CHD": "Crash_Bandicoot.png",
    "Pokémon & café": "Pok__mon___caf__.png",
    "A" * 100: "A" * 63 + ".png",
}
for title, expected in cases.items():
    actual = module.swan_filename(title)
    assert actual == expected, (title, actual, expected)

print("PASS: SwanStation fallback cover IDs match title cleanup and byte-based truncation.")
