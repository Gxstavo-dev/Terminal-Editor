# Versiones y contexto del proyecto

## Objetivo

Editor de texto para terminal inspirado en Nano, con interfaz moderna, mouse como navegación principal y consumo de RAM cercano al Nano observado por el usuario: aproximadamente **6,9 MiB de RSS cuando el editor está inactivo y tiene un buffer vacío**. El consumo de los procesos externos de lint se mide aparte.

## Decisiones actuales

- Implementación en C con ncurses.
- Sidebar con archivos y directorios de la carpeta actual, navegación al directorio padre y buffers abiertos.
- Mouse para mover el cursor, seleccionar texto, navegar el sidebar y desplazar el contenido. Teclado para escribir y atajos.
- Sin barras persistentes arriba o abajo ni scrollbar; números de línea blancos en tema oscuro y paleta con acentos turquesa.
- Fuente elegida en la aplicación de terminal; el proyecto documentará cómo cambiarla en distintos emuladores.
- Configuración para tema, tabulación, autoindentación, sidebar y comandos de lint por extensión.
- Linters externos se ejecutan al guardar (límite: cinco segundos y 1 MiB de salida). Los errores y avisos aparecen inline junto a las líneas y se pueden pulsar para saltar a su ubicación.
- La API de plugins se pospone hasta que la configuración y la integración de diagnósticos estén asentadas.

## Hoja de ruta

### v0.1 — Prototipo funcional

- Abrir archivos por argumento o desde el sidebar; editar, crear y guardar archivos.
- Mantener varios buffers y proteger los cambios sin guardar.
- Sidebar de archivos y buffers, operable con mouse y ocultable con `F2`.
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

El diseño está escrito en `docs/superpowers/specs/2026-10-31-terminal-editor-design.md` y espera revisión del usuario. Después de aprobarlo, se prepara el plan de implementación y se empieza v0.1. Mantener este archivo actualizado al cerrar cada versión o cambiar una decisión.
