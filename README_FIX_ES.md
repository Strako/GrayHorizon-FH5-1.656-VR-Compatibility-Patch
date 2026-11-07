# Forza Horizon 5 1.656.386.0 + GrayHorizon 1.2.1

## Diagnóstico

El repositorio `Forza-Horizon-VR` contiene **solo documentación** (`README.md`, `INSTALL.md`, etc.).
El mod real que se ejecuta es `xinput1_3.dll` (GrayHorizon 1.2.1), que estaba en
`C:\Users\404\Desktop\GrayHorizon-FH5-1.2.1\`.

El juego instalado en `F:\Games\Forza Horizon 5` es:

| Dato | Valor |
|---|---|
| `ForzaHorizon5.exe` FileVersion | **1.656.386.0** |
| `SizeOfImage` | **0xA809000** |
| Build que espera GrayHorizon 1.2.1 (Steam) | **1.688.109.0** (`SizeOfImage` 0xAC09000) |
| Build que espera GrayHorizon 1.2.1 (Game Pass) | **3.688.109.0** (`SizeOfImage` 0xA7DB000) |

El mod comprueba `SizeOfImage` al cargar. Al no coincidir, escribe en `gh5vr.log`:

```
ForzaHorizon5.exe SizeOfImage 0xA809000: not Steam 1.688.109.0 (0xAC09000)
or Game Pass 3.688.109.0 (0xA7DB000) - different game version, not hooking
```

y **no instala ningún hook**. Ese es el bloqueo exacto.

## Por qué no basta con saltarse la comprobación

En el DLL, la comprobación está en el offset de fichero **0x20A3D**
(RVA `0x2163D`):

```asm
mov  edx, [rdi+0x50]        ; SizeOfImage del EXE
cmp  edx, 0x0AC09000        ; Steam 1.688.109.0
jne  ...
cmp  edx, 0x0A7DB000        ; Game Pass 3.688.109.0
je   ...
; si no coincide -> "different game version, not hooking" y return 0
```

Si se cambia `0x0AC09000` por `0x0A809000`, el mod **sí arranca**, pero después usa
una **tabla de direcciones absolutas del EXE 1.688** (25 entradas en `.data`,
RVA `0xCF000`). Esa tabla no es un simple desplazamiento: contiene vtables y
funciones concretas del build 1.688.

Prueba realizada (sin tocar archivos del juego, solo el DLL del mod):

- Versión aceptada: `build: Steam 1.688.109.0`
- Se copió una vtable de 209 slots y se instalaron los hooks DX.
- Resultado en cockpit real:
  - `ForzaMultiCam vtable check failed (0 slots)` con la tabla original.
  - Con una vtable candidata de 1.656: `ForzaMultiCam vtable copied: 209 slots`,
    pero el mod sigue reportando `multicam 0` / `cockpit 0` y no hace estereo.
  - El escáner de memoria del mod no encuentra las instancias de cámara de 1.656
    (el build usa `CMultiCam`/`CCamDriver` y protección de punteros distinta).

Conclusion: **no es un problema de una sola dirección ni de un offset constante**.
El build 1.656 cambió el sistema de cámara y la tabla del mod está pensada para
1.688. Un parche ciego haría que el juego arrancase sin VR o con riesgo de crash.

## Evidencia encontrada en tu EXE 1.656

Direcciones reales localizadas en ejecución (RVA, sin ASLR):

| Clase / objeto | RVA 1.656 |
|---|---|
| `ForzaMultiCam` (constructor 0x8B0 bytes) vtable primaria | `0x6C11AC0` |
| `CMultiCam` (constructor 0x880 bytes) vtable primaria | `0x66E8268` |
| `CMultiCam` subobjeto `+0x530` | `0x66E8608` |
| `CCamDriver` (cámara cockpit, 0x660 bytes) vtable primaria | `0x68A36F8` |
| `CCamHood` (0x650 bytes) vtable primaria | `0x679E988` |
| `_Ref_count_obj2<ForzaMultiCam>` vtable | `0x6D9AC20` |
| `_Ref_count_obj2<CCamDriver>` vtable | `0x656A4B8` |

El campo `+0x5C8` que usa el mod existe en `CMultiCam`/`ForzaMultiCam`, pero en
1.656 las instancias vivas no aparecen con el escaneo de punteros directos que
hace el mod (posible ofuscación/protección del build). Por eso el mod no llega
a enganchar la cámara.

## Opciones

### 1. Recomendada: actualizar a la versión soportada

GrayHorizon 1.2.1 soporta **Steam 1.688.109.0** o **Game Pass 3.688.109.0**.
Es la única ruta fiable y la que el propio autor del mod documenta:

- Steam: actualizar/verificar integridad del juego.
- Game Pass / Store: actualizar desde Xbox.
- Después, copiar de nuevo `xinput1_3.dll`, `openxr_loader.dll`, `gh5vr.ini`
  y `GrayHorizon Settings.exe` a la carpeta del juego.
- En SteamVR: poner SteamVR como runtime OpenXR y resolución al 100 %.

### 2. Si tienes que quedarte en 1.656

Haría falta un **backport real** del mod:

1. Localizar en 1.656 las vtables de cámara (`CMultiCam`, `CCamDriver`, etc.).
2. Adaptar el escáner de memoria que busca `+0x5C8`.
3. Reconstruir las ~25 entradas de la tabla (`0xCF008`…`0xCF6E0`).
4. Probar en cockpit y ajustar el hook de Present/DXGI.
5. Verificar que el build no ofusca los punteros de objeto.

Esto no se puede entregar como un parche seguro sin el EXE 1.688 de referencia
o el código fuente del mod. El `README.md` del repositorio dice literalmente:
**“Source is not public yet.”**

## Archivos de este parche

- `xinput1_3_test1656.dll`: solo salta la comprobación de versión. **Diagnóstico,
  no funcional para VR.**
- `xinput1_3_test1656_realvt.dll`: además usa `0x6C11AC0` como vtable candidata.
  Sirve para ver hasta dónde llega el mod en el log.
- `xinput1_3_test1656_nocheck.dll`: copia la vtable candidata y no aborta si
  el slot 4 no coincide. Solo para depuración.
- `xinput1_3_test1656_shift.dll`: prueba de desplazamiento `-0x400000` (descartada).
- `patch_gh5vr_1656.py`: script que aplica los cambios anteriores sobre una copia.
- `gh5vr.original.log`: log original que aportaste.

**Ninguno de estos DLL es un fix VR completo.** No los dejes instalados como si
fueran la versión buena.

## Intento de versión compatible (v2)

Se decompiló el mod con **Ghidra 12.2 headless** (JDK 25) y se inspeccionó el
juego en vivo con **Frida** y una **DLL de sondeo** compilada con MSVC.

Hallazgos principales:

- El escáner `FUN_18000d7a0` **ya recorre toda la memoria** con `VirtualQuery`
  (regiones `MEM_PRIVATE` + `PAGE_READWRITE`), no solo `.data`. Por eso el
  problema no era el escáner, sino la vtable y el objeto objetivo.
- En 1.656 la clase equivalente a `ForzaMultiCam` es **`CMultiCam`**:
  - vtable primaria: `0x66E8268` (41 slots útiles).
  - objeto de 0x880 bytes; constructor en RVA `0x680280`.
- La cámara cockpit es **`CCamDriver`**:
  - vtable primaria: `0x68A36F8`.
  - objeto de 0x660 bytes; constructor base en RVA `0x1AC0120`.
- `CCamHood`: vtable `0x679E988`, 0x650 bytes.
- `ForzaMultiCam` (0x8B0 bytes) también existe en 1.656 (vtable `0x6C11AC0`),
  pero no es la que usa el juego en conducción normal.
- El sondeo en cockpit encontró una **lista de objetos de cámara** con vtables
  en el rango `0x665xxxx` y un campo común `+0x10 = 0x6655A50`; son las cámaras
  de los distintos modos. La cámara activa hay que seleccionarla por posición
  (el escáner del mod ya filtra por posición en `FUN_18001c470`).

Versión parcial generada: `xinput1_3_compat1656_test1.dll`.

- Acepta `SizeOfImage = 0xA809000`.
- Usa `CMultiCam` (`0x66E8268`) como cámara principal.
- Copia la vtable: **`ForzaMultiCam vtable copied: 41 slots`**.
- Instala los hooks DXGI/D3D12 y detecta el renderer de UI.
- **Todavía no engancha la cámara en cockpit**: el log sigue mostrando
  `multicam 0000000000000000` y `cockpit 0`.

Herramientas añadidas para continuar:

- `probe.cpp` / `probe.dll`: DLL de sondeo que vuelca a
  `C:\Users\404\Desktop\fh5_probe.log` todos los objetos con vtable grande o
  con un puntero válido en `+0x5C8`.
- `inject.cpp` / `inject.exe`: inyector genérico (`inject.exe <pid> probe.dll`).
- `xinput1_3_compat1656_test1.dll`: versión parcial con `CMultiCam` + `CCamDriver`.

Para terminar el backport falta:

1. Identificar la vtable exacta de la **cámara de presentación activa** dentro
   de la lista `0x665xxxx` (la que coincide con la posición del jugador).
2. Ajustar `cf010` / `cf018` a ese par de vtables.
3. Ajustar el "director" (`cf020`, `cf028..cf048`) y su vtable (`cf650`) a la
   clase de cámara activa de 1.656.
4. Validar en cockpit que el log muestre `multicam <ptr>` y `cockpit 1`.

Sin esa validación, la DLL parcial no debe usarse como si fuera VR funcional.

## Versión v4 (la más avanzada)

Archivo: `xinput1_3_compat1656_v4.dll`.

Cadena de direcciones usada:

| Elemento | RVA 1.656 |
|---|---|
| `ForzaMultiCam` manager vtable | `0x71BD5C8` (74 slots) |
| Director (`[manager+0x5C8]`) vtable | `0x71B66F0` |
| Presentación (`[manager+0xF0]`) vtable | `0x71BD388` (35 slots) |
| Slot 4 manager (usado por el mod) | `0x59089A0` |
| Slots presentación 0x68/0x1A0/0x1A8/0x1E8 | `0x5903FA0` / `0x5903FC0` / `0x5907C40` / `0x5903FF0` |

Parches adicionales de la v4:

1. `FUN_18000D3C0` se sustituye por `mov rax, rcx` en RVA `0x1A795`,
   para que el mod use directamente `ForzaMultiCam+0xF0`.
2. Las escrituras de hooks `[rdi+0x70]` / `[rdi+0x1F0]` (slots 13/61)
   se cambian a `[rdi+0x18]` / `[rdi+0x58]` (slots 2/10).

Resultado real en cockpit con la v4:

```
ForzaMultiCam vtable copied: 74 slots
hooked ForzaMultiCam 000002F242990A58
presentation camera: off-centre view volume available (eye_frustum=1)
presentation camera 000002F2429... hooked (director ...): per-eye ON, TAA jitter ON
```

Pero sigue:

```
report: multicam ..., 0.0 updates/s, 0.0 stereo frames/s, cockpit 0
```

El mod ya localiza y engancha ambas cámaras, pero el juego 1.656 llama a
los slots 2/10 sobre **otro objeto de cámara** (la cámara activa), no sobre el
objeto plantilla que el mod reemplaza. Por eso no se ejecuta el update y no
hay frames estéreo.

Datos de Frida sobre la vtable de presentación (slots realmente llamados):
`2`, `10`, `28`, `19`, `21`, `30`, `32`, `4`, `12`, `31`.
El mod escucha en `13`/`61` (y la v4 los movió a `2`/`10` de la copia), pero
la cámara activa usa otra instancia/copia de la vtable.

Para terminar el port falta:

1. Identificar el objeto de cámara activo (el que recibe `this` en los slots 2/10).
2. Reemplazar su puntero de vtable por la copia del mod, o parchear la vtable
   compartida que realmente usa.
3. Verificar que el reporte muestre `updates/s > 0`, `stereo frames/s > 0` y
   `cockpit 1`.

Herramientas añadidas:

- `probe_fast.cpp` / `probe_fast.dll`: sondeo rápido en memoria.
- `vtable_patch.cpp` / `vtable_patch.dll`: parchea en caliente la vtable
  original (slots 2/10) con los hooks del mod.
- `frida_vtable.py`, `frida_pres.py`: enganchan vtables y muestran los slots
  que llama el juego.
- `xinput1_3_compat1656_v3.dll` / `_v4.dll`: versiones con el bypass del
  director y los hooks en slots 2/10.

## Fase final: objeto de cámara activo

Con Frida se engancharon los slots 2/10 de la vtable de presentación y se
capturó el objeto que recibe `this`:

```
self  = 0x1E34A178188
vt    = 0x7FF6438ED348   -> RVA 0x71BD348
```

La cámara activa usa su propia vtable **`0x71BD348`** (0x40 bytes antes de la
vtable de presentación `0x71BD388`). El mod copia y engancha la vtable de
**presentación** (`0x71BD388`), que no es la que el juego usa para actualizar
la cámara en 1.656.

Se reemplazó en caliente el puntero de vtable del objeto activo por la copia
del mod, pero el reporte siguió en:

```
report: multicam ..., 0.0 updates/s, 0.0 stereo frames/s, cockpit 0
```

Conclusión: para terminar el port hay que crear una copia de la vtable
`0x71BD348`, instalar en sus slots 2/10 las funciones de update del mod y
adaptar esas funciones al layout de la clase activa. Eso ya no es un parche
de direcciones: requiere adaptar el código de update del mod a la clase de
cámara de 1.656 (layout de campos distinto al de presentación).

Herramienta añadida: `frida_active.py` (engancha slots 2/10 y reemplaza la
vtable del objeto activo por la copia del mod).

### Comparación de la clase activa y la de presentación

| Slot | Activa `0x71BD348` | Presentación `0x71BD388` |
|---|---|---|
| `+0x68` | `0x5903FA0` | `0x5903FA0` |
| `+0x70` | `0x5903FB0` | `0x5903FB0` |
| `+0xA8` | `0x5903FA0` | `0x5904050` |
| `+0x1A0` | `0x66EE6C6564` (no es código) | `0x5903FC0` |
| `+0x1A8` | `0x5903B00` | `0x5907C40` |
| `+0x1E8` | `0x5907C40` | `0x5903FF0` |

La clase activa **no es compatible** con la copia de presentación: sus slots
`0x1A0/0x1A8/0x1E8` son distintos y `0x1A0` ni siquiera es un puntero de código.
Por eso, aunque se instale la copia con hooks en los slots 2/10 del objeto
activo, las funciones de update del mod no producen el efecto esperado y el
reporte sigue en `0 updates/s`.

Para terminar el backport habría que:

1. Decompilar la clase activa `0x71BD348` y sus funciones de update
   (slots 2/10, RVA `0x594E4B0`).
2. Reescribir/adaptar `FUN_18000FB10` y `FUN_18000FB50` al layout de la clase
   activa (campos de matrices/FOV en offsets distintos).
3. Verificar `updates/s > 0`, `stereo frames/s > 0` y `cockpit 1`.

Herramienta añadida: `frida_active3.py` (parchea solo objetos con vtable
`0x71BD348` y les instala una copia con hooks en 2/10).

## Decompilación del update (Ghidra)

Se decompilaron las funciones clave del mod:

- `FUN_18000FB10` (hook `PcInit`, slot 2 en la v4): llama a `FUN_18000F230`.
- `FUN_18000FB50` (hook `PcZClip`, slot 10 en la v4): llama a `FUN_18000DF70`.
- `FUN_18000DF70`: guarda FOV cercano/lejano solo si `param_1 == DAT_1800D9678`.
- `FUN_18000F230`: es el update estéreo real. Condición exacta:

```c
if (param_1 == DAT_1800D9678 &&
    (DAT_180259B24 != 0 || bVar2) &&
    -1 < (int)DAT_1800CF0B0)
{
    ...
    (*DAT_1800D9660)(param_1, &local_b8);
    (*DAT_1800D9668)(param_1, &local_f8);
    _DAT_1800D9680++;   // contador "updates/s"
}
```

Por tanto, para que el update arranque en 1.656 hay que cumplir **las tres**:

1. `param_1 == DAT_1800D9678` — el objeto que recibe el hook debe ser el mismo
   que el puntero global de presentación.
2. `DAT_180259B24 != 0` — flag de per-eye (ya está a 1 con `per_eye=1`).
3. `DAT_1800CF0B0 >= 0` — valor de configuración que en el `.data` inicial
   vale `0xFFFFFFFF`; lo escribe el parser de `gh5vr.ini`.

Además, `FUN_18001A3C0` revalida cada 5 s que `[DAT_1800D9678]` sea la copia
`DAT_1800D3588 + 8`; si no, registra “our vtable is gone” y vuelve a buscar.

Conclusión: la vía correcta no es parchear slots 2/10 al azar, sino:

1. Poner en el objeto de cámara activo la **copia de presentación**
   (`DAT_1800D3588 + 8`).
2. Apuntar `DAT_1800D9678` a ese objeto.
3. Asegurar `DAT_1800CF0B0 >= 0`.
4. Llamar a `FUN_18000F230` desde el **update real** de la cámara activa
   (no desde un getter de propiedad).

Archivos de decompilación añadidos: `upd_FUN_18000f230.c`,
`upd_FUN_18000df70.c`, `upd_FUN_18000e0c0.c`, `frida_combo.py`.

## ABI del update y llamada directa

Ensamblador de las funciones clave:

- `FUN_18000DF70` recibe `this` en **rcx** y los dos floats en
  **xmm1** y **xmm2** (xmm0 se ignora).
- `FUN_18000F230` recibe `this` en **rcx** y `param_2` en **edx**.

Se probó llamarlas directamente desde Frida cada 16 ms con el objeto de
presentación:

```js
call70(pres, 0.0, 0.1, 10000.0);  // xmm1=0.1, xmm2=10000
call230(pres, 0);
```

Resultado: **access violation accediendo a 0x0**. Es decir, el update del mod
depende de punteros/globals que en 1.656 no están inicializados (funciones de
FOV/matrices, `DAT_1800D9660`, `DAT_1800D9668`, `DAT_1800D8D18`,
`DAT_1800D8D20`, etc.). No basta con llamar al update: hay que reconstruir
esos enlaces con las funciones reales de la cámara de 1.656.

Por eso el backport por parches ha llegado a su límite. La siguiente fase ya
no es parchear el DLL de 1.688, sino escribir un **hook de cámara nuevo** para
1.656:

1. Enganchar la función real de actualización de cámara por frame.
2. Leer las matrices/FOV del objeto activo (`0x71BD348`).
3. Construir las dos matrices de ojo y llamar a los setters de la cámara.
4. Entregar las imágenes al runtime OpenXR (la parte DX/OpenXR del mod sí
   funciona).

Archivos añadidos: `frida_direct2.py`, `dis_abi.py`.

## Estado de la carpeta del juego

Durante el análisis se copiaron temporalmente `xinput1_3.dll`, `openxr_loader.dll`
y `gh5vr.ini` a `F:\Games\Forza Horizon 5` para probar. Al terminar se eliminaron,
se borraron los `gh5vr.prev*.log` generados y se restauró `gh5vr.log` al original.
No se modificó ningún archivo propio del juego.
