# Proyecto de Sistemas Operativos
## Proyecto 4: Copy-on-Write y MiniDB (xv6)

### Integrantes

- Juan José Acevedo
- Miguel Marín
- Sebastian Henao

### Curso

Sistemas Operativos

### Semestre

2026-2

---

# Descripción

Partimos de xv6-riscv en el commit `06aad25` con la solución de referencia del Proyecto 3 (`base_proyecto3.patch`).

**Parte A: Copy-on-Write.** `fork()` ya no copia la memoria del padre. El hijo mapea las mismas páginas físicas y, si la página era de usuario y escribible, en las dos tablas se le quita `PTE_W` y se le pone `PTE_COW` (el bit 8, reservado para el software). La primera escritura de cualquiera de los dos provoca un page fault de escritura. El kernel lo atiende en `cowfault()`: si la página todavía la usa otro proceso, el que escribe recibe una copia propia; si ya nadie más la usa, solo se le devuelve el permiso de escritura. Para saber cuándo una página física se puede liberar, `kalloc.c` lleva un conteo de referencias por página, protegido con su propio spinlock. `copyout()` usa la misma función antes de escribir en memoria de usuario, porque el kernel no pasa por la traducción del usuario y nunca genera ese page fault. Si no hay memoria para una copia, el proceso muere y el kernel sigue funcionando.

**Parte B: `cowstat()`.** Una llamada al sistema que devuelve las páginas libres y las páginas compartidas de todo el sistema, y los fallos y las copias CoW del proceso que la llama. Los dos contadores del proceso son campos nuevos de `struct proc` y empiezan en 0 en cada proceso.

**Parte C: MiniDB.** Completamos `minidb snapshot M`. El padre calcula el checksum de la tabla, limpia los bits de acceso y hace `fork()`. El hijo escribe `snap.db` directamente desde la tabla, que para él quedó congelada en el instante del fork. Mientras tanto, el padre aplica las `M` transferencias sin esperarlo. Cada página que el padre modifica se separa por CoW, así que el hijo nunca ve esos cambios. Al final el padre relee el archivo y verifica que tiene el checksum de antes del fork y que el saldo total se conservó.

---

# Archivos modificados

**`kernel/riscv.h`**
`PTE_COW` como el bit 8 de la PTE, uno de los dos bits que RISC-V deja libres para el sistema operativo. El hardware no lo mira; para nosotros significa "esta página no se puede escribir por ahora, pero se va a copiar cuando alguien lo intente".

**`kernel/kalloc.c`**
El conteo de referencias. Un arreglo con un contador por cada página física entre `KERNBASE` y `PHYSTOP`, protegido por el lock `kref`, separado del lock de la lista libre.
- `kalloc()` deja la página con 1 referencia.
- `krefinc()` agrega una referencia cuando `fork()` comparte la página.
- `kfree()` quita una referencia y solo devuelve la página a la lista libre cuando llega a 0.
- `freerange()` le da una referencia a cada página antes de pasarla por `kfree()`, porque esas páginas nunca pasaron por `kalloc()`.
- `krefcount()`, `kfreepages()` y `ksharedpages()` leen los contadores para `cowfault()` y `cowstat()`.

**`kernel/vm.c`**
- `uvmcopy()` ya no copia páginas. Mapea en el hijo la misma página física y le suma una referencia. Si la página es de usuario y escribible, cambia `PTE_W` por `PTE_COW` en las dos tablas.
- `cowfault()` (nueva) atiende la escritura sobre una página CoW: copia si la página tiene más de una referencia, o solo devuelve `PTE_W` si tiene una. Cuenta los fallos y las copias del proceso.
- `copyout()`, si la página destino tiene `PTE_COW`, llama a `cowfault()` antes de escribir.

**`kernel/trap.c`**
En `usertrap()`, un fallo de escritura (`scause` = 15) se intenta resolver primero con `cowfault()`. Si no era una página CoW, sigue el camino de siempre: `vmfault()` para las páginas perezosas o matar al proceso. Si no hubo memoria para la copia, el proceso se marca con `setkilled()`.

**`kernel/proc.h`, `kernel/proc.c`**
Los campos `cowfaults` y `cowcopies` en `struct proc`, que `allocproc()` pone en 0 para cada proceso nuevo.

**`kernel/cowstat.h`**
La estructura `struct cowstat` del enunciado.

**`kernel/sysproc.c`**
`sys_cowstat()`, que llena la estructura y la copia al usuario con `copyout()`.

**`kernel/syscall.h`, `kernel/syscall.c`, `user/usys.pl`, `user/user.h`**
El registro de la llamada `cowstat` (número 25) y su declaración para los programas de usuario.

**`kernel/defs.h`**
Prototipos de las funciones nuevas de `kalloc.c` y de `cowfault()`.

**`user/minidb.c`**
El esqueleto completado: `snapshot()`, `guardar_snapshot()` y tres funciones auxiliares (`saldo_total()`, `aplicar_transferencias()` y `verificar_archivo()`).

**`Makefile`**
`pgtbltest`, `cowtest` y `minidb` agregados a `UPROGS`.

---

# Integración con xv6

1. Clonar xv6-riscv, hacer `git switch -c proyecto4 06aad25` y
   aplicar `git apply base_proyecto3.patch`.
2. Copiar los archivos entregados en sus rutas.
3. Copiar los archivos del docente: user/cowtest.c, user/minidb.h,
   user/pgtbltest.c y user/usertests.c.
4. Compilar con `make qemu CPUS=3`.
5. Ejecutar `cowtest`, `minidb consulta 3`, `minidb snapshot 2000`,
   `pgtbltest` y `usertests -q`.

---

# Respuestas a las preguntas

## 1. PTE antes y después de una escritura CoW

En una ejecución de `minidb mapa`, la tabla quedó en `0x5000`. Esta es la PTE de esa página en el hijo:

| | PTE | Bits bajos | Dirección física |
|---|---|---|---|
| Antes de escribir | `0x21fb61d3` | `0x1d3` | `0x87ed8000` |
| Después de escribir | `0x21fca0d7` | `0x0d7` | `0x87f28000` |

Los bits de cada una:

| Bit | V (0) | R (1) | W (2) | X (3) | U (4) | A (6) | D (7) | COW (8) |
|---|---|---|---|---|---|---|---|---|
| Antes | 1 | 1 | **0** | 0 | 1 | 1 | 1 | **1** |
| Después | 1 | 1 | **1** | 0 | 1 | 1 | 1 | **0** |

Antes de escribir, la página está compartida con el padre. `fork()` le quitó `W` y le puso `COW`. `A` y `D` vienen encendidos porque el padre ya había escrito en esa página antes del fork, y `uvmcopy()` copia los bits de la PTE tal como están.

Cuando el hijo escribe en `0x5000`, el hardware encuentra la página sin `W` y genera un page fault de escritura. `cowfault()` ve que la página tiene dos referencias (el padre y el hijo), así que reserva una página nueva, copia los 4096 bytes y mapea **esa copia** en el hijo con `W` y sin `COW`. Por eso cambia la dirección física: el hijo deja de apuntar a la página compartida y pasa a tener la suya.

En el padre, la PTE no cambia en ese momento: sigue apuntando a `0x87ed8000`, sin `W` y con `COW`. Lo que cambia es el conteo de la página, que baja de 2 a 1. Si el padre escribe después, también tiene un fallo CoW, pero como la página ya es solo suya, `cowfault()` no copia nada: le devuelve `W`, le quita `COW` y el padre se queda con la misma dirección física.

## 2. Páginas sin PTE_W y USYSCALL en fork()

`uvmcopy()` solo convierte en CoW las páginas que tienen `PTE_W` y `PTE_U` a la vez. Las demás se comparten tal como están, sumando una referencia:

- **El código** no tiene `W`. Se comparte de solo lectura, sin `COW`. Si un proceso intenta escribir en su código después de un `fork()`, `cowfault()` ve que no es una página CoW y el proceso muere, igual que antes.
- **La página de guarda** sí tiene `W`, pero no tiene `U`. Tampoco se marca CoW: nadie la escribe y el usuario no la puede tocar.
- **`USYSCALL` y el trapframe** ni siquiera pasan por `uvmcopy()`, porque están por encima de `p->sz`. Cada proceso los recibe nuevos en `allocproc()`.

Si el código se marcara CoW, una escritura sobre él ya no mataría al proceso: el kernel le haría una copia escribible y el programa podría modificar su propio código. `PTE_COW` existe justo para diferenciar "no escribible por ahora" de "no escribible nunca".

Si `USYSCALL` se compartiera, el hijo vería la página del padre, y `ugetpid()` en el hijo devolvería **el pid del padre**. Además, como no tiene `W`, nunca se separaría por un fallo de escritura, así que el error no se corregiría solo.

## 3. Carrera en el conteo de referencias

Restar una referencia parece una operación, pero son tres pasos: leer el contador, restarle 1 y guardarlo. Supongamos una página P compartida por un padre y un hijo, con contador 2, y dos CPUs **sin** lock:

| Paso | CPU 1: el hijo hace `exit()` | CPU 2: el padre escribe en P | Contador |
|---|---|---|---|
| 1 | `kfree(P)` lee 2 | | 2 |
| 2 | | `cowfault()` copia P y llama a `kfree(P)`, que lee 2 | 2 |
| 3 | Resta y guarda 1 | | 1 |
| 4 | | Resta y guarda 1 | **1** |

Los dos soltaron la página, pero el contador quedó en 1. P nunca vuelve a la lista libre: **se pierde para siempre**.

El caso al revés también es posible. Supongamos que P la usan el padre y un primer hijo (contador 2). En la CPU 1 el padre hace otro `fork()` y `krefinc()` lee 2 para subirlo a 3; al mismo tiempo, en la CPU 2 el primer hijo termina y `kfree()` también lee 2 para bajarlo a 1. Si la resta se guarda de último, el contador queda en 1, aunque P la usan dos procesos: el padre y el hijo nuevo. Cuando uno de los dos la suelte, el contador llega a 0 y P vuelve a la lista libre **mientras el otro todavía la usa**. Otro proceso la recibe con `kalloc()`, la sobrescribe, y el que la seguía usando ve sus datos cambiar.

Con el lock `kref`, la lectura, la resta y la escritura de un contador ocurren sin que otra CPU pueda meterse en medio.

## 4. Consistencia del snapshot

`snap.db` tiene el checksum de antes del fork porque el hijo nunca ve las escrituras del padre. Justo después del `fork()`, las 64 páginas de la tabla quedan compartidas y marcadas CoW en los dos procesos. Cuando el padre aplica una transferencia y escribe en una página, ese fallo le da al **padre** una copia nueva, y es esa copia la que se modifica. La página original, la que tiene el hijo en su tabla, queda igual. El hijo escribe el archivo desde una tabla que, para él, está congelada en el instante del fork.

`copias_cow` es menor que 64 porque solo se copian las páginas que el padre escribe. Casi todas las transferencias ocurren entre las 1.024 cuentas más activas, que caben en 8 páginas, y solo una de cada 256 toca cualquier cuenta de la tabla. En nuestra ejecución, el padre tocó 30 páginas y hubo 30 copias. Las otras 34 nunca se copiaron.

Si el padre esperara al hijo antes de aplicar las transferencias, `copias_cow` sería 0. Cuando el hijo termina, sus referencias se liberan y cada página de la tabla queda con una sola: la del padre. Las escrituras del padre seguirían generando fallos CoW (`fallos_cow` seguiría en 30), pero `cowfault()` solo les devolvería el permiso de escritura, sin copiar. Lo comprobamos con `minidb snapshot 0`: sin transferencias mientras el hijo escribe, no hay ninguna copia.

## 5. Aceleración y planificador

Estos son los ticks de `minidb consulta K` en nuestro entorno de pruebas. El detalle está en `REPORTE.pdf`:

| K | 1 | 2 | 3 | 4 | 8 |
|---|---|---|---|---|---|
| `CPUS=3` | 34 | 18 | 17 | 17 | 17 |
| `CPUS=1` | 34 | 34 | 34 | 34 | 34 |

Casi todo el tiempo de la consulta es la firma, que solo usa CPU. Con `CPUS=3`, al pasar de 1 a 3 trabajadores cada uno puede correr en una CPU distinta **al mismo tiempo**, y el trabajo se reparte. En nuestro entorno el tiempo ya bajó a la mitad con 2 trabajadores y no siguió bajando con 3. El computador donde corrió QEMU tenía solo 2 núcleos reales, así que las 3 CPUs virtuales no podían correr las tres en paralelo de verdad.

De 3 a 8 trabajadores no mejora porque solo hay 3 CPUs. El planificador de xv6 es Round Robin: en cada CPU recorre la tabla de procesos y le da a cada proceso listo un turno, hasta la siguiente interrupción del reloj. Con 8 trabajadores, nunca hay más de 3 corriendo a la vez y los demás esperan su turno. El trabajo total es el mismo, repartido en más pedazos, y se suma un poco de costo por los cambios de contexto.

Con `CPUS=1` no hay ninguna mejora: Round Robin turna a todos los trabajadores en la única CPU, así que la firma de las 8.192 cuentas se calcula en serie, sin importar cuántos procesos haya.

---

# Resultados de las pruebas

Las cuatro suites se ejecutaron con `ejecutar_pruebas.py` y 3 CPUs. La salida completa está en `pruebas/`.

- **cowtest:** PASÓ, 11 de 11. Los mensajes `usertrap(): unexpected scause 0xf` de `solo_lectura` son esperados: son los intentos de escribir en el código y en `USYSCALL` después de un `fork()`.
- **minidb:** PASÓ. Las cuatro consultas dan la firma `0x2DD2F610` y el reporte esperado. `snapshot 2000` aplica 1978 transferencias, rechaza 22 y guarda un `snap.db` de 262160 bytes con checksum `0x6915D295`. `snapshot 0` no copia ninguna página. `mapa` muestra `COW` sin `W` antes de escribir, y `W` sin `COW` con otra dirección física después.
- **pgtbltest:** PASÓ, 11 de 11.
- **usertests -q:** PASÓ (`ALL TESTS PASSED`), incluida la verificación final de páginas libres.

También verificamos que, al copiar solo los archivos entregados y los del docente sobre una copia limpia de la base, el kernel compila y las pruebas pasan.

---

# Decisiones de diseño

**Un lock propio para el conteo de referencias.** No reutilizamos el lock de la lista libre (`kmem`), para que `kalloc()` y `kfree()` no se bloqueen mientras otra CPU cuenta páginas compartidas en `cowstat()`. `kfree()` toma los dos, pero nunca al mismo tiempo: primero baja el contador con `kref` y, solo si llegó a 0, toma `kmem` para devolver la página.

**Solo las páginas de usuario escribibles pasan a CoW.** La condición `PTE_W` y `PTE_U` deja por fuera el código, que no tiene `W`, y la página de guarda, que no tiene `U`. Así una escritura indebida sigue matando al proceso.

**Una sola función para los dos caminos de escritura.** `cowfault()` la usan `usertrap()`, cuando escribe el programa, y `copyout()`, cuando escribe el kernel. Así los dos caminos copian igual y cuentan igual en `cowstat()`.

**Sin copia cuando hay una sola referencia.** Antes de copiar, `cowfault()` mira el contador. Si vale 1, la página ya es solo del proceso que escribe, así que basta con devolverle `W`. Ese 1 no puede cambiar mientras se mira: la única forma de que otro proceso empiece a usar la página es un `fork()` de este mismo proceso, que en ese momento está ocupado atendiendo el fallo.

**Sin memoria, el proceso muere y el kernel sigue.** `cowfault()` devuelve un código distinto cuando `kalloc()` falla. `usertrap()` y `copyout()` marcan al proceso con `setkilled()`, y al terminar el proceso todas sus referencias se liberan.

**El snapshot escribe desde la tabla.** El hijo no copia la tabla a otro buffer: escribe directo desde ella, porque CoW ya garantiza que para él no cambia. Para releer el archivo, el padre usa `sbrk()`, porque la pila de usuario es de una sola página.

---

# Limitaciones conocidas

- `cowstat()` cuenta las páginas libres recorriendo toda la lista libre, y las compartidas recorriendo el arreglo completo de contadores, con el lock tomado. Para un sistema con mucha memoria sería mejor llevar esos totales actualizados en cada `kalloc()`, `kfree()` y `krefinc()`.
- Los contadores de referencias son `int`. Alcanzan de sobra para los procesos que caben en xv6 (`NPROC` = 64).
- Si `uvmcopy()` falla a mitad de camino por falta de memoria para las tablas del hijo, las páginas del padre que ya se marcaron CoW se quedan así. No es un error: con una sola referencia, la siguiente escritura del padre solo le devuelve `W`.
- La aceleración de `minidb consulta` depende de cuántos núcleos reales tenga el computador que ejecuta QEMU. En el nuestro eran 2, y por eso el tiempo dejó de bajar desde 2 trabajadores.
- Al compilar aparecen las advertencias del enlazador `LOAD segment with RWX permissions`. No vienen de nuestro código: también salen al compilar xv6 sin modificar. El compilador no da ninguna advertencia, y se compila con `-Werror`.

---

# Declaración de uso de IA

Usamos Claude (Anthropic) como apoyo durante el proyecto. Lo usamos para:

- Estudiar los conceptos del enunciado: Copy-on-Write, el conteo de referencias y sus condiciones de carrera, y el uso de `fork()` para snapshots.
- Revisar el código de la base y encontrar los puntos delicados, como `copyout()`, la página de guarda, `USYSCALL` y la inicialización del conteo en `freerange()`.
- Proponer la implementación de las Partes A, B y C.2, que nosotros integramos, compilamos, probamos y revisamos.
- Redactar este README y el reporte técnico a partir de nuestros resultados.

Los integrantes revisamos y entendemos todo el código entregado y asumimos plena responsabilidad académica sobre el contenido.
