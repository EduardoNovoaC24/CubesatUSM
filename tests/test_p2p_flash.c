#define _GNU_SOURCE
#include "main.h"
#include "p2p_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

FLASH_TypeDef fake_flash = {FLASH_OPTR_DUALBANK};
SYSCFG_TypeDef fake_syscfg;
static int fail_after = -1;
static unsigned erases;
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,
                                    uint32_t *error) {
  assert(e->Banks == 2 && e->Page >= 254 && e->Page <= 255 && e->NbPages == 1);
  memset((void *)(uintptr_t)(0x08080000UL + e->Page * 2048U), 255, 2048);
  *error = 0;
  erases++;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address,
                                    uint64_t value) {
  (void)type;
  assert(address >= 0x080FF000UL && address + 8U <= 0x08100000UL &&
         address % 8 == 0);
  if (fail_after == 0)
    return HAL_ERROR;
  if (fail_after > 0)
    fail_after--;
  uint64_t *word = (uint64_t *)(uintptr_t)address;
  assert((*word & value) == value);
  *word = value;
  return HAL_OK;
}
int main(void) {
  void *region = mmap((void *)0x080FF000UL, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  assert(region == (void *)0x080FF000UL);
  memset(region, 255, 4096);
  P2P_Config c, loaded;
  P2P_ConfigDefaults(&c);
  assert(!P2P_FlashLoad(&loaded));
  assert(P2P_FlashSave(&c, 0));
  assert(P2P_FlashLoad(&loaded) && !memcmp(&c, &loaded, sizeof(c)));
  assert(P2P_FlashSave(&c, 1) && erases == 1);
  c.gain = 0.075f;
  assert(!P2P_FlashSave(&c, 1000) && erases == 1);
  assert(P2P_FlashSave(&c, 61000) && erases == 2);
  assert(P2P_FlashLoad(&loaded) && loaded.gain == 0.075f);
  c.gain = 0.2f;
  fail_after = 3;
  assert(!P2P_FlashSave(&c, 122000));
  assert(P2P_FlashLoad(&loaded) &&
         loaded.gain == 0.075f); /* Older committed copy survives. */
  fail_after = -1;
  assert(P2P_FlashSave(&c, 123000));
  assert(P2P_FlashLoad(&loaded) && loaded.gain == 0.2f);
  ((unsigned char *)region)[16] ^=
      1; /* CRC invalidation falls back to prior generation. */
  assert(P2P_FlashLoad(&loaded) && loaded.gain == 0.075f);
  fake_flash.OPTR = 0;
  assert(!P2P_FlashLoad(&loaded) && !P2P_FlashSave(&c, 200000));
  fake_flash.OPTR = FLASH_OPTR_DUALBANK;
  fake_syscfg.MEMRMP = 1;
  assert(!P2P_FlashSave(&c, 200000));
  assert(munmap(region, 4096) == 0);
  puts("P2P flash journal/power-loss/CRC/wear checks passed (host simulation)");
}
