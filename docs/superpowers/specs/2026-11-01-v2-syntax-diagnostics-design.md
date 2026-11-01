# Diseño v2: resaltado de sintaxis y diagnósticos por lenguaje

## Objetivo

Extender terminal-editor con resaltado de sintaxis ligero y diagnósticos ErrorLens en vivo, manteniendo bajo el consumo de memoria. El usuario quiere que los servicios de lenguaje se activen únicamente para el archivo compatible que está editando. Los ejemplos de uso son `.tsx` y `.py`.

## Estado actual

El editor está escrito en C con ncurses. Conserva varios buffers y uno activo. `src/ui.c` dibuja cada línea directamente y ya muestra diagnósticos junto a su línea. Los linters definidos con `lint.<extension>` se ejecutan al guardar y al recargar el buffer activo. No hay resaltado de sintaxis ni protocolo LSP.

## Diseño

### Resaltado

- Añadir resaltado de sintaxis dentro del proceso del editor, sin proceso externo ni dependencia obligatoria.
- Elegir el lexer por extensión del buffer activo. El resaltado debe usar los pares de color existentes y respetar selección, desplazamiento horizontal y tema.
- Mantener el análisis acotado a las líneas necesarias para dibujar la vista cuando sea posible; no guardar una copia del documento ni estructuras grandes por cada buffer.
- Para extensiones desconocidas, dibujar texto como ahora.

### Servicios de lenguaje y ErrorLens

- Detectar el lenguaje por extensión y seleccionar automáticamente un servidor conocido si su ejecutable está disponible en `PATH`. La primera versión contempla Python (`.py`, `pylsp`) y TypeScript/TSX (`.ts`, `.tsx`, `typescript-language-server --stdio`). No se requiere configurar el comando para el uso normal.
- No instalar ni descargar servidores automáticamente: son dependencias externas. Si el servidor de una extensión no está instalado, el resaltado local sigue disponible y no se inicia ningún proceso.
- Permitir una configuración opcional `language.<extension>=comando argumentos...` para reemplazar el comando predeterminado, siguiendo el formato de argumentos de `lint.<extension>`.
- Integrar el protocolo LSP necesario para inicializar el servidor, sincronizar cambios del documento activo y recibir diagnósticos. Mostrar errores, advertencias e información en el gutter y junto a la línea con el ErrorLens que ya existe.
- Mantener como máximo un servidor de lenguaje activo: iniciarlo al activar un buffer cuyo sufijo tenga servidor detectado, y detenerlo al cambiar a un buffer sin servicio disponible, cerrar el buffer o salir. Al cambiar directamente entre buffers compatibles, detener el servidor anterior antes de iniciar el correspondiente al nuevo buffer. Esto mantiene el costo de procesos externos ligado al buffer activo.
- Los buffers sin ruta o sin servidor instalado/detectado no inician procesos. El resaltado local puede funcionar aunque no exista servidor.
- Mantener los linters `lint.<extension>` actuales como ejecución al guardar. Los diagnósticos del linter y del servidor comparten la presentación; actualizar o reemplazar los diagnósticos de una fuente no debe borrar los de la otra.
- Si el ejecutable no está disponible, el servidor falla o el protocolo devuelve datos inválidos, mantener el editor operativo, mostrar un mensaje breve y liberar el proceso y sus recursos.

## Alcance inicial

- Lenguajes iniciales: Python y TypeScript/TSX. El editor detecta sus extensiones y busca los servidores predeterminados instalados en el sistema; una configuración avanzada puede sustituir el comando.
- El sistema de lexers debe permitir añadir extensiones; no se requiere API de plugins en este cambio.
- No incluye autocompletado, navegación semántica, formato, refactorización ni instalación de servidores.

## Criterios de aceptación

1. Un archivo con extensión reconocida muestra tokens con colores de sintaxis; un archivo no reconocido conserva el renderizado normal.
2. Un archivo sin servidor instalado/detectado no lanza procesos de lenguaje.
3. Abrir o activar un archivo con servidor configurado inicia solo el servicio de ese buffer; cambiar de buffer o cerrar/salir detiene el proceso que deja de corresponder al buffer activo.
4. Los cambios del documento llegan al servidor y sus diagnósticos aparecen junto a la línea correcta, con salto al diagnóstico mediante la interacción actual.
5. Los diagnósticos del linter configurado siguen apareciendo al guardar y coexisten con los del servidor.
6. Una falla al iniciar o comunicarse con un servidor no bloquea la edición ni deja procesos huérfanos.
7. El camino sin resaltado especial ni servidor configurado no crea buffers de protocolo o procesos adicionales.

## Verificación prevista

Verificar compilación, renderizado en extensiones admitidas, apertura/cambio/cierre de buffers con un servidor LSP de prueba, llegada y limpieza de diagnósticos, y que el servidor termina al dejar de estar activo. Comparar RSS inactivo con la versión previa; medir por separado el proceso externo mientras un archivo compatible está activo.

## Decisiones aprobadas

- La prioridad de v2 es mantener terminal-editor optimizado y con bajo consumo de RAM.
- El resaltado será local y ligero; los servicios externos son opcionales y se detectan por extensión.
- Solo el buffer activo puede tener su servidor de lenguaje vivo.
- La detección de lenguaje y el comando predeterminado del servidor serán automáticos; la configuración explícita será solo una opción avanzada.
