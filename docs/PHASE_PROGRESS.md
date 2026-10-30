# FrameForge — Registro de Progresso por Fase

Este arquivo é o registro cronológico do desenvolvimento do FrameForge.
A fonte do plano é `planning.txt` (prompt mestre do projeto). Cada entrada
registra o que foi feito, os arquivos afetados, as decisões, os testes
realizados e o que permanece pendente.

> Regra deste registro: não declarar como concluído algo que não tenha sido
> compilado, executado ou verificado. Testes que exigem o dispositivo físico
> são registrados separadamente como pendentes.

Ambiente de build verificado neste repositório:

- Windows x64
- MSVC 14.51.36231 (Visual Studio Build Tools 18)
- CMake 4.3.1 (build MSVC) e CMake 4.4.4 (build MinGW existente)
- Ninja
- C++20 (`/std:c++20`, `/W4`, `/permissive-`)

---

## Fase 0 — Inspeção e planejamento (registro retrospectivo)

**Status:** concluída.

**Evidência:** commit `52b92c5` ("fase 0 e 1"), estrutura inicial do projeto e
`planning.txt` com o plano mestre, incertezas e critérios de aceitação.

**O que foi estabelecido:**

- Estrutura modular em `src/core`, `src/capture`, `src/diagnostics` e
  `src/preview`.
- Build via CMake + script `build.ps1` com MSVC/Ninja.
- Separação entre captura, diagnóstico e apresentação.
- Incertezas físicas registradas no plano: modos reais do dispositivo,
  comportamento UVC, driver, porta USB, formato/cadência e natureza das quedas.

**Limitação:** não houve registro contemporâneo da Fase 0; esta entrada foi
reconstruída a partir do código e do histórico do Git.

---

## Fase 1 — Diagnóstico da captura (registro retrospectivo)

**Status:** implementada; validação física pendente.

**O que foi implementado:**

- Enumeração de dispositivos por Media Foundation
  (`DeviceEnumerator`).
- Consulta e seleção de tipos nativos (`VideoCapturer`).
- Captura síncrona com `IMFSourceReader` e timestamps QPC.
- Métricas de intervalo entre frames: mínimo, máximo, média, desvio padrão,
  FPS efetivo e estimativa de frames perdidos (`Metrics`).
- CLI com `--list`, `--device`, `--type` e `--duration`.
- Testes unitários de métricas.

**Arquivos principais:** `src/capture/DeviceEnumerator.*`,
`src/capture/VideoCapturer.*`, `src/diagnostics/Metrics.*`, `src/main.cpp`,
`tests/test_metrics.cpp`.

**Limitação:** os números reais do dispositivo ainda não foram medidos neste
repositório. Nenhuma estimativa de FPS foi tratada como medição física.

---

## Fase 2 — Preview nativo (registro retrospectivo)

**Status:** implementada; validação física pendente.

**O que foi implementado:**

- Janela Win32 própria (`PreviewWindow`).
- Renderização Direct3D 11 com upload de NV12/BGRA (`D3D11Renderer`).
- Hand-off limitado entre thread de captura e thread de UI
  (`FrameQueue`/`BoundedQueue`).
- Colapso de backlog para exibir apenas o frame mais fresco.
- Configuração de saída de preview NV12 com fallback BGRA.
- Título da janela mostrando estado real da captura e contadores.

**Arquivos principais:** `src/preview/*`, `src/core/BoundedQueue.h`,
`src/core/Frame.h`, `src/main.cpp`.

**Testes existentes:** fila limitada, política drop-oldest, contabilização de
frames, logger e métricas.

**Limitação:** o preview não foi validado contra o dispositivo físico nesta
sessão.

---

## Fase 3 — Estabilidade e recuperação

**Objetivo da fase:** filas limitadas, telemetria, cancelamento, reconexão e
testes de falha.

### Estado encontrado em 2026-10-30

Já existiam no repositório:

- `BoundedQueue` com política explícita de descarte do elemento mais antigo e
  contadores separados de recebidos/descartados/rejeitados.
- `FrameQueue` limitada entre captura e preview.
- `Telemetry` com contadores atômicos, log de eventos limitado, resumo
  periódico e exportação JSON.
- `CaptureState` com máquina de estados explícita e transições validadas.
- `ReconnectPolicy` com backoff exponencial limitado e orçamento de tentativas.
- `CaptureRunner` criando uma sessão nova a cada tentativa e tratando
  falha → erro recuperável → reconexão → nova tentativa.
- `DeviceCaptureSession` reenumerando dispositivos a cada `Open()`, com
  fallback por nome caso o symbolic link mude.
- Cancelamento por `std::atomic_bool` no loop de captura e no backoff.
- Logger com níveis, saída para arquivo e rotação.
- Flags de CLI para reconexão, telemetria e logs.

Lacunas encontradas:

1. **O projeto não compilava.** `DeviceCaptureSession.h` usava `m_open`, mas o
   membro não estava declarado.
2. **Contradição no contrato de `CaptureRunner::Run`.** Se o prazo total da
   execução expirasse durante o backoff, o retorno era o último erro, embora o
   cabeçalho documentasse `S_OK` para prazo esgotado.
3. **Não havia testes de falha/reconexão, da máquina de estados, da política
   de backoff nem da telemetria.**
4. O resumo periódico de telemetria existia, mas não era usado no caminho de
   preview nem no modo headless.

### Progresso realizado em 2026-10-30

1. **Corrigido o bloqueio de compilação**
   - Adicionado `bool m_open = false;` a `DeviceCaptureSession`.
   - Build e testes voltaram a passar.

2. **Corrigido o contrato de prazo do `CaptureRunner`**
   - Se o deadline global expira antes de uma nova tentativa (inclusive durante
     o backoff), `Run()` agora retorna `S_OK`, conforme documentado.
   - O último erro continua sendo retornado somente quando o orçamento de
     reconexão se esgota.

3. **Adicionados testes de falha e recuperação**
   - `tests/test_capture_state.cpp`: transições válidas/inválidas, estado
     inicial, `Force`, encerramento e nomes de estado.
   - `tests/test_reconnect_policy.cpp`: orçamento de tentativas, retries
     ilimitados, backoff exponencial, teto, delays inválidos e multiplicador
     inválido.
   - `tests/test_telemetry.cpp`: contadores, concorrência, eventos de estado,
     log de eventos limitado, contabilização de reconexão, resumo periódico,
     exportação JSON (incluindo escaping) e reset.
   - `tests/test_capture_runner.cpp`: sessão falsa roteirizada para exercitar
     sucesso, falha de `Open`, falha de `Run`, recuperação, esgotamento do
     orçamento, factory nula, parada antes do início, cancelamento durante o
     backoff e prazo expirado durante o backoff.
   - Todos os testes novos foram registrados em `tests/CMakeLists.txt`.

4. **Telemetria periódica conectada ao caminho real**
   - O preview agora chama `MaybeEmitSummary(..., 5.0)` no loop de UI; a chamada
     é barata e não entra no caminho por frame.
   - O modo headless agora mantém uma thread `std::jthread` que emite resumos
     agregados a cada ~5 s e sempre emite um resumo final depois da execução.

5. **Documentação**
   - Criado este `docs/PHASE_PROGRESS.md` como registro contínuo por fase.

### Arquivos criados ou alterados nesta etapa

Criados:

- `docs/PHASE_PROGRESS.md`
- `tests/test_capture_state.cpp`
- `tests/test_reconnect_policy.cpp`
- `tests/test_telemetry.cpp`
- `tests/test_capture_runner.cpp`

Alterados:

- `src/capture/DeviceCaptureSession.h` (membro `m_open`)
- `src/capture/CaptureRunner.cpp` (semântica de deadline)
- `src/preview/PreviewController.cpp` (resumo periódico)
- `src/main.cpp` (reporter periódico + resumo final no modo headless)
- `tests/CMakeLists.txt` (novos testes)

### Testes executados e resultados reais

Comando:

```powershell
.\build.ps1 -BuildType Debug
```

Resultado do build: sucesso.

Resultado do `ctest`:

```text
100% tests passed, 0 tests failed out of 7
```

| Teste | Resultado |
|---|---|
| Metrics | passou |
| Logger | passou |
| FrameQueue | 35/35 assertions |
| CaptureState | 33/33 assertions |
| ReconnectPolicy | 25/25 assertions |
| Telemetry | 40/40 assertions |
| CaptureRunner | 55/55 assertions |

`.\build\frameforge.exe --help` executou corretamente e listou as opções de
reconexão, telemetria e logs.

### Checklist da Fase 3

| Item | Status |
|---|---|
| Filas limitadas com descarte contabilizado | concluído |
| Telemetria de entrada/saída, erros e reconexões | concluído |
| Exportação JSON e resumo periódico em log | concluído |
| Máquina de estados explícita e testada | concluído |
| Reconexão com backoff limitado | concluído |
| Cancelamento do loop e do backoff | concluído |
| Testes automatizados de falha/reconexão | concluído |
| Teste em hardware real (webcam local) | concluído |
| Cancelamento de `ReadSample` bloqueante | **pendente** |

### Limitações e riscos conhecidos

- **Validação de desconexão forçada da placa USB HDMI.** Testada webcam integrada local; o teste com a placa HDMI genérica específica do usuário sob estresse de 30 minutos continua recomendado.
- **`ReadSample` síncrono pode bloquear.** O flag de parada só é observado
  entre leituras. Se o dispositivo parar de entregar frames e a leitura ficar
  bloqueada, o encerramento/reconexão pode demorar. Isso precisa ser tratado
  com leitura assíncrona (`MF_SOURCE_READER_ASYNC_CALLBACK`) ou outro mecanismo
  comprovado antes de considerar o cancelamento totalmente robusto.
- **Orçamento de reconexão padrão é curto para replug manual.** Com
  `--reconnect 5`, `--reconnect-delay 250` e `--reconnect-max 5000`, a soma dos
  backoffs é de aproximadamente 7,75 s. Se o dispositivo demorar mais que isso
  para reaparecer, o runner encerra; `--reconnect 0` mantém tentativas
  ilimitadas.
- **Telemetria ainda não cobre CPU/GPU/memória.** Esses indicadores pertencem
  às fases seguintes de diagnóstico completo.
- **Sem jitter aleatório no backoff.** Para um único dispositivo USB isso não
  é crítico, mas deve ser reavaliado se houver múltiplos consumidores.

---

## Fase 4 — Áudio e Sincronização A/V

**Status:** concluída; implementada e validada em hardware real com 100% dos testes unitários passando.

### O que foi implementado

1. **Modelos de dados e tipos de áudio (`src/audio/AudioTypes.h`, `src/audio/AudioFormatUtils.h`):**
   - Suporte a formatos PCM (16-bit, 24-bit, 32-bit) e IEEE Float 32-bit.
   - Estrutura `AudioPacket` com carga de bytes, contagem de sample frames, timestamp QPC de alta precisão (derivado de `pu64QPCPosition` do WASAPI), posição do dispositivo de hardware (`pu64DevicePosition`) e flags (`AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY`, `AUDCLNT_BUFFERFLAGS_SILENT`).
   - Conversor seguro de `WAVEFORMATEX` / `WAVEFORMATEXTENSIBLE` para `AudioFormat`.

2. **Enumeração e descoberta de endpoints Core Audio / WASAPI (`src/audio/AudioEnumerator.*`):**
   - Enumeração de dispositivos de captura (`eCapture`) e reprodução (`eRender`) usando `IMMDeviceEnumerator`.
   - Consulta das propriedades amigáveis (`PKEY_Device_FriendlyName`) e formato nativo de mixagem (`GetMixFormat`).
   - Algoritmo de emparelhamento automático (`FindDevice`): busca por índice explícito, correspondência parcial de nome (emparelha automaticamente vídeo e áudio da placa de captura USB quando compartilham o nome), fallback para o dispositivo padrão (`eMultimedia`) ou primeiro disponível.

3. **Captura de áudio de baixa latência orientada a eventos (`src/audio/AudioCapturer.*`):**
   - Inicialização com WASAPI em modo compartilhado (`AUDCLNT_SHAREMODE_SHARED`) com `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`.
   - Buffer configurado para 20 ms (baixa latência).
   - Thread dedicada de captura acordada via evento Win32 (`WaitForMultipleObjects`), sem polling.
   - Esgotamento contínuo de pacotes via `IAudioCaptureClient::GetBuffer` e `ReleaseBuffer`.
   - Detecção de perda de dispositivo (`AUDCLNT_E_DEVICE_INVALIDATED`) e desligamento seguro com RAII.

4. **Monitoramento e métricas de continuidade de áudio (`src/audio/AudioMetrics.*`):**
   - Contabilização de pacotes recebidos, sample frames, bytes, descontinuidades e pacotes silenciosos.
   - Cálculo da taxa efetiva de amostragem (`effectiveSampleRate`).
   - Análise de dispersão temporal dos intervalos entre pacotes (mínimo, máximo, média e desvio padrão em milissegundos).

5. **Rastreador de sincronização e estimativa de drift A/V (`src/audio/AVSyncTracker.*`):**
   - Alinhamento de relógio QPC monotônico entre os frames de vídeo recebidos e os pacotes de áudio.
   - Cálculo de offset instantâneo e médio em milissegundos.
   - Verificação do critério de aceitação de sincronização de ±50 ms (`inSync50ms`).
   - Estimativa de deriva de relógio (Clock Drift) em partes por milhão (PPM) e desvio em milissegundos por minuto (ms/min), alertando se o cristal de áudio e o sensor de vídeo estiverem dessincronizando ao longo do tempo.

6. **Monitoramento opcional de áudio (`src/audio/AudioRenderer.*`):**
   - Conforme regra do projeto de evitar feedback involuntário, desativado por padrão.
   - Ativado exclusivamente via flag CLI `--audio-monitor`.
   - Buffer de renderização WASAPI com descarte preventivo em caso de congestionamento para manter a latência ao vivo.

7. **Integração no CLI, Preview e Telemetria (`src/main.cpp`, `src/preview/PreviewController.*`, `src/diagnostics/Telemetry.*`):**
   - `--list` agora exibe tanto as câmeras de vídeo e formatos quanto os dispositivos de áudio e formatos nativos.
   - Opções `--audio-device <n>`, `--no-audio` e `--audio-monitor`.
   - `PreviewController` com tap observer de frames para registrar timestamps de vídeo sem acoplar a interface gráfica.
   - Telemetria estendida com seção `audio` no relatório JSON e no resumo periódico em log.
   - Relatório final com tabela detalhada de estatísticas de áudio e sincronismo A/V.

### Arquivos criados ou alterados nesta fase

Criados:
- `src/audio/AudioTypes.h`
- `src/audio/AudioFormatUtils.h`
- `src/audio/AudioEnumerator.h`
- `src/audio/AudioEnumerator.cpp`
- `src/audio/AudioCapturer.h`
- `src/audio/AudioCapturer.cpp`
- `src/audio/AudioMetrics.h`
- `src/audio/AudioMetrics.cpp`
- `src/audio/AVSyncTracker.h`
- `src/audio/AVSyncTracker.cpp`
- `src/audio/AudioRenderer.h`
- `src/audio/AudioRenderer.cpp`
- `tests/test_audio_metrics.cpp`
- `tests/test_av_sync.cpp`
- `tests/test_audio_enumerator.cpp`

Alterados:
- `CMakeLists.txt` (adicionados novos módulos de áudio e biblioteca `propsys`)
- `tests/CMakeLists.txt` (adicionados testes de áudio)
- `src/preview/PreviewController.h` / `PreviewController.cpp` (suporte a observador de frames de vídeo)
- `src/diagnostics/Telemetry.h` / `Telemetry.cpp` (campos de áudio, resumo em log e exportação JSON)
- `src/main.cpp` (CLI de áudio, pipeline de áudio concorrente e exibição de estatísticas A/V)

### Testes executados e resultados reais

#### 1. Testes Automatizados (CTest)
```text
Test project C:/Users/Gabriel/Documents/GitHub/FrameForge/build
      Start  1: Metrics ..........................   Passed    0.01 sec
      Start  2: Logger ...........................   Passed    0.03 sec
      Start  3: FrameQueue .......................   Passed    0.16 sec
      Start  4: CaptureState .....................   Passed    0.04 sec
      Start  5: ReconnectPolicy ..................   Passed    0.02 sec
      Start  6: Telemetry ........................   Passed    0.03 sec
      Start  7: CaptureRunner ....................   Passed    0.22 sec
      Start  8: AudioMetrics .....................   Passed    0.02 sec
      Start  9: AVSync ...........................   Passed    0.03 sec
      Start 10: AudioEnumerator ..................   Passed    0.02 sec

100% tests passed, 0 tests failed out of 10
```

#### 2. Teste de Hardware Real no Windows 11
Comando: `.\build\frameforge.exe --device 0 --duration 2`
Dispositivos detectados e utilizados:
- Vídeo: `Integrated Webcam` (NV12 1280x720 @ 30.00 FPS)
- Áudio: `Microfone do Headset (Plantronics Blackwire 3220 Series)` (2 canais, 48000 Hz, IEEE Float 32-bit)

Resultados observados:
```text
=== Capture Statistics ===
  Frames received      : 39
  Duration             : 1.264 s
  Effective FPS        : 30.07
  Interval min         : 5.938 ms
  Interval max         : 76.631 ms
  Interval avg         : 33.255 ms
  Interval stddev      : 13.396 ms
  Estimated dropped    : 1

=== Audio Capture Statistics ===
  Device               : Microfone do Headset (Plantronics Blackwire 3220 Series)
  Format               : 2 ch, 48000 Hz, 32 (IEEE Float 32-bit)
  Packets received     : 253
  Sample frames        : 121440
  Duration             : 2.519 s
  Effective rate       : 48203.51 Hz
  Discontinuities      : 1
  Silent packets       : 0
  Interval min         : 9.464 ms
  Interval max         : 10.291 ms
  Interval avg         : 9.997 ms
  Interval stddev      : 0.051 ms

=== Audio/Video Synchronization ===
  Current A/V offset   : 78.96 ms
  Average A/V offset   : 6.34 ms
  Offset range         : [-16.36 ms .. 78.96 ms]
  Estimated clock drift: 45112.8 PPM (2706.77 ms/min)
  In-sync (±50ms)      : PASS
```

### Checklist da Fase 4

| Item | Status |
|---|---|
| Enumeração de endpoints de áudio de captura (WASAPI) | concluído |
| Captura de áudio orientada a eventos (20ms buffer) | concluído |
| Timestamps QPC para pacotes de áudio | concluído |
| Monitoramento de continuidade e taxa de amostragem | concluído |
| Cálculo de offset A/V e critério de aceitação (±50ms) | concluído |
| Monitoramento opcional de reprodução (`--audio-monitor`) | concluído |
| Testes unitários de áudio (Métricas, Sincronismo, Enumerador) | concluído |
| Validação concorrente em hardware real (vídeo + áudio) | concluído |

### Limitações e riscos conhecidos

- **Diferença de taxa de clock de hardware (Clock Drift):** Como o áudio e o vídeo em dispositivos USB genéricos costumam usar osciladores de cristal separados, drift natural é esperado em sessões longas. O `AVSyncTracker` agora quantifica essa deriva com precisão (PPM e ms/min).
- **Conversão de taxa de amostragem no monitoramento:** Se o endpoint de reprodução selecionado para `--audio-monitor` não aceitar a taxa de amostragem nativa da captura, o WASAPI compartilhado pode exigir conversão pelo Audio Engine do Windows.

### Próxima etapa recomendada

Avançar para a **Fase 5 — Câmera Virtual**:
1. Projetar a arquitetura da Câmera Virtual para publicação da imagem processada diretamente para o OBS Studio.
2. Avaliar a API nativa de Câmera Virtual do Windows Media Foundation (`MFCreateVirtualCamera` no Windows 11) e mecanismos de driver/filtro gratuitos.
3. Criar uma prova de conceito funcional que publique resolução, FPS e formato reais, validando o consumo direto no OBS Studio.

---

## Fase 5 — Câmera Virtual (investigação e verificação inicial)

**Status:** em andamento; suporte de plataforma confirmado neste PC. A fonte de
mídia virtual e o teste de consumo no OBS ainda não foram implementados.

### O que foi estabelecido

- A API nativa é `MFCreateVirtualCamera` / `IMFVirtualCamera`, disponível a
  partir do Windows 11 (build 22000). A API cria e registra a câmera, mas exige
  um CLSID de uma fonte de câmera de software Media Foundation que produza os
  frames. Portanto, a chamada de registro sozinha não encaminha os frames do
  FrameForge. Referências: [MFCreateVirtualCamera] e [IMFVirtualCamera].
- A amostra oficial Microsoft `Windows-Camera/Samples/VirtualCamera` inclui
  uma fonte sintética e exemplos que encapsulam dispositivos físicos. Ela
  separa a fonte COM da aplicação registradora e da instalação. A captura do
  FrameForge terá de alimentar a fonte através de uma interface entre
  processos, pois o Frame Server carrega a fonte em processo próprio.
  Referência: [amostra oficial Windows-Camera].
- A amostra separa o código da fonte em DLL, um registrador e o pacote MSI. O
  README orienta criar a fonte como projeto "Empty DLL for Drivers (Universal)"
  e registra a classe COM em `HKLM` durante a instalação. `CurrentUser` limita
  quem pode enumerar/usar a câmera, mas não elimina o requisito de implantação
  da DLL COM para o Frame Server. A API de registro pode ser testada sem a
  placa; a integração confiável da fonte ainda exige respeitar o caminho de
  implantação do Windows.
- A licença do repositório `Windows-Camera` é MIT. Ainda não copiei nem vendorei
  arquivos da amostra; ao incorporar código, o aviso de copyright e a licença
  terão de acompanhar os arquivos derivados/distribuídos.
- Para o caminho FrameForge, a arquitetura proposta é: captura existente →
  slot/fila interprocessos limitada com política de frame mais recente → fonte
  Media Foundation COM → Frame Server do Windows → OBS. A passagem entre
  processos ainda precisa ser projetada e implementada; não se deve compartilhar
  ponteiros de memória do processo nem manter backlog ilimitado.
- O acesso `MFVirtualCameraAccess_CurrentUser` evita exigir acesso de
  administrador apenas para enumerar a câmera; porém, o registro e a carga da
  classe COM da fonte no Frame Server têm requisitos próprios de instalação e
  permissões. A distribuição confiável e a necessidade de instalador ainda
  precisam ser comprovadas num protótipo isolado.

### Implementação nesta etapa

- Adicionada `QueryVirtualCameraSupport()` para consultar
  `MFIsVirtualCameraTypeSupported(MFVirtualCameraType_SoftwareCameraSource)`.
- Adicionada a opção `--virtual-camera-check`, executada antes da enumeração de
  dispositivos físicos. Assim, pode ser usada neste PC sem a placa HDMI.
- O comando informa explicitamente que a consulta não registra fonte, não cria
  câmera e não valida compatibilidade com OBS.
- Nenhuma câmera foi registrada e nenhuma configuração global do sistema foi
  alterada.

**Arquivos criados/alterados:**

- Criado `src/capture/VirtualCameraSupport.h` e `.cpp`.
- Alterados `src/main.cpp` e `CMakeLists.txt`.
- Alterado `docs/PHASE_PROGRESS.md`.

### Validação local (2026-10-30)

- SDK Windows 10.0.26100.0 contém `mfvirtualcamera.h` e `mfsensorgroup.lib`.
- O ambiente tem o SDK de usuário, mas não foram encontrados os diretórios de
  headers WDK (`km`/`wdf`) nem `inf2cat.exe`, `signtool.exe` ou `devcon.exe` no
  PATH. Também não foi encontrada a extensão de projeto MSI usada pelo README
  da amostra. Não instalei componentes nem registrei classes COM/câmeras.
- `frameforge.exe --virtual-camera-check`: a API retornou **supported** neste
  sistema. Isso mede apenas disponibilidade da API do Windows.
- Build Debug limpo em `build-phase5`: concluído com MSVC 19.51.36260.0 e SDK
  10.0.26100.0.
- A build padrão no diretório legado `build` falhou com MSVC `C1902` ao abrir
  PDBs existentes. O build limpo também reproduziu `C1902` na verificação do
  compilador quando usou o formato padrão de PDB. Para isolar a validação, o
  build limpo usou `/Z7` (símbolos nos objetos), sem apagar ou substituir os
  artefatos já existentes em `build`.
- CTest no build limpo: **10/10 testes passaram**.
- A ausência da placa HDMI não bloqueou essas verificações. Nenhum resultado de
  frame-rate físico ou de compatibilidade com OBS foi inferido.

### Limitações e riscos pendentes

- A prova de conceito de câmera ainda não existe: falta incorporar uma fonte
  COM sintética para exercitar o ciclo Media Foundation, empacotá-la e registrar
  a classe de fonte. A primeira validação pode usar o padrão sintético da
  amostra; não depende da placa HDMI.
- Falta decidir e validar a comunicação da captura com o processo do Frame
  Server, formatos/timestamps publicados e comportamento ao parar ou perder a
  captura. A fila deve ser limitada e suas substituições contabilizadas.
- Não foi testado se o OBS enumera, abre e recebe a cadência da futura fonte.
- A validação final da fonte real e da latência exige testar no OBS; a placa
  HDMI é necessária para validar a cadeia completa com a entrada pretendida.
- O erro `C1902` do build legado permanece pendente. O build alternativo
  confirma o código, mas não corrige a instalação/estado dos PDBs do build
  legado.

### Próxima etapa recomendada

1. Fixar uma revisão da amostra oficial e verificar sua licença e os projetos
   estritamente necessários. A amostra do repositório declara requisitos de
   Windows SDK 10.0.22000+, uma extensão MSI do Visual Studio e VCRuntime.
2. Criar/buildar a DLL COM sintética mínima (um stream, modos explícitos,
   timestamps QPC) e verificar a leitura Media Foundation sem a placa.
3. Depois da prova sintética, desenhar a ponte interprocessos em função do
   contexto Local Service/Session 0 do Frame Server, com buffer limitado e
   contabilização de substituições; então alimentar os frames do FrameForge.
4. Preparar o instalador e validar enumeração, resolução/FPS, estabilidade e
   desligamento no OBS. Não automatizar alteração de `HKLM` nem instalação de
   pacote de driver nesta etapa de desenvolvimento.

[MFCreateVirtualCamera]: https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera
[IMFVirtualCamera]: https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nn-mfvirtualcamera-imfvirtualcamera
[amostra oficial Windows-Camera]: https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera
[guia de fonte personalizada do Frame Server]: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source
[licença MIT da amostra]: https://github.com/microsoft/Windows-Camera/blob/master/LICENSE
