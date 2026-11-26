#ifndef P2P_FLASH_MOCK_H
#define P2P_FLASH_MOCK_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct {
  uint32_t OPTR;
} FLASH_TypeDef;
typedef struct {
  uint32_t MEMRMP;
} SYSCFG_TypeDef;
extern FLASH_TypeDef fake_flash;
extern SYSCFG_TypeDef fake_syscfg;
#define FLASH (&fake_flash)
#define SYSCFG (&fake_syscfg)
#define FLASH_SIZE (1024UL * 1024UL)
#define FLASH_OPTR_DUALBANK 1U
#define FLASH_OPTR_BFB2 2U
#define SYSCFG_MEMRMP_FB_MODE 1U
#define FLASH_PAGE_SIZE 2048U
#define FLASH_FLAG_ALL_ERRORS 0xFFFFU
#define FLASH_TYPEERASE_PAGES 0U
#define FLASH_BANK_2 2U
#define FLASH_TYPEPROGRAM_DOUBLEWORD 0U
#define __HAL_FLASH_CLEAR_FLAG(x) ((void)(x))
typedef struct {
  uint32_t TypeErase, Banks, Page, NbPages;
} FLASH_EraseInitTypeDef;
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *, uint32_t *);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t, uint32_t, uint64_t);
#endif
