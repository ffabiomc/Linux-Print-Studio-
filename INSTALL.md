# Installation / Instalação

Download the package for your distribution from [Releases](https://github.com/ffabiomc/Linux-Print-Studio-/releases/latest). / Baixe o pacote da sua distribuição na página de releases.

## Native packages / Pacotes nativos

The current packages target x86_64. / Os pacotes atuais destinam-se a x86_64.

| Distribution | Install / Instalar |
|---|---|
| Ubuntu 24.04 | `sudo apt install ./PACKAGE.deb` |
| Debian 13 | `sudo apt install ./PACKAGE.deb` |
| Fedora 42 | `sudo dnf install ./PACKAGE.rpm` |
| openSUSE Tumbleweed | `sudo zypper install ./PACKAGE.rpm` |
| Arch Linux | `sudo pacman -U ./PACKAGE.pkg.tar.zst` |
| Manjaro | `sudo pacman -U ./PACKAGE.pkg.tar.zst` |

Replace PACKAGE with the actual downloaded filename. Choose the package explicitly labeled for your distribution; do not interchange Fedora and openSUSE or Arch and Manjaro packages. / Substitua PACKAGE pelo nome real do arquivo. Escolha o pacote específico da distribuição.

## Integrity / Integridade

Download SHA256SUMS from the same release. In the download directory, run:

```sh
sha256sum --ignore-missing -c SHA256SUMS
```

Check that your package is listed as OK. / Confira se o pacote aparece como OK. Checksums detect corruption but do not independently authenticate the publisher.

## Start / Iniciar

Run `linux-print-studio` or open the application menu. / Execute o comando ou abra pelo menu de aplicativos.

## Upgrade / Atualizar

Download the newer package for the same distribution and repeat the installation command; on Fedora use `sudo dnf upgrade ./PACKAGE.rpm`. / Baixe o pacote mais recente da mesma distribuição e repita a instalação.

## Remove / Desinstalar

If enabled, remove the virtual printer first:

```sh
sudo lps-virtual-printer remove
systemctl --user disable --now lps-receive.service
```

Then / Depois:

- Debian/Ubuntu: `sudo apt remove linux-print-studio-cpp`
- Fedora: `sudo dnf remove linux-print-studio-cpp`
- openSUSE: `sudo zypper remove linux-print-studio-cpp`
- Arch/Manjaro: `sudo pacman -R linux-print-studio-cpp`

## Limitations / Limitações

Linux Mint and other distribution versions are not independently validated. AppImage is not currently published. Physical printer compatibility requires manual testing. / Linux Mint e outras versões não foram validados independentemente. AppImage ainda não está publicado. Impressoras físicas exigem testes manuais.

See [README](README.md), [manual em português](docs/manual.pt_BR.md) and [LICENSE](LICENSE).
