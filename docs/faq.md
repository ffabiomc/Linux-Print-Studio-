# FAQ / Perguntas frequentes

## Which package should I download? / Qual pacote baixar?
Use the distribution-specific x86_64 package from Releases. See [installation](../INSTALL.md).

## No printers appear / Nenhuma impressora aparece
Check `lpstat -p` and `systemctl status cups`. Configure the physical printer in the operating system first.

## Can I print on both sides without automatic duplex?
Use guided manual duplex and calibrate the printer. Test a few sheets first. / Use duplex manual guiado e calibre a impressora.

## Can I save a booklet without printing?
Use booklet layout and PDF export. Review the imposed pages before printing.

## Virtual printer missing / Impressora virtual ausente
See [virtual printer guide](virtual-printer.md). Try the system print dialog. Do not disable security controls or run the GUI as root.

## Language not changing / Idioma não muda
Select the language in general settings and restart. English is the fallback. Check installed Gettext catalogs.

## Is every printer supported?
No universal compatibility is claimed. See [compatibility](compatibility.md).

## Is the software free?
Project-owned code is GPL-2.0-only. See [LICENSE](../LICENSE).
