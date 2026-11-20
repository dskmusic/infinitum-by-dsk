# Infinitum by DSK

**Generador de atmósferas infinitas: un paisaje sonoro que nunca se repite.**

Instrumento gratuito VST3 / AU / Standalone de [DSK Music](https://www.dskmusic.com/infinitum/) para ambient, cine y meditación.
**[Descarga](#descarga)** · [Página del producto](https://www.dskmusic.com/infinitum/) · [Última versión](https://github.com/dskmusic/infinitum-by-dsk/releases/latest)
Pulsa **Play** y seis capas (un drone que muta, un pad, una textura granular, shimmer, viento y campanas) entran y salen solas,
mientras la armonía, la luz y el espacio evolucionan despacio durante minutos u horas sin repetirse nunca.

![Paisaje de Infinitum](docs/screenshots/landscape.png)

## Funciones

- **Motor de paisaje vivo:** seis capas que tocan solas (Drone, Pad, Textura, Shimmer, Viento, Campanas). Cada una tiene Nivel,
  Presencia (cuánto tiempo está presente), Tono y Carácter; una modulación lenta que nunca se repite decide cuándo aparece.
- **Drone con Spectral Mutation:** armónicos que mutan y derivan como genes.
- **Textura granular y Shimmer en primer plano:** una nube de granos afinada a octavas y quintas, y una reverb con
  realimentación transpuesta (octava u octava + quinta).
- **Armonía en movimiento:** 12 escalas, cualquier tonalidad, de quintas abiertas a acordes de 7ª y 9ª, con cambios de acorde
  automáticos y conducción de voces.
- **Cuatro macros grandes:** Evolución, Densidad, Luz (de noche a mediodía) y Espacio (de una sala a una catedral infinita), y **Congelar**.
- **Botón Play / Stop** que además sigue el transporte de tu DAW. La evolución sale de una **semilla** y de la posición en la línea
  de tiempo: lo que exportas del DAW suena igual que lo que oíste, y la misma semilla siempre da el mismo viaje.
- **Exportación de audio largo:** de 1 minuto a 3 horas en **WAV, FLAC u OGG**, con fundido final (YouTube, meditación, estudio).
- **Temporizador de sesión** con fundidos de entrada y salida, y afinación a **432 Hz**.
- **Generador por estados de ánimo:** 8 estados: Cine, Ambient, Meditación, Oscuro, Sci-Fi, Etéreo, Océano y Nostalgia (de tres a cinco variantes cada uno). Cada resultado se
  renderiza, se comprueba y se nivela antes de que lo oigas. Mutar, mezclar e historial de la sesión.
- **136 presets de fábrica** (incluidas noches de ciudad futurista bajo la lluvia y naves abandonadas), navegador con etiquetas, favoritos y valoraciones, comparación A/B, deshacer/rehacer, MIDI learn.
- **MIDI:** cambia la tonalidad desde tu teclado, toca notas encima, o ambas.
- **Seis paisajes animados** (Valle, Campo estelar, Nebulosa, Neón, Océano, Aurora) que reaccionan a cada capa, con **pantalla completa** y atajos de teclado.
- **Generador de ruido** (rosa, marrón, blanco, olas de océano, crepitar de vinilo), pad **analógico** de los 80 con chorus de ensemble, y campanas que tocan pequeñas frases melódicas.
- Escalable del 50 al 200 %, interfaz en **inglés y español** (automática o manual).

## Descarga

**Página del producto:** https://www.dskmusic.com/infinitum/

| Sistema | Recomendado | Otros formatos |
|---|---|---|
| **Windows** 10/11 (64 bits) | [Instalador (.exe)](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-win-x64-setup.exe) | [VST3 (zip)](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-win-x64-vst3.zip) · [Standalone (zip)](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-win-x64-standalone.zip) |
| **macOS** 11+ (Universal: Apple Silicon + Intel) | [Instalador (.pkg)](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-macos-universal-installer.pkg) | [VST3](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-macos-universal-vst3.zip) · [AU](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-macos-universal-au.zip) · [Standalone](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-macos-universal-standalone.zip) |
| **Linux** x86_64 | [VST3 (tar.gz)](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-linux-x64-vst3.tar.gz) | [Standalone (tar.gz)](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/Infinitum-by-DSK-linux-x64-standalone.tar.gz) |

Todos los archivos y notas: [última versión](https://github.com/dskmusic/infinitum-by-dsk/releases/latest) · checksums: [SHA256SUMS.txt](https://github.com/dskmusic/infinitum-by-dsk/releases/latest/download/SHA256SUMS.txt)

> Los binarios no están firmados. **Windows:** si aparece SmartScreen, pulsa *Más información* → *Ejecutar de todas formas*.
> **macOS:** si dice que no se puede abrir o que está dañado, haz clic derecho → *Abrir*, o ejecuta `xattr -dr com.apple.quarantine <archivo>` en Terminal.

### Primeros pasos

1. Carga **Infinitum** como instrumento y pulsa **Play** (en el plugin o en el transporte de tu DAW).
2. Gira **Evolución**, **Densidad**, **Luz** y **Espacio**, elige un paisaje o pulsa **GENERAR** en la página Generador.
3. Render largo para vídeos o meditación: página **Exportar** → duración → WAV, FLAC u OGG.

Los presets y ajustes se guardan en `Documentos/DSK Music/Infinitum/`.

## Compilar

Ver la sección *Build from source* del [README en inglés](README.md).

## Licencia

GNU GPL v3 (ver [LICENSE](LICENSE)). Hecho con JUCE (GPLv3) y el VST3 SDK (MIT). Fuente Orbitron con licencia SIL OFL.
Partes del motor (Spectral Mutation, reverb FDN, shimmer) vienen de [Genesix by DSK](https://github.com/dskmusic/genesix-by-dsk).
