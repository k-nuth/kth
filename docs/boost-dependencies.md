# Dependencias de Boost en Knuth y UTXO-Z

Fecha: 2026-11-13.

## Objetivo y alcance

Reducir la dependencia de Boost y, en lo posible, conservar únicamente usos
header-only. La prioridad es eliminar la necesidad de compilar y distribuir
bibliotecas binarias de Boost, sin reemplazar contenedores o aritmética que ya
cumplen ese objetivo y aportan valor.

La inspección corresponde a los siguientes commits de `master`:

- Knuth: `fb8e3e61a3aa00facf742625834f127eaf039b4d`.
- UTXO-Z: `a1e0cce48e905cbaa5991f243979fdb2b55279d0`.

Ambos declaran Boost 1.91.0. Se revisaron código, includes y configuración de
CMake y Conan. No se compilaron todas las variantes ni se verificó la lista final
de símbolos enlazados. Los includes, por sí solos, no demuestran un uso efectivo
ni la necesidad de enlazar un binario.

## Conclusión

El requisito binario más claro en Knuth es **Boost.Thread**: CMake exige y enlaza
`Boost::thread` en builds nativos. La mayor parte de los otros usos actuales son
header-only o emplean sólo la parte de templates de una biblioteca que también
ofrece componentes compilados.

La normalización Unicode opcional merece un tratamiento separado. No debe
confundirse con las conversiones UTF implementadas mediante templates de Locale.

UTXO-Z actual ya utiliza Boost como headers y no enlaza bibliotecas binarias de
Boost. Su producción depende principalmente de Unordered y ContainerHash.

## Inventario de Knuth

Las rutas de esta sección son relativas a la raíz del repositorio.

| Biblioteca | Uso y ubicación | Evaluación y acción propuesta |
|---|---|---|
| Thread | `src/infrastructure/include/kth/infrastructure/utility/thread.hpp`: aliases de mutex compartidos, mutex con promoción y locks. Los consumen `block_pool`, utilidades de infraestructura y estructuras antiguas de sincronización. `utility/asio.hpp` conserva un alias a `boost::thread`. | Requisito binario confirmado en CMake. Prioridad alta: migrar usos necesarios a primitivas estándar y retirar aliases sin uso. La promoción de locks no tiene un reemplazo directo en `std`; revisar la semántica antes de cambiarla. |
| Unordered | Mapas y sets planos y concurrentes en mempool, validación batch, UTXO builder, header index, peers, banlist, minería y C-API. Ejemplos: `src/blockchain/src/validate/batch_validate.cpp`, `src/network/include/kth/network/peer_database.hpp` y `src/database/include/kth/database/header_index.hpp`. | Header-only. Mantener inicialmente: `std::unordered_map` no es un reemplazo equivalente en rendimiento ni concurrencia. |
| ContainerHash | `src/infrastructure/include/kth/infrastructure/math/hash.hpp`, hashes de transacciones y claves de mempool; también `boost/functional/hash.hpp` y sus declaraciones adelantadas. | Header-only. Mantener inicialmente. |
| Bloom | `src/database/include/kth/database/databases/utxoz_database.hpp`: `boost::bloom::filter<utxoz::raw_outpoint, 7>`. | Header-only. Mantener inicialmente. |
| Multiprecision | `src/infrastructure/include/kth/infrastructure/math/uint256.hpp`, `math/hash.hpp`, `hash_define.hpp` y código de números grandes. | El backend `cpp_int` utilizado es header-only. No reemplazar aritmética relacionada con consenso como parte de una limpieza de dependencias. |
| Bimap | `src/blockchain/include/kth/blockchain/pools/block_pool.hpp`: índices por bloque y altura. También `src/node/include/kth/node/utility/check_list.hpp`. | Header-only. Comprobar qué estructuras siguen activas antes de considerar una sustitución. |
| SmartPtr | `src/blockchain/include/kth/blockchain/interface/block_chain.hpp`: `boost::shared_ptr` y `boost::atomic_shared_ptr` para publicar la vista de cadena y la caché de templates. | Header-only. Mantener inicialmente. El código documenta una limitación de libc++ en macOS que motivó evitar `std::atomic<std::shared_ptr>`; verificar soporte real antes de proponer ese reemplazo. |
| Algorithm.String | `src/infrastructure/src/utility/string.cpp`: join, split y trim. También configuración de scripts, witnesses y conversión de opcodes en Domain. | Header-only. Candidato a migrar a algoritmos estándar o utilidades pequeñas, conservando delimitadores, compresión de separadores, tratamiento de espacios y comportamiento de mayúsculas/minúsculas. |
| LexicalCast | `src/infrastructure/include/kth/infrastructure/impl/utility/string.ipp`: conversiones genéricas de texto. | Header-only. Revisar las instanciaciones y migrar conversiones numéricas adecuadas a `from_chars` y `to_chars`, preservando errores y aceptación de entrada. |
| Range | `src/domain/src/chain/block.cpp`, `chain_state.cpp` y código de base de datos: recorridos invertidos y otros adaptadores. | Header-only. Candidato a sustituir por `std::ranges` y `std::views`. |
| Iostreams | `src/infrastructure/include/kth/infrastructure/utility/container_sink.hpp`: stream que escribe en un contenedor. Includes adicionales en `src/network/include/kth/network/define.hpp` y `src/c-api/src/node.cpp`. | El stream con sink propio utiliza templates. El include de `device/file_descriptor.hpp` en C-API no tiene un uso correspondiente identificado y es candidato a limpieza. No inferir que se necesita `libboost_iostreams` sólo por incluirlo. |
| Interprocess | `src/infrastructure/include/kth/infrastructure/unicode/file_lock.hpp`, utilizado por `src/infrastructure/src/utility/interprocess_lock.cpp`. También permanece código antiguo de almacenamiento. | Header-only para estos usos. Mantener inicialmente. El bloqueo de archivos utiliza detalles internos de Boost; conviene encapsularlos o evaluar una implementación específica por plataforma en un cambio separado. |
| Move | `src/infrastructure/include/kth/infrastructure/unicode/file_lock.hpp`. | Header-only y auxiliar al bloqueo de archivos. Revisar junto con Interprocess. |
| Beast | `src/network/src/websocket_transport.cpp`: parser HTTP del handshake WebSocket. | Uso compatible con header-only. Mantener inicialmente. |
| Asio y System | Alternativa configurable a Asio standalone en `utility/asio_helper.hpp` y `error.hpp`. El parser de Beast usa `boost::asio::buffer` y `boost::system::error_code` incluso con standalone. | No son el obstáculo binario principal. Eliminar la alternativa Boost.Asio sería un cambio independiente; no eliminar sus headers de forma indiscriminada mientras Beast use esos tipos. |
| Locale | `src/infrastructure/src/unicode/unicode.cpp`: conversiones UTF mediante `conv::utf_to_utf`. Con `WITH_ICU`, normalización Unicode y funciones de wallet relacionadas. | Separar las conversiones basadas en templates del soporte opcional de normalización que requiere revisar las dependencias compiladas. `with_icu=False` es el default de Conan. |
| DateTime | `src/consensus/src/bch-rules/util/time.cpp`, en el componente opcional Consensus. El bloqueo de archivos también incluye tipos de DateTime. | Determinar la necesidad binaria por las funciones utilizadas, no por la presencia del header. Verificar ambas variantes de Consensus al reducir el paquete. |
| Core | `src/node/src/executor/executor.cpp`: `boost::null_deleter`. | Header-only. Puede sustituirse por una lambda si el uso continúa siendo necesario. |

### Includes y código heredado

- Hay includes aparentemente sobrantes de Array y CircularBuffer, por ejemplo en
  `src/domain/include/kth/domain/message/heading.hpp` y
  `src/network/include/kth/network/define.hpp`. Confirmar con compilación antes
  de eliminarlos.
- El código del logging antiguo conserva referencias a Log, Filesystem, Bind,
  Spirit, Intrusive y ScopeExit. No debe contarse automáticamente como requisito
  del logger actual, que usa spdlog. Comprobar también los headers públicos antes
  de retirar archivos.
- Program_options ya fue reemplazado por CLI11.
- La presencia de archivos del almacenamiento antiguo no implica que ese backend
  forme parte de la ejecución actual con UTXO-Z.

## Configuración de build y paquete

Knuth actualmente:

- Declara `boost/1.91.0` con headers y bibliotecas transitivos en `conanfile.py`.
- Configura `header_only=True` para Emscripten, pero no de forma general para
  los builds nativos.
- Desactiva Cobalt por un problema del recipe; no hay uso de Cobalt identificado.
- Exige `Boost COMPONENTS thread` en Infrastructure nativo y enlaza
  `Boost::thread` desde Infrastructure y Domain.
- Declara `boost::boost` entre los requisitos del componente Infrastructure.

Hay que alinear esas declaraciones con el uso efectivo después de retirar
Thread. Cambiar únicamente una opción de Conan no resuelve los requisitos
explícitos de CMake ni garantiza que todas las variantes funcionen.

## Comparación con UTXO-Z

| Ámbito | Uso |
|---|---|
| Producción | Unordered: mapas de UTXOs, referencias, catálogo de versiones, caché de archivos y estadísticas. ContainerHash: propiedades y comportamiento del hashing. `BOOST_VERSION` identifica la versión de Boost en información de formato/configuración. |
| Tests | JSON para analizar resultados y manifiestos; Hash2 para digests SHA-256; detalles internos de Unordered para controles de compatibilidad del formato. |
| Herramientas | Consultas de versión de Boost y mapas utilizados para dimensionamiento. |
| `legacy/` | Interprocess y contenedores de implementaciones anteriores. No equivalen a dependencias de producción actuales. |

UTXO-Z configura `header_only=True`, enlaza `Boost::headers` y declara
`boost::headers` en la información de su paquete. Sus tests compilan la
implementación de JSON desde `tests/support/boost_json.cpp`; eso no agrega una
biblioteca binaria de Boost a los requisitos de los consumidores de UTXO-Z.

## Plan propuesto

### Primera limpieza del logger heredado

Se retiraron los headers y fuentes obsoletos del logger Boost.Log, sus colectores
de archivos, rotación, StatsD y envío UDP. También se eliminó del build un archivo
de tests que sólo contenía ejemplos comentados. Se conservaron `log/sink.hpp`,
`log/source.hpp` y `src/log/sink.cpp`, que corresponden al logger actual con spdlog.

La comprobación local compiló el header público agregado de Infrastructure y el
logger activo, enlazó un consumidor e inicializó el logger para verificar una
escritura real en archivo. No quedaron referencias a los headers eliminados.
Esta limpieza no modifica todavía los requisitos de Boost de Conan o CMake ni
equivale a una validación de todas las plataformas.

### Inventario después de la limpieza

Se repitió la búsqueda de includes activos de Boost en los archivos C++, headers
y plantillas del árbol `src/`. No quedan includes activos de Boost.Log,
Filesystem, Bind, Spirit, Intrusive, ScopeExit ni Preprocessor. La limpieza quitó
55 includes de las seis primeras bibliotecas y el include de Preprocessor del
archivo de ejemplos comentados. Esto describe referencias directas del proyecto,
no las dependencias internas transitivas entre headers de Boost.

Continúan presentes los siguientes grupos de headers:

| Grupo | Estado tras la limpieza |
|---|---|
| Unordered, ContainerHash y Functional.Hash | Mapas, sets y funciones de hashing; sin cambios. |
| Bloom y Multiprecision | Filtro de UTXOs y aritmética; sin cambios. |
| Thread | Sigue siendo el requisito binario explícito en builds nativos. |
| Bimap y SmartPtr | Índices bidireccionales y publicación de snapshots; sin cambios. |
| Algorithm.String, LexicalCast y Range | Utilidades de texto y recorridos; sin cambios. |
| Iostreams | Sink de contenedores y includes a revisar; sin cambios. |
| Interprocess y Move | Bloqueo de archivos y almacenamiento heredado; sin cambios. |
| Beast, Asio y System | Parser HTTP, buffers, errores y alternativa de Asio; sin cambios. |
| Locale y DateTime | Conversiones y funcionalidades opcionales; sin cambios. |
| Core | `null_deleter` en el ejecutor; sin cambios. |
| Array y CircularBuffer | Includes aparentemente sobrantes, todavía pendientes de limpieza. |
| Test | Includes en dos archivos antiguos de tests de Database; su presencia no demuestra que esos tests estén registrados en el build. |
| Version | Consulta de versión en `utility/coroutine_config.hpp`; no es una biblioteca binaria. |

Por tanto, retirar el logging viejo elimina referencias obsoletas, pero todavía
no permite activar `header_only=True` indiscriminadamente en Knuth nativo. El
siguiente trabajo con mayor impacto sigue siendo retirar Boost.Thread.

### Pasos restantes

1. **Retirar Boost.Thread.** Inventariar usos efectivos de los aliases, eliminar
   los obsoletos y migrar los necesarios a primitivas estándar. Revisar
   especialmente promoción de locks, orden de adquisición y comportamiento en
   parada. No hacer una sustitución mecánica.
2. **Reducir los requisitos de build.** Quitar `Boost::thread` de CMake cuando el
   código ya no lo necesite y ajustar Conan para el paquete mínimo. Verificar
   las variantes opcionales antes de habilitar header-only para todo Knuth.
3. **Resolver Locale opcional.** Mantener las conversiones UTF necesarias y
   decidir cómo soportar la normalización con ICU sin imponer dependencias al
   build habitual.
4. **Limpiar includes y código obsoleto.** Empezar por includes sin símbolos
   utilizados y seguir con logging y almacenamiento heredados, verificando el
   impacto en headers públicos.
5. **Reducir utilidades pequeñas.** Migrar Range, Algorithm.String y LexicalCast
   cuando simplifique el código. Estos cambios reducen superficie de Boost,
   pero no son los que eliminan sus binarios.

No se propone sustituir inicialmente Unordered, Bloom, Multiprecision, Bimap,
SmartPtr o Beast sólo para reducir el número de nombres de Boost. El criterio es
el costo y el riesgo frente al beneficio concreto.

## Validación necesaria

- Build y tests nativos de Linux, macOS y Windows.
- Build de WebAssembly y browser node.
- Configuraciones con `consensus=False` y `consensus=True`.
- Configuración con normalización ICU habilitada, además del default desactivado.
- Tests de concurrencia y sanitizadores para los cambios de locks y threads.
- Instalación del paquete y compilación de un consumidor externo.
- Inspección de símbolos y dependencias del ejecutable/paquete para confirmar
  que no queden requisitos binarios de Boost, incluidos los transitivos.

Hasta completar esas comprobaciones, la viabilidad de un paquete nativo
enteramente header-only debe considerarse una conclusión a validar, no un
resultado ya probado.

## Referencias

- [Boost: bibliotecas header-only y compiladas](https://www.boost.org/doc/libs/1_75_0/more/getting_started/unix-variants.html).
- [Boost.Iostreams: componentes que necesitan código compilado](https://www.boost.org/doc/libs/1_50_0/libs/iostreams/doc/installation.html).
- [Knuth en el commit inspeccionado](https://github.com/k-nuth/kth/tree/fb8e3e61a3aa00facf742625834f127eaf039b4d).
- [UTXO-Z en el commit inspeccionado](https://github.com/utxo-z/utxo-z/tree/a1e0cce48e905cbaa5991f243979fdb2b55279d0).
