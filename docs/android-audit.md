# Auditoria inicial — Android ARM64

Data: 2026-09-07. Base auditada: `c8c21e2781acd134de6b2ccf12b666145e00ea6c`
do upstream `https://github.com/theokyr/Project8Recomp` (v0.3.0 documentada).
Trabalho do fork em `android-port`. Escopo: leitura estática, Git e documentação.

## Conclusão e limites

O projeto oferece uma base plausível para Android ARM64, mas **não contém um
port Android pronto nem evidência de execução Android**. A recompilação estática
PowerPC → C++, o runtime separado e a execução macOS ARM64 relatada pelo upstream
são pontos favoráveis. Os maiores obstáculos estão na integração do sistema
operacional e do SDK, não na troca isolada de uma flag de arquitetura.

Esta auditoria cobre README, todos os documentos textuais de `docs/`, os quatro
projetos CMake, presets, organização de `src/`, fluxos de inicialização e I/O,
hooks de CPU e diagnósticos, ferramentas, CI, manifesto e os dois patches do SDK.
Foram usadas buscas locais antes das leituras dirigidas. Imagens/fontes não foram
tratadas como código. O SDK completo, suas dependências e fontes geradas não estão
neste checkout; não foram baixados. A leitura dos patches não equivale a auditar
todo o SDK. Compatibilidade, memória residente, desempenho e correção em aparelho
continuam sem medição.

Classificação usada abaixo: **observado** = verificável nos arquivos desta base;
**relatado** = documentação upstream, sem reprodução; **a verificar** = risco ou
hipótese que exige dependência completa ou validação futura. Não houve build,
configuração CMake, execução do jogo ou codegen local. Houve CI remota automática
indesejada após o push obrigatório de `main`; ver a seção de validação e o diário.

## 1. Organização Git

| Referência | Estado ao finalizar o alinhamento |
| --- | --- |
| `origin` | `https://github.com/Cyberlym/Project8Recomp` — repositório do usuário |
| `upstream` | `https://github.com/theokyr/Project8Recomp` — fonte oficial |
| `main` local | Exatamente a base auditada; tracking `upstream/main` |
| `origin/main` | Hash remoto confirmado igual à base auditada após push |
| `android-port` | Criada a partir desse `main`; alterações desta etapa ficam aqui |

O único commit inicial anterior era `50f8eb52984673f62b390a7cddc7a6a03223857c`.
Sua substituição foi explicitamente autorizada pelo usuário, com `reset --hard`
local e lease remoto preso a esse hash. A autorização não vale para reescritas
futuras. Nenhum `git clean`, exclusão grande ou atualização de submódulos ocorreu.
Alinhar o conteúdo não transforma automaticamente o repositório em um fork na
rede de forks do GitHub; isso não impede o fluxo `origin`/`upstream` solicitado.

## 2. Arquitetura atual e dependências

O código do jogo é traduzido antecipadamente no host; no destino, o C++ compilado
usa ReXGlue para contexto PowerPC, despacho de funções, serviços Xbox, memória,
entrada, áudio e GPU. O runtime continua lendo os dados locais do jogo e validando
o executável original do usuário. Não existe um caminho sem esses dados para
validar gameplay. A configuração distribuída registra fatos do binário, não
substitui os arquivos privados nem as unidades de tradução ausentes.

```text
Host de desenvolvimento (futuro, exige autorização)
  manifesto local + executável privado → rexglue codegen → C++ privado
  C++ privado + host code + SDK do alvo → binário do jogo

Execução desktop atual
  Project8Recomp → thps_p8_gui
                    ├─ thps_p8_identify → SDK / identificação / extração
                    └─ thps_p8_launch → thps_p8 → rexruntime
                                                  └─ plugin rexgpu-xenos / Vulkan
```

| Componente | Responsabilidade e fronteira | Avaliação para Android |
| --- | --- | --- |
| [Entrada pública](../src/launcher/src/entry.cpp) | `Project8Recomp` repassa argumentos e solicita Play à GUI; `--gui` muda essa política | Política reaproveitável; `ExecReplace` e executáveis vizinhos são desktop |
| [GUI](../src/launcher/CMakeLists.txt) | C++20, SDL3 compartilhada, SDL3_image, RmlUi fixada em `588874489295b5dcd13d9eaa002ea2d73b8addbf`, FreeType/fontes; não liga SDK | Separação útil; janela, arquivos e processos exigem adaptação; não é prioridade inicial |
| [Identificação](../src/identify/main.cpp) | C++23, SHA-256, `DiscImageDevice`, leitura mapeada e extração; liga `rexruntime` | Regra de identificação reaproveitável; acesso a arquivos e isolamento precisam de desenho Android |
| [Supervisor](../src/game/src/launcher/main.cpp) | C++23 sem SDK; preflight, subprocesso, sinais, limpeza de memória órfã e registro `last_launch.json` | Mecânica Linux não serve diretamente ao app Android |
| [Jogo](../src/game/src/main.cpp) | Inclui `generated/default/thps_p8_init.h`, registra `ThpsP8App` com `REX_DEFINE_APP` | Corpo C++ favorável; entrypoint real, contexto da janela e runtime pertencem ao SDK |
| [Aplicação](../src/game/src/thps_p8_app.h) | Hooks de setup, gate de dados, entrada, overlay, captura e shutdown | Bons pontos de integração; não há ponte Activity/JNI nesta árvore |
| [Tabela compartilhada](../src/common/supported_dumps.h) | Uma lista aceita, consumida pelo worker e gate do jogo | Diretamente reaproveitável como dados C++; preservar uma única definição |

Em `ThpsP8App`, `OnPreSetup` instala entrada roteirizada e gravação;
`OnPostSetup` instala hooks e diagnósticos; `OnCreateDialogs` registra overlays;
`OnConfigurePaths` aplica o gate antes de construir o runtime; `OnShutdown`
desconecta observadores e captura. Esses hooks não demonstram suporte a perda de
surface, background, recriação de Activity ou morte do processo Android.

O [packager](../tools/package_release.sh) distingue runtime Release, Debug e
RelWithDebInfo e inclui o plugin GPU separadamente, pois ele é carregado por nome
e não aparece necessariamente na tabela de dependências do executável. Misturar
configurações pode carregar dois runtimes com singletons distintos. Essa fronteira
deve permanecer consistente em qualquer embalagem Android.

## 3. CMake, codegen e compilação cruzada

**Observado:** não existe `CMakeLists.txt` na raiz. Há projetos independentes em
`src/launcher`, `src/identify`, `src/game` e `src/game/src/launcher`. O jogo exige
CMake 3.25/C++23; os outros projetos exigem CMake 3.24, com C++20 para GUI e C++23
para worker/supervisor. O worker usa headers que dependem de `std::byteswap`.

O [CMake do jogo](../src/game/CMakeLists.txt), linha 10, inclui
`../config/generated/rexglue.cmake`: a partir de `src/game`, isso aponta para
`src/config/generated/rexglue.cmake`. O manifesto distribuído está em
`config/thps_p8_manifest.toml`. O patch do template informa que o helper fica em
`<diretório do manifesto>/generated`. Seguir literalmente a cópia local descrita
em [BUILDING.md](BUILDING.md) produz um diretório esperado diferente do include.
Além disso, `main.cpp` inclui `generated/default/thps_p8_init.h`, enquanto o
manifesto define `../generated/default`. É necessário conferir a raiz de includes
efetivamente emitida pelo SDK antes de propor uma correção. **Inconsistência
estática de layout identificada; não é um erro de build reproduzido.**

O mesmo CMake já separa `THPS_P8_REXGLUE_HOST_EXECUTABLE` do SDK do alvo quando
`CMAKE_CROSSCOMPILING` está ativo. Isso é aproveitável: o recompiler deve executar
no host e as bibliotecas devem ser compiladas para Android. Entretanto, o include
gerado acontece antes dessa lógica. O patch também emite `add_custom_command`
para codegen e um target `_codegen`; portanto um build futuro pode disparar
regeneração implicitamente. Auditar o grafo antes de autorizar sua execução.

Os [presets](../src/game/CMakePresets.json) chamados Linux/Windows ARM64 apenas
adicionam `-march=armv8-a`; não definem NDK, sysroot, ABI Android nem nível de API.
O preset Linux fixa `clang-20`. Um nome ARM64 não prova cross-compilation a partir
de um host x86_64. O [toolchain Windows](../tools/cross/windows-msvc.cmake) é
específico de MSVC/x86-64-v3 e não é base pronta para Android.

Para Android, o toolchain do NDK precisa selecionar `arm64-v8a` e um nível mínimo
de API explícito. O triple correspondente é `aarch64-linux-android`; `-march`
isoladamente não escolhe o sistema alvo. Não foi fixada uma versão de NDK ou API
sem examinar os requisitos do SDK e do aparelho.
Fontes: [CMake/NDK](https://developer.android.com/ndk/guides/cmake) e
[toolchain NDK](https://developer.android.com/ndk/guides/other_build_systems).

O launcher usa `FetchContent_MakeAvailable` com clone RmlUi não shallow: até
**configurar** esse projeto pode baixar dependências. O SDK não está vendorizado
aqui; o guia solicita submódulos recursivos. Esses comandos não foram executados.

## 4. CPU, ARM64 e sincronização

- **Relatado:** [README](../README.md) e [KNOWN_ISSUES](KNOWN_ISSUES.md) registram
  execução nativa no Apple M1, incluindo renderização. É evidência favorável de
  que o caminho C++/SDK não é exclusivamente x86, sem comprovar Android/Bionic.
- **Observado:** `guest_vertex_unpack.h` protege `immintrin.h`, SSSE3/SSE4.1 e
  `_mm_*` com `__x86_64__`/`_M_X64`. Há caminho escalar e `kHasSimd`; ARM64 não
  exige implementar NEON nesta etapa. `guest_u8x4_unpack.h` usa operações escalares
  e stores guest explícitos.
- `guest_ring_wait.h`, `guest_swap_wait.h` e os dois unpackers desativam hooks
  `REX_HOOK_RAW` em Apple por diferenças de aliases. Android não entra nesse guard;
  resolver aliases `__imp__sub_*` em ELF/AArch64 ainda precisa de validação.
  O sucesso macOS não testa esses mesmos hooks. `guest_spin_yield.h` também oferece
  substituição pelo dispatcher, independente desses aliases.
- Os endereços guest permanecem valores de 32 bits, com contexto/registros e
  acessos `REX_LOAD_*`/`REX_STORE_*`. Preservar endian, alinhamento, arredondamento,
  reservas atômicas, estado de thread, `setjmp`/`longjmp` e ABI dos callbacks.
  SIMDe aparece nas dependências, mas seu caminho ARM64 completo não está aqui.
- O [manifesto](../config/thps_p8_manifest.toml) mantém localização de registradores
  desabilitada e relata tentativas com crash/congelamento. Não reativar isso para
  tentar obter um primeiro boot. O helper `win_math_compat.cpp` só resolve funções
  matemáticas faltantes no Windows; não demonstra quais símbolos Bionic fornece
  no nível de API que vier a ser escolhido.
- **Risco estático importante:** o patch `0001`, seção `src/core/clock.cpp`
  (linhas 1003–1060), publica `guest_scale_` não atômico sob mutex, mas copia esse
  objeto no leitor sem o mesmo mutex, entre leituras atômicas de sequência.
  Se houver publicação concorrente, esses acessos são conflitantes; a sequência
  não torna o payload automaticamente livre de data race C++. A concorrência
  efetiva e a solução exigem ler todos os call sites do SDK. Não foi observado
  crash, nem alterada a implementação. Revisar antes de confiar no caminho ARM64.

Os wait hooks usam mutex/condition_variable/timeouts para reduzir polling, mas
o predicado ainda lê memória guest escrita por outras threads. A correção depende
das garantias do SDK; não assumir que comportamento observado em x86 prova a
ordenação ARM64. `guest_call.h` exige executar trabalho no thread guest de polling
de entrada, não em callbacks da UI/GPU; preservar essa restrição.

## 5. Memória, exceções e processos — prioridade máxima

O supervisor seleciona `platform_linux.inc` para `__linux__` em
[platform.h](../src/game/src/launcher/platform.h). Não há branch `__ANDROID__`.
Esse arquivo enumera `/proc`, lê `comm`/`stat`, usa `/proc/self/exe`, varre
`/dev/shm` e chama `shm_unlink`. **Bloqueio concreto de API:** Bionic lista
`shm_open`/`shm_unlink` entre as funções POSIX ausentes. Não basta compilar o ramo
Linux com outro processador.
[Fonte Bionic](https://android.googlesource.com/platform/bionic/+/main/docs/status.md).

O patch do SDK altera MMIO e write-watch: `MMIOHandler::ExceptionCallback`,
`Memory::AccessViolationCallback`, `QueryProtect` e recuperação de proteção stale.
Há uso de `memory::page_size()` e `host_page_size`, mas a reserva completa, aliases
de memória física, backing store, handler de sinal e decodificação de instruções
ARM64 não são mostrados integralmente. Esses são **itens a verificar**, não suporte
Android confirmado nem ausência comprovada de suporte dentro do SDK.

Não confundir tamanho do espaço guest reservado, RAM comprometida e memória GPU.
A referência histórica a órfãos que consumiram 13 GiB em `/dev/shm` descreve várias
execuções desktop acumuladas, não um requisito de 13 GiB por jogo. Nesta etapa não
há medida confiável de pico de RAM nem recomendação de RAM mínima para Android.

A próxima inspeção do SDK deve localizar: reserva virtual e aliases; backend de
memória compartilhada; `mmap`/`mprotect`; contexto de exceção AArch64 e retomada de
stores; TLS; páginas guest versus host; teardown e recuperação após morte. Qualquer
alternativa de backing deve manter a semântica de aliases/write-watch, não apenas
substituir nomes de APIs.

Android pode usar páginas de 16 KiB. Alinhamento ELF/APK e granularidade de
mapeamento/proteção são problemas distintos: não corrigir indiscriminadamente
constantes guest de 4096 para 16384. O código que passa tamanhos ao kernel deve
respeitar a página real do host; validar também bibliotecas terceiras.
[Fonte: páginas Android](https://developer.android.com/guide/practices/page-sizes).

## 6. Entrada da aplicação, lifecycle e armazenamento

O desktop usa `posix_spawn`, `fork`, `execv`, `waitpid`, sinais e executáveis
vizinhos. `platform_posix.inc` abre pastas com `xdg-open` e altera
`LD_LIBRARY_PATH` para o stub de RenderDoc; `thps_p8_app.h` instala `SIGTERM`/
`SIGINT`, faz `prctl(PR_SET_PTRACER, ...)` e termina por `_exit`.
Essas decisões não constituem um ciclo de vida Android.

Em apps que miram Android 10+, executar binários a partir do diretório gravável
do app é restringido. Assim, copiar o ZIP desktop e executar seus filhos nesse
diretório não é uma arquitetura Android válida por simples equivalência POSIX.
Isso não significa que todo subprocesso seja proibido. É preciso desenhar uma
entrada Android e, se necessário, um processo de worker gerenciado pela plataforma,
mantendo o isolamento de identificação e a GUI sem SDK.
[Fonte Android](https://developer.android.com/privacy-and-security/security-best-practices).

Não há Manifest Android, Gradle, JNI, `ANativeWindow`, `android_main`, NativeActivity
ou SDLActivity nos fontes/configurações pesquisados. A integração oficial de SDL
oferece SDLActivity e eventos de background/foreground/baixa memória; é uma opção
a avaliar junto ao entrypoint do ReXGlue. Não escolher Compose ou criar uma GUI
nova antes de provar o runtime mínimo.
[Fonte SDL](https://wiki.libsdl.org/SDL3/README-android).

O [worker](../src/identify/main.cpp) recebe um pathname, monta a imagem, mapeia o
executável e só extrai após hash aceito. Recusa componentes de caminho inseguros,
escreve em blocos de até 4 MiB e separa JSON/progresso em stdout dos logs SDK.
Seu parser roda em filho para conter crashes em imagens truncadas. Migrar o parser
para dentro da UI apagaria uma proteção existente.

O [gate do runtime](../src/game/src/dump_gate.h) usa `std::filesystem`/`ifstream`
para `game_data_root/default.xex`, lê o arquivo inteiro e recusa por `_exit(3)`.
A regra de hash é portável; término abrupto e leitura por caminho precisam ser
adequados ao processo escolhido. O tamanho só classifica a mensagem de rejeição;
não autoriza a execução. O comentário de `supported_dumps.h` sobre title ID é mais
ambicioso que os consumidores atuais, que usam tamanho para distinguir near-miss.

O launcher pressupõe `game/`, `saves/`, `config/`, `logs/` ao lado do executável;
escaneia discos próximos e usa `SDL_ShowOpenFileDialog`. No Android, documentos
selecionados via SAF são acessados por URI e `ContentResolver`, inclusive por
descritor; uma URI não deve ser passada a `ifstream` como se fosse um pathname.
Ainda será preciso decidir acesso seekable/mapeável versus preparação privada,
separar arquivos empacotados de dados graváveis e preservar permissões e saves.
Nenhum arquivo do jogo foi solicitado ou manipulado nesta etapa.
[Fonte SAF](https://developer.android.com/training/data-storage/shared/documents-files).

## 7. GPU, áudio, entrada e diagnósticos

**GPU:** a arquitetura já usa Vulkan/Xenos e o renderer real fica no SDK/plugin.
O CMake da GUI usa renderer SDL para RmlUi; isso é separado do renderer do jogo.
No Android, verificar carregamento do plugin e bibliotecas, surface/swapchain,
extensões/features/limites e formatos exigidos pelo SDK. Vulkan disponível no
aparelho não garante suporte a todas as necessidades do backend; a documentação
Android também distingue disponibilidade da API e capacidades do dispositivo.
[Fonte Vulkan](https://developer.android.com/ndk/guides/graphics/getting-started).

O patch toca sparse shared memory, uploads, barreiras, descriptors, texturas,
readbacks e tradução SPIR-V. Não há evidência suficiente nesta árvore para fixar
uma versão Vulkan mínima real do jogo, lista exata de extensões obrigatórias ou
compatibilidade Adreno/Mali. O pin de headers Vulkan 1.4 no patch macOS não é uma
declaração de requisito de dispositivo. MoltenVK resolve apresentação Apple e não
é o backend Android proposto. Não introduzir Turnip nem novos caminhos otimizados.

**CPU/GPU baseline:** [settings.cpp](../src/launcher/src/settings.cpp),
`PerformanceFlags`/`RenderArgv`, habilita 17 flags quando performance está unset
ou On, incluindo residência, prefetch, caches, waits e unpack nativo. Portanto,
o default da GUI não é igual ao default das cvars do runtime. Para uma experiência
futura controlada, escolher e registrar explicitamente a baseline; não copiar
automaticamente o preset medido no Steam Deck. O cap deve ser preservado na
validação inicial porque o upstream relata cutscenes aceleradas sem ele.

**Áudio/vídeo:** o patch altera a espera do audio worker e registra dispatches;
[RELINKING.md](RELINKING.md) descreve FFmpeg estático no runtime, fork
`wmarti/FFmpeg`, commit `0604b464c7cb4ebc94940cf1f324a3b26b87717c`, com headers
Android/aarch64. Isso é um indício de portabilidade de uma dependência, não prova
de build Android do SDK. Backend de áudio efetivo, decodificação XMA/vídeo,
latência, foco de áudio e pausa/retomada continuam a verificar. Não assumir uso
de MediaCodec/AAudio nem propor troca antes de ler o backend fixado.

**Entrada:** SDL3 trata gamepads/hotplug na GUI; o runtime tem drivers embrulhados
por `rexglue_script_input.h` e `rexglue_input_record.h`. O primeiro injeta ações
em slot guest sem eventos de janela, o segundo registra JSONL; são úteis para
futura reprodução controlada. Touch/back, controle físico e perda de foco precisam
de verificação humana Android. `console_input_guard.h` impede texto da console
de chegar ao guest; não confundir ImGui do runtime com RmlUi do launcher.

**Diagnósticos:** `dev_console.h`, `dev_cheats.h`, `guest_probe.h` e `guest_call.h`
implementam comandos e fila no thread guest; `debug_panel.h`/`native_indicator.h`
exibem estado; `parity_capture.h` captura saída guest por readback e requer
sincronização/arquivos. São instrumentos futuros, não requisitos de UI inicial.
A maioria é opt-in. Não alegar custo literalmente zero de toda telemetria:
o patch de áudio incrementa contadores fora de uma cvar de habilitação.

## 8. Patches, ferramentas, licenças e privacidade

| Item lido | Resultado da auditoria |
| --- | --- |
| [Patch 0001](../patches/rexglue-sdk/0001-thp8-runtime-stack.patch) | Overlay sobre SDK v0.10.0: relógio/waits/vblank, MMIO/write-watch, saves/delete-on-close, SHA-256, GPU/caches/residência, console/fixtures, codegen e remapeamento de paths. Não é apenas tuning descartável |
| [Patch 0002](../patches/rexglue-sdk/0002-macos-update-moltenvk-1.4.2.patch) | Atualiza pin e gitlink MoltenVK para 1.4.2; específico Apple |
| [series](../patches/rexglue-sdk/series) | Dois patches, ordem explícita; aplicação futura com `--index` conforme README; aplicação no SDK não foi testada |
| [check_no_game_content.sh](../tools/check_no_game_content.sh) | Gate de nomes/caminhos e limite de 8 MiB, inclui untracked não ignorados; não é inspeção semântica de todo conteúdo |
| [check_patch_series.sh](../tools/check_patch_series.sh) | Verifica inventário/duplicação, não compilação nem aplicabilidade dos patches |
| [check_linux_abi.py](../tools/check_linux_abi.py) | Mede versões GLIBC/GLIBCXX para desktop; não comprova ABI Android, Bionic ou alinhamento de páginas |
| [check_gpl_boundary.sh](../tools/check_gpl_boundary.sh) | Procura símbolos binutils em binários; sem artefatos para validar nesta etapa |
| [make_notice.py](../tools/make_notice.py) | Inventário explícito de dependências e textos de licença; exige SDK, não foi executado |
| [package_release.sh](../tools/package_release.sh) | Só Linux x86_64, Windows x86_64 e macOS ARM64; staging ZIP, cópias, stripping/rpath e exclusões. Não produz APK/AAB |
| [ci/build_sdl3.sh](../tools/ci/build_sdl3.sh) | Baixa e compila SDL3 3.2.16/SDL3_image 3.2.4, `JOBS=4` default; não executar automaticamente |
| [ci/bundle_macos.sh](../tools/ci/bundle_macos.sh) | Percorre dylibs e reescreve caminhos; ferramenta desktop, sem papel Android imediato |
| [cross/](../tools/cross/) | xwin, sysroot Windows, exclusão de sysroot e helpers de divisão 128 bits; script informa cache de cerca de 1,7 GB. Fora do escopo Android |
| [CI](../.github/workflows/launcher.yml) | Builds desktop da GUI em push `main`, tags, PRs e dispatch; não testa runtime Android |

O SDK deve permanecer separado da GUI. Uma futura embalagem com múltiplas `.so`
também precisa de uma política consistente para a biblioteca C++: o NDK orienta
usar um único runtime C++ por app. Auditar configuração/ABI/símbolos de cada
dependência antes de juntar bibliotecas de origens diferentes.
[Fonte NDK](https://developer.android.com/ndk/guides/cpp-support).

[README](../README.md), [NOTICE](../NOTICE) e [RELINKING](RELINKING.md) registram
as licenças e obrigações tratadas pelo upstream, incluindo FFmpeg/libmspack
estáticos e separação de binutils do produto. Não houve mudança de dependências,
regeneração de NOTICE nem nova conclusão jurídica nesta auditoria. Um pacote
Android futuro precisará conservar os avisos e revisar sua própria composição.

**Lacuna de ignore observada:** `.gitignore` cobre `/generated/`, `/game/`,
`/saves/`, `/logs/`, extensões de disco e `config/*.local.toml`. Entretanto,
`git check-ignore` não encontra regra para `config/generated/rexglue.cmake`,
`src/config/generated/rexglue.cmake`, `src/generated/default/example.cpp` ou
`src/game/out/build/.../CMakeCache.txt`. O gate de conteúdo recusa `*/generated/*`,
mas recusar publicação não é o mesmo que manter o caminho privado/ignorado.
Nada foi gerado nesses locais; não foram criados arquivos de teste do jogo.
Resolver o layout e seus ignores antes de qualquer geração futura. Não relaxar
o gate nem adicionar fontes geradas à branch para resolver um include ausente.

Há comentários históricos que citam SDK 0.8.0, Makefiles/relatórios ausentes e
patches numerados antigos. O pin atual distribuído é 0.10.0 mais `series`;
comentários antigos não devem ser convertidos em comandos executáveis por
suposição. Essa divergência foi registrada, sem limpeza editorial fora do escopo.

## 9. Prioridades e próxima etapa recomendada

| Prioridade | Bloqueio | Evidência/critério para fechar |
| --- | --- | --- |
| P0 | SDK Android e memória/exceções | Inspecionar arquivos fixados de build/platform/memory/exception/entrypoint; mapear APIs Bionic e semântica de aliases; não basta o suporte M1 |
| P0 | Lifecycle e entrada | Definir ponte mínima com surface, pausa/retomada e erros, preservando separação worker/GUI/runtime |
| P0 | Caminhos CMake/codegen e ignores | Determinar onde o SDK emite helper/includes, separar host/alvo e garantir que toda saída privada é ignorada antes de gerar |
| P1 | Plugin Vulkan e dependências | Identificar features obrigatórias, descoberta do plugin e conjunto coerente de `.so` ARM64/NDK; verificar páginas 4/16 KiB |
| P1 | Ordenação/relógio/ABI PowerPC | Rever publicação de `guest_scale_`, aliases, TLS, atomics e semântica FP; testar futuramente de forma isolada e depois no runtime |
| P1 | Dados e saves privados | Definir root gravável, contrato URI/FD/path, gate de hash e tratamento de morte/interrupção sem perder saves |
| P1 | Áudio, entrada e encerramento | Provar funcionamento básico e lifecycle em dispositivo, com verificação humana |
| P2 | APK e validação repetível | Empacotar runtime coerente e diagnóstico básico, sem UI nova nem otimizações |

Recomendação imediata: uma **segunda inspeção estática e limitada dos arquivos
críticos do SDK v0.10.0**, escolhidos por inventário, para fechar P0 antes de
qualquer build. Reutilizar fontes/caches já existentes se disponíveis; se for
necessário um download potencialmente caro, apresentar tamanho e comando e
esperar autorização. Não clonar o SDK recursivamente por conveniência.

Somente após autorização de implementação/compilação: definir NDK/API/aparelho;
provar carga do runtime, memória e surface com um harness mínimo sem conteúdo do
jogo; depois validar boot com material privado já disponível ao usuário. O gate
do jogo não deve ser removido para o harness. Builds C++ grandes começam com
`-j2`, preservando diretório/cache e analisando cada erro antes de repetir.

## 10. Validação desta etapa

Verificações locais previstas para o fechamento: igualdade de hashes/árvores de
`main`, `origin/main` e `upstream/main`; conteúdo original de AGENTS preservado
como prefixo; diff limitado aos três documentos; gates de conteúdo e inventário;
`git diff --check`. Resultados finais ficam no [diário](android-progress.md).
Sem testes compilados, CTest, aplicação dos patches no SDK, codegen ou execução
Android. Os testes existentes de settings e contratos de launcher foram lidos
como especificação, não executados.

**Desvio remoto:** o push solicitado de `main` disparou o workflow herdado
[Launcher, execução 34133233674](https://github.com/Cyberlym/Project8Recomp/actions/runs/34133233674).
Ao detectar o disparo, o agente tentou cancelá-lo; GitHub respondeu HTTP 403,
`Resource not accessible by integration`. Na consulta dos jobs, Linux/macOS já
tinham compilado a GUI e Windows estava compilando. Portanto não seria correto
afirmar que não houve builds em lugar algum. Não houve build/download de
dependências local, nem evidência Android produzida por essa CI. Nenhum workflow
foi alterado em `main`, que precisa continuar idêntico ao upstream. Revisar os
gatilhos Actions antes de qualquer push/PR futuro sob restrição de custo.

Consulta final: a execução terminou com `conclusion=success` nos três sistemas.
Esse resultado não foi usado como validação do port ou dos documentos Android.
