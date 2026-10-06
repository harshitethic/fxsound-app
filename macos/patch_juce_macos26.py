#!/usr/bin/env python3
from pathlib import Path
import re
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_juce_macos26.py <JUCE directory>")

juce = Path(sys.argv[1])
path = juce / "modules/juce_gui_basics/native/juce_mac_Windowing.mm"
if not path.is_file():
    raise SystemExit(f"JUCE source not found: {path}")

text = path.read_text(encoding="utf-8")
marker = "FxSound macOS 26 compatibility"
if marker in text:
    print("JUCE macOS compatibility patch already applied.")
    raise SystemExit(0)

pattern = re.compile(
    r"static Image createNSWindowSnapshot \(NSWindow\* nsWindow\)\s*\{.*?\n\}\s*\n\s*"
    r"Image createSnapshotOfNativeWindow",
    re.S,
)

replacement = """static Image createNSWindowSnapshot (NSWindow* nsWindow)
{
    // FxSound macOS 26 compatibility:
    // JUCE 6.1.6 used CGWindowListCreateImage here. Apple marks that API
    // unavailable in the macOS 15+ SDK. FxSound does not use native-window
    // screenshots, so disable only this optional helper.
    (void) nsWindow;
    return {};
}

Image createSnapshotOfNativeWindow"""

updated, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise SystemExit("Could not locate JUCE createNSWindowSnapshot() for patching.")

path.write_text(updated, encoding="utf-8")
print(f"Patched {path}")
