# Linux Print Studio — GTK Edition

New C++20/GTK4 implementation. The Rust repository is unchanged.

## Debian build

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libgtk-4-dev libadwaita-1-dev libpoppler-glib-dev libcairo2-dev libcups2-dev
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/linux-print-studio
```

This is the initial implementation, **not feature parity** with the Rust edition. The GTK UI, PDF loading, N-up and booklet PDF export, printer discovery and CUPS submission are being ported. Physical printing is not validated.
