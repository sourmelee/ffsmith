#pragma once
//
// FFSmith — canonical version
// ===========================
// Single source of truth for the engine's version, mirroring the Toolkit's
// ``Python/ffd/__init__.py`` ``__version__``. Every consumer reads from here:
// the ``CMakeLists.txt`` ``project(... VERSION ...)`` line, the project
// dashboard (``dashboard/generate_dashboard.py`` parses ``FFSMITH_VERSION``),
// and any future ``--version`` output.
//
// Versioning policy (mirrors the Toolkit — see ``../../CLAUDE.md``):
//   * Semantic Versioning. The version starts at 0.1.0.
//   * PATCH (0.1.1) — bug fixes and small internal changes; bump freely.
//   * MINOR (0.2.0) — a new milestone or subsystem; *ask before bumping*.
//   * MAJOR (1.0.0) — stable line / breaking save- or bundle-format change.
//
// Bump this in the SAME commit as the matching CHANGELOG.md entry, promote
// ``[Unreleased]`` to the new version with today's date, update the README
// badge, and keep ``project(FFSmith VERSION ...)`` in CMakeLists.txt in sync.
//
#define FFSMITH_VERSION_MAJOR 0
#define FFSMITH_VERSION_MINOR 1
#define FFSMITH_VERSION_PATCH 2
#define FFSMITH_VERSION "0.1.2"
