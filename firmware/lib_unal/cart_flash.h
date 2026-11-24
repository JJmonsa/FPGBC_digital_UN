// Ubicación: FPGBC_digital_UN/firmware/lib_unal/cart_flash.h
#ifndef CART_FLASH_H
#define CART_FLASH_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    CART_FMT_RAW = 0,   /* Archivos binarios directos: .gb, .gbc, .bin */
    CART_FMT_IHEX,      /* Archivos Intel HEX: :LLAAAATTDD...CC */
    CART_FMT_READMEMH   /* Archivos Verilog $readmemh: palabras hex de 32 bits */
} cart_fmt_t;

typedef enum {
    CART_OK         =  0, /* Grabación y verificación completadas con éxito */
    CART_ERR_ID     = -1, /* Cartucho no insertado o Flash desconocida (esperado: 0x01, 0x7E) */
    CART_ERR_ERASE  = -2, /* Fallo al borrar sector de 64 KB */
    CART_ERR_PROG   = -3, /* Fallo al escribir byte en la Flash S29GL064N */
    CART_ERR_VERIFY = -4, /* Verificación byte a byte fallida */
    CART_ERR_HEX    = -5  /* Archivo .hex corrupto o con formato inválido */
} cart_err_t;

/* Callback opcional para actualizar el progreso en pantalla */
typedef void (*cart_progress_cb)(uint32_t bytes_procesados, uint32_t total_bytes);

/* Lee el ID de fabricante (0x01) y dispositivo (0x7E) del cartucho insertado */
void cart_leer_id(uint8_t *manuf, uint8_t *dev);

/* Borra, decodifica, graba y verifica un archivo alojado en la memoria de la consola */
cart_err_t cart_grabar_desde_memoria(const uint8_t *origen,
                                     size_t tamano,
                                     cart_fmt_t formato,
                                     cart_progress_cb prog_fn);

#endif
