# Diseño: prototipo de editor moderno para terminal

**Estado:** propuesta para revisión  
**Fecha:** 2026-10-31

## Objetivo

Crear un prototipo exploratorio de editor de texto para terminal, inspirado en Nano, con una interfaz más moderna. Debe abrir, editar y guardar archivos, incluir un panel lateral con los archivos de la carpeta actual y los buffers abiertos, y mantener un consumo de memoria cercano al Nano observado por el usuario (aproximadamente 6,9 MiB de RSS).

El objetivo de memoria se medirá con el mismo método y entorno que Nano. Se aplica al arranque con un buffer vacío; el consumo aumentará al abrir archivos y no se promete un límite fijo para cualquier tamaño de archivo.

## Enfoque técnico

Implementar en C con ncurses para manejar ventanas, entrada de teclado, redibujado y colores. Se elige sobre el control manual de secuencias ANSI porque permite dedicar el prototipo al diseño y al comportamiento del editor, manteniendo el control directo de las asignaciones de memoria.

El programa aceptará una ruta como argumento; si aún no existe, iniciará un buffer nuevo asociado a esa ruta. Sin argumento, abrirá la carpeta actual para navegar desde el sidebar. El listado será de un nivel; no recorrerá directorios recursivamente en segundo plano.

## Interfaz

- Dos paneles: sidebar a la izquierda y área de edición a la derecha.
- El sidebar mostrará archivos y directorios de la carpeta actual, además de una sección de buffers abiertos.
- El panel de edición mostrará el texto y números de línea blancos.
- La edición usará autoindentación y tabuladores de cuatro espacios, siguiendo la configuración actual de Nano.
- Sin barra superior, barra inferior persistente ni scrollbar. Los avisos de guardado y error serán temporales.
- Paleta oscura sobria con acentos turquesa, adaptada a las capacidades del terminal.
- El sidebar podrá ocultarse y mostrarse con `F2` para ampliar el área de edición. `Tab` cambiará el foco entre sidebar y editor.

## Comportamiento

- Navegar el sidebar con las flechas; `Enter` abre un archivo o entra en un directorio.
- Editar texto con inserción, borrado y movimiento del cursor.
- Al crear una línea, conservar la indentación de la anterior; `Tab` insertará cuatro espacios.
- `Ctrl+O` guarda el buffer actual, siguiendo la convención de Nano. Si no tiene ruta, solicitará una.
- `Ctrl+X` cierra el editor. Si hay cambios sin guardar, pedirá confirmación.
- Los errores de lectura o escritura se mostrarán como avisos temporales.

## Límites del prototipo

La primera versión se concentra en edición de texto, archivos y exploración visual. No incluye autocompletado, LSP, plugins, edición colaborativa ni resaltado avanzado por lenguaje. La edición se organizará por líneas y los buffers se cargarán al abrirlos.

## Organización interna

- **Bucle de aplicación:** procesa eventos y controla el estado del editor.
- **Renderizado e entrada:** administra las ventanas de ncurses y traduce teclas en acciones.
- **Buffer:** mantiene líneas, cursor, cambios pendientes y operaciones de edición.
- **Archivos:** abre, lee y guarda rutas; informa errores sin descartar cambios.
- **Sidebar:** enumera el directorio actual y mantiene la lista de buffers abiertos.

El listado del sidebar será bajo demanda y de un solo nivel para limitar trabajo y memoria. El buffer de cada archivo se libera al cerrarlo.

## Criterios de aceptación

1. El programa inicia con o sin una ruta de archivo y permite editar texto; una ruta inexistente se trata como archivo nuevo.
2. El usuario puede navegar archivos del directorio actual, abrirlos, cambiar entre buffers y guardar los cambios.
3. Los cambios no guardados no se descartan accidentalmente al cerrar.
4. El sidebar se puede ocultar con `F2`; no aparecen barras fijas arriba, abajo ni scrollbar al costado.
5. El uso de memoria RSS al iniciar con buffer vacío se mide en el mismo entorno que Nano y se compara con los aproximadamente 6,9 MiB observados. Si se desvía de forma importante, se revisan las asignaciones y bibliotecas cargadas antes de dar el prototipo por terminado.

## Riesgos y decisiones pendientes

- El RSS varía por sistema, terminal, biblioteca ncurses y método de medición; la comparación requiere el mismo entorno.
- El tamaño y el comportamiento del sidebar en terminales estrechas se ajustarán al construir el prototipo; en pantallas muy pequeñas podrá ocultarse.
- El soporte para Unicode y archivos con finales de línea distintos deberá definirse según lo que revele el primer prototipo.
