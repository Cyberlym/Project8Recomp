# Diário técnico — Android ARM64

## 2026-09-07 — Etapa 1: Git e auditoria estática

**Base:** upstream `c8c21e2781acd134de6b2ccf12b666145e00ea6c`.
**Branch de trabalho:** `android-port`, criada a partir de `main` alinhado.

- `origin` preservado: `https://github.com/Cyberlym/Project8Recomp`.
- `upstream` adicionado: `https://github.com/theokyr/Project8Recomp`.
- O usuário autorizou excepcionalmente substituir o commit inicial
  `50f8eb52984673f62b390a7cddc7a6a03223857c`. `main` local passou a acompanhar
  `upstream/main`; `origin/main` foi confirmado remotamente no mesmo hash.
- Arquivos: `AGENTS.md` acrescido de Android Port Rules, sem substituir o texto
  original; criados `docs/android-audit.md` e este diário. Nenhum fonte/CMake,
  patch, ignore ou workflow alterado.

**Comandos relevantes executados (não são instruções para repeti-los):**

```text
git status --short --branch; git remote -v; git branch -vv
git ls-remote origin refs/heads/main
git ls-remote https://github.com/theokyr/Project8Recomp refs/heads/main
git remote add upstream https://github.com/theokyr/Project8Recomp
git fetch --filter=blob:none --no-tags upstream main
git ls-tree -r --name-only upstream/main
git count-objects -vH
curl (API GitHub: árvore recursiva do commit para conferir tamanhos)
git reset --hard upstream/main
git branch --set-upstream-to=upstream/main main
git push --force-with-lease=refs/heads/main:50f8eb52984673f62b390a7cddc7a6a03223857c origin main:main
git rev-list --objects upstream/main --missing=print
git fetch --refetch --filter=blob:limit=1m --no-tags upstream main
git -c diff.renames=false log upstream/main --format= --name-only
git push --force-with-lease=refs/heads/main:50f8eb52984673f62b390a7cddc7a6a03223857c origin main:main
git ls-remote origin refs/heads/main
git switch -c android-port
rg / leituras cat e sed / git check-ignore -v (caminhos hipotéticos, sem criá-los)
gh api (execuções e jobs Actions do commit enviado)
gh run cancel 34133233674 --repo Cyberlym/Project8Recomp
```

**Falhas analisadas:** DNS e escrita em `.git` bloqueados pelo sandbox exigiram
execução com permissão. Primeiro push recusado por objetos históricos ausentes
no fetch parcial; `origin/main` não mudou nessa tentativa. Fetch com limite de
1 MiB por blob completou o histórico, sem objetos faltantes, e o mesmo lease
permitiu o segundo push. A árvore atual soma 3.127.006 bytes, maior arquivo
631.484 bytes; packs locais ficaram em cerca de 3,30 MiB após completar objetos.
Não houve clone pesado, SDK/submódulos, ISO, executável ou assets do jogo baixados.

**Desvio de escopo:** Actions herdada iniciou builds desktop da GUI automaticamente
no push obrigatório de `main`. Cancelamento recusado com HTTP 403 por falta de
permissão da integração. Linux/macOS já tinham concluído builds e Windows estava
compilando na consulta. Usuário informado com link da
[execução](https://github.com/Cyberlym/Project8Recomp/actions/runs/34133233674).
Consulta final: workflow concluído com sucesso nos três sistemas desktop.
Não houve build, configuração CMake, codegen ou execução do jogo no Codespace.

**Descobertas:** base ARM64 plausível pelo relato M1 e C++/SDK; falta integração
Android nesta árvore. Bloqueios principais: SDK/memória/exceções, processo e
lifecycle, layout CMake/codegen, plugin Vulkan e ABI NDK. Encontradas lacunas de
ignore para saídas aninhadas e risco de publicação concorrente no relógio do
patch. Detalhes e referências em [android-audit.md](android-audit.md).

**Validação final:** `tools/check_no_game_content.sh` passou (92 arquivos
publicáveis), `tools/check_patch_series.sh` passou (2 patches), `git diff --check`
passou. Hashes e árvores de `main`, `origin/main` e `upstream/main` iguais.
Verificação Python confirmou AGENTS original preservado byte a byte e links
locais dos dois documentos existentes. Hooks locais de commit inspecionados
(Git LFS). Fechamento em commit documental local com `git add` dos três arquivos,
`git diff --cached --check` e `git commit`, sem push de `android-port`.
Testes compilados não autorizados; builds do AGENTS original não executados
localmente nesta etapa. Nenhum teste Android foi realizado.

**Próximo passo:** inspeção estática limitada do SDK fixado para fechar P0, antes
de autorizar implementação ou compilação. Verificar gatilhos de CI antes de novos
pushes/PRs; não publicar `android-port` nem abrir PR automaticamente nesta etapa.
