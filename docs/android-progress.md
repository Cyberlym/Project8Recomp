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

## 2026-09-07 — Etapa 4: probe SDL3 + Vulkan Android

**Dependências instaladas fora do repositório:** SDK command-line tools 19.0,
Platform API 35, Build Tools 35.0.0, NDK 27.2.12479018 (r27c) e Ninja 1.13.2.
O pacote oficial SDL3 3.2.16 Android AAR foi mantido em `/tmp`; não foi copiado
nem commitado. Não foram instalados emulator, system images, Android Studio ou
platform-tools.

**Arquivos do probe:** `android/probe/probe.cpp` inicializa SDL3 e Vulkan, cria e
recria a surface Android por `SDL_Vulkan_CreateSurface`, enumera GPUs/extensões
e grava checkpoints no logcat e em `probe.log`. `ProbeActivity.java` herda a
`org.libsdl.app.SDLActivity` oficial. O manifesto usa o package
`com.cyberlym.project8probe`. O CMake liga somente SDL3, Vulkan, `log` e `android`.
`build-apk.sh` configura Ninja/NDK, compila `libmain.so`, empacota classes em JAR
para D8, gera o APK, alinha e assina com chave de debug local.

**Erros encontrados e correções:**

1. Ninja ausente: instalado somente o pacote `ninja` e restaurado `-G Ninja`.
2. Macros Vulkan Android ausentes: definido `VK_USE_PLATFORM_ANDROID_KHR` antes
   de `vulkan.h`.
3. D8 recusou diretório: classes Java passaram a ser empacotadas em
   `probe-classes.jar`.
4. `aapt2` exigiu `package`: manifesto recebeu
   `com.cyberlym.project8probe`.
5. `apksigner` recusou manifesto protobuf: removido `--proto-format`.

**Build:** a quinta tentativa autorizada compilou/linkou novamente e o build
incremental final encontrou `ninja: no work to do`; D8, aapt2, zipalign e
apksigner concluíram. O APK é
`android/Project8VulkanProbe-arm64-v8a.apk`, com 3.371.623 bytes, package
`com.cyberlym.project8probe`. A assinatura verifica v1, v2 e v3 com um signer.
O `.apk.idsig` auxiliar fica ignorado.

**Logging e recuperação:** `SDL_GetPrefPath` usa o armazenamento interno privado
retornado por `SDL_GetAndroidInternalStoragePath`; o arquivo lógico é
`files/probe.log` (na prática, sob o diretório privado do package). Cada linha é
enviada a SDL/logcat e imediatamente flushed no arquivo; erros incluem o último
checkpoint. Sem ADB, o arquivo pode ser visualizado pelo gerenciador de arquivos
do Moto G34 em armazenamento interno privado somente se o app/gerenciador
permitir acesso a `Android/data`; caso contrário, a próxima etapa deve adicionar
um botão mínimo de compartilhamento/exportação, sem pedir armazenamento amplo.

**Checkpoints esperados no Moto G34:** `ANDROID_ENTRY`, `SDL_INIT_OK`,
`SDL_WINDOW_OK`, `VULKAN_INSTANCE_OK`, `ANDROID_SURFACE_OK`, `GPU_ENUM_OK` e
`PROBE_READY`, além de `GPU name=`, `vendor_id=`, `device_id=`, `api=`, `driver=`
e linhas `GPU_EXTENSION`. Ao pausar/minimizar, esperar
`ANDROID_SURFACE_DESTROYED`; ao retornar, `ANDROID_SURFACE_RECREATED result=ok`.

**Observação de empacotamento:** a inspeção `aapt dump badging` do APK manual
mostrou permissões implícitas históricas porque o manifesto não contém uma tag
`uses-sdk`; isso é um ponto a corrigir no próximo ciclo de empacotamento para
declarar explicitamente minSdk 26/targetSdk 35. Nenhum novo build foi iniciado
após essa observação, conforme o limite desta etapa.

## 2026-09-07 — Diagnóstico de compatibilidade do APK

A inspeção estática confirmou que o APK anterior não declarava `uses-sdk`:
`aapt dump badging` não mostrava min/target e reportava permissões históricas
implícitas. O manifesto também exigia `android.hardware.vulkan.version`, uma
restrição de instalação desnecessária para este probe, que já valida Vulkan em
tempo de execução. A menor correção foi declarar explicitamente
`minSdkVersion=26` e `targetSdkVersion=35` e remover essa feature obrigatória;
nenhum código nativo foi alterado por essa correção.

O reempacotamento incremental reutilizou `/tmp/project8-android-probe-build-ninja`
e concluiu com Ninja, D8, aapt2, zipalign e apksigner. O APK final reporta
package `com.cyberlym.project8probe`, compile SDK 35, min SDK 26, target SDK 35,
somente ABI `arm64-v8a`, Activity exportada correta e assinatura v2/v3 válida.
Não há permissões explícitas nem feature Vulkan obrigatória no manifesto.
`aapt2 dump xmltree` desta versão requer sintaxe de arquivo compilado diferente;
o manifesto final foi validado pelo `aapt dump xmltree`/`aapt dump badging` e pela
listagem ZIP. O tamanho final é 3.371.623 bytes.

**Não implementado:** ReXGlue, runtime, game code, codegen, arquivos do jogo,
launcher, touch customizado, Turnip/AdrenoTools, lifecycle do runtime, exportação
do log e testes no Moto G34. Nenhum push foi feito.

## 2026-09-07 — Diagnóstico visível no aparelho

O APK instalou no Moto G34, mas a tela preta era consequência de o probe não
renderizar interface. Foi adicionada uma sobreposição Android mínima sobre a
SDLActivity, sem Compose: estados dos sete checkpoints, GPU, IDs, versões Vulkan
e status da surface são atualizados por JNI a cada 500 ms. O nativo continua
gravando `probe.log` com flush imediato; `EXPORT LOG` usa
`ACTION_CREATE_DOCUMENT` e copia o log real para a pasta escolhida, enquanto
`COPY REPORT` envia o resumo para a área de transferência.

## 2026-09-07 — Auditoria de integridade do Vulkan Probe

| checkpoint | função/API real | condição de sucesso | condição de falha | evidência encontrada | status de validação |
|---|---|---|---|---|---|
| `ANDROID_ENTRY` | `main()` → `RunProbe()` | `RunProbe` foi alcançado pelo entrypoint nativo | não há checkpoint se o processo não entrar no `main` | `Checkpoint` é a primeira operação de probe | validado estaticamente |
| `SDL_INIT_OK` | `SDL_Init(SDL_INIT_VIDEO \| SDL_INIT_EVENTS)` | retorno diferente de zero | retorno zero; erro de SDL é registrado | checkpoint imediatamente após o teste | validado estaticamente |
| `SDL_WINDOW_OK` | `SDL_CreateWindow` | ponteiro `SDL_Window*` não nulo | ponteiro nulo; erro de SDL é registrado | checkpoint imediatamente após o teste | validado estaticamente |
| `VULKAN_INSTANCE_OK` | `vkCreateInstance` | `VkResult == VK_SUCCESS` e `VkInstance != VK_NULL_HANDLE` | resultado diferente de sucesso ou handle nulo | `VK_INSTANCE_CREATE` registra resultado/validade antes do checkpoint | validado estaticamente |
| `ANDROID_SURFACE_OK` | `SDL_Vulkan_CreateSurface` | retorno SDL bem-sucedido e `VkSurfaceKHR != VK_NULL_HANDLE` | retorno SDL falso ou handle nulo | `ANDROID_SURFACE_CREATE` registra sucesso/validade; surface é testada antes do checkpoint | validado estaticamente |
| `GPU_ENUM_OK` | duas chamadas a `vkEnumeratePhysicalDevices` | primeira e segunda `VK_SUCCESS`, contagem > 0 e todo handle obtido não nulo; propriedades vêm de `vkGetPhysicalDeviceProperties` | resultado diferente de sucesso, contagem zero, contagem inconsistente ou handle nulo | relatório/log registram ambos os resultados e contagens; IDs/nome/versões vêm de `VkPhysicalDeviceProperties` | validado estaticamente |
| `PROBE_READY` | sequência `CreateSurface` + `EnumerateGpus` | ambas retornam verdadeiro após todos os checkpoints anteriores | qualquer etapa retorna falso e o probe encerra sem emitir o checkpoint | emissão ocorre somente no `if` que testa as duas funções | validado estaticamente |

Não foram encontradas constantes ou strings específicas de Adreno 619, vendor
20803/`0x5143` ou device 100731137 no probe. O nome, IDs, `apiVersion` e
`driverVersion` exibidos são lidos em runtime de `vkGetPhysicalDeviceProperties`;
o relatório também mostra os valores originais e decodificados. A criação de
surface usa a API SDL, que retorna sucesso/erro booleano e entrega o handle;
o probe não inventa um `VkResult` que essa API não fornece.

**Status:** a integridade lógica dos sete checkpoints está validada estaticamente
após a correção dos testes de handles e enumeração. A execução física atual no
Moto G34 continua sendo evidência relatada nos registros da Etapa 4, não foi
reproduzida nesta sessão.

## 2026-09-07 — Etapa 5: blocker no núcleo ReXGlue

**Fato:** a árvore pública fixa o ReXGlue SDK `v0.10.0` e os patches listados em
`patches/rexglue-sdk/series`, mas não contém o checkout/prefix do SDK nem fontes
geradas. O mecanismo documentado exige aplicar os patches a um checkout externo
e construir o SDK.

**Dependência exata:** o único target ReXGlue do projeto, `thps_p8`, inclui
`generated/default/thps_p8_init.h`, inclui `config/generated/rexglue.cmake` e
chama `rexglue_setup_target(thps_p8)`. O primeiro arquivo é produzido por
`rexglue codegen` a partir de `default.xex`; o segundo e as bibliotecas vêm do
SDK configurado. Sem esses artefatos, não há target independente observável para
linkar ou inicializar no Android.

**Decisão:** a Etapa 5A está bloqueada antes da Fase 5B/5C. Nenhum stub,
substituição de API ou runtime presumido foi criado; não houve build, codegen,
download, uso de ISO/default.xex ou alteração de código Android.

## 2026-09-07 — Etapa 5A: SDK v0.10.0 verificado

**Versão e patches:** foi obtido temporariamente, fora do repositório, o
checkout oficial `rexglue/rexglue-sdk` na tag `v0.10.0`, commit
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`. O checkout iniciou limpo; os dois
patches de `patches/rexglue-sdk/series` passaram `git apply --check --index` e
foram aplicados na ordem `0001-thp8-runtime-stack.patch`,
`0002-macos-update-moltenvk-1.4.2.patch`. O resultado tem somente as 61
alterações indexadas desses patches, sem diferenças não indexadas.

**Targets observados:** o SDK configura `rexcore` (OBJECT),
`rexfilesystem`/`rexui`/`rexinput`/`rexaudio` (OBJECT), `rexruntime` (SHARED),
`rexgpu-xenos` (SHARED), `rexcodegen` (STATIC) e o executável `rexglue`, além
dos targets de terceiros. `rexruntime` liga os objetos de core, filesystem, UI,
input e áudio, SDL3, fmt, spdlog, tomlplusplus, simde, VMA e headers Vulkan.

**Generated code:** o template emitido por `rexglue_cmake.inja` encontra o SDK
independentemente, inclui `sources.cmake` somente se codegen já existir, mas
`rexglue_setup_target(thps_p8)` sempre adiciona a dependência
`thps_p8_codegen`, liga `rex::runtime` e cria `thps_p8_recomp` quando existem as
fontes geradas. No Project8Recomp, `main.cpp` inclui diretamente
`generated/default/thps_p8_init.h`; portanto o target do jogo continua
obrigatoriamente dependente de generated code, embora `rexruntime` em si não
inclua esse header.

**Android/ARM64 observado no SDK:** `rex/platform.h` reconhece
`__ANDROID__`/`__aarch64__`; `memory_posix.cpp` usa `ASharedMemory_create` via
`dlopen`, `mmap`, `mprotect` e `MAP_FIXED_NOREPLACE`/`MAP_FIXED`; threading POSIX
tem condicionais Android; o presenter Vulkan chama realmente
`vkCreateAndroidSurfaceKHR` quando recebe `AndroidNativeWindowSurface`; e há
funções Vulkan Android declaradas. Não há, porém, `surface_android.h/.cpp` no
checkout.

**Blocker real:** `src/ui/CMakeLists.txt` seleciona `surface_gnulinux.cpp` para
todo `else()` não-Windows/macOS e exige `pkg-config`, X11-XCB e Wayland em
qualquer `UNIX`. Isso inclui Android. O SDK também tem `seh_posix.cpp` e outros
caminhos POSIX que precisam de validação Bionic; a presença de condicionais
Android não prova que o runtime inteiro compila ou inicializa no aparelho.

**Resultado:** `ETAPA 5B: NO-GO`. Existe um núcleo conceitual real (`rexruntime`
e seus targets OBJECT), mas não existe ainda um subconjunto Android suportado e
linkável no grafo atual: UI/surface e dependências desktop precisam de uma
decisão e correção arquitetural. Nenhum build pesado, configuração CMake,
alteração do SDK, codegen ou fonte do projeto foi executado nesta auditoria.

## 2026-09-07 — Etapa 5B: decisão de surface/UI Android

**Decisão arquitetural:** adicionar ao SDK um backend real
`AndroidNativeWindowSurface` (`surface_android.h/.cpp`) e selecioná-lo em
`WindowSDL::CreateSurfaceImpl` via `REX_PLATFORM_ANDROID`. O objeto deve tomar
emprestados a `SDL_Window*` e a `ANativeWindow*` obtida da propriedade SDL
`SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER`; o tamanho continua vindo de
`SDL_GetWindowSizeInPixels`. Não deve possuir nem criar `VkInstance` ou
`VkSurfaceKHR`.

**Evidência de ownership:** `WindowSDL` cria e destrói a `SDL_Window` e registra
seus eventos; `Window` possui o `Surface` em `presenter_surface_`; `Presenter`
mantém somente ponteiros emprestados para `Window`/`Surface`. O
`VulkanProvider` cria internamente `VulkanInstance`; `VulkanPresenter` já aceita
`kTypeIndex_AndroidNativeWindow`, chama `vkCreateAndroidSurfaceKHR` e possui a
`VkSurfaceKHR`, destruindo swapchain e surface com `vkDestroySurfaceKHR` ao
desconectar. Logo, receber uma `VkInstance` ou `VkSurfaceKHR` externa contrariaria
o fluxo atual e exigiria novas APIs de ownership.

**Lifecycle:** antes de SDL destruir ou substituir o `ANativeWindow`, Android
deve chamar `Window::OnSurfaceChanged(false)`, que desconecta o presenter e
destrói a swapchain/`VkSurfaceKHR`; quando uma nova native window válida existir,
deve chamar `OnSurfaceChanged(true)`. Resize em pixels já percorre
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` → `OnActualSizeUpdate` →
`Presenter::OnSurfaceResizeFromUIThread`. O mapeamento exato dos eventos SDL de
background/foreground para perda e retorno da native window ainda precisa ser
confirmado no aparelho; o código atual trata minimized/restored apenas como
notificações e não recria a surface.

**CMake mínimo:** em `src/ui/CMakeLists.txt`, selecionar `surface_android.cpp`
antes do ramo GNU/Linux e limitar `pkg-config`, X11-XCB e Wayland a
`UNIX AND NOT APPLE AND NOT ANDROID`. Em `window_sdl.cpp`, selecionar
`surface_android.h` antes do fallback GNU/Linux. Windows, macOS e GNU/Linux
mantêm seus backends atuais.

**Opções comparadas:** reutilizar uma janela/surface externa da Activity exigiria
injeção em `Window`, `Presenter` e `VulkanProvider`, além de definir ownership de
handles que hoje é interno. O backend Android acompanha a abstração já usada por
Win32, Wayland/XCB e macOS, reutiliza diretamente o caminho Vulkan Android já
presente e confina a mudança à seleção de plataforma, ao wrapper de native
window e ao lifecycle Android. Turnip/AdrenoTools continuam sendo uma decisão do
loader/device Vulkan e não exigem mudar essa abstração de surface; isso é uma
inferência arquitetural, ainda não uma compatibilidade validada.

**Critério para implementação:** compilar sem X11/Wayland, obter
`ANativeWindow*` não nula da `SDL_Window`, reportar tamanho real, criar e destruir
a `VkSurfaceKHR` somente pelo presenter, e comprovar em aparelho perda/recriação
durante pause/resume e orientation sem use-after-free. Rollback consiste em
remover o novo backend e os ramos `ANDROID`; nenhum contrato desktop precisa ser
alterado.

## 2026-09-07 — Etapa 5B.1: backend Android mínimo

Foi adicionada à série do SDK a patch
`0003-android-native-window-surface.patch`. Ela introduz
`AndroidNativeWindowSurface`, que implementa a interface `Surface`, toma
emprestadas a `SDL_Window*` e a `ANativeWindow*` e obtém dimensões físicas por
`SDL_GetWindowSizeInPixels`. `WindowSDL::CreateSurfaceImpl` só cria o wrapper
quando `SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER` retorna um ponteiro não nulo.

No CMake de `rexui`, `ANDROID` seleciona apenas `surface_android.cpp`; a busca e
o link de X11-XCB/Wayland ficam restritos a Unix desktop por
`UNIX AND NOT APPLE AND NOT ANDROID`. Os ramos Windows, macOS e GNU/Linux não
tiveram seus backends alterados.

**Validação:** a série completa aplicou sem conflito, na ordem documentada,
sobre o commit oficial `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` da tag
`v0.10.0`, e passou em `git diff --check`. `surface_android.cpp` passou em
compilação sintática isolada com o Clang AArch64 Android API 26 do NDK
27.2.12479018 e os headers SDL3 já existentes. O SDK completo não foi
configurado ou compilado: seus submódulos continuam ausentes e esta subetapa não
autoriza downloads. Portanto ainda não está comprovado que `window_sdl.cpp` ou
`rexui` completo linkam no Android.

**Limite desta subetapa:** nenhum APK ou runtime foi produzido. Obter e exibir a
`ANativeWindow*` real e o tamanho físico pertence à 5B.2; criação Vulkan pelo
presenter pertence à 5B.3; destruição/recriação em lifecycle físico pertence à
5B.4.

## 2026-09-07 — Etapa 5B.2: integração no probe para teste físico

`AndroidNativeWindowSurface::Create(SDL_Window*)` passou a concentrar o caminho
usado por `WindowSDL::CreateSurfaceImpl`: rejeita janela nula, consulta
`SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER` por `SDL_GetWindowProperties` e rejeita
`ANativeWindow*` nula. O wrapper mantém somente referências emprestadas. O
tamanho continua vindo de `SDL_GetWindowSizeInPixels` e falha para retorno falso
ou dimensão não positiva.

O probe existente compila diretamente `surface_android.cpp` do checkout oficial
v0.10.0 com os patches 0001, 0002 e 0003. Após `SDL_CreateWindow`, ele chama a
mesma função `Create` usada por `WindowSDL` e só então reporta
`SDL_WINDOW_REAL: OK`, `ANDROID_NATIVE_WINDOW: OK (pointer=...)` e
`SURFACE_PIXEL_SIZE: largura x altura`. Ponteiro nulo ou tamanho inválido são
falhas terminais e não geram `OK`.

**Validação estática/build:** a série reaplicou limpa sobre
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; o build incremental Android ARM64
com Ninja `-j2` compilou o wrapper e o probe e gerou APK assinado. O símbolo de
`AndroidNativeWindowSurface::Create` e o de `GetSizeImpl` estão em `libmain.so`;
o hash da biblioteca dentro do APK é idêntico ao artefato do build. Os valores
de ponteiro e tamanho ainda não são fatos até execução física no Moto G34.

**Etapa 5B.3 bloqueada nesta sessão:** o caminho Android já existente do
`VulkanPresenter` chama `vkCreateAndroidSurfaceKHR`, mas compilar e executar esse
arquivo exige o grafo completo de `rexui`. Os submódulos oficiais necessários
estão ausentes no checkout local, e downloads não foram autorizados. O probe
continua usando seu caminho Vulkan comprovado e não foi apresentado como teste
do presenter. Nenhuma segunda `VkInstance`, duplicação do presenter ou mudança
de ownership foi adicionada. Pelo mesmo motivo, `tools/make_notice.py` não pôde
revalidar o `NOTICE`: os textos de licença que ele consulta ficam nesses
submódulos; o arquivo existente não foi alterado.

## 2026-09-07 — Etapa 5B.3: grafo real e blocker de dependências

**Fato físico incorporado:** a execução do APK 5B.2 no Moto G34 confirmou
`SDL_WINDOW_REAL: OK`, `ANDROID_NATIVE_WINDOW: OK`, ponteiro não nulo e
`SURFACE_PIXEL_SIZE: 720 x 1600`. Isso comprova o wrapper e a aquisição via
SDL3, mas não executa o `VulkanPresenter`.

**Grafo oficial observado:** `rexui` é um único target OBJECT. Ele sempre agrega
as fontes core de UI, overlays e backend de plataforma; com
`REXGLUE_USE_VULKAN`, agrega também todas as fontes Vulkan, incluindo
`vulkan_provider.cpp`, `vulkan_presenter.cpp`, device, instance, samplers,
immediate drawer, VMA e utilitários. Não existe opção oficial para selecionar
somente provider/presenter.

Para esse target, `rexui` liga `rexcore`, ImGui, SDL3, UTF8-CPP, Vulkan-Headers,
Vulkan Memory Allocator e headers SPIR-V. `rexcore` acrescenta fmt, SIMDe,
toml++, spdlog, xxHash, UTF8-CPP e headers CLI11. O conjunto mínimo indicado
pelo grafo contém 14 submódulos: `cli11`, `tomlplusplus`, `simde`, `xxHash`,
`spdlog`, `fmt`, `utfcpp`, `imgui`, `sdl3`, `vulkan-headers`,
`vulkan-memory-allocator`, `spirv-headers`, `spirv-tools` e `glslang`. Tracy e
FidelityFX podem ser desligados por opções oficiais.

O CMake top-level ainda exige incondicionalmente `libmspack` e `FFmpeg` na
verificação de submódulos e adiciona todos os diretórios do SDK, embora esses
dois não sejam dependências do caminho de apresentação. Assim, usar o target
oficial sem mudança de build graph exigiria inicializar um conjunto ainda mais
amplo. Criar um target paralelo selecionando fontes manualmente deixaria de
provar o target `rexui` real e introduziria uma arquitetura de build nova.

**Resultado:** `5B.3 BUILD: FAIL / NOT RUN`. Nenhum submódulo foi baixado e
nenhum código foi alterado. O caminho estático
`AndroidNativeWindowSurface` → `VulkanPresenter` →
`vkCreateAndroidSurfaceKHR` continua presente, mas não foi compilado, executado
ou promovido a checkpoint. É necessária uma decisão explícita entre autorizar o
conjunto amplo de submódulos do build oficial ou autorizar uma mudança focada no
grafo CMake upstream para tornar `rexui` configurável isoladamente.

### Classificação detalhada do grafo 5B.3

O SDK não possui opção oficial de componentes nem suporta adicionar diretamente
`src/ui`: esse CMake pressupõe funções, variáveis e targets criados pelo
top-level e por `thirdparty`. O target `rexui` também não é granular: compila UI
core, overlays e todas as fontes Vulkan. `VulkanProvider::Create(true/false,
true)` exige instance, device e `UISamplers`; `CreatePresenter` instancia o
`VulkanPresenter`, cuja inicialização surface-independent cria recursos Vulkan
antes de a conexão com `Window` chegar à criação da surface.

**REQUIRED_FOR_5B3 pelo target atual:** SDL3; Vulkan-Headers; Vulkan Memory
Allocator; ImGui; UTF8-CPP; fmt; spdlog; SIMDe; toml++; xxHash; CLI11; e os
headers de SPIRV-Tools/SPIRV-Headers. `renderdoc`, `stb` e `disruptorplus` também
são consumidos, mas já estão presentes como fontes locais, não submódulos.

**CONFIGURATION_ONLY:** `libmspack`, FFmpeg, glslang e o target o1heap são
configurados pelo top-level/`thirdparty`, mas não pertencem à cadeia de link de
`rexui` + `rexcore`. O codegen e seus consumidores, incluindo inja, são
**UNRELATED_TO_5B3**. Tracy é **UNRELATED_TO_5B3** e pode ser desligado pela
opção oficial; FidelityFX e testes já são opt-in e permanecem desligados. O
Vulkan Loader e MoltenVK são condicionais Apple e **UNRELATED_TO_5B3** no
Android.

Uma refatoração pequena apenas na lista `REQUIRED_SUBMODULES` não resolve: o
top-level ainda adiciona todos os subdiretórios, e os targets OBJECT monolíticos
fazem `rexcore` compilar inclusive memória, exception/SEH e fontes POSIX que não
são necessárias para provar a surface. Configurar `src/ui` isoladamente exigiria
recriar manualmente seus targets transitivos, deixando de usar o grafo oficial.
Separar provider/presenter de UI e separar o núcleo mínimo de `rexcore` seria
uma mudança de arquitetura CMake maior que esta etapa autoriza.

Há ainda dois condicionais Android incorretos no build oficial: o top-level
classifica todo `UNIX AND NOT APPLE` como `REX_PLATFORM_LINUX`, e o CMake
vendorizado do SDL habilita X11, Wayland e áudio Linux para todo `UNIX AND NOT
APPLE`. `platform.h` reconhece `__ANDROID__`, mas isso não corrige as decisões
CMake. Esses pontos exigiriam correções Android específicas mesmo após obter as
dependências.

**Decisão pendente:** o menor caminho que preserva os targets reais ainda baixa
ao menos 13 submódulos diretamente usados e carrega os targets monolíticos de
`rexcore`/`rexui`; o caminho top-level acrescenta dependências apenas de
configuração. Nenhum download foi iniciado. Prosseguir requer autorização
explícita para esse conjunto amplo e para compilar o `rexcore` monolítico, ou
autorização para uma refatoração arquitetural de granularidade dos targets.

## 2026-09-07 — Decisão de granularização para 5B.3

**ARCHITECTURE DECISION: NOT SAFE.** A inspeção em nível de translation unit
mostrou que uma extração exclusivamente CMake não consegue formar o caminho
solicitado sem carregar partes substanciais dos targets atuais ou depender de
eliminação de código morto pelo linker.

O fluxo real precisa de `surface_android.cpp`, `window.cpp`, `window_sdl.cpp`,
`windowed_app_context.cpp`, `windowed_app_context_sdl.cpp`, `presenter.cpp`,
`renderdoc_api.cpp`, `vulkan_instance.cpp`, `vulkan_device.cpp`,
`ui_samplers.cpp`, `vulkan_provider.cpp`, `vulkan_presenter.cpp`,
`vulkan_submission_tracker.cpp` e `vulkan_util.cpp`. `window_sdl.cpp` também
referencia `sdl_virtual_key.cpp`; `window.cpp` contém operações de `MenuItem` e
inclui ImGui; portanto `menu_item.cpp` e ImGui permanecem no fechamento seguro
do objeto.

Há acoplamentos adicionais dentro das próprias translation units:
`VulkanProvider::CreatePresenter` e `CreateImmediateDrawer` estão no mesmo
`vulkan_provider.cpp`. Assim, o objeto referencia `vulkan_immediate_drawer.cpp`,
que por sua vez exige `vulkan_upload_buffer_pool.cpp`,
`graphics_upload_buffer_pool.cpp` e `immediate_drawer.cpp`. O
`VulkanPresenter` também inicializa recursos surface-independent completos; não
há uma entrada pública limitada à criação da `VkSurfaceKHR`.

No núcleo, cvars/logging exigem `cvar.cpp`, `logging.cpp`, CLI11, toml++, spdlog
e platform env; janela/contexto exigem threading; Vulkan instance exige dynlib e
RenderDoc. Determinar e extrair um núcleo fechado requer separar fontes comuns e
fontes de plataforma hoje pertencentes a `rexcore`. Isso toca diretamente o
invariante documentado em `rexglue_link_audit.cmake`: globals de cvar/logging
devem existir em uma única cópia, pois duplicação já causou registros duplicados
e falha no encerramento.

O desenho mínimo tecnicamente coerente teria dois targets internos de objetos —
fundação core para apresentação e UI/presenter — consumidos tanto pelos targets
normais quanto pelo probe. Porém, para evitar referências indevidas, também
exigiria mover métodos entre translation units, reorganizar `rexcore` e `rexui`,
propagar exatamente PCH/defines/includes/link interfaces e atualizar a auditoria
de unicidade. Isso não é uma pequena refatoração CMake e não permite demonstrar
agora que Windows, Linux e macOS continuam semanticamente idênticos.

Uma simples opção Android opt-in apenas esconderia dependências e manteria os
acoplamentos; um target paralelo com listas copiadas deixaria de provar os mesmos
objetos; depender de `--gc-sections` não prova fechamento de símbolos nem
preserva o modelo de build. As três alternativas foram rejeitadas.

**Resultado:** nenhum patch 0004, submódulo, build ou APK 5B.3 foi produzido.
A dependência externa poderia cair de 13 para aproximadamente oito pelo exame de
includes, mas essa estimativa não é uma prova de link e depende da refatoração
C++/OBJECT descrita acima. Logo o critério de redução comprovada e preservação
integral de desktop não foi satisfeito.

## 2026-09-07 — Etapa 5B.3 Rota A: build monolítico e blocker de fibers

Foi escolhida a configuração oficial monolítica, sem granularizar `rexcore` ou
`rexui`. Foram inicializados nas revisões fixadas por `v0.10.0` os submódulos
diretamente usados por esses targets (`cli11`, `fmt`, `imgui`, `sdl3`, `simde`,
`spdlog`, `spirv-headers`, `spirv-tools`, `tomlplusplus`, `utfcpp`,
`vulkan-headers`, `vulkan-memory-allocator` e `xxHash`) e os exigidos apenas pela
configuração top-level (`FFmpeg`, `glslang`, `inja`, `libmspack` e `o1heap`). O
checkout raso ocupava aproximadamente 507 MB antes dos artefatos de build.
Catch2, Tracy, MoltenVK e Vulkan Loader não foram inicializados porque testes,
profiling e caminhos Apple estavam desligados.

O patch `0004-android-cmake-platform.patch` identifica `android-arm64`, mantém
SDL3 compartilhada para que Activity e `rexui` usem um único estado SDL e
impede que Android configure X11, XCB, Wayland, ALSA, PulseAudio ou PipeWire.
Windows, GNU/Linux desktop e macOS permanecem nos ramos anteriores. Os patches
`0005-android-float-from-chars.patch` e
`0006-android-chrono-clock-cast.patch` cobrem lacunas comprovadas do libc++ do
NDK em parsing de ponto flutuante e `clock_cast`. O patch
`0008-android-libcxx-jthread.patch` reutiliza exatamente o tratamento já usado
no Apple para expor `std::jthread`/`std::stop_token` somente em
`timer_queue.cpp`; nenhuma implementação de threading foi substituída.

O target real `rexui` compilou isoladamente, incluindo `window_sdl.cpp`,
`surface_android.cpp`, `vulkan_instance.cpp`, `vulkan_device.cpp`,
`vulkan_provider.cpp` e `vulkan_presenter.cpp`. O patch opt-in
`0007-android-presentation-diagnostics.patch` registra, dentro desses caminhos
reais, a criação do wrapper e o retorno/handle produzidos pela chamada real a
`vkCreateAndroidSurfaceKHR`; quando a macro do probe não é definida, não há
símbolos nem comportamento novo.

Para o APK, o probe passou a consumir o top-level e os OBJECT targets reais e a
empacotar o `libSDL3.so` e as classes Java da mesma revisão vendorizada. O build
incremental comprovou que `rexcore` não fecha no Android: seu ramo `UNIX`
seleciona `fiber_posix.cpp`, que depende de `getcontext`, `makecontext` e
`swapcontext`. O Bionic/NDK 27.2.12479018 não declara essas APIs e a compilação
falhou nesse translation unit. Além da seleção CMake por `UNIX`, o arquivo é
ativado porque `platform.h` define `REX_PLATFORM_LINUX=1` também para Android.
O erro não foi mascarado removendo a fonte nem
criando stubs. Escolher ou implementar um backend de fibers Android/AArch64
altera a arquitetura de `rexcore` e exige decisão separada.

**Resultado:** `5B.3 BUILD: FAIL`; nenhum APK novo foi produzido e nenhum
checkpoint `REXUI_*` foi executado no aparelho. A série 0001–0008 reaplicou sem
conflitos sobre `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` e passou em
`git diff --check`. A falha atual é anterior ao link e à execução do presenter,
não evidência de falha Vulkan/Android.

O gate de NOTICE foi executado, mas não pôde gerar uma comparação: ele exige
também os textos de Snappy, Tracy e volk. Essas dependências não pertencem ao
build 5B.3 selecionado e não foram baixadas apenas para satisfazer o gerador;
o `NOTICE` existente não foi alterado.

## 2026-09-07 — Etapa 5B.3: backend de fibers Android/AArch64

**Causa raiz comprovada:** o backend POSIX de `rex::thread::Fiber` selecionado
no Android chama `getcontext`, `makecontext` e `swapcontext`, funções ausentes
no Bionic/NDK. O contrato do SDK usa somente essas operações e os campos
`uc_stack`/`uc_link`; nenhum call site acessa registradores ou o layout interno
de `mcontext`. A stack host é um `std::vector<uint8_t>` de no mínimo 256 KiB no
caminho de runtime, e o entrypoint/argumento são transportados pelo objeto
`Fiber` em TLS, não pelos argumentos variádicos de `makecontext`.

O patch `0009-android-libucontext-fibers.patch` adiciona como submódulo apenas
para Android a libucontext oficial 1.5.2, commit
`49e671dd52ff6791295d8161ad3b6da7dc5f6f9d`, sob licença ISC. O backend usa os
símbolos prefixados `libucontext_*` e o tipo freestanding próprio da biblioteca;
não substitui APIs globalmente. AArch64 salva/restaura GPRs, SP, PC e q8–q15.
O ReXGlue restaura explicitamente o FPCR do guest antes do switch. A biblioteca
não preserva signal mask no modo rápido, mas o código ReXGlue alcançável por
fibers não altera a mask entre contextos; os usos encontrados configuram masks
de handlers, sem `sigprocmask`/`pthread_sigmask` no runtime do SDK.

Não existe backend AArch64 alternativo no ReXGlue v0.10.0, e a árvore oficial
atual do Xenia não contém implementação de fibers reutilizável. Uma
implementação manual teria maior risco de corrupção de contexto e foi
descartada. Linux desktop continua usando ucontext da libc; Windows e macOS
permanecem nos ramos existentes.

**Validação de build:** `libucontext`, o `fiber_posix.cpp` real e o executável
isolado `rex_fiber_android_test` compilaram e linkaram para Android API 26,
ARM64, com Ninja `-j2`. O ELF contém os símbolos `libucontext_getcontext`,
`libucontext_makecontext`, `libucontext_swapcontext` e
`libucontext_setcontext`, e declara GNU stack RW, não executável. O teste faz 64
trocas, compara estado local e compartilhado e prepara os checkpoints
`FIBER_CREATE`, `FIBER_ENTER`, `FIBER_YIELD`, `FIBER_RESUME`, `FIBER_RETURN` e
`MULTIPLE_SWITCHES`. `FIBER_RETURN` valida o handoff explícito ao fiber principal
usado pelo runtime antes que o entrypoint host retorne; com `uc_link` nulo, o
entrypoint não deve retornar diretamente.

Não havia ADB/aparelho disponível nesta sessão, portanto a execução do teste de
fiber continua **AINDA NÃO COMPROVADA**. Após a correção, o build incremental de
`rexcore` passou por `fiber_posix.cpp` e parou no primeiro blocker não relacionado:
`memory_posix.cpp` referencia `rex::GetAndroidApiLevel()`, símbolo não declarado
nesse build. Conforme o escopo desta subetapa, esse novo blocker não foi
alterado. A série completa 0001–0009 reaplicou com `git apply --check --index`
sobre o commit oficial v0.10.0 e passou em `git diff --check`.

`GetAndroidApiLevel` não tinha declaração nem definição no SDK; além disso,
`threading_posix.cpp` incluía o header inexistente `rex/main_android.h`. O patch
`0010-android-api-level.patch` expõe em `platform.h` um wrapper Android-only da
API oficial `android_get_device_api_level()` (disponível para minSdk 26) e remove o
include órfão. `memory_posix.cpp` passou a compilar; o próximo blocker comprovado
é o uso incondicional de mutex POSIX robusto, cuja API não é exposta pelo NDK
quando o target é API 26.

O patch `0011-android-non-robust-mutex.patch` restringe a recuperação
`EOWNERDEAD` a GNU/Linux, onde as APIs robustas existem. Android usa o caminho
`std::mutex` já existente para plataformas sem mutex robusto; Windows, macOS e
GNU/Linux não mudam de comportamento. Com isso, o target OBJECT `rexcore`
compilou integralmente para Android ARM64/API 26.

No primeiro link completo de `rexruntime`, `mapped_memory_posix.cpp` expôs uma
declaração Android sem implementação no SDK: abertura de `content://` como file
descriptor. O patch `0012-android-content-file-descriptor.patch` implementa o
contrato com o `JNIEnv` e a Activity fornecidos pelo SDL3,
`ContentResolver.openFileDescriptor` e `ParcelFileDescriptor.detachFd`; o fd
passa ao `MappedMemory`, que já o fecha. A fonte e o vínculo com SDL3 são
selecionados somente no Android. Ela compilou e removeu o símbolo indefinido;
o link avançou até seu único erro restante, o callback opt-in do diagnóstico de
apresentação definido fora de `librexruntime.so`.
