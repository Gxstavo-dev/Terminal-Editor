# Diseño: ampliar herramientas de lenguaje de la v2

## Objetivo

Completar la v2 de terminal-editor con resaltado local para Python, Rust, TSX, JSX, JavaScript, TypeScript, Go, C y C++; y funciones LSP de diagnósticos, autocompletado automático, formato y refactorización. La prioridad sigue siendo conservar una experiencia ágil y limitar el consumo de memoria.

Esta especificación amplía el alcance inicial descrito en `2026-11-01-v2-syntax-diagnostics-design.md`, que dejaba autocompletado, formato y refactorización para versiones futuras.

## Alcance y compatibilidad

### Resaltado local

- Implementar lexers ligeros dentro del proceso C del editor, sin iniciar procesos de lenguaje para resaltar texto.
- Extensiones cubiertas: `.py`, `.rs`, `.tsx`, `.jsx`, `.js`, `.ts`, `.go`, `.c`, `.h`, `.cc`, `.hh`, `.cpp`, `.cxx`, `.hpp` y `.hxx`.
- Usar un lexer C/C++ compartido para las extensiones de esa familia. El resaltado es léxico, no pretende sustituir análisis semántico ni compilación.
- Respetar el tema, selección, scroll horizontal y rendering incremental de la vista actual. No persistir copias de tokens por carácter o línea.

### Servidores de lenguaje

Detectar por extensión el primer ejecutable disponible en `PATH` y mantener como máximo una sesión para el buffer activo:

| Lenguaje | Extensiones | Servidor |
| --- | --- | --- |
| Python | `.py` | `pylsp` |
| Rust | `.rs` | `rust-analyzer` |
| TypeScript/JavaScript | `.ts`, `.tsx`, `.js`, `.jsx` | `typescript-language-server --stdio` |
| Go | `.go` | `gopls` |
| C/C++ | extensiones listadas arriba | `clangd` |

- Los servidores son dependencias externas: el editor los detecta, pero no los instala ni descarga.
- Conservar la configuración avanzada `language.<extension>` para sustituir el comando predeterminado.
- Determinar la raíz del proyecto mediante marcadores del lenguaje, como `pyproject.toml`, `Cargo.toml`, `package.json`, `go.mod`, `compile_commands.json` o `.git`, con un fallback seguro al directorio del documento.
- Negociar las capacidades durante `initialize`; solo ofrecer una función que el servidor anuncie. Si falta el ejecutable o una capacidad, la edición y el resaltado continúan y se muestra un mensaje breve.
- El servidor se inicia al activar un archivo compatible, permanece limitado al buffer activo y se cierra al cambiar a un buffer sin servidor, cerrar el buffer o salir.

## Interacciones

### Autocompletado

- Solicitar sugerencias automáticamente después de 250 ms sin nuevas teclas, tras un carácter de identificador o un disparador que anuncie el servidor. No solicitar al mover el cursor, al escribir texto en la sidebar ni cuando el editor no tenga el foco.
- Cancelar solicitudes anteriores cuando cambia el documento o se emite otra solicitud. Ignorar respuestas cuya versión ya no corresponda al buffer.
- Mostrar como máximo 50 sugerencias en una lista compacta. `↑` y `↓` recorren la lista; `Enter` o `Tab` acepta; `Esc` cierra. Sin lista abierta, las flechas mueven el cursor como ahora.
- No almacenar respuestas viejas: liberar la lista al cerrarla, aceptar un elemento, cambiar de buffer o recibir un nuevo resultado.

### Formato

- `F3` solicita `textDocument/formatting` al servidor activo.
- Validar y aplicar todas las ediciones de texto del documento como una operación de buffer deshacible. Actualizar el servidor con el nuevo contenido después de aplicar el formato.
- Si el servidor no ofrece formato, devuelve una respuesta inválida o las ediciones se solapan, conservar el contenido y mostrar el motivo.

### Refactorización y renombrado

- `F4` solicita `textDocument/codeAction` para la selección o posición actual y presenta una lista breve con las acciones disponibles. Incluir las acciones de refactor que el servidor proporcione, como extraer o reorganizar código, sin hardcodear acciones de un solo lenguaje.
- `F5` pide el nuevo nombre y llama a `textDocument/rename` para el símbolo bajo el cursor.
- Validar y mostrar confirmación antes de aplicar un cambio que afecte varios archivos. Las ediciones de archivos ya abiertos se reflejan en sus buffers; las de otros archivos se aplican de forma segura y se guardan solo después de la confirmación.
- Agrupar los cambios de cada buffer afectado como una sola operación deshacible. No aplicar respuestas que apunten fuera del workspace raíz ni operaciones de archivo que el editor no pueda validar con seguridad; informar de las acciones no compatibles.
- Aplicar `WorkspaceEdit` respetando sus versiones de documento y detectar solapamientos o versiones obsoletas antes de cambiar cualquier contenido, para evitar aplicar parcialmente una refactorización.

## Diseño técnico

- Extender el cliente LSP existente en C con requests JSON-RPC con identificadores, cola/respuesta no bloqueante, cancelación de solicitudes obsoletas, límites de mensaje y negociación de capacidades.
- Mantener la lectura/escritura en el event loop sin esperar sincrónicamente a un servidor. La UI continúa aceptando teclas mientras el servidor calcula.
- Convertir posiciones LSP UTF-16 a offsets/columnas del buffer UTF-8 de forma validada. Aplicar las ediciones de cada documento de atrás hacia adelante para conservar offsets.
- Representar la selección de completion/code actions como estado temporal de UI; no crear una estructura persistente por carácter o por buffer.
- Mantener separados los diagnósticos de lint y LSP, compartiendo el ErrorLens ya existente.
- Para el autocompletado, limitar el trabajo por debounce, cancelación y máximo de elementos; el servicio externo puede mantener su índice del proyecto mientras el documento compatible está activo.

## Medición de memoria y apertura

Antes de introducir las nuevas capacidades, registrar una línea base del binario actual y repetirla con el resultado final en el mismo entorno:

1. Editor con buffer vacío.
2. Editor con un archivo sintético de texto de 25 MiB, sin servidor de lenguaje.
3. Editor con un archivo fuente sintético grande de 25 MiB y el servidor correspondiente activo.

Registrar el RSS del editor y de cada proceso servidor por separado, además del tiempo desde la apertura hasta el primer render y hasta el primer resultado LSP. Repetir cada caso tres veces y anotar mediana, sistema, terminal, tamaño/líneas del archivo y servidor/versiones instaladas. No sumar el RSS del servidor al del editor sin mostrar también ambas cifras individuales.

`pylsp` y `gopls` no están instalados en el entorno de diseño; el protocolo se cubrirá con servidores LSP de prueba y la validación con esos ejecutables quedará marcada como no ejecutada hasta que estén disponibles. En este entorno sí están `rust-analyzer`, `typescript-language-server` y `clangd`.

## Pruebas y criterios de aceptación

- Cada extensión listada usa la familia de resaltado correcta; extensiones no compatibles conservan texto normal.
- La detección selecciona el servidor esperado y no inicia un proceso en archivos sin soporte o cuando falta el ejecutable.
- Autocompletado: respeta debounce, cancelación, límite de 50, estado de foco y aceptación/cierre por teclas; las respuestas viejas no sustituyen las nuevas.
- Formato: aplicación correcta de múltiples ediciones, undo/redo, versiones UTF-16 y rechazo transaccional de rangos inválidos o solapados.
- Refactor/rename: lista de acciones, confirmación multiarchivo, conservación de buffers modificados y rechazo seguro de WorkspaceEdit inválidos/obsoletos.
- Diagnósticos y ErrorLens se mantienen actualizados después de aplicar cualquiera de estas operaciones.
- Cambios rápidos de buffer y cierre garantizan como máximo un hijo activo y ningún proceso huérfano.
- `make test`, compilación completa y revisión manual de los servidores instalados.
- Publicar en la hoja de ruta las mediciones de RAM/tiempo y las limitaciones de servidores que no se pudieron ejecutar.

## Fuera de alcance de esta ampliación

- Instalar servidores, compiladores o paquetes del proyecto automáticamente.
- Resaltado semántico del protocolo LSP; el resaltado del editor permanece léxico y local.
- API de plugins, navegación a definición/referencias, documentación emergente o depuración integrada.
- Aplicar silenciosamente refactorizaciones multiarchivo o aceptar operaciones de sistema de archivos sin validación.

## Decisiones aprobadas en conversación

- Mantener el cliente LSP existente en C, detección automática de ejecutables y una sola sesión para el buffer activo.
- Autocompletado automático con pausa/debounce, no con `Ctrl+Space`.
- `F3` para formato, `F4` para acciones de código/refactor y `F5` para renombrar.
- Confirmar antes de aplicar cambios multiarchivo.
- Medir un archivo de texto sintético y otro de código de 25 MiB, separando RSS del editor y del servidor.
