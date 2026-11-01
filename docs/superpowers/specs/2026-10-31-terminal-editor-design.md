# Diseño: prototipo de editor moderno para terminal

**Estado:** implementada como v0.1; pendiente de revisión manual en el terminal del usuario
**Fecha:** 2026-10-31

## Objetivo

Crear un prototipo exploratorio de editor de texto para terminal, inspirado en Nano, con una interfaz más moderna. Debe abrir, editar y guardar archivos, incluir un panel lateral con los archivos de la carpeta actual y los buffers abiertos, y mantener un consumo de memoria cercano al Nano observado por el usuario (aproximadamente 6,9 MiB de RSS).

El objetivo de memoria se medirá con el mismo método y entorno que Nano. Se aplica al arranque con un buffer vacío; el consumo aumentará al abrir archivos y no se promete un límite fijo para cualquier tamaño de archivo.

## Enfoque técnico

Implementar en C con ncurses para manejar ventanas, entrada de teclado y mouse, redibujado y colores. Se elige sobre el control manual de secuencias ANSI porque permite dedicar el prototipo al diseño y al comportamiento del editor, manteniendo el control directo de las asignaciones de memoria.

El programa aceptará una ruta como argumento; si aún no existe, iniciará un buffer nuevo asociado a esa ruta. Sin argumento, abrirá la carpeta actual para navegar desde el sidebar. El listado será de un nivel e incluirá una entrada `..` para volver al directorio padre; no recorrerá directorios recursivamente en segundo plano.

## Interfaz

- Dos paneles: sidebar a la izquierda y área de edición a la derecha.
- El sidebar mostrará archivos y directorios de la carpeta actual, además de una sección de buffers abiertos.
- El panel de edición mostrará el texto y números de línea blancos en el tema oscuro; el tema claro ajustará el contraste.
- La edición usará autoindentación y tabuladores de cuatro espacios, siguiendo la configuración actual de Nano.
- Sin barra superior, barra inferior persistente ni scrollbar. Los avisos de guardado y error serán temporales.
- Paleta oscura sobria con acentos turquesa sobre el fondo elegido en la terminal, adaptada a sus capacidades.
- El sidebar mostrará solo el explorador de archivos, con texto blanco y un control `×` para ocultarlo; al ocultarlo aparecerá `+` para mostrarlo. También podrá alternarse con `F2`.
- Un footer de una línea bajo el editor mostrará la rama Git actual, o `sin repo Git` fuera de un repositorio; el sidebar reservará toda su altura para archivos.
- El mouse será una forma principal de navegación: clic para ubicar el cursor o seleccionar elementos del sidebar, arrastre para seleccionar texto y rueda para desplazar el panel bajo el puntero.
- La configuración en `~/.config/terminal-editor/config` permitirá cambiar paleta, ancho de tabulación, autoindentación, preferencias del sidebar y comandos de lint por extensión.
- Los diagnósticos de lint aparecerán junto a la línea afectada, con marcas de severidad en el margen. No habrá un panel inferior persistente.
- La fuente la elige la aplicación de terminal. El proyecto incluirá instrucciones para cambiarla en la terminal; no intentará fijar una fuente de forma no portable.

## Comportamiento

- Un clic en un archivo lo abre; un clic en un directorio lo abre en el sidebar; un clic en un buffer lo activa.
- Un clic en el texto coloca el cursor; arrastrar selecciona texto. La rueda desplaza el panel bajo el puntero.
- Escribir y borrar con el teclado; reposicionar el cursor con clic y seleccionar texto arrastrando.
- Al crear una línea, conservar la indentación de la anterior; `Tab` insertará cuatro espacios.
- `Ctrl+S` y `Ctrl+O` guardan el buffer actual. Si no tiene ruta, solicitará una.
- `Ctrl+W` guarda automáticamente y cierra el buffer activo; para un buffer sin ruta, solicitará dónde guardarlo.
- Los cambios externos se detectarán mientras el editor está abierto. Los buffers limpios se recargarán automáticamente; los buffers con cambios locales conservarán su contenido y ofrecerán `Ctrl+R` para recargar o `Ctrl+S` para confirmar la sobrescritura del archivo externo.
- El árbol de archivos se actualizará cuando cambie el contenido del directorio actual.
- `Ctrl+X` cierra el editor. Si hay cambios sin guardar, pedirá confirmación.
- La configuración asociará extensiones de archivo con comandos de lint externos. El lint se ejecutará al guardar, con un límite de cinco segundos y 1 MiB de salida; leerá diagnósticos con el formato `ruta:línea:columna: severidad: mensaje`, donde severidad es `error`, `warning` o `info`; hacer clic en un diagnóstico llevará el cursor a su ubicación.
- Los errores de lectura o escritura se mostrarán como avisos temporales.

## Límites del prototipo

El prototipo ofrecerá configuración y lint mediante comandos externos configurados por extensión. Para mantener acotada la primera versión, los comandos deberán emitir diagnósticos en el formato documentado por el proyecto. No incluye API de plugins, autocompletado, LSP, edición colaborativa ni resaltado avanzado por lenguaje. La edición se organizará por líneas y los buffers se cargarán al abrirlos.

## Organización interna

- **Bucle de aplicación:** procesa eventos y controla el estado del editor.
- **Renderizado e entrada:** administra las ventanas de ncurses y traduce teclas en acciones.
- **Buffer:** mantiene líneas, cursor, cambios pendientes y operaciones de edición.
- **Archivos:** abre, lee y guarda rutas; informa errores sin descartar cambios.
- **Sidebar:** enumera el directorio actual y mantiene la lista de buffers abiertos.
- **Configuración y lint:** lee las preferencias, lanza el comando asociado al archivo y normaliza sus diagnósticos para mostrarlos en el editor.

El listado del sidebar será bajo demanda y de un solo nivel para limitar trabajo y memoria. El buffer de cada archivo se libera al cerrarlo.

## Criterios de aceptación

1. El programa inicia con o sin una ruta de archivo y permite editar texto; una ruta inexistente se trata como archivo nuevo.
2. El usuario puede navegar archivos del directorio actual, abrirlos, cambiar entre buffers y guardar los cambios.
3. Los cambios no guardados no se descartan accidentalmente al cerrar.
4. El sidebar se puede ocultar con `F2`; no aparecen barras fijas arriba, abajo ni scrollbar al costado.
5. El uso de memoria RSS del editor inactivo con buffer vacío se mide en el mismo entorno que Nano y se compara con los aproximadamente 6,9 MiB observados. Si se desvía de forma importante, se revisan las asignaciones y bibliotecas cargadas antes de dar el prototipo por terminado.
6. Al guardar un archivo con errores de lint, aparecen diagnósticos junto a las líneas afectadas; al pulsar uno con el mouse, el cursor salta a esa ubicación.
7. Cambiar opciones en el archivo de configuración cambia el tema y las preferencias acordadas sin recompilar.

## Riesgos y decisiones pendientes

- El RSS varía por sistema, terminal, biblioteca ncurses y método de medición; la comparación requiere el mismo entorno.
- Los procesos de lint son hijos externos y pueden elevar el consumo total de RAM mientras se ejecutan; se medirá aparte del RSS inactivo del editor.
- Los eventos de mouse dependen de que el emulador de terminal informe clics y rueda; el teclado seguirá disponible para escribir y usar atajos.
- La fuente es una preferencia del emulador de terminal; sus controles y formatos de configuración dependen de cada emulador. [ncurses](https://man7.org/linux/man-pages/man3/ncurses.3x.html) dibuja en celdas de caracteres; [xterm](https://invisible-island.net/xterm/manpage/xterm.html) tiene controles propios de fuentes que no son universales.
- El tamaño y el comportamiento del sidebar en terminales estrechas se ajustarán al construir el prototipo; en pantallas muy pequeñas podrá ocultarse.
- El soporte para Unicode y archivos con finales de línea distintos deberá definirse según lo que revele el primer prototipo.
