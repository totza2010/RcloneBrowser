#!/usr/bin/env python3
"""Layer boundary checker for the core/GUI separation effort.

Two jobs:

1.  Report which source files are free of GUI coupling, so the core extraction
    (see docs/ARCHITECTURE.md) can be tracked file by file instead of guessed at.
2.  Fail when a file listed as CORE in docs/ARCHITECTURE.md gains a GUI
    dependency, so the boundary does not rot while other work happens.

It also collects `// CORE:` / `// LAYER:` markers left in the code, which is how
separation points get recorded in place while working on unrelated changes.

Usage:
    python scripts/check_layers.py            # report
    python scripts/check_layers.py --check    # report + exit 1 on violations
    python scripts/check_layers.py --markers  # only list recorded markers

Caveat: this is an include-level heuristic. `src/pch.h` pulls in <QtGui> for the
whole project, so a file can look clean here and still fail to link without
Qt6::Widgets. The authoritative test is `cmake -DNO_GUI=ON` once that option
exists; until then treat this as a tracking aid, not proof.
"""

import argparse
import pathlib
import re
import sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "src"
ARCH_DOC = pathlib.Path(__file__).resolve().parent.parent / "docs" / "ARCHITECTURE.md"

# Qt modules that are not available to a headless (-DNO_GUI=ON) build.
GUI_INCLUDE = re.compile(
    r"#\s*include\s*[<\"]"
    r"(QtWidgets|QtGui|QWidget|QDialog|QMainWindow|QListWidget|QTreeWidget|"
    r"QTableWidget|QLineEdit|QComboBox|QCheckBox|QRadioButton|QSpinBox|"
    r"QPlainTextEdit|QTextEdit|QMessageBox|QPushButton|QToolButton|QLabel|"
    r"QApplication|QGuiApplication|QStyle|QIcon|QPixmap|QAction|QMenu|"
    r"QClipboard|QFileDialog|QSystemTrayIcon|QLayout|QSplitter|ui_)"
)
UI_MEMBER = re.compile(r"\bui\.|Ui::")
MARKER = re.compile(r"//\s*(CORE|LAYER|SECURITY|TEST)\s*:\s*(.+)")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)

# Files declared CORE in docs/ARCHITECTURE.md. Keep in sync with that table --
# adding a name here is a promise that the file links without Qt6::Widgets.
CORE_FILES = {
    "global.h",
    "qcron.h", "qcron.cpp",
    "qcronfield.h", "qcronfield.cpp",
    "qcronnode.h", "qcronnode.cpp",
    "list_of_job_options.h", "list_of_job_options.cpp",
    "job_options.h", "job_options.cpp",
    "utils.h", "utils.cpp",
}


def strip_comments(text):
    """Blank out comments, keeping line numbers intact.

    A comment saying that something used to reach through ui.foo is not a
    dependency on ui.foo, but counting raw text cannot tell the difference --
    and writing that sentence down is much of the point of moving code into
    the core. Newlines survive so marker line numbers still land correctly.
    """
    return COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def scan(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    code = strip_comments(text)
    gui = GUI_INCLUDE.findall(code)
    ui = len(UI_MEMBER.findall(code))
    markers = [(i, m.group(1), m.group(2).strip())
               for i, line in enumerate(text.splitlines(), 1)
               for m in [MARKER.search(line)] if m]
    return gui, ui, markers


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if a declared-CORE file has GUI coupling")
    ap.add_argument("--markers", action="store_true",
                    help="only print recorded CORE:/LAYER: markers")
    args = ap.parse_args()

    rows, all_markers, violations = [], [], []

    for path in sorted(SRC.glob("*.[ch]*")):
        if path.suffix not in (".c", ".h", ".cpp", ".hpp", ".mm"):
            continue
        gui, ui, markers = scan(path)
        rows.append((path.name, len(gui), ui, sorted(set(gui))))
        all_markers += [(path.name, ln, kind, txt) for ln, kind, txt in markers]
        if path.name in CORE_FILES and (gui or ui):
            violations.append((path.name, sorted(set(gui)), ui))

    if args.markers:
        print_markers(all_markers)
        return 0

    clean = [r for r in rows if r[1] == 0 and r[2] == 0]
    coupled = [r for r in rows if r[1] or r[2]]

    print(f"=== GUI-free files ({len(clean)}/{len(rows)}) ===")
    for name, _, _, _ in clean:
        mark = "  [declared CORE]" if name in CORE_FILES else ""
        print(f"  {name}{mark}")

    print(f"\n=== GUI-coupled files ({len(coupled)}) ===")
    print(f"  {'file':<32} {'gui_inc':>7} {'ui_refs':>8}")
    for name, ngui, ui, _ in sorted(coupled, key=lambda r: -r[2]):
        print(f"  {name:<32} {ngui:>7} {ui:>8}")

    print_markers(all_markers)

    if violations:
        print(f"\n=== BOUNDARY VIOLATIONS ({len(violations)}) ===")
        for name, gui, ui in violations:
            print(f"  {name}: gui includes {gui}, ui refs {ui}")
        print("\n  A file declared CORE in docs/ARCHITECTURE.md gained a GUI")
        print("  dependency. Either revert it or move the file out of CORE_FILES")
        print("  and update the register in docs/ARCHITECTURE.md.")
        if args.check:
            return 1
    else:
        print("\nNo boundary violations.")

    return 0


def print_markers(markers):
    print(f"\n=== recorded markers ({len(markers)}) ===")
    if not markers:
        print("  none yet -- leave `// CORE: <note>` where you find one")
        return
    for kind in ("CORE", "LAYER", "SECURITY", "TEST"):
        group = [m for m in markers if m[2] == kind]
        if not group:
            continue
        print(f"\n  -- {kind} ({len(group)}) --")
        for name, line, _, text in group:
            print(f"    {name}:{line}  {text}")


if __name__ == "__main__":
    sys.exit(main())
