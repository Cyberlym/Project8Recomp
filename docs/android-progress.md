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

## 2026-09-07 — Etapa 2: aprofundamento arquitetural

**Escopo executado:** leitura estática dirigida dos P0 da Etapa 1. Foram lidos
arquivos pontuais do SDK v0.10.0 no GitHub, no commit
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, sem clone/submódulos. A árvore remota
indicava cerca de 164 MB/1.301 blobs, por isso não foi baixada. Um `git ls-remote`
direto falhou por DNS do sandbox; a leitura prosseguiu pelo conector GitHub já
disponível, limitada a texto. Também foram consultadas páginas oficiais SDL3,
Android/NDK e upstream RmlUi.

**Comandos locais:** `git status`, `rg`, `find`, `sed`, `cat` e `tail`, somente
para inventário e leitura. Não houve configuração CMake, build, teste compilado,
codegen, aplicação de patch, execução do jogo, download grande ou push.

**Arquivos alterados:** apenas `docs/android-audit.md` e este diário.

**Descobertas:** o SDK tem Vulkan Android no presenter e caminhos POSIX/ARM64
parciais, mas falta a classe de surface Android, entrypoint de biblioteca e as
implementações das pontes Android; o CMake ainda força X11/Wayland. O runtime
Android não depende de `/dev/shm`: em API 26+ ele pretende usar
`ASharedMemory_create`; `/dev/shm` pertence ao supervisor desktop. Memória fixa,
aliases e recuperação de faults ARM64 continuam como bloqueador que só um probe
em aparelho poderá fechar. SDLActivity é o melhor encaixe com a arquitetura SDL
existente. O layout público de codegen foi determinado e a divergência
`src/config` versus `config` confirmada.

**Riscos:** Android também define `__linux__`, ativando hoje supervisor,
`prctl`/signals e hooks que não foram validados em ELF/AArch64. `ucontext_t`, W^X,
endereços virtuais fixos e páginas de 16 KiB precisam de validação futura.
Arquivos privados gerados continuam ausentes e devem permanecer ignorados.

**Próximo passo recomendado:** na Etapa 3, primeiro corrigir apenas o caminho
CMake do helper gerado e os ignores correspondentes. Depois separar Android de
GNU Linux no CMake do SDK, antes de implementar SDLActivity/surface. Qualquer
compilação continua sujeita à autorização explícita e começa com `-j2`.

## 2026-09-07 — Etapa 3: primeiro esqueleto Android

**Arquivos alterados/criados:**

- `src/game/CMakeLists.txt`: o helper agora é incluído de
  `../../config/generated/rexglue.cmake`, que corresponde ao diretório gerado ao
  lado de `config/thps_p8_manifest.toml`; o target desktop `thps_p8_launch` não é
  criado quando o toolchain define `ANDROID`.
- `.gitignore`: `config/generated/` e `src/config/generated/` ficaram ignorados
  explicitamente, cobrindo o helper e evitando que uma geração futura crie
  estado rastreável por engano.
- `src/game/src/launcher/platform.h`: Android seleciona o backend genérico em
  vez do backend Linux que varre `/proc` e `/dev/shm`. O supervisor não é um
  caminho Android nesta etapa.
- `src/game/src/thps_p8_app.h`: `prctl`, handlers SIGTERM/SIGINT e a dependência
  de sinais foram limitados a Linux desktop; Android não herda esse lifecycle.
- `android/CMakeLists.txt`: esqueleto isolado que exige NDK, `arm64-v8a`, API 26,
  SDL3 e Vulkan, sem declarar target do jogo ou fontes geradas.
- `android/README.md`: contrato e limites do esqueleto, incluindo o probe futuro.

**Decisões:** a menor correção do layout é ajustar o include existente; não foi
criada nenhuma pasta `generated`. A separação Android/Linux foi feita somente nos
pontos identificados: supervisor e hooks específicos, sem tocar no POSIX comum,
no runtime ou no backend Vulkan. O esqueleto Android permanece uma interface
vazia para impedir que uma configuração acidental dispare codegen ou carregue
conteúdo do jogo.

**Itens deliberadamente não implementados:** Activity/SDLActivity, JNI,
`ANativeWindow`, `VkSurfaceKHR`, runtime, launcher Android, leitor XISO/XDVDFS,
SAF, memória virtual fixa, W^X, páginas de 16 KiB, fault recovery, signals
complexos, touch, Turnip, AdrenoTools, Compose, assets e translation units.

**Validação:** revisão de `git diff` e `git diff --check` após o grupo inicial;
nenhuma configuração CMake foi executada. Não houve build, teste compilado,
codegen, download grande, ISO, `default.xex`, conteúdo do jogo, limpeza ou push.

**Blockers restantes:** o SDK ainda precisa de uma branch CMake Android própria,
as pontes `AndroidNativeWindowSurface`/JNI e um entrypoint SDLActivity; o target
do jogo continua dependente dos arquivos privados gerados. Memória/aliases,
W^X, faults ARM64 e lifecycle só devem ser investigados em probe autorizado no
Moto G34.

**Próximo probe exato:** uma aplicação SDL3 nativa mínima, ligada ao contrato de
`android/CMakeLists.txt`, que crie uma `SDL_Window`, obtenha o `ANativeWindow`
Android, crie uma `VkInstance` com `VK_KHR_android_surface`, chame
`vkCreateAndroidSurfaceKHR`, registre resize/pause/resume e destrua a surface
corretamente. Ela não deve carregar ReXGlue, runtime, XISO ou arquivos do jogo.
