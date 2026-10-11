<p align="center"><img src="sce_sys/icon0.png" width="128"/></p>
<h1 align="center">Visor</h1>
<p align="center">Seu Meta Quest 2 na TV, direto por um PS5 desbloqueado.<br/>
<a href="README.md">English</a></p>

![Tela inicial do Visor](docs/home.png)

O Visor é um homebrew de PS5 que espelha o Quest 2 na TV. O PS5 fala com o
headset direto pelo Wi-Fi: sem PC no meio e sem instalar nada no Quest.

- Acha os headsets da rede sozinho
- Um olho só, recortado em 16:9, em vez da imagem dupla crua
- Presets de qualidade (resolução, bitrate, fps) ou valores manuais, H.264 ou H.265
- Enquadramento ao vivo pelo direcional enquanto assiste
- Atalho na tela inicial do PS5
- Português e inglês

## Requisitos

- PS5 desbloqueado com um carregador de ELF na porta 9021 (todo jailbreak atual
  tem). Mais nada: sem websrv, sem Homebrew Launcher.
- Quest 2 em modo desenvolvedor com ADB por Wi-Fi ligado. Isso pede cabo USB e
  um computador uma vez a cada reinício do headset:

  ```
  adb tcpip 5555
  ```

- PS5 e headset na mesma rede.

## Instalação

1. Baixe o `visor.elf` nas [releases](../../releases).
2. Envie para o PS5 como qualquer outro payload, por exemplo:

   ```
   socat -t 99999999 - TCP:<ip-do-ps5>:9021 < visor.elf
   ```

3. Uma notificação avisa que o Visor está pronto e o ícone **Visor** aparece
   na tela inicial. Abra por ali.

O `visor.elf` é um arquivo só: ele se extrai em `/data/visor`, cria o ícone e
fica em segundo plano para abrir o app quando o ícone é apertado. Como todo
payload, ele some quando o console reinicia: envie de novo depois do jailbreak
(ou coloque na lista de autoload do seu carregador). O ícone e os ajustes ficam.

Na primeira conexão, coloque o headset: ele pergunta se permite a depuração
USB. Marque "Sempre permitir".

## Controles

| Tela | Botão | Ação |
| --- | --- | --- |
| Menus | Direcional / X / Círculo | Mover / selecionar / voltar |
| Assistindo | Options | Mostra ou esconde os números e as dicas |
| Assistindo | Direcional | Reenquadra a imagem |
| Assistindo | L1 / R1 | Afasta / aproxima o zoom |
| Assistindo | Círculo | Para |

Reenquadrar reinicia a transmissão (cerca de um segundo), porque o recorte é
feito no headset.

## Limites

- **Sem áudio.** Capturar o som deixaria mudo quem está com o headset.
- **Só Quest 2 por enquanto.** Outros headsets têm telas diferentes e o recorte
  do olho sairia errado. "Os dois" olhos, nos ajustes, funciona em qualquer um.
- O headset precisa estar acordado (na cabeça) para transmitir.

## Como funciona

O Visor leva um cliente ADB próprio. Ele conecta no headset pela porta 5555,
envia o servidor do [scrcpy](https://github.com/Genymobile/scrcpy), inicia com
o recorte e a qualidade escolhidos e decodifica o vídeo com FFmpeg numa textura
SDL2. A interface é RmlUi.

O app é aberto como um aplicativo normal do PS5 pelo carregador de homebrew do
[websrv](https://github.com/ps5-payload-dev/websrv), embutido no `visor.elf`.

## Compilando

No Ubuntu 24.04 (WSL serve), `sudo bash tools/setup-wsl.sh` instala os
compiladores e o toolchain do
[ps5-payload-dev](https://github.com/ps5-payload-dev/pacbrew-repo). Depois:

```
bash tools/build-ps5.sh                             # dist/visor.elf
bash tools/send-elf.sh <ip-do-ps5> dist/visor.elf   # envia para o console
```

O log da última execução fica em `/data/visor/visor.log` no console.
`curl http://<ip-do-ps5>:8744/` abre o app sem pegar no controle.

```
```

Também há um build de PC para trabalhar sem o console:

```
bash tools/run-coretest.sh <ip-do-quest>   # ADB + stream + decode, salva um frame
bash tools/uishot.sh saida.bmp "down ok"   # renderiza uma tela sem monitor
```

## Licença

GPL-3.0. Componentes de terceiros estão em [NOTICE.md](NOTICE.md).
