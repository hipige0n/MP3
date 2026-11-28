/* Root directory listing and full-file CRC32 verification. No writes. */
#include "fat_verify.h"
void MP3_Verify(const char *path);
#include "ff.h"
#include "sdio.h"
#include <stdio.h>
#include <string.h>
static FATFS filesystem;
static FIL file;
static DIR directory;
static FILINFO info;
static char long_name[256];
static char first_mp3[16]; /* Use FAT short alias to open even Chinese names. */
static BYTE buffer[1024];
static const char *result_name(FRESULT r)
{
    static const char *const names[] = {
        "FR_OK", "FR_DISK_ERR", "FR_INT_ERR", "FR_NOT_READY", "FR_NO_FILE",
        "FR_NO_PATH", "FR_INVALID_NAME", "FR_DENIED", "FR_EXIST", "FR_INVALID_OBJECT",
        "FR_WRITE_PROTECTED", "FR_INVALID_DRIVE", "FR_NOT_ENABLED", "FR_NO_FILESYSTEM",
        "FR_MKFS_ABORTED", "FR_TIMEOUT", "FR_LOCKED", "FR_NOT_ENOUGH_CORE",
        "FR_TOO_MANY_OPEN_FILES", "FR_INVALID_PARAMETER"
    };
    return (unsigned)r < sizeof(names)/sizeof(names[0]) ? names[r] : "UNKNOWN";
}
static void error(const char *step, FRESULT r)
{
    printf("[FAIL] %s: %s (%u), SD error=0x%08lX\r\n", step,
           result_name(r), (unsigned)r, (unsigned long)HAL_SD_GetError(&hsd));
    if (r == FR_NO_FILESYSTEM)
        printf("Use FAT16/FAT32. exFAT/NTFS are unsupported. No formatting is performed.\r\n");
}
static int is_mp3(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && ext[1] && ext[2] && ext[3] && !ext[4] &&
           (ext[1]=='m'||ext[1]=='M') && (ext[2]=='p'||ext[2]=='P') && ext[3]=='3';
}
static uint32_t crc_update(uint32_t crc, const BYTE *data, UINT size)
{
    while (size--) {
        crc ^= *data++;
        for (unsigned bit=0; bit<8; bit++)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc;
}
static void verify_file(const char *name)
{
    FRESULT r;
    UINT n;
    DWORD total=0, expected;
    uint32_t crc=0xFFFFFFFFUL, started;
    r=f_open(&file,name,FA_READ);
    if (r!=FR_OK) { error("open",r); return; }
    expected=f_size(&file);
    printf("[OPEN] %s size=%lu bytes\r\n",name,(unsigned long)expected);
    started=HAL_GetTick();
    r=f_read(&file,buffer,64,&n);
    if (r!=FR_OK) { error("preview",r); (void)f_close(&file); return; }
    printf("[HEAD] first %u bytes:\r\n",n);
    for (UINT i=0;i<n;i++) {
        printf("%02X ",buffer[i]);
        if ((i&15U)==15U) printf("\r\n");
    }
    if (n&15U) printf("\r\n");
    crc=crc_update(crc,buffer,n); total=n;
    while (r==FR_OK) {
        r=f_read(&file,buffer,sizeof(buffer),&n);
        if (r!=FR_OK || n==0) break;
        crc=crc_update(crc,buffer,n); total+=n;

    }
    uint32_t elapsed=HAL_GetTick()-started;
    if (r!=FR_OK) error("read",r);
    FRESULT close_result=f_close(&file);
    if (close_result!=FR_OK) error("close",close_result);
    if (r==FR_OK && close_result==FR_OK && total==expected)
        printf("[PASS] EOF bytes=%lu CRC32=%08lX elapsed=%lu ms\r\n",
               (unsigned long)total,(unsigned long)(crc^0xFFFFFFFFUL),(unsigned long)elapsed);
    else printf("[FAIL] read incomplete: actual=%lu expected=%lu\r\n",
                (unsigned long)total,(unsigned long)expected);
}
void FAT_Verify(void)
{
    FRESULT r;
    unsigned files=0,folders=0,mp3s=0;
    printf("\r\n=== FAT read-only verification ===\r\n");
    (void)f_mount(NULL,"0:",0);
    r=f_mount(&filesystem,"0:",1);
    if (r!=FR_OK) { error("mount",r); return; }
    printf("[PASS] mount 0: FAT%u, cluster=%u sectors\r\n",
           filesystem.fs_type==FS_FAT12?12:filesystem.fs_type==FS_FAT16?16:32,
           filesystem.csize);
    first_mp3[0]=0;
    r=f_opendir(&directory,"0:/");
    if (r!=FR_OK) { error("opendir",r); return; }
    memset(&info,0,sizeof(info));
    info.lfname=long_name; info.lfsize=sizeof(long_name);
    printf("[ROOT] name (GBK), short alias, size\r\n");
    for (;;) {
        long_name[0]=0;
        r=f_readdir(&directory,&info);
        if (r!=FR_OK || !info.fname[0]) break;
        if (info.fname[0]=='.') continue;
        const char *display=long_name[0]?long_name:info.fname;
        printf("%s %s [%s] %lu bytes\r\n",(info.fattrib&AM_DIR)?"[DIR]":"[FILE]",
               display,info.fname,(unsigned long)info.fsize);
        if (info.fattrib&AM_DIR) { folders++; continue; }
        files++;
        if (is_mp3(info.fname)) {
            mp3s++;
            if (!first_mp3[0]) snprintf(first_mp3,sizeof(first_mp3),"0:/%s",info.fname);
        }
    }
    FRESULT close_result=f_closedir(&directory);
    if (r!=FR_OK) { error("readdir",r); return; }
    if (close_result!=FR_OK) { error("closedir",close_result); return; }
    printf("[PASS] root scan: files=%u folders=%u MP3=%u\r\n",files,folders,mp3s);
    if (first_mp3[0]) { MP3_Verify(first_mp3); }
    else printf("[INFO] No MP3 in root. Put a test MP3 in root and press PA0.\r\n");
    printf("Press PA0 to repeat. Audio test firmware.\r\n");
}

