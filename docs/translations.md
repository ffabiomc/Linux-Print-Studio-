# Translations / Traduções

The application uses Gettext catalogs in po/. English is the source fallback. Existing locales: pt_BR, pt_PT, es, fr, de, it, ru, zh_CN, ja, ko, ar, fi, sv.

## Workflow
1. Edit the appropriate PO catalog using a UTF-8-aware editor.
2. Preserve format placeholders, escaping and meaningful whitespace.
3. Validate with `msgfmt --check --check-format -o /dev/null po/pt_BR.po` (replace locale).
4. Build with CMake and install the generated MO catalogs.
5. Select the language in General settings, restart and inspect the interface.
6. Submit a focused pull request with screenshots or test notes.

For a new language, inspect the current language selector and locale mapping in src/main.cpp, add the catalog and update the CI locale checks. CMake discovers PO files automatically. Test fallback, long labels, plural forms and layout. Arabic requires explicit right-to-left visual validation. Do not claim complete translation coverage solely from successful msgfmt.
