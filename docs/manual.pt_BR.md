# Manual de utilização — Linux Print Studio

[Voltar à documentação](../README.pt_BR.md)

## 1. Primeiro acesso

Instale o pacote correspondente à distribuição conforme o README. Abra o Linux Print Studio pelo menu de aplicativos. Não execute a interface gráfica como administrador.

## 2. Impressão de PDF

1. Clique em Abrir PDF ou arraste um PDF local para a janela.
2. Escolha a impressora física.
3. Selecione papel, orientação e quantidade de cópias.
4. Escolha todas as páginas, pares, ímpares ou intervalo personalizado, como 1-5, 8, 12-15.
5. Confira a pré-visualização e clique em Imprimir.

## 3. Layout e exportação

Escolha Normal, Livreto ou 2, 4, 6, 9 ou 16 páginas por folha. O modo livreto reorganiza as páginas para dobragem e pode inserir páginas em branco. Use Exportar PDF para gerar um documento com o layout selecionado sem imprimir imediatamente.

## 4. Configurações avançadas

Ajuste escala, margens, centralização, modo de cor e qualidade. Confira a prévia. A qualidade e as opções efetivamente aceitas dependem da impressora e do driver.

## 5. Frente e verso

Para impressoras com duplex automático, selecione a borda longa ou curta conforme a encadernação. Para duplex manual:

1. Selecione a impressora e utilize as folhas de calibração.
2. Identifique a orientação correta de realimentação e salve a calibração.
3. Imprima as frentes e aguarde o término.
4. Recoloque as folhas conforme a orientação calibrada.
5. Confirme e imprima os versos.

Teste com poucas folhas antes de uma tiragem grande. A orientação física varia conforme o equipamento.

## 6. Perfis e preferências

Salve as preferências de impressão para reutilizar os ajustes. Use perfis nomeados para configurações recorrentes. Nas configurações gerais, escolha tema e idioma. Reinicie o aplicativo após mudar o idioma. A redefinição dos padrões de impressão preserva os perfis nomeados e a calibração.

## 7. Impressora virtual

Abra o gerenciamento da impressora virtual e instale a fila mediante autorização administrativa. Em outro aplicativo, selecione Linux Print Studio na janela de impressão. O receptor deverá abrir o PDF na interface para que você escolha o layout e a impressora física.

Se a fila não aparecer no navegador, tente a janela de impressão do sistema. A compatibilidade com filas RAW varia.

## 8. Diagnóstico

```sh
lpstat -p
systemctl status cups
systemctl --user status lps-receive.service
journalctl --user -u lps-receive.service -n 50
```

Ao relatar problemas, informe distribuição, ambiente gráfico, versão do pacote, modelo da impressora e passos de reprodução. Não publique documentos confidenciais ou logs com dados pessoais.

## 9. Limitações

O seletor de idiomas, RTL árabe e a compatibilidade de todos os pacotes e impressoras ainda precisam de validação. Este manual descreve o código implementado; não certifica o funcionamento em todos os ambientes. Capturas reais da interface serão acrescentadas após obtenção e revisão.
