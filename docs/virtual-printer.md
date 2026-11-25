# Virtual printer / Impressora virtual

The local CUPS queue receives PDF jobs from other applications. / A fila local CUPS recebe trabalhos PDF de outros aplicativos.

## Setup / Configuração

Run as the intended desktop user, using sudo only for queue management:

```sh
sudo lps-virtual-printer install
systemctl --user daemon-reload
systemctl --user enable --now lps-receive.service
lpstat -p Linux_Print_Studio
```

Select Linux Print Studio in the source application's print dialog. The backend stages PDF jobs in /var/spool/cups-pdf/lps-delivery and the user receiver opens them in the GUI. / Selecione Linux Print Studio na janela de impressão do aplicativo de origem.

## Diagnostics / Diagnóstico

```sh
lpstat -p
systemctl status cups
systemctl --user status lps-receive.service
journalctl --user -u lps-receive.service -n 50
```

Try the system print dialog if the browser does not list the queue. Do not disable SELinux or weaken spool permissions. Do not run the GUI as root. The queue is restricted to the installing desktop user. RAW compatibility varies; application-level delivery acknowledgment is not guaranteed.

## Removal / Remoção

```sh
sudo lps-virtual-printer remove
systemctl --user disable --now lps-receive.service
```

Physical printing and desktop-specific integration still require manual validation.
