# Linux Print Studio

Native Linux PDF printing and layout application built with C++20, GTK4 and libadwaita.

[Português (Brasil)](README.pt_BR.md) · [Downloads](https://github.com/ffabiomc/Linux-Print-Studio-/releases) · [Builds](https://github.com/ffabiomc/Linux-Print-Studio-/actions) · [Report an issue](https://github.com/ffabiomc/Linux-Print-Studio-/issues)

[Changelog](CHANGELOG.md) · [User manual (Portuguese)](docs/manual.pt_BR.md)

## Features

- Local PDF loading, drag and drop, preview and zoom.
- Normal, booklet and 2/4/6/9/16-up layouts; PDF export.
- All, odd, even or custom page ranges (e.g. `1-5, 8, 12-15`).
- Paper size, orientation, copies, scale, margins, centering, color and quality controls.
- CUPS printer discovery and submission; automatic and guided manual duplex with per-printer calibration.
- Saved printing defaults, named profiles, light/dark/system theme.
- Virtual CUPS printer for receiving PDF jobs from other applications.
- Language selection and persistent preferences.

## Installation

Download the appropriate package from Releases or the successful Actions build artifacts (GitHub sign-in may be required). Extract the artifact archive before installing.

Debian/Ubuntu: `sudo apt install ./PACKAGE.deb`

Fedora: `sudo dnf install ./PACKAGE.rpm` (use `dnf upgrade` for an installed version).

Replace PACKAGE with the downloaded filename. Start Linux Print Studio from the application menu or run `linux-print-studio`.

## Build from source

Debian/Ubuntu:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config gettext libgtk-4-dev libadwaita-1-dev libpoppler-glib-dev libcairo2-dev libcups2-dev cups
```

Fedora:

```sh
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config gettext gtk4-devel libadwaita-devel poppler-glib-devel cairo-devel cups-devel cups
```

```sh
git clone https://github.com/ffabiomc/Linux-Print-Studio-.git
cd Linux-Print-Studio-
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
linux-print-studio
```

For distributions using /usr/libexec/cups/backend, configure with `-DLPS_CUPS_BACKEND_DIR=libexec/cups/backend`. The /usr prefix matches the installed receiver service. Installing from source is not tracked by the package manager.

## Printing

Open a PDF, select a physical printer, configure the layout and preview, then print or export a new PDF. For manual duplex, calibrate the printer, print the fronts, reload the paper as instructed, confirm and print the backs. Test with a few sheets before large jobs.

## Virtual printer

Enable the virtual printer in the application or run:

```sh
sudo lps-virtual-printer install
systemctl --user daemon-reload
systemctl --user enable --now lps-receive.service
lpstat -p Linux_Print_Studio
```

Select Linux Print Studio in another application's print dialog. The CUPS backend stages PDF jobs in /var/spool/cups-pdf/lps-delivery; the user receiver opens them in the GUI. The queue is local, restricted to the installing desktop user, and requires administrator authorization. Do not run the GUI as root.

Remove the queue with `sudo lps-virtual-printer remove`; stop the receiver with `systemctl --user disable --now lps-receive.service`.

## Languages

English (source fallback), Portuguese (Brazil and Portugal), Spanish, French, German, Italian, Russian, Simplified Chinese, Japanese, Korean, Arabic, Finnish and Swedish. Choose a language in General settings or follow the system; restart after changing it. Catalogs are in po/ and compiled by msgfmt when available. The new selector strings still need localization and Arabic RTL needs visual testing.

## Architecture and testing

| Path | Responsibility |
|---|---|
| src/main.cpp | GTK interface, preview, preferences and application lifecycle |
| src/core.cpp, include/ | PDF layout, rendering, page selection and CUPS integration |
| tests/core_tests.cpp | Page ranges, imposition, duplex and option validation |
| po/ | Gettext catalogs |
| scripts/lps | CUPS virtual backend |
| scripts/lps-receive | User-side PDF receiver |
| scripts/lps-virtual-printer | Queue management |
| data/ | Desktop integration, icon and systemd user service |
| CMakeLists.txt | Build, tests and installation |

Run CTest after changes. For translations, validate with `msgfmt --check -o /dev/null po/fi.po` (repeat for each catalog). Review Actions results before distributing packages.

## Troubleshooting and limitations

- No printers: check `lpstat -p` and `systemctl status cups`.
- Virtual printer not receiving: check `systemctl --user status lps-receive.service` and `journalctl --user -u lps-receive.service -n 50`. On SELinux systems inspect denials rather than disabling SELinux.
- Missing translations: install gettext before building and install the generated catalogs; restart the application.
- Browser discovery and RAW queue compatibility vary. Try the system print dialog where available.
- Physical printer, desktop environment and distribution compatibility are not universally validated. Manual duplex requires calibration. Virtual delivery does not yet guarantee an application-level acknowledgment.
- The latest language-selector build and package contents require validation; this document describes implemented source features, not a certification of all releases.

## Contributing

Open an issue with distribution, desktop environment, package/build version, printer model, reproduction steps and relevant logs. Remove private document names and personal data from logs. Submit focused pull requests with tests where appropriate.

## License

Project-owned code is licensed under the [GNU GPL version 2 only (GPL-2.0-only)](LICENSE). Third-party components and dependencies retain their respective licenses.

## Documentation

- [Installation](INSTALL.md)
- [Manual PT-BR](docs/manual.pt_BR.md) · [Manual EN](docs/manual.en.md)
- [Virtual printer](docs/virtual-printer.md) · [FAQ](docs/faq.md)
- [Compatibility](docs/compatibility.md) · [Architecture](docs/architecture.md)
- [Translations](docs/translations.md) · [Contributing](CONTRIBUTING.md)
