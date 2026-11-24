// Ubicación: FPGBC_digital_UN/firmware/lib_unal/cart_flash.c
#include "cart_flash.h"

/* Dirección base del periférico gbbus en el SoC */
#define GB_BASE   0x00470000u
#define R_ADDR    0x00
#define R_WDATA   0x04
#define R_RDATA   0x08
#define R_CTRL    0x0C
#define R_STATUS  0x10

#define REG_W(off, v) (*(volatile uint32_t *)(GB_BASE + (off)) = (uint32_t)(v))
#define REG_R(off)    (*(volatile uint32_t *)(GB_BASE + (off)))

/* Registros del controlador MBC5 (FPGA GW1NZ-LV1 del cartucho FunnyPlaying) */
#define MBC5_BANK_LO  0x2000u  /* Bits 0..7 del banco de ROM (bancos de 16 KB) */
#define MBC5_BANK_HI  0x3000u  /* Bit 8 del banco de ROM (bancos 256..511) */
#define MBC5_WIN_BASE 0x4000u  /* Ventana conmutable 0x4000..0x7FFF */

static uint8_t bus(uint16_t a, uint8_t d, int wr) {
    REG_W(R_ADDR, a);
    REG_W(R_WDATA, d);
    REG_W(R_CTRL, (wr ? 1u : 0u) | 2u);
    while (REG_R(R_STATUS) & 1u);
    return (uint8_t)REG_R(R_RDATA);
}

static inline void wr(uint16_t a, uint8_t d) { bus(a, d, 1); }
static inline uint8_t rd(uint16_t a)         { return bus(a, 0, 0); }

static void mbc5_set_bank(uint16_t bank) {
    wr(MBC5_BANK_LO, (uint8_t)(bank & 0xFFu));
    wr(MBC5_BANK_HI, (uint8_t)((bank >> 8) & 0x01u));
}

/* Lee 1 byte en la dirección lineal de 23 bits (0 .. 8 MB) */
static uint8_t flash_read_abs(uint32_t addr23, uint16_t *cached_bank) {
    uint16_t bank = (uint16_t)(addr23 >> 14);
    if (bank != *cached_bank) {
        mbc5_set_bank(bank);
        *cached_bank = bank;
    }
    return rd((uint16_t)(MBC5_WIN_BASE | (addr23 & 0x3FFFu)));
}

/* Comandos JEDEC para S29GL064N en Modo Byte (0x0AAA y 0x0555 caen en Banco 0 fijo) */
static inline void flash_unlock(void) {
    wr(0x0AAA, 0xAA);
    wr(0x0555, 0x55);
}

static inline void flash_reset(void) {
    wr(0x0000, 0xF0);
}

void cart_leer_id(uint8_t *manuf, uint8_t *dev) {
    flash_reset();
    flash_unlock();
    wr(0x0AAA, 0x90);
    *manuf = rd(0x0000);  /* Esperado: 0x01 (Spansion/Cypress) */
    *dev   = rd(0x0002);  /* Esperado: 0x7E (S29GL064N en modo byte) */
    flash_reset();
}

static int flash_wait_dq7(uint16_t win_addr, uint8_t esperado, uint32_t tmo) {
    while (tmo--) {
        uint8_t r = rd(win_addr);
        if ((r & 0x80u) == (esperado & 0x80u)) return 0;
        if (r & 0x20u) {  /* DQ5 = Timeout interno de programación/borrado */
            r = rd(win_addr);
            flash_reset();
            return ((r & 0x80u) == (esperado & 0x80u)) ? 0 : -1;
        }
    }
    flash_reset();
    return -1;
}

/* Borra un sector de 64 KB (4 bancos MBC5 de 16 KB) */
static int flash_erase_sector(uint8_t sector_64k) {
    uint16_t base_bank = (uint16_t)sector_64k << 2;
    flash_unlock();
    wr(0x0AAA, 0x80);
    flash_unlock();
    mbc5_set_bank(base_bank);
    wr(MBC5_WIN_BASE, 0x30);
    return flash_wait_dq7(MBC5_WIN_BASE, 0xFF, 3000000u);
}

static int flash_program_byte(uint32_t addr23, uint8_t data, uint16_t *cached_bank) {
    if (data == 0xFFu) return 0; /* Una celda borrada ya vale 0xFF */
    uint16_t bank = (uint16_t)(addr23 >> 14);
    uint16_t win_addr = (uint16_t)(MBC5_WIN_BASE | (addr23 & 0x3FFFu));

    flash_unlock();
    wr(0x0AAA, 0xA0);
    if (bank != *cached_bank) {
        mbc5_set_bank(bank);
        *cached_bank = bank;
    }
    wr(win_addr, data);
    return flash_wait_dq7(win_addr, data, 200000u);
}

typedef struct {
    uint8_t  sector_borrado[128]; /* 128 sectores * 64 KB = 8 MB */
    uint16_t cached_bank;
    uint32_t escritos;
    uint32_t total_estimado;
    cart_progress_cb prog_fn;
} grabador_state_t;

static cart_err_t grabar_bloque(grabador_state_t *st, uint32_t addr, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint32_t a = addr + (uint32_t)i;
        uint8_t sec = (uint8_t)(a >> 16);
        if (sec >= 128u) return CART_ERR_PROG;

        if (!st->sector_borrado[sec]) {
            if (flash_erase_sector(sec) != 0) return CART_ERR_ERASE;
            st->sector_borrado[sec] = 1;
            st->cached_bank = 0xFFFFu;
        }

        if (flash_program_byte(a, buf[i], &st->cached_bank) != 0)
            return CART_ERR_PROG;

        if (flash_read_abs(a, &st->cached_bank) != buf[i])
            return CART_ERR_VERIFY;

        st->escritos++;
        if (st->prog_fn && ((st->escritos & 0x3FFu) == 0)) {
            st->prog_fn(st->escritos, st->total_estimado);
        }
    }
    return CART_OK;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_byte(const char *p) {
    int hi = hex_nibble(p[0]), lo = hex_nibble(p[1]);
    return (hi < 0 || lo < 0) ? -1 : ((hi << 4) | lo);
}

cart_err_t cart_grabar_desde_memoria(const uint8_t *origen,
                                     size_t tamano,
                                     cart_fmt_t formato,
                                     cart_progress_cb prog_fn) {
    uint8_t m = 0, d = 0;
    cart_leer_id(&m, &d);
    if (m != 0x01u || d != 0x7Eu) return CART_ERR_ID;

    grabador_state_t st;
    for (int i = 0; i < 128; i++) st.sector_borrado[i] = 0;
    st.cached_bank = 0xFFFFu;
    st.escritos = 0;
    st.total_estimado = (uint32_t)tamano;
    st.prog_fn = prog_fn;

    /* 1. Archivos binarios directos (.gb, .gbc, .bin) */
    if (formato == CART_FMT_RAW) {
        cart_err_t err = grabar_bloque(&st, 0, origen, tamano);
        flash_reset();
        if (prog_fn && err == CART_OK) prog_fn(st.escritos, st.escritos);
        return err;
    }

    /* 2. Archivos de texto (.hex en formato Intel HEX o $readmemh) */
    size_t pos = 0;
    uint32_t base_addr = 0;
    uint32_t linear_addr = 0;
    uint8_t line_buf[64];

    while (pos < tamano) {
        while (pos < tamano && (origen[pos] == ' ' || origen[pos] == '\t' ||
               origen[pos] == '\r' || origen[pos] == '\n')) {
            pos++;
        }
        if (pos >= tamano) break;

        /* Ignorar comentarios // o directivas de dirección @ */
        if (origen[pos] == '/' || origen[pos] == '@') {
            while (pos < tamano && origen[pos] != '\n') pos++;
            continue;
        }

        if (formato == CART_FMT_IHEX) {
            if (origen[pos] != ':' || (pos + 11) > tamano) return CART_ERR_HEX;
            int count = hex_byte((const char *)&origen[pos + 1]);
            int ahi   = hex_byte((const char *)&origen[pos + 3]);
            int alo   = hex_byte((const char *)&origen[pos + 5]);
            int type  = hex_byte((const char *)&origen[pos + 7]);
            if (count < 0 || ahi < 0 || alo < 0 || type < 0 || count > 64)
                return CART_ERR_HEX;
            if ((pos + 9 + (size_t)count * 2) > tamano) return CART_ERR_HEX;

            uint16_t off = (uint16_t)((ahi << 8) | alo);
            for (int i = 0; i < count; i++) {
                int b = hex_byte((const char *)&origen[pos + 9 + i * 2]);
                if (b < 0) return CART_ERR_HEX;
                line_buf[i] = (uint8_t)b;
            }
            if (type == 0x00) {
                cart_err_t err = grabar_bloque(&st, base_addr + off, line_buf, (size_t)count);
                if (err != CART_OK) { flash_reset(); return err; }
            } else if (type == 0x04 && count == 2) {
                base_addr = ((uint32_t)line_buf[0] << 24) | ((uint32_t)line_buf[1] << 16);
            } else if (type == 0x01) {
                break; /* Fin de archivo Intel HEX */
            }
            while (pos < tamano && origen[pos] != '\n') pos++;
        }
        else if (formato == CART_FMT_READMEMH) {
            uint32_t word = 0;
            int digits = 0;
            while (pos < tamano && origen[pos] != '\r' && origen[pos] != '\n' &&
                   origen[pos] != ' ' && origen[pos] != '\t') {
                int nib = hex_nibble((char)origen[pos++]);
                if (nib < 0) { flash_reset(); return CART_ERR_HEX; }
                word = (word << 4) | (uint32_t)nib;
                digits++;
            }
            if (digits > 0) {
                uint8_t le[4] = {
                    (uint8_t)(word & 0xFFu),
                    (uint8_t)((word >> 8) & 0xFFu),
                    (uint8_t)((word >> 16) & 0xFFu),
                    (uint8_t)((word >> 24) & 0xFFu)
                };
                cart_err_t err = grabar_bloque(&st, linear_addr, le, 4);
                if (err != CART_OK) { flash_reset(); return err; }
                linear_addr += 4;
            }
        }
    }

    flash_reset();
    if (prog_fn) prog_fn(st.escritos, st.escritos);
    return CART_OK;
}
