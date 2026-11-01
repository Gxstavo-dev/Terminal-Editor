# Versiones y contexto del proyecto esto es testo de la forma en la que se guarda las cosas

## Objetivo

Editor de texto para terminal inspirado en Nano, con interfaz moderna, mouse como navegación principal y consumo de RAM cercano al Nano observado por el usuario: aproximadamente **6,9 MiB de RSS cuando el editor está inactivo y tiene un buffer vacío**. El consumo de los procesos externos de lint se mide aparte.

## Decisiones actuales

- Implementación en C con ncurses.
- Sidebar con archivos y directorios de la carpeta actual, navegación al directorio padre y buffers abiertos.
- Mouse para mover el cursor, seleccionar texto, navegar el sidebar y desplazar el contenido. Teclado para escribir y atajos.
- Sin barras persistentes arriba o abajo ni scrollbar; fondo de la terminal, números de línea blancos en tema oscuro y paleta con acentos turquesa.
- Fuente elegida en la aplicación de terminal; el proyecto documentará cómo cambiarla en distintos emuladores.
- Configuración para tema, tabulación, autoindentación, sidebar y comandos de lint por extensión.
- Linters externos se ejecutan al guardar (límite: cinco segundos y 1 MiB de salida). Los errores y avisos aparecen inline junto a las líneas y se pueden pulsar para saltar a su ubicación.
- La API de plugins se pospone hasta que la configuración y la integración de diagnósticos estén asentadas.
- Medición más reciente del binario en este entorno: **5.048 kB RSS** con un buffer vacío; la comparación exacta debe hacerse en el equipo del usuario.

## Hoja de ruta

### v0.1 — Prototipo funcional

- Abrir archivos por argumento o desde el sidebar; editar, crear y guardar archivos.
- Mantener varios buffers y proteger los cambios sin guardar.
- Sidebar de archivos con texto blanco, selección sutil y controles `×`/`+`; operable con mouse y ocultable con `F2`.
- Guardado con `Ctrl+S` y `Ctrl+O`.
- Historial de deshacer/rehacer por buffer: `Ctrl+Z` y `Ctrl+Y`, con un máximo de 100 ediciones.
- `Ctrl+W` guarda los cambios y cierra el buffer activo.
- Detectar cambios externos: recargar automáticamente buffers limpios; conservar buffers modificados y ofrecer recarga con `Ctrl+R` o sobrescritura confirmada con `Ctrl+S`.
- Actualizar automáticamente el árbol lateral al crear o borrar archivos desde fuera del editor.
- Borrar archivos desde el sidebar con clic derecho y confirmación; proteger carpetas y buffers con cambios locales.
- Mostrar la rama Git actual en un footer de una línea bajo el editor y actualizarla al cambiar de rama.
- Configuración legible en `~/.config/terminal-editor/config` para tema, tabulación, autoindentación, sidebar y comandos de lint.
- Ejecutar el linter configurado al guardar y mostrar diagnósticos inline tipo ErrorLens. Formato inicial de diagnóstico: `ruta:línea:columna: severidad: mensaje` (`error`, `warning` o `info`).
- Medir RSS inactivo en el mismo entorno que Nano; revisar el diseño si se aleja mucho de 6,9 MiB.

### v0.2 — Extensiones

- Diseñar una API pequeña de plugins sobre los puntos de integración ya estabilizados.
- Permitir extensiones de lenguaje y comandos sin acoplarlos al núcleo del editor.
- Medir por separado el costo de memoria de cada extensión.

### v0.3 — Compatibilidad y pulido

- Ampliar integraciones de lenguajes y formatos de diagnóstico según el uso del prototipo.
- Mejorar Unicode, archivos grandes y adaptación a terminales estrechas.
- Ajustar interacción de mouse, diagnósticos y preferencias tras probar el prototipo.

## Estado y siguiente paso

La v0.1 está implementada en la rama `feature/v0.1-prototype` y compila sin advertencias. La interacción completa aún requiere revisión manual en el terminal del usuario. Mantener este archivo actualizado al cerrar cada versión o cambiar una decisión.
