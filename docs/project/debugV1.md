Este archivo conserva los problemas encontrados durante el prototipo, cómo se localizaron y qué cambios los resolvieron. Las rutas se refieren a este repositorio, salvo cuando se indica la configuración de Kitty.

## 1. `Ctrl+S` no guardaba

**Síntoma:** al presionar `Ctrl+S`, el editor no recibía la tecla.

**Causa:** el modo `cbreak()` deja activo el control de flujo IXON de la terminal. En ese modo, `Ctrl+S` puede ser consumido como pausa de salida antes de llegar a ncurses.

**Solución:** `src/ui.c` usa `raw()` y trata el código de `Ctrl+S` (`19`) como guardar, junto con `Ctrl+O`. Como `raw()` también desactiva las señales normales, `Ctrl+C` (`3`) se maneja explícitamente como salida. Las entradas de nueva línea aceptan `\n`, `\r` y `KEY_ENTER`.

## 2. Los iconos del árbol aparecían como rectángulos vacíos

**Síntoma:** `▱` y `▯` se veían como figuras geométricas vacías, no como carpetas y archivos.

**Causa:** esos caracteres Unicode son rectángulos/paralelogramos, no iconos de archivo. Cambiar a Noto Sans Mono no podía convertirlos en iconos.

**Solución:** `src/ui.c` usa los glifos de Nerd Fonts U+F07B (carpeta) y U+F15B (archivo). En `~/.config/kitty/kitty.conf` se configuró el mapa de Kitty:

```conf
font_family Noto Sans Mono
symbol_map U+F07B,U+F15B Symbols Nerd Font Mono
```

La configuración debe recargarse en Kitty con `Ctrl+Shift+F5`, y el editor debe iniciarse con el binario recompilado.

## 3. El árbol del sidebar se veía muy apretado

**Síntoma:** los nombres estaban demasiado cerca de los iconos y las filas tenían poco espacio vertical.

**Solución:** `src/ui.c` deja más separación horizontal y usa una fila vacía entre elementos. El cálculo de índices para clics y scroll se ajustó a ese espaciado.

## 4. El sidebar parpadeaba al desplazar el texto y llegaba a mostrar archivos duplicados

**Síntoma:** al desplazarse por el contenido, el sidebar parpadeaba; en algunos repintados quedaban entradas repetidas o residuos de filas anteriores.

**Intentos que no bastaron:** se quitó el borrado de pantalla completa y se desactivaron las optimizaciones de inserción/eliminación de líneas de ncurses. El usuario confirmó que el parpadeo continuaba, así que esas medidas por sí solas no resolvían el problema.

**Primera corrección:** el árbol se redibuja solo cuando cambia su estado visible: directorio, archivos, selección, desplazamiento, tamaño o presencia de un mensaje. Cuando se actualiza, se limpia toda el área del sidebar; la clave de renderizado está en `sidebar_key()` de `src/ui.c`. El usuario reportó después que el parpadeo persistía.

**Causa adicional encontrada:** el bucle principal volvía a dibujar el contenido del editor cada vez que vencía el timeout de 200 ms, aunque no hubiera entrada. Kitty puede mostrar actualizaciones parciales de aplicaciones de pantalla completa si estas repintan continuamente.

**Corrección actual, pendiente de confirmación visual:** el bucle ahora redibuja solo después de una entrada o cuando cambia la visibilidad de un aviso. En Kitty, cada actualización de pantalla se envuelve en el modo de actualización sincronizada `CSI ? 2026 h/l`, para que el terminal presente el cuadro completo. El código está en `ui_run()` y `draw_screen()` de `src/ui.c`. Kitty documenta que su modo de actualización sincronizada suprime la presentación de estados intermedios ([rendimiento de Kitty](https://sw.kovidgoyal.net/kitty/performance/)); también advierte que el repintado continuo de aplicaciones de pantalla completa puede causar parpadeo con ciertos retardos de entrada ([configuración de Kitty](https://sw.kovidgoyal.net/kitty/conf/#opt-kitty.input_delay)).

## 5. Borde derecho del sidebar

**Solicitud:** dibujar un borde con color `#313131`.

**Solución:** `src/ui.c` usa un par de color dedicado. En terminales que permiten cambiar colores, toma temporalmente la entrada 255 de la paleta, la ajusta a `#313131` y restaura el valor previo al salir del editor. Si el terminal no permite cambiar la paleta, utiliza un color aproximado.

## 6. Fondo negro en lugar del fondo de la terminal

**Síntoma:** el editor pintaba explícitamente un fondo negro, que no coincidía con el fondo configurado en la terminal.

**Solución:** los pares normales de ncurses usan fondo predeterminado (`-1`) mediante `use_default_colors()`. Así el fondo proviene de Kitty u otra terminal; los pares de selección conservan su fondo propio.

## 7. Medición de memoria del proceso

Desde el entorno de Codex no se veía el proceso abierto en la terminal del usuario: `ps` corre en un espacio de procesos aislado. La lectura hecha por el usuario fue `RSS=5496 KiB`, aproximadamente **5.37 MiB**. En la medición anterior de Nano, `7076 KiB` equivalían a aproximadamente **6.91 MiB**.

Para consultar RSS en la misma terminal donde corre el editor:

```bash
ps -C terminal-editor -o pid,rss,cmd
```

RSS se expresa en KiB; dividirlo entre 1024 da MiB.

## 8. Cambios externos en archivos abiertos

**Necesidad:** mostrar cambios hechos desde fuera del editor mientras el archivo sigue abierto.

**Solución:** cada buffer conserva una instantánea de metadatos del archivo (`dev`, `ino`, tamaño, `mtime` y `ctime`). El bucle de eventos la comprueba cada vez que vence el timeout de entrada. Si el buffer está limpio, carga el archivo actualizado conservando la posición del cursor cuando sea posible. Si tiene cambios locales, no sustituye el contenido: muestra un aviso persistente. `Ctrl+R` permite recargar el archivo y confirma antes de descartar cambios locales; `Ctrl+S` confirma antes de sobrescribir una versión externa.

**Límite:** la detección usa metadatos del sistema de archivos y el sondeo ocurre durante los tiempos de espera del bucle; no observa cada escritura instantáneamente.

El mismo sondeo compara los metadatos del directorio actual. Si se crea o elimina una entrada desde otra aplicación, vuelve a listar el directorio y actualiza el sidebar, conservando la selección si el archivo seleccionado aún existe.

## 10. El cursor de texto terminaba sobre `Git: main`

**Síntoma:** el cursor visible del terminal aparecía en el footerbar y parecía apuntar siempre a `Git: main`.

**Causa:** `draw_editor()` colocaba el cursor sobre el contenido, pero `draw_footerbar()` escribía después en la última fila y cambiaba la posición activa de ncurses.

**Solución:** `draw_footerbar()` guarda y restaura las coordenadas de ncurses alrededor de su dibujo, dejando el cursor en el editor.

## 11. El cursor de texto era demasiado ancho

**Solicitud:** mostrar un cursor fino tipo barra vertical.

**Solución:** `src/ui.c` solicita el estilo DECSCUSR 6 (barra vertical fija) al iniciar y restaura el estilo predeterminado DECSCUSR 0 al salir. El terminal debe soportar esta secuencia para mostrar el cambio.

## 12. Se descartó el zoom con `Ctrl` + rueda

**Síntoma:** `Ctrl` + rueda no cambiaba el tamaño de fuente en el entorno del usuario, incluso después de permitir `BUTTON_CTRL` en ncurses y configurar Kitty para aceptar el comando remoto `set-font-size`.

**Resultado:** se retiró la integración porque no funcionó de forma fiable. Se quitó también la autorización remota agregada a `~/.config/kitty/kitty.conf`. La rueda vuelve a usarse únicamente para desplazarse por el contenido o el sidebar.

## 9. El footer duplicaba su contenido al desplazar el editor

**Síntoma:** el footer se veía duplicado al llegar al final del documento y el rótulo de Git se veía demasiado pequeño.

**Causa:** el rótulo se imprimía con el protocolo de escalado de texto de Kitty directamente en la terminal, fuera del estado de pantalla que ncurses mantiene. Al actualizar filas durante el scroll, esa salida podía quedar desincronizada.

**Solución:** el rótulo ahora se dibuja con ncurses al tamaño normal del texto del editor. Se conserva la fila vacía de separación sobre el footer.

## Verificación de compilación

Después del cambio al bucle de repintado por eventos, `make` compiló correctamente y `git diff --check` no reportó problemas. Falta que el usuario confirme el resultado visual en Kitty.
