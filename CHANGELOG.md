# Changelog

This file records project changes. Build numbers and release tags are available in [GitHub Actions](https://github.com/ffabiomc/Linux-Print-Studio-/actions) and [Releases](https://github.com/ffabiomc/Linux-Print-Studio-/releases).

## Unreleased

### Added
- Gettext catalogs for Portuguese (Brazil and Portugal), Spanish, French, German, Italian, Russian, Simplified Chinese, Japanese, Korean, Arabic, Finnish and Swedish, alongside English source strings.
- Persistent language selection with a system-language option.
- Arabic right-to-left interface direction when selected manually.
- English and Brazilian Portuguese project documentation.

### Pending validation
- Build and package validation for the language selector.
- Localization of new selector messages and visual RTL testing.

## 1.0.2 — development baseline

The following summarizes the GTK edition development baseline, not a verified release-by-release history.

- Native C++20, GTK4 and libadwaita interface.
- PDF preview, export, page ranges, N-up and booklet imposition.
- CUPS printer discovery, printing and guided manual duplex calibration.
- Advanced rendering options, printing profiles and persistent defaults.
- Virtual printer backend and systemd user receiver.
- Desktop launcher, icon, theme selection and DEB/RPM packaging.
- Core tests for page selection, imposition and rendering option validation.

Physical printer and distribution compatibility remain subject to testing.
