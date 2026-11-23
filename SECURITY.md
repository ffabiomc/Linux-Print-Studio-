# Política permanente de segurança de memória (C++20)

Esta política é obrigatória para todo código novo e toda alteração no Linux Print Studio C++.

## Regras de revisão
1. **RAII por padrão**: todo recurso adquirido (GObject, PopplerPage, Cairo, CUPS, arquivos e memória) deve ter proprietário e liberação determinística, inclusive em exceções.
2. **Sem ownership implícito**: preferir `std::unique_ptr`, tipos de valor e wrappers RAII com deleters apropriados. Ponteiros crus só podem representar referências não proprietárias documentadas. Evitar `new`/`delete` explícitos.
3. **GTK/GLib**: documentar transferências de referência e vida útil de callbacks; não capturar ponteiros para objetos destruídos; remover/desconectar handlers quando necessário.
4. **Limites**: validar índices, contagem de páginas, dimensões, multiplicações, tamanhos, cópias, caminhos e estados antes do uso. Tratar PDFs como entradas não confiáveis.
5. **Sem comportamento indefinido**: proibir acesso fora de limites, use-after-free, double-free, conversões numéricas inseguras e uso de objetos não inicializados.
6. **Testes obrigatórios**: compilar e executar testes normais e testes com AddressSanitizer e UndefinedBehaviorSanitizer em CI; analisar alertas antes do merge.
7. **Análise estática**: executar clang-tidy nos arquivos do projeto, com foco em `bugprone-*`, `cert-*` e `clang-analyzer-*`. Corrigir ou justificar alertas relevantes.
8. **Casos adversariais**: acrescentar regressões para PDFs vazios, malformados, enormes, índices inválidos e falhas de abertura/exportação, com limites de recursos.
9. **Dependências**: acompanhar correções de segurança em GTK, GLib, Poppler, Cairo e CUPS; não ignorar alertas de bibliotecas externas.
10. **Bloqueio de release**: falhas nos testes, sanitizers ou achados graves de análise estática impedem a publicação de uma versão considerada estável.

## Critérios de aceite
- Mudanças de ownership têm testes de regressão.
- Build normal e build com sanitizers passam.
- Alertas de análise estática são revisados.
- Nenhuma garantia de equivalência à segurança de memória de Rust é alegada: C++20 continua exigindo disciplina e auditoria.

## Dívida técnica existente
O uso atual de `new State`, referências manuais `g_object_unref` e callbacks GTK precisa ser migrado/revisado gradualmente. A adoção desta política não significa que a dívida existente esteja resolvida.
