# Agent guide

Read this before changing anything here.

## What this repository is

A static recompilation of Tony Hawk's Project 8 (Xbox 360), published as
checkpointed releases.

- **`src/`, `config/` and `patches/` are synchronised from the project's
  development tree.** Changes to them are applied there and arrive here in a
  release, so a pull request against those paths may be applied as a patch
  rather than merged as a commit. It will be credited either way. The README,
  `docs/`, `tools/`, `.github/`, and the licence files belong to this repository
  and are edited here directly.
- **This repo has exactly one goal:** make it easy for someone who owns a legal
  copy to end up with a ready-to-play build. It is not a research dump.

## The rule that overrides everything

**No game content is ever committed or published.** Not assets, audio, video,
textures, `default.xex`, disc images, decrypted intermediates, platform keys, or
the translation units the recompiler generates from a dump.

`tools/check_no_game_content.sh` enforces it against the file listing, not the
ignore rules. Run it before any commit that adds files. There is no allowlist,
and adding one is not a change to make casually.

If you are unsure whether something counts: it counts.

## Layout

```
src/launcher/    the GUI launcher. Links no SDK; builds anywhere, needs no dump.
src/identify/    disc identity + extraction. Links the SDK; does what the GUI must not.
src/game/        the game's host code. Needs the SDK and generated sources.
src/common/      shared between the identity worker and the game's dump gate.
config/          recompiler configuration: addresses, sizes, names.
patches/         patches against the SDK the port depends on.
tools/           staging, licence generation, and the release gates.
docs/            end-user and contributor documentation.
```

## Things that are the way they are on purpose

**The launcher links no part of the SDK.** That is what lets CI build it, lets
it build when the game does not, and keeps it from failing in the dynamic
linker. Disc identity is answered by a separate binary that does link the SDK.
Do not "simplify" this by merging them.

**One dump table, two gates.** `src/common/supported_dumps.h` is used by both
the launcher's identity check and the game's own startup gate. Never copy it;
two copies mean one of them is never tested.

**Settings render to argv by omission.** An unset setting emits no flag at all,
never `--flag=`. An empty value is consumed as the next argument and silently
shifts the whole command line. `src/launcher/tests/test_settings.cpp` holds this;
do not route around it.

**The dump gate has no override flag.** Now that the executable ships prebuilt,
that gate is the only remaining guarantee that a user's assets match the build
compiled against them.

**Generated sources are never committed.** They are a mechanical translation of
the user's own executable, regenerated locally from configuration that *is*
committed here. Addresses and table entries are facts about a binary; the binary
is not.

## Verification, and its limit

**Headless testing cannot verify this launcher's UI.** There is no window
manager in CI, so nothing delivers focus and synthetic input never reaches a
control. CI proves the RML/RCSS documents parse with zero diagnostics and that
code paths run. It cannot prove a user can reach them.

This is not a theoretical caveat. A previous fully-green headless pass shipped
three defects that a human with a controller found in minutes: every button
silently discarding its click, B doing nothing, and Play producing a black
screen from one missing flag.

So: **anything input-driven needs a human before it is called done.** Say
"implemented, unverified" and add it to `docs/KNOWN_ISSUES.md` rather than
implying a machine confirmed it. Do not fake a screenshot, and do not describe a
headless run as evidence that a control works.

**Windows is unverified in the same way, only more so.** A community report
confirms that v0.1.0 started on Windows 10, but the maintainers have not run a
current complete build on real Windows hardware. Never describe it as verified,
and never describe a compatibility-layer result as evidence about real
Windows.

## Comments

Explain why, not what, particularly when the obvious approach was tried and
failed. That history is the most valuable thing in a comment and the easiest to
lose.

**Do not cite documents that are not in this repository.** No plan
identifiers, milestone numbers or release-gate item numbers: none of them exist
here, so they read as nonsense. State the reasoning the citation stood for.

## Before committing

```sh
tools/check_no_game_content.sh
tools/check_patch_series.sh
cmake -S src/launcher -B build/launcher -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/launcher && ctest --test-dir build/launcher --output-on-failure
```

Touching a third-party dependency also means:

```sh
tools/make_notice.py --sdk /path/to/rexglue-sdk > NOTICE
```

Cutting a release also means:

```sh
tools/check_gpl_boundary.sh <every binary being shipped>
```

That last one exists because the SDK tree contains GPL binutils for the
recompiler, and no shipped binary may link it. It follows symlinks, because
staged runtimes usually are symlinks and a gate that silently skips its target
is worse than no gate.

## Tone in user-facing text

The launcher's player-facing failure copy avoids developer vocabulary: no
"sha256", "XEX", "shared memory segment", or exit codes. Say what is wrong in
terms of the disc and the game, then say what to do about it. Contributor docs
may use exact hashes and executable names when those facts are the task. The
failure copy in `src/launcher/src/launcher_app.cpp` is the reference.

## Android Port Rules

Estas regras adicionais regem o trabalho Android deste fork; as regras originais
acima permanecem preservadas. Instruções explícitas do usuário sobre escopo e
autorização prevalecem. Em conflitos, preserve a regra mais segura e restritiva.


### Evidência, escopo e comportamento protegido

Antes de qualquer mudança, leia somente os arquivos necessários e diferencie claramente FATO, HIPÓTESE e BLOCKER. Não trate hipótese como causa raiz, não aplique correção especulativa e feche a causa raiz com evidência sempre que possível. Sem evidência física ou de device, prefira instrumentação mínima a adivinhar um fix.

Cada sessão deve atacar somente o objetivo solicitado e preferir o menor diff possível. Não faça refactor amplo, limpeza não relacionada, modernização de arquitetura, alteração de nomes ou estilo por preferência, correções adjacentes não solicitadas ou exploração de subsistemas sem relação direta com o blocker.

Funcionalidade BUILD VERIFIED ou DEVICE VERIFIED é protegida. Não a reabra, reescreva ou “melhore” sem evidência direta de relação com o blocker atual. Antes de editar essa área, prove a necessidade e explique o risco de regressão; pare e peça autorização se a mudança não for obviamente pequena e determinística. Uma correção nunca deve introduzir regressão em outro caminho validado.

Mantenha correções deste port no Android quando possível e preserve Linux desktop, Windows e macOS. Não mude comportamento multiplataforma sem necessidade comprovada.

### Lifecycle, ownership e Vulkan

Lifecycle Android é sensível. Sem causa raiz comprovada e análise explícita de regressão, não altere SDL generation management, generation-safe handoff, recriação de `SDLActivity`, reaper, join/thread teardown, `nativeSendQuit`, `nativeQuit`, o caminho `SDL_EVENT_QUIT` para `QuitFromUIThread`, ou ownership validado de `ANativeWindow`. Nunca substitua solução segura por bloquear eventos, ignorar erros, matar processo, force-stop ou join indefinido na UI thread.

O ownership de `ANativeWindow` deve permanecer explícito e balanceado. Se o wrapper Android tem referência independente, acquire/release devem ser perfeitamente pareados; não armazene ponteiro emprestado de lifetime implícito do SDL nem enfraqueça ownership para “resolver” erro Vulkan.

Vulkan deve preservar um caminho principal, uma instance e um presenter, sem framebuffer readback, cópias extras por frame, sincronização extra, alocações por frame ou recriação desnecessária de surface/swapchain. Nunca masque `VkResult`, ignore erro Vulkan, crie segunda instance/presenter ou recrie Vulkan inteiro para ocultar problema de lifecycle. Diferencie resize, recriação de swapchain, surface loss, native window replacement e Activity recreation; resize recuperável não prova perda definitiva de surface.

### ReXGlue e patches

ReXGlue está pinado: nunca use `latest`, fork aleatório, outra revisão “para testar” ou atualize o pin sem autorização explícita. Use o checkout/cache existente quando disponível; o checkout conhecido é `/workspaces/.project8-build-cache/rexglue-v0.10.0`, utilizável para leitura e build incremental. Correções persistentes e reproduzíveis devem ser representadas pelos patches versionados, não apenas pelo cache ou working tree.

Correções em ReXGlue pertencem a `patches/rexglue-sdk/`. Não reescreva patch antigo validado só para encaixar correção: prefira novo patch sequencial (`0017`, `0018`, etc.). Não faça commits em submodules. Antes de considerar patch pronto, inspecione `git diff`, rode `git diff --check`, valide a série, confirme aplicação sobre o pin certo e confirme que nenhum pin de submodule mudou sem intenção.

### Eficiência, builds e verificação

Minimize custo de Codex, CPU, I/O e tempo: use `rg` ou `git grep`, intervalos pequenos e pesquisa dirigida; não leia o repositório inteiro quando poucas funções bastam, nem repita comando ou análise que já respondeu à pergunta. Antes de trabalhar, leia somente a parte relevante de `docs/android-progress.md` e reutilize evidência já registrada. Por padrão, não use web quando o código local basta, não faça downloads, regeneração de SDK/ReXGlue/SDL, recriação de build directory ou clean build sem necessidade. Nunca repita build sem analisar antes seu erro. Reutilize caches e builds, procure alternativa incremental antes de operação cara e, quando uma compilação C++ grande for autorizada, use Ninja `-j2` inicialmente.

Não execute build grande, clean build, grandes downloads, clone pesado, codegen ou operação demorada sem autorização explícita: informe exatamente a operação e espere permissão. Depois de corrigir, mostre diff, rode verificações estáticas e valide patches; se a autorização de build for exigida, pare antes dele. Autorizado o build, faça só o incremental necessário, sem clean, rebuild completo, redownload de toolchain ou reconstrução desnecessária de dependências.

BUILD VERIFIED não é DEVICE VERIFIED. Lifecycle, Vulkan surface, swapchain, input, rendering, runtime e performance permanecem DEVICE TEST REQUIRED até teste físico; DEVICE VERIFIED exige evidência física adequada.

### Roadmap, conteúdo e Git

Respeite o roadmap: não avance conteúdo do jogo, ISO, XEX, codegen ou runtime guest antes do Stage correspondente e não use dados do jogo como atalho para infraestrutura validável sem eles. `Runtime::Setup`, VM guest, fault e signal handling não podem ser ativados antecipadamente sem autorização. Não implemente UI bonita, Compose, Turnip ou otimizações antes de o runtime básico Android funcionar.

O projeto aceita somente conteúdo legal do usuário. Além da regra global, nunca adicione ISO, `default.xex`, assets extraídos, conteúdo protegido, chaves ou dados semelhantes ao Git. No Stage adequado, receba conteúdo legalmente possuído em runtime ou fluxo definido; mantenha-o local, privado e ignorado, confirme o ignore do caminho e nunca use `git add -f` para contorná-lo.

Antes e depois de mudanças, verifique `git status`; não sobrescreva trabalho não inspecionado. Nunca use `git clean -xfd`, `git reset --hard`, force push ou remoção grande sem autorização. Não faça push sem autorização, mantenha commits locais até ela e não misture mudanças não relacionadas em um commit. Antes de push ou PR, inspecione gatilhos de CI; se puder disparar build, testes, packaging, codegen ou outro workflow caro, informe quais workflows e peça autorização antes do push. Se CI caro iniciar sem autorização, tente cancelá-lo e registre em `docs/android-progress.md`.

Mantenha `origin` em `https://github.com/Cyberlym/Project8Recomp` e `upstream` em `https://github.com/theokyr/Project8Recomp`; `main` permanece limpo seguindo `upstream/main`, e trabalho Android pertence a `android-port`.

### Estado dinâmico e sessões futuras

Estado momentâneo pertence a `docs/android-progress.md`: Stage, BUILD/DEVICE VERIFIED, blockers, testes físicos, hashes de APK, commits relevantes, decisões temporárias e próximo passo. Mantenha ali o diário técnico curto; conclusões de auditoria pertencem a `docs/android-audit.md`, não a este guia.

Toda sessão futura deve ler este `AGENTS.md`, a parte relevante de `docs/android-progress.md`, identificar o objetivo e reutilizar evidência anterior, sem reiniciar investigação concluída, no menor escopo e custo possíveis. As regras valem até exceção explicitamente autorizada pelo usuário.

Ao terminar cada etapa, informe arquivos alterados, comandos executados, descobertas, riscos e próximo passo recomendado.
