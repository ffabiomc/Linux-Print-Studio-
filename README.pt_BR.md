# Linux Print Studio

[Site oficial](https://ffabiomc.github.io/Linux-Print-Studio-/pt.html)

Aplicativo nativo Linux para impressão e diagramação de documentos PDF, desenvolvido em C++20, GTK4 e libadwaita.

[English](README.md) · [Downloads](https://github.com/ffabiomc/Linux-Print-Studio-/releases) · [Compilações](https://github.com/ffabiomc/Linux-Print-Studio-/actions) · [Relatar problemas](https://github.com/ffabiomc/Linux-Print-Studio-/issues)

[Histórico de alterações](CHANGELOG.md) · [Manual de utilização](docs/manual.pt_BR.md)

## Funcionalidades

- Abertura de PDF local, arrastar e soltar, pré-visualização e zoom.
- Impressão normal, livreto e 2, 4, 6, 9 ou 16 páginas por folha.
- Exportação PDF e seleção de páginas pares, ímpares, todas ou intervalos personalizados.
- Tamanho do papel, orientação, cópias, escala, margens, centralização, cor e qualidade.
- Integração CUPS, frente e verso automático e manual com calibração por impressora.
- Preferências persistentes, perfis de impressão e temas claro, escuro e do sistema.
- Impressora virtual e seleção de idioma.

## Instalação

Baixe o pacote em Releases ou nos artefatos de uma compilação bem-sucedida em Actions. Extraia o arquivo compactado antes de instalar.

Debian/Ubuntu: `sudo apt install ./PACOTE.deb`

Fedora: `sudo dnf install ./PACOTE.rpm` (para atualizar, utilize `dnf upgrade`).

Substitua PACOTE pelo nome do arquivo baixado. Abra pelo menu de aplicativos ou execute `linux-print-studio`.

## Compilação

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

Se o backend CUPS da distribuição estiver em /usr/libexec/cups/backend, acrescente `-DLPS_CUPS_BACKEND_DIR=libexec/cups/backend` à configuração CMake. O prefixo /usr corresponde ao caminho do serviço receptor. A instalação manual não é gerenciada pelo gerenciador de pacotes.

## Como imprimir

Abra o PDF, selecione a impressora, configure o layout e confira a prévia. Imprima ou exporte um novo PDF. Para frente e verso manual, calibre a impressora, imprima as frentes, recoloque o papel conforme as instruções, confirme e imprima os versos. Teste poucas folhas antes de grandes tiragens.

## Impressora virtual

Ative pelo aplicativo ou execute:

```sh
sudo lps-virtual-printer install
systemctl --user daemon-reload
systemctl --user enable --now lps-receive.service
lpstat -p Linux_Print_Studio
```

Selecione Linux Print Studio na janela de impressão de outro aplicativo. O backend CUPS recebe PDFs em /var/spool/cups-pdf/lps-delivery e o serviço receptor abre os documentos na interface. A fila é local e restrita ao usuário que a instalou. Não execute a interface gráfica como root.

Para remover: `sudo lps-virtual-printer remove`. Para desativar o receptor: `systemctl --user disable --now lps-receive.service`.

## Idiomas

Inglês, português brasileiro, português de Portugal, espanhol, francês, alemão, italiano, russo, chinês simplificado, japonês, coreano, árabe, finlandês e sueco.

Escolha em Configurações gerais ou siga o idioma do sistema. Reinicie o aplicativo após a alteração. Os catálogos estão em po/ e são compilados com msgfmt. As novas expressões do seletor ainda precisam de tradução e a interface árabe precisa de validação visual RTL.

## Arquitetura

| Caminho | Responsabilidade |
|---|---|
| src/main.cpp | Interface GTK, prévia e preferências |
| src/core.cpp e include/ | Layout, renderização, seleção de páginas e CUPS |
| tests/core_tests.cpp | Testes de layout, intervalos, duplex e validação |
| po/ | Traduções Gettext |
| scripts/lps | Backend virtual CUPS |
| scripts/lps-receive | Receptor de PDF do usuário |
| scripts/lps-virtual-printer | Gerenciamento da fila |
| data/ | Ícone, integração desktop e serviço systemd |
| CMakeLists.txt | Compilação, testes e instalação |

## Solução de problemas

- Impressora ausente: execute `lpstat -p` e `systemctl status cups`.
- Impressora virtual: verifique `systemctl --user status lps-receive.service` e `journalctl --user -u lps-receive.service -n 50`.
- Em sistemas SELinux, investigue as negativas de acesso sem desativar a proteção.
- Traduções ausentes: instale gettext antes de compilar, instale os catálogos e reinicie.
- Navegadores podem apresentar limitações na descoberta de filas RAW; tente a janela de impressão do sistema.

## Testes e limitações

Execute CTest após alterações. Valide cada catálogo com msgfmt --check (por exemplo, `msgfmt --check -o /dev/null po/fi.po`). Confira os resultados de Actions antes de distribuir pacotes.

A compatibilidade com todos os modelos de impressora, distribuições e ambientes gráficos ainda não foi validada. A impressão manual frente e verso requer calibração. A entrega virtual ainda não possui confirmação garantida de recebimento pela interface. A compilação do seletor de idiomas e o conteúdo dos pacotes precisam de validação.

## Contribuições

Ao relatar um problema, informe distribuição, ambiente gráfico, versão/build, modelo da impressora, passos para reprodução e logs relevantes. Remova dados pessoais e nomes de documentos privados. Envie pull requests focados, acompanhados de testes quando aplicável.

## Licença

O código próprio do projeto é licenciado sob a [GNU GPL versão 2 exclusivamente (GPL-2.0-only)](LICENSE). Dependências e componentes de terceiros mantêm suas respectivas licenças.

## Documentação

- [Instalação](INSTALL.md)
- [Manual PT-BR](docs/manual.pt_BR.md) · [Manual EN](docs/manual.en.md)
- [Virtual printer](docs/virtual-printer.md) · [FAQ](docs/faq.md)
- [Compatibility](docs/compatibility.md) · [Architecture](docs/architecture.md)
- [Translations](docs/translations.md) · [Contributing](CONTRIBUTING.md)
