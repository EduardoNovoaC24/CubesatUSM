#include "main.h"
#include "p2p_control.h"
#include <stddef.h>
#include <string.h>

/* Last 4 KiB reserved by both linker scripts. Each 2 KiB page holds one
 * committed record; replacement erases only the older page. */
#define CONFIG_BASE 0x080FF000UL
#define CONFIG_MAGIC 0x50325031UL
#define CONFIG_COMMIT 0x434F4D4D49543131ULL
typedef struct {
  uint32_t magic, version, size, generation;
  P2P_Config config;
  uint32_t crc, padding;
  uint64_t commit;
} FlashRecord;
_Static_assert(sizeof(FlashRecord) % 8 == 0,
               "Flash record must use doublewords");
static uint32_t last_save_ms;
static bool saved_this_boot;

static bool supported(void) {
  /* Reject bank swapping, single-bank geometry and smaller MCU variants. */
  return FLASH_SIZE == 1024UL * 1024UL && (FLASH->OPTR & FLASH_OPTR_DUALBANK) &&
         !(FLASH->OPTR & FLASH_OPTR_BFB2) &&
         !(SYSCFG->MEMRMP & SYSCFG_MEMRMP_FB_MODE);
}
static bool valid(const FlashRecord *r) {
  return r->magic == CONFIG_MAGIC && r->version == 1U &&
         r->size == sizeof(P2P_Config) && r->commit == CONFIG_COMMIT &&
         r->crc == P2P_Hash(r, offsetof(FlashRecord, crc)) &&
         P2P_ConfigValid(&r->config);
}
static const FlashRecord *latest(void) {
  const FlashRecord *a = (const FlashRecord *)CONFIG_BASE;
  const FlashRecord *b = (const FlashRecord *)(CONFIG_BASE + 2048U);
  bool va = valid(a), vb = valid(b);
  if (!va)
    return vb ? b : NULL;
  if (!vb)
    return a;
  return (int32_t)(b->generation - a->generation) > 0 ? b : a;
}
bool P2P_FlashLoad(P2P_Config *c) {
  if (!supported())
    return false;
  const FlashRecord *r = latest();
  if (!r)
    return false;
  *c = r->config;
  return true;
}
bool P2P_FlashSave(const P2P_Config *c, uint32_t now) {
  if (!supported() || !P2P_ConfigValid(c))
    return false;
  const FlashRecord *old = latest();
  if (old && !memcmp(c, &old->config, sizeof(*c)))
    return true; /* No wear for unchanged config. */
  if (saved_this_boot && now - last_save_ms < 60000U)
    return false;
  uint32_t target =
      old && (uintptr_t)old == CONFIG_BASE ? CONFIG_BASE + 2048U : CONFIG_BASE;
  FlashRecord r;
  memset(&r, 0xFF, sizeof(r));
  r.magic = CONFIG_MAGIC;
  r.version = 1;
  r.size = sizeof(*c);
  r.generation = old ? old->generation + 1U : 1U;
  r.config = *c;
  r.crc = P2P_Hash(&r, offsetof(FlashRecord, crc));
  r.commit = CONFIG_COMMIT;
  if (HAL_FLASH_Unlock() != HAL_OK)
    return false;
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0;
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_2;
  erase.Page = (target - 0x08080000UL) / FLASH_PAGE_SIZE;
  erase.NbPages = 1;
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &page_error);
  /* commit is programmed last: a torn record cannot replace the old page. */
  for (size_t i = 0; status == HAL_OK && i < sizeof(r); i += 8U) {
    uint64_t word;
    memcpy(&word, (const uint8_t *)&r + i, 8);
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, target + i, word);
  }
  HAL_FLASH_Lock();
  if (status != HAL_OK || !valid((const FlashRecord *)(uintptr_t)target))
    return false;
  saved_this_boot = true;
  last_save_ms = now;
  return true;
}
