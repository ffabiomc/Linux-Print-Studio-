# Architecture / Arquitetura

Linux Print Studio is a native C++20 GTK4/libadwaita application. Poppler GLib provides PDF processing, Cairo rendering and CUPS printer integration.

| Component | Responsibility |
|---|---|
| src/main.cpp | GUI, preview, preferences, lifecycle |
| src/core.cpp and include/ | Layout, rendering, page selection and CUPS integration |
| scripts/lps | Virtual CUPS backend |
| scripts/lps-receive | User-side PDF receiver |
| scripts/lps-virtual-printer | Queue management |
| data/ | Desktop icon, desktop entry and systemd user service |
| po/ | Gettext translations |
| tests/core_tests.cpp | Core tests |
| CMakeLists.txt | Build and installation |

## Security and testing
Keep GUI unprivileged; administrative authorization is limited to queue management. Preserve spool access restrictions. Prefer RAII, bounded containers, explicit ownership and checked conversions. Avoid unchecked indexing and raw owning pointers. Run CTest, ASan/UBSan and clang-tidy checks in CI. Passing automated tests does not establish physical printer compatibility.

## Data flow
Local PDF or virtual CUPS PDF delivery → GUI preview and layout settings → PDF export or CUPS submission. Virtual delivery does not guarantee application-level acknowledgment.
