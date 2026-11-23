## Checklist obrigatório para alterações C++

- [ ] Segui as regras de [SECURITY.md](../../SECURITY.md).
- [ ] Documentei ownership e tempo de vida de objetos GTK, Poppler, Cairo e CUPS modificados.
- [ ] Validei limites, índices, dimensões e entradas não confiáveis.
- [ ] Acrescentei testes de regressão para falhas e casos-limite.
- [ ] Verifiquei o job `memory-safety` (ASan, UBSan e clang-tidy).
- [ ] Não introduzi `new`/`delete` manual ou ponteiros proprietários sem justificativa.

### Justificativas / riscos residuais

Descreva aqui exceções à política e riscos ainda não resolvidos.
