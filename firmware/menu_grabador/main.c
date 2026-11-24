// Ubicación: FPGBC_digital_UN/firmware/menu_grabador/main.c
#include <stdio.h>
#include <stdint.h>

#include <irq.h>
#include <system.h>
#include <libbase/uart.h>
#include <generated/csr.h>
#include <generated/mem.h>

#include <lib_unal/game_buttons.h>
#include <lib_unal/cart_flash.h>

/* Catálogo de archivos alojados en la Flash SPI interna de la consola (W25Q16JV, 2 MB).
 * El bitstream ocupa los primeros ~512 KB (0x000000..0x07FFFF).
 * A partir de 0x080000 quedan 1.5 MB libres para almacenar los archivos a grabar. */
typedef struct {
    const char *nombre;
    uint32_t    offset_flash;
    uint32_t    tamano;
    cart_fmt_t  formato;
} entrada_menu_t;

static const entrada_menu_t CATALOGO[] = {
    { "1. Calculadora (.gb)",  0x080000u, 32768u, CART_FMT_RAW      },
    { "2. Calculadora (.hex)", 0x090000u, 36864u, CART_FMT_READMEMH },
    /* Ejemplo para agregar un tercer juego/binario precargado en 0x0A0000:
    { "3. Juego / Binario (.gb)", 0x0A0000u, 65536u, CART_FMT_RAW   },
    */
};

#define NUM_ITEMS (sizeof(CATALOGO) / sizeof(CATALOGO[0]))

static void actualizar_progreso(uint32_t actual, uint32_t total) {
    /* Aquí el equipo de interfaz gráfica puede actualizar una barra en pantalla */
    printf("Grabando: %lu / %lu bytes\r", (unsigned long)actual, (unsigned long)total);
}

int main(void) {
#ifdef CONFIG_CPU_HAS_INTERRUPT
    irq_setmask(0);
    irq_setie(1);
#endif
    uart_init();

    int seleccion = 0;
    uint32_t prev_btn = 0;

    printf("\n=== MENU GRABADOR AUTONOMO FPGBC ===\n");
    printf("Controles: ARRIBA/ABAJO = Elegir | A = Grabar cartucho\n");
    printf("Seleccionado: %s\n", CATALOGO[seleccion].nombre);

    while (1) {
        /* Lectura de botones físicos (activos en bajo en game_buttons_n) */
        uint32_t btn = ~game_buttons_in_read() & 0xFFu;
        uint32_t flanco = btn & ~prev_btn;
        prev_btn = btn;

        if (flanco & (1u << GAME_BUTTONS_DOWN)) {
            seleccion = (seleccion + 1) % NUM_ITEMS;
            printf("Seleccionado: %s\n", CATALOGO[seleccion].nombre);
            busy_wait(15);
        }
        if (flanco & (1u << GAME_BUTTONS_UP)) {
            seleccion = (seleccion + NUM_ITEMS - 1) % NUM_ITEMS;
            printf("Seleccionado: %s\n", CATALOGO[seleccion].nombre);
            busy_wait(15);
        }
        if (flanco & (1u << GAME_BUTTONS_A)) {
            const entrada_menu_t *item = &CATALOGO[seleccion];
            const uint8_t *origen = (const uint8_t *)(SPIFLASH_BASE + item->offset_flash);

            printf("\nIniciando grabacion de: %s...\n", item->nombre);
            cart_err_t res = cart_grabar_desde_memoria(
                origen, item->tamano, item->formato, actualizar_progreso
            );

            if (res == CART_OK) {
                printf("\n[OK] Cartucho grabado y verificado correctamente.\n");
                /* Aquí el menú puede ceder el control al núcleo de ejecución del cartucho */
            } else if (res == CART_ERR_ID) {
                printf("\n[ERROR] Cartucho no detectado (esperado ID 0x01, 0x7E). Verifica conexion.\n");
            } else {
                printf("\n[ERROR] Fallo durante la grabacion (codigo %d).\n", (int)res);
            }
            busy_wait(20);
        }
    }
    return 0;
}
