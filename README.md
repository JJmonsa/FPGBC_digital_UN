# Quemador Autónomo de Cartuchos — FPGBC (`FPGBC_digital_UN`)

Implementación de un **grabador local autónomo de cartuchos** para la consola **FunnyPlaying FPGBC** (`GW2AR-LV18EQ144`), capaz de leer archivos `.gb`, `.gbc`, `.bin` y `.hex` almacenados en la memoria Flash SPI interna de la placa (`W25Q16JV` de 2 MB) y grabarlos directamente en un cartucho **FunnyPlaying EverSave MBC5** (`GW1NZ-LV1` + Flash NOR `S29GL064N` de 8 MB + FRAM `FM28V100` de 128 KB) **sin necesidad de un computador conectado durante la grabación**.

> **Nota sobre el cartucho:** No se requiere modificar ni reprogramar el repositorio del cartucho (`FPGBC_cartucho`), ya que su FPGA viene configurada de fábrica con el controlador de bancos **MBC5**. Todo el desarrollo reside en este repositorio (`FPGBC_digital_UN`).

---

## Arquitectura del Sistema

```text
+-------------------------------------------------------------------+
|                     Consola FPGBC (GW2AR-18)                      |
|                                                                   |
|  +-------------------+      +----------------------------------+  |
|  |  Flash SPI W25Q16 |      |        SoC LiteX (FemtoRV32)     |  |
|  |  (2 MB / 16 Mbit) |      |                                  |  |
|  |                   |      |  CPU RISC-V  <--->  Bus Wishbone |  |
|  | 0x000000 Bitstream|      |                       |          |  |
|  | 0x080000 Catálogo |----->|  SPI Flash Ctrl       |          |  |
|  +-------------------+      |  Periférico gbbus (0x00470000)   |  |
|                             +-----------------------+----------+  |
+-----------------------------------------------------|-------------+
                                                      | Bus 8-bit
                                                      | (LVCMOS33)
                                                      v
+-------------------------------------------------------------------+
|              Cartucho FunnyPlaying EverSave MBC5                  |
|                                                                   |
|   FPGA GW1NZ-LV1 (MBC5)  --->  Flash NOR S29GL064N (8 MB, ROM)    |
|                          --->  FRAM FM28V100 (128 KB, Guardado)   |
+-------------------------------------------------------------------+
```

---

## Estructura del Repositorio

```text
FPGBC_digital_UN/
├── README.md                          # Guía de arquitectura, compilación y pruebas
├── soc/
│   ├── gbbus.v                        # [Etapa 1] Periférico maestro Wishbone -> bus del cartucho
│   ├── FPGBC_unal_platform.py         # [Etapa 1] Asignación de pines físicos del conector "cart"
│   └── FPGBC_unal_target.py           # [Etapa 1] Instanciación de gbbus (0x00470000) y SPI Flash
└── firmware/
    ├── cart_calc/                     # Binarios de ejemplo precargables (.gb y .hex)
    ├── lib_unal/
    │   ├── cart_flash.h               # [Etapa 2] API pública del grabador (RAW, Intel HEX, $readmemh)
    │   ├── cart_flash.c               # [Etapa 2] Controlador MBC5, comandos JEDEC y verificación
    │   └── Makefile                   # [Etapa 2] Compila cart_flash.o dentro de lib_unal.a
    └── menu_grabador/
        ├── Makefile                   # [Etapa 3] Compilación del firmware con crt0.S y lib_unal.a
        ├── linker.ld                  # [Etapa 3] Script de enlazado de memoria del SoC
        └── main.c                     # [Etapa 3] Menú interactivo con botones físicos y SPI Flash
```

---

## Requisitos Previos

* **Hardware:**
  * Placa principal **FunnyPlaying FPGBC** (`GW2AR-LV18EQ144`).
  * Programador USB-JTAG **FT232RL** (solo necesario durante la **Etapa 4**).
  * Cartucho regrabable **FunnyPlaying EverSave MBC5** (`S29GL064N`).
  * *(Opcional)* Adaptador USB-UART conectado al puerto **GB Link** (`115200 8N1`) para visualizar los logs en consola.
* **Software (entorno `pixi`):**
  ```bash
  git clone [https://github.com/jjmonsa/FPGBC_digital_UN.git](https://github.com/jjmonsa/FPGBC_digital_UN.git)
  cd FPGBC_digital_UN
  pixi install
  pixi run setup-env
  ```

---

## Guía de las 5 Etapas (Implementación, Compilación y Comprobación)

### Etapa 1: Hardware / Gateware en LiteX (`soc/`)

Configura la FPGA de la consola para controlar eléctricamente el bus paralelo del cartucho y habilitar la lectura directa de la memoria Flash SPI interna.

1. **`soc/gbbus.v` (Periférico maestro de bus de 8 bits):**
   * Traduce transacciones de 32 bits del procesador **FemtoRV32** a ciclos asíncronos de 8 bits compatibles con los tiempos de la Flash `S29GL064N` a 40 MHz (`SETUP = 3` ciclos / 75 ns y `STROBE = 6` ciclos / 150 ns).
   * Expone 5 registros mapeados desde la dirección base **`0x00470000`**:

| Offset | Registro | Acceso | Descripción |
| :--- | :--- | :---: | :--- |
| `0x00` | `ADDR` | W | Dirección de 16 bits (`A15..A0`) en el bus del cartucho |
| `0x04` | `WDATA` | W | Byte de datos (`D7..D0`) a escribir |
| `0x08` | `RDATA` | R | Byte leído (`D7..D0`) desde el cartucho |
| `0x0C` | `CTRL` | W | Bit 0: operación (`1` = escribir, `0` = leer); Bit 1: iniciar ciclo (`1`) |
| `0x10` | `STATUS` | R | Bit 0: estado del controlador (`1` = ocupado, `0` = listo) |

2. **`soc/FPGBC_unal_platform.py` (Asignación de pines físicos):**
   * Define el recurso `"cart"` con estándar `LVCMOS33` conectando los buffers de nivel de la placa FPGBC:
     * **Direcciones `a[15:0]`:** pines `64 65 66 67 68 69 70 71 72 76 78 79 80 82 83 84`
     * **Datos `d[7:0]`:** pines `45 44 43 42 41 40 39 38`
     * **Control:** `clk` (`60`), `wr_n` (`61`), `rd_n` (`62`), `cs_n` (`63`), `dir` (`59`), `oe_n` (`48`)
3. **`soc/FPGBC_unal_target.py` (Integración en el SoC):**
   * Instancia `gbbus` en **`0x00470000`** (`size=0x100`, `cached=False`) gestionando el bus bidireccional con `TSTriple(8)`.
   * Habilita la memoria Flash SPI interna (`W25Q16JV` de 2 MB) mediante `with_spi_flash = True`.

**Cómo comprobar la Etapa 1:**
```bash
grep -n "0x00470000\|with_spi_flash" soc/FPGBC_unal_target.py
```

---

### Etapa 2: Librería del Grabador Autónomo (`firmware/lib_unal/`)

Implementa el driver en C (`cart_flash.h` y `cart_flash.c`) compilado dentro de `lib_unal.a` para gestionar el controlador MBC5 y los comandos JEDEC de la memoria Flash NOR `S29GL064N`.

* **Conmutación de bancos MBC5:** Escribe en `0x2000` (bits `0..7` del banco) y `0x3000` (bit `8`) para mapear bloques de 16 KB en la ventana `0x4000..0x7FFF` (cubriendo los 8 MB / 512 bancos del cartucho).
* **Protocolo JEDEC (`S29GL064N` en modo byte):**
  1. Secuencia de desbloqueo (`0x0AAA ← 0xAA`, `0x0555 ← 0x55`).
  2. Lectura de identificación (`0x90`): valida fabricante **`0x01`** (Spansion/Cypress) y dispositivo **`0x7E`** (`S29GL064N`).
  3. Borrado selectivo por sectores de **64 KB** (`0x80` + `0x30`) bajo demanda antes de escribir cada bloque.
  4. Programación byte a byte (`0xA0`) con sondeo de estado por bits `DQ7` / `DQ5` (omitiendo bytes `0xFF`).
  5. Retorno a modo lectura (`0xF0`) y **verificación byte a byte** contra el origen.
* **Formatos soportados (`cart_fmt_t`):**
  * `CART_FMT_RAW`: Archivos binarios directos (`.gb`, `.gbc`, `.bin`).
  * `CART_FMT_IHEX`: Archivos en formato Intel HEX (`:LLAAAATTDD...CC`).
  * `CART_FMT_READMEMH`: Archivos hexadecimales generados por `$readmemh`.

**Cómo comprobar la Etapa 2:**
```bash
cat firmware/lib_unal/Makefile
```

---

### Etapa 3: Aplicación del Menú de Grabación (`firmware/menu_grabador/`)

Firmware interactivo que se ejecuta en el procesador FemtoRV32 al encender la consola, lee los botones físicos y copia el archivo seleccionado desde la Flash SPI interna (`SPIFLASH_BASE + offset_flash`) hacia el cartucho.

#### Mapa de Memoria de la Flash SPI Interna (`W25Q16JV` — 2 MB)

| Rango de Direcciones | Tamaño | Contenido |
| :--- | :---: | :--- |
| `0x000000` – `0x07FFFF` | ~512 KB | Bitstream de la FPGA (`FPGBC_unal_target.fs`) |
| `0x080000` – `0x08FFFF` | 64 KB | Slot 1 del menú: `firmware_mbc5.gb` (32,768 bytes, `CART_FMT_RAW`) |
| `0x090000` – `0x09FFFF` | 64 KB | Slot 2 del menú: `firmware.hex` (36,864 bytes, `CART_FMT_READMEMH`) |
| `0x0A0000` – `0x1FFFFF` | 1.37 MB | Espacio libre para agregar más ROMs o programas en `CATALOGO[]` |

#### Controles del Menú
* **ARRIBA / ABAJO:** Cambia el archivo seleccionado en `CATALOGO[]`.
* **Botón A:** Inicia la detección del cartucho, borrado de sectores, grabación y verificación byte a byte.

**Cómo añadir nuevos juegos o programas al menú:**
1. Edita `CATALOGO[]` en `firmware/menu_grabador/main.c` agregando una entrada con su nombre, offset alineado a 64 KB (`0x0A0000u`, `0x0B0000u`, etc.), tamaño exacto en bytes y formato (`CART_FMT_RAW`, `CART_FMT_IHEX` o `CART_FMT_READMEMH`).
2. Recompila el menú (Etapa 4) y carga el archivo en ese mismo offset con `openFPGALoader`.

---

### Etapa 4: Compilación y Carga Inicial en la Consola (Única vez con PC)

Conecta el programador **FT232RL** a los pines JTAG de la placa FPGBC y ejecuta los siguientes pasos desde la raíz del repositorio:

1. **Generar el SoC base y compilar la librería `lib_unal.a`:**
   ```bash
   pixi run python3 soc/FPGBC_unal_target.py --build
   ```

2. **Compilar el firmware del menú grabador:**
   ```bash
   pixi run make -C firmware/menu_grabador
   ```
   *Verificación:* Debe generarse `firmware/menu_grabador/firmware.bin` sin errores.

3. **Integrar el menú en la ROM de arranque y flashear el bitstream en la FPGA:**
   ```bash
   pixi run python3 soc/FPGBC_unal_target.py \
       --integrated-rom-init firmware/menu_grabador/firmware.bin \
       --build --flash
   ```

4. **Precargar los archivos del catálogo en la Flash SPI interna (`W25Q16JV`):**
   ```bash
   pixi run openFPGALoader --cable ft232RL --pins TXD:CTS:RTS:RXD \
       --external-flash -o 0x080000 firmware/cart_calc/firmware_mbc5.gb

   pixi run openFPGALoader --cable ft232RL --pins TXD:CTS:RTS:RXD \
       --external-flash -o 0x090000 firmware/cart_calc/firmware.hex
   ```
   *(Nota: Si tu cable FT232RL usa `DTR` en lugar de `RTS` para `TCK`, cambia `--pins TXD:CTS:RTS:RXD` por `--pins TXD:CTS:DTR:RXD`).*

5. **Desconectar el programador USB/FT232RL:** La consola queda lista para funcionar de manera 100 % autónoma.

---

### Etapa 5: Operación Física y Comprobación Autónoma (Sin PC)

1. **Inserción segura del cartucho:**
   * Con la consola **apagada**, inserta el cartucho **FunnyPlaying EverSave MBC5** en la ranura de 32 pines.
   * *(Opcional)* Conecta un monitor serial al puerto **GB Link** a **115200 baudios (8N1)** para observar los mensajes de diagnóstico.
2. **Encendido y selección:**
   * Enciende la consola FPGBC (o presiona el botón de reset). Verás el mensaje `=== MENU GRABADOR AUTONOMO FPGBC ===`.
   * Usa los botones **ARRIBA** o **ABAJO** para elegir entre `1. Calculadora (.gb)` y `2. Calculadora (.hex)`.
3. **Ejecución de la grabación:**
   * Presiona el botón **A**.
   * El sistema verificará el ID de la Flash (`0x01`, `0x7E`), borrará los sectores de 64 KB necesarios, grabará los datos y verificará cada byte.
   * **Resultado esperado:**
     ```text
     [OK] Cartucho grabado y verificado correctamente.
     ```
4. **Comprobación de persistencia no volátil:**
   * Apaga la consola, extrae el cartucho y vuelve a insertarlo (en la FPGBC o en una Game Boy / Game Boy Color original) para confirmar que el programa quedó guardado de forma permanente.

---

## Diagnóstico de Códigos de Error

| Código | Constante | Causa probable | Solución |
| :---: | :--- | :--- | :--- |
| `0` | `CART_OK` | Grabación y verificación exitosas | Ninguna |
| `-1` | `CART_ERR_ID` | ID distinto de `0x01, 0x7E` | Limpia los contactos del cartucho, asegúrate de insertarlo hasta el fondo con la consola apagada y verifica los pines en `FPGBC_unal_platform.py`. |
| `-2` | `CART_ERR_ERASE` | Tiempo agotado al borrar sector de 64 KB | Revisa la alimentación de 3.3 V hacia la ranura del cartucho y la señal `oe_n` (pin 48). |
| `-3` | `CART_ERR_PROG` | Error al escribir byte o dirección >= 8 MB | Verifica que el tamaño en `CATALOGO[]` no supere los 8 MB (`0x800000`). |
| `-4` | `CART_ERR_VERIFY` | Un byte leído no coincide con el grabado | Revisa los tiempos `SETUP` y `STROBE` en `soc/gbbus.v` o falsos contactos en `D0..D7`. |
| `-5` | `CART_ERR_HEX` | Formato inválido en archivo `.hex` | Verifica que el archivo en SPI Flash coincida con `CART_FMT_IHEX` o `CART_FMT_READMEMH` y su offset exacto. |
