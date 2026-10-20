#include "app_config.h"
#include "typedef.h"
#include "fs.h"
#include "update_loader_download.h"
#include "rec_nor/nor_interface.h"

#if (USER_FILE_UPDATE_V2_EN)

#define LOG_TAG "[UP_NAND_F_DNLD]"
#define LOG_INFO_ENABLE
#define LOG_ERROR_ENABLE
#include "system/debug.h"

typedef struct _user_file_base_info_t {
    struct list_head 	entry;
    user_chip_update_info_t *(*get_res_base_info)(void *priv,
            u32(**local_read_hdl)(void *buf, u32 addr, u32 len),
            u32(**local_write_hdl)(void *buf, u32 addr, u32 len),
            u32(**local_erase_hdl)(u32 cmd, u32 addr));
    int (*user_chip_release_opt)(user_chip_update_info_t *update_info);
    void *priv;
} user_file_base_info_t;

typedef struct _nand_bin_file_update_info_t {
    user_chip_update_info_t info;
    void *dev_hdl;
    u32 erase_addr;
    u32 pre_end_addr;
} nand_bin_file_update_info_t;

typedef enum _FLASH_ERASER {
    CHIP_ERASER,
    BLOCK_ERASER,
    SECTOR_ERASER,
    PAGE_ERASER,
} FLASH_ERASER;

#define ERASE_BLOCK_SIZE	(128 * 1024)

#define NAND_BIN_FILE_NAME		"nand.bin"

static nand_bin_file_update_info_t *g_nand_bin_info = NULL;
#define __this (g_nand_bin_info)

#define NAND_UPDATE_IN_FILE_SYSTEM  		1
#define NAND_FLASH_FILE_DOWNLOAD_DEBUG		0
#define NAND_FLASH_ROOT_PATH    "storage/nandflash_ftl/C/"

// 打开函数
static int nandflash_f_open(u32 file_len)
{
    int ret = 0;
#if NAND_FLASH_FILE_DOWNLOAD_DEBUG
    printf("%s, %d, %x\n", __func__, __LINE__, file_len);
#endif
#if NAND_UPDATE_IN_FILE_SYSTEM
    if (__this) {
        __this->dev_hdl = fopen(NAND_FLASH_ROOT_PATH"new_res.bin", "w+");
        if (__this->dev_hdl) {
            log_info("open dev succ 0x%x\n", __this->dev_hdl);
        } else {
            ret = -1;
        }
    } else {
        ret = -2;
    }
#else
    if (__this) {
        //__this->dev_hdl = dev_open("nand_flash", NULL);
        __this->dev_hdl = dev_open("nand_flash_ota", NULL); // 需要打开对应的区域
        if (__this->dev_hdl) {
            log_info("open dev succ 0x%x\n", __this->dev_hdl);
        } else {
            ret = -1;
        }
    } else {
        ret = -2;
    }
#endif
    return ret;
}

static u32 nandflash_f_read(void *buf, u32 addr, u32 len)
{
    int rlen = 0;
#if NAND_FLASH_FILE_DOWNLOAD_DEBUG
    printf("%s, %d, %x, %x\n", __func__, __LINE__, addr, len);
#endif
#if NAND_UPDATE_IN_FILE_SYSTEM
    if (__this && __this->dev_hdl) {
        wdt_clear();
        fseek(__this->dev_hdl, addr, SEEK_SET);
        rlen = fread(__this->dev_hdl, buf, len);
    }
    return rlen;
#else
    if (__this && __this->dev_hdl) {
        wdt_clear();
        if (len % 512) {
            if (len > 512) {
                rlen = dev_bulk_read(__this->dev_hdl, buf, (addr) / 512, len / 512);
            }
            if (dev_bulk_read(__this->dev_hdl, buf + rlen * 512, addr / 512 + rlen, 1)) {
                return rlen * 512 + (len % 512);
            }
        } else {
            rlen = dev_bulk_read(__this->dev_hdl, buf, addr / 512, len / 512);
        }
    }
    return rlen * 512;
#endif
}

extern u16 CRC16(const void *ptr, u32 len);
static u32 nandflash_f_write(void *buf, u32 addr, u32 len)
{
    int rlen = 0;
#if NAND_FLASH_FILE_DOWNLOAD_DEBUG
    printf("%s, %d, %x, %x, %x\n", __func__, __LINE__, addr, len, CRC16(buf, len));
#endif
#if NAND_UPDATE_IN_FILE_SYSTEM
    if (__this && __this->dev_hdl) {
        wdt_clear();
        fseek(__this->dev_hdl, addr, SEEK_SET);
        rlen = fwrite(__this->dev_hdl, buf, len);
    }
    return rlen;
#else
    if (__this && __this->dev_hdl) {
        wdt_clear();
        if (len % 512) {
            if (len > 512) {
                rlen = dev_bulk_write(__this->dev_hdl, buf, (addr) / 512, len / 512);
            }
            memset(buf + len, 0xff, 512 - (len % 512));
            if (dev_bulk_write(__this->dev_hdl, buf + rlen * 512, addr / 512 + rlen, 1)) {
                return rlen * 512 + (len % 512);
            }
        } else {
            rlen = dev_bulk_write(__this->dev_hdl, buf, addr / 512, len / 512);
        }
    }

    return rlen * 512;
#endif
}


// 私有信息特殊处理，遍历当前所有需要升级的结束地址信息
static int erase_addr_filter(u32 addr)
{
    int ret = 1;
    if (__this) {
        u16 offset = 0;
        u16 files_info_len = 0;
        user_chip_update_info_t *update_info = NULL;
        if (__this->info.priv) {
            update_info = (user_chip_update_info_t *)__this->info.priv;
            files_info_len = __this->info.files_info_len;
        } else {
            update_info = &__this->info;
        }

        do {
            // 如果地址在当前范围内，记录旧地址
            if (addr >= update_info->dev_addr && addr < update_info->dev_addr + update_info->len) {
                // 如果旧地址存在的128K范围内，不需要擦除
                if (__this->pre_end_addr
                    && (__this->pre_end_addr / ERASE_BLOCK_SIZE + 1) * ERASE_BLOCK_SIZE > addr
                    && addr > __this->pre_end_addr / ERASE_BLOCK_SIZE * ERASE_BLOCK_SIZE) {
                    ret = 0;
                }

                if (addr >= update_info->dev_addr && addr < (update_info->dev_addr / ERASE_BLOCK_SIZE + 1) * ERASE_BLOCK_SIZE
                    && __this->pre_end_addr != (update_info->dev_addr + update_info->len)) {
                    __this->pre_end_addr = update_info->dev_addr + update_info->len;
                }
                break;
            }

            if (files_info_len) {
                update_info++;
                offset += sizeof(user_chip_update_info_t);
            }
        } while (files_info_len > offset);
    }
    return ret;
}

static u16 nandflash_f_erase(u32 cmd, u32 addr)
{
    u32 type = 0;
#if (0 == NAND_UPDATE_IN_FILE_SYSTEM)
    if (__this) {
        if ((addr / ERASE_BLOCK_SIZE * ERASE_BLOCK_SIZE) != __this->erase_addr) {
            __this->erase_addr = addr / ERASE_BLOCK_SIZE * ERASE_BLOCK_SIZE;
            // 擦除erase_addr
            if (erase_addr_filter(addr)) {
#if NAND_FLASH_FILE_DOWNLOAD_DEBUG
                printf("%s, %d, %x\n", __func__, __LINE__, __this->erase_addr);
#endif
                dev_ioctl(__this->dev_hdl, 201, __this->erase_addr);
            }
        }
    }
#endif
    return 0;
}

// 填充当前操作的方式的特殊的读、写和擦函数，并返回对应的info信息
static user_chip_update_info_t *nand_flash_get_res_base_info(void *priv,
        u32(**local_read_hdl)(void *buf, u32 addr, u32 len),
        u32(**local_write_hdl)(void *buf, u32 addr, u32 len),
        u32(**local_erase_hdl)(u32 cmd, u32 addr))
{
    nand_bin_file_update_info_t *res_info = (nand_bin_file_update_info_t *)priv;
    if (local_write_hdl && local_write_hdl && local_erase_hdl) {
        printf("nand flash start_addr %x and len %d update...\n", res_info->info.dev_addr, res_info->info.len);
        __this = (nand_bin_file_update_info_t *)priv;
        *local_read_hdl = nandflash_f_read;
        *local_write_hdl = nandflash_f_write;
        *local_erase_hdl = nandflash_f_erase;
    }
    return &res_info->info;
}

static int nand_flash_update_release(user_chip_update_info_t *update_info_entry)
{
    if (update_info_entry) {
        if (update_info_entry->priv) {
            free(update_info_entry->priv);
            update_info_entry->priv = NULL;
        }
        free(update_info_entry);
        update_info_entry = NULL;
    }
    return 0;
}

struct list_head *nand_flash_file_update_init(void *priv, u8 type, u8 *file_name, int (*update_info_get)(u8 type, user_chip_update_info_t *info, void *priv))
{
    // 库中传出来的，包含远端地址、长度和crc信息的结构体
    user_chip_update_info_t *info = (user_chip_update_info_t *)priv;
    user_file_base_info_t *base_info = NULL;
    __this = NULL;

    // 先根据当前的名字和类型，匹配当前是否是需要处理
    if (NULL == priv || 0 != memcmp(file_name, NAND_BIN_FILE_NAME, strlen(file_name))) {
        goto _ERR_RET;
    }
    // 重新分配多一个info结构体，把地址、长度和crc信息填充进入
    __this = zalloc(sizeof(nand_bin_file_update_info_t));
    if (NULL == __this) {
        printf("%s, alloc fail\n", __func__);
        goto _ERR_RET;
    }
    memcpy(&__this->info, info, sizeof(user_chip_update_info_t));

    // 再通过update_info_get传入，获取当前的信息，这样做里面就不需要再对info中的priv进行free操作
    if (update_info_get(type, &__this->info, NULL)) {
        printf("%s, get info err\n", __func__);
        goto _ERR_RET;
    }

    // 尝试open设备，并记录相关信息
    if (nandflash_f_open(__this->info.len)) {
        printf("%s, open fail\n", __func__);
        goto _ERR_RET;
    }

    // 分配当前链表信息，记录上面的数据，返回当前链表
    base_info = zalloc(sizeof(user_file_base_info_t));
    if (NULL == base_info) {
        printf("%s, alloc base_info fail\n", __func__);
        goto _ERR_RET;
    }

    base_info->get_res_base_info = nand_flash_get_res_base_info;
    base_info->user_chip_release_opt = nand_flash_update_release;
    base_info->priv = (void *)__this;

    return &base_info->entry;

_ERR_RET:
    if (__this) {
        // 注意，如果info中的priv成员有值，最后是需要free掉
        if (__this->info.priv) {
            free(__this->info.priv);
            __this->info.priv = NULL;
        }
        free(__this);
        __this = NULL;
    }
    if (base_info) {
        free(base_info);
    }
    return NULL;
}

#endif

