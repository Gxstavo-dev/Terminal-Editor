# Características de NanoTUI — v1

Resumen de las funciones disponibles en la primera versión del editor de terminal.

## Abrir y navegar archivos

- Abre un archivo existente o crea un buffer para una ruta nueva: `./terminal-editor archivo.md`.
- Si se pasa una carpeta, la muestra en el árbol lateral. Sin argumento, muestra la carpeta actual.
- El sidebar lista carpetas y archivos del directorio actual, con iconos y selección visual.
- El botón `+` junto al título permite crear archivos o carpetas en el directorio actual. El archivo nuevo se abre al crearlo y no se sobrescriben nombres existentes.
- Refresca el árbol cuando se crean o borran archivos del directorio desde otro programa.
- Un clic abre archivos y entra en carpetas; `..` permite subir al directorio padre.
- El clic derecho sobre un archivo permite borrarlo permanentemente tras confirmar. No borra carpetas ni archivos con cambios locales sin guardar.
- Hacer clic en un archivo que ya está abierto vuelve a su buffer.
- El sidebar se oculta con `×` o `F2` y se vuelve a mostrar con `+` o `F2`.
- Un footer de una línea debajo del editor muestra la rama Git actual en blanco o `sin repo Git` fuera de un repositorio; el sidebar conserva todo su alto para archivos.
- En Kitty, el puntero cambia a mano sobre elementos interactivos del sidebar.

## Editar texto

- Haz clic en el texto para colocar el cursor.
- Arrastra para seleccionar texto; Backspace elimina la selección o el carácter anterior.
- Escribe con el teclado. `Tab` inserta espacios y Enter crea una línea nueva.
- `Ctrl+C` copia la selección al portapapeles del sistema como UTF-8, incluyendo los saltos de línea seleccionados y los arrastres en cualquier dirección. Sin selección muestra `Select text to copy` y no modifica el portapapeles.
- `Ctrl+V` pega el portapapeles en el cursor y reemplaza la selección activa. Conserva Unicode y saltos de línea, inserta los saltos sin autoindentación y registra un solo paso de deshacer. Si el texto no es UTF-8 válido o falla la asignación de memoria, el buffer y el historial no cambian.
- La autoindentación conserva la indentación de la línea anterior cuando está activada.
- La vista del contenido se desplaza con la rueda del mouse sobre el panel de edición.
- Muestra números de línea, usa el fondo de la terminal y no mantiene barras superior o inferior ni scrollbar.

## Guardar y salir

- `Ctrl+S` y `Ctrl+O` guardan el buffer actual.
- `Ctrl+Z` deshace la última edición y `Ctrl+Y` la rehace. Cada buffer conserva hasta 100 estados; una edición nueva descarta su historial de rehacer.
- `Ctrl+W` guarda automáticamente los cambios y cierra el buffer activo; si no tiene ruta, solicita una. Si el guardado falla o se cancela, el buffer permanece abierto.
- Si el buffer todavía no tiene nombre, el editor solicita una ruta.
- `Ctrl+X` sale; si hay cambios sin guardar, solicita confirmación. `Ctrl+C` ya no cierra el editor.

## Cambios hechos desde otros programas

- Comprueba periódicamente si cambió en disco algún archivo abierto.
- Recarga automáticamente un buffer limpio cuando detecta cambios externos.
- Si el buffer tiene cambios locales, conserva el texto y muestra un aviso persistente.
- `Ctrl+R` recarga la versión externa. Si hay cambios locales, pide confirmación antes de descartarlos.
- `Ctrl+S` permite guardar los cambios locales, pero pide confirmación antes de sobrescribir la versión externa.

## Lint y diagnósticos

- Puede ejecutar un comando de lint configurado para la extensión del archivo después de guardar o recargar.
- El proceso de lint tiene un límite de cinco segundos y 1 MiB de salida.
- Los diagnósticos aparecen junto a las líneas afectadas, con una marca de severidad en el margen.
- Al hacer clic en un diagnóstico o su marca, el cursor salta a esa ubicación.
- El formato de cada diagnóstico es `ruta:línea:columna: severidad: mensaje`, con severidad `error`, `warning` o `info`.

## Configuración

El archivo de configuración está en `~/.config/terminal-editor/config`, o en `$XDG_CONFIG_HOME/terminal-editor/config` si se define `XDG_CONFIG_HOME`.

```ini
theme=dark
tab_width=4
autoindent=true
sidebar_visible=true
sidebar_width=28
lint.c=clang -Wall -fsyntax-only
```

Se pueden configurar tema oscuro o claro, ancho de tabulación de 1 a 16 espacios, autoindentación, visibilidad y ancho del sidebar, y comandos de lint por extensión. Los comandos se ejecutan directamente, sin shell; el editor agrega la ruta del archivo como último argumento.

## Terminal, fuente e iconos

- La fuente se configura en el emulador de terminal, no en el editor.
- En Kitty, los iconos del sidebar usan Nerd Font Symbols y el puntero cambia al pasar sobre controles interactivos.
- El sidebar incluye un separador derecho; en terminales con colores configurables se muestra en `#313131`.

## Portapapeles del sistema

- En Wayland usa `wl-copy` y `wl-paste` del paquete `wl-clipboard`.
- En X11 usa `xclip` o `xsel`.
- Son dependencias opcionales de ejecución: el editor las detecta al copiar o pegar y las ejecuta directamente, sin shell.
- Si falta un helper o la operación falla, muestra un aviso y no modifica el buffer.

## Distribución y licencia

- El proyecto se distribuye bajo GNU GPL-3.0-or-later, con atribución a `Gxstavo-dev` (2026).
- `./packaging/appimage/build.sh` compila y genera `dist/terminal-editor-x86_64.AppImage` para Linux x86_64.
- El AppImage se ejecuta desde una terminal, con o sin un archivo como argumento.
- Los AppDir generados, las herramientas descargadas para empaquetar y los `.AppImage` quedan fuera de Git.

## Uso de memoria observado

En la terminal del usuario se midieron **5496 KiB (≈5.37 MiB) de RSS** para el editor abierto. La medición anterior de Nano fue **7076 KiB (≈6.91 MiB)**. El consumo puede variar según los archivos abiertos y los procesos externos de lint; RSS de lint se contabiliza aparte.

## Límites de v1

- No tiene API de plugins, LSP, autocompletado ni resaltado avanzado por lenguaje.
- No ofrece búsqueda y reemplazo.
- El explorador es de un solo nivel; entrar a una carpeta cambia el directorio mostrado.
- La recarga externa se detecta por sondeo de metadatos del archivo, no mediante un servicio de notificaciones del sistema.
- Los iconos Nerd Font y el cambio de puntero dependen de Kitty y de su soporte de protocolo/configuración.
- Copiar y pegar dependen de `wl-clipboard`, `xclip` o `xsel` y del servicio de portapapeles de la sesión gráfica.
- El AppImage es un artefacto para Linux x86_64; no es un paquete para macOS ni Windows.
