# Dependency licensing review / Revisão de licenças

Status: preliminary, not legal clearance. CMake directly links GTK4, libadwaita, Poppler GLib, Cairo and CUPS. Review exact upstream versions and installed package copyright notices before distributing each build.

| Dependency | Preliminary license family (verify exact version) |
|---|---|
| GTK4 | LGPL-2.1-or-later |
| libadwaita | LGPL-2.1-or-later |
| Poppler | GPL-2.0-or-later; verify optional components |
| Cairo | LGPL-2.1 or MPL-1.1 dual licensing |
| CUPS | Modern releases use Apache-2.0 with an exception; inspect exact exception and GPLv2 applicability |

Apache-2.0 alone is not compatible with GPL-2.0-only. Do not assume the CUPS exception resolves every linking or redistribution scenario. Check the actual library license and exception, transitive dependencies, bundled files, translations and contributor rights. Preserve notices and provide corresponding source and build scripts as required. Do not claim a completed compliance audit until evidence is recorded.
