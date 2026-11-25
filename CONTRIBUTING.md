# Contributing / Contribuindo

Report bugs through GitHub Issues with distribution, desktop environment, package build, printer model, reproduction steps and sanitized logs. Never upload confidential PDFs or credentials.

For changes, create a focused branch, follow existing C++20 style, prefer RAII and safe ownership, and add regression tests. Build using the README instructions and run `ctest --test-dir build --output-on-failure`. Validate translations using `msgfmt --check -o /dev/null po/LANGUAGE.po`. Do not bypass sanitizer or static-analysis failures.

Document changes and distinguish automated tests from physical printer testing. Submit a pull request explaining scope, tests and limitations. Contributions must be compatible with GPL-2.0-only; disclose third-party code and its license. See [LICENSE](LICENSE) and [SECURITY](SECURITY.md).
