# FAT 文件读取串口验证

打开原工程 D:\STM32 project\sd_card\MDK-ARM\sd_card.uvprojx，编译并烧录。

## 接线
- SD 卡插开发板板载卡座；本阶段不用连接 PCM5102。
- PA9（USART1 TX）接 USB-TTL RX，GND 接 GND。
- 如需连接接收线，PA10 接 USB-TTL TX。本程序无需发送串口命令。
- USB-TTL 使用 3.3V 逻辑电平，开发板按原方式供电。
- 串口参数：115200、8 数据位、无校验、1 停止位、无流控。
- 中文文件名采用 GBK 输出，串口工具请选择 GBK/GB2312；其余提示为英文。

## 操作
1. 使用已有 FAT16/FAT32 格式的卡。此版本不支持 exFAT/NTFS，也绝不会格式化或写卡。
2. 将至少一个 MP3 放在根目录。本阶段列出根目录文件和文件夹，不递归进入子目录。
3. 先打开串口，再按复位。程序初始化 SD 卡并挂载 0:。
4. 串口列出长文件名、短文件名、文件大小；选择目录顺序中的第一个 MP3。
5. 打印其前 64 字节，再完整读取至 EOF，输出字节数和 CRC32。
6. 按板载 PA0，重复挂载、扫描和读取。按键读取期间不接受再次触发，松开后才能再触发。

预期输出结构（示例占位，不代表已上板实测）：
```
SD Card OK (4-bit SDIO)
=== FAT read-only verification ===
[PASS] mount 0: FAT32, cluster=... sectors
[ROOT] name (GBK), short alias, size
[FILE] ... [....MP3] ... bytes
[PASS] root scan: files=... folders=... MP3=...
[OPEN] 0:/....MP3 size=... bytes
[HEAD] first 64 bytes:
...
[PASS] EOF bytes=... CRC32=........ elapsed=... ms
```

MP3 文件不一定以 ID3 开头；十六进制预览只验证文件内容读取，尚不解析或播放 MP3。
CRC32 为标准 CRC-32/ISO-HDLC（与 Python zlib.crc32 相同）。把同一文件放在电脑上，运行：
```
python crc32_file.py "歌曲文件完整路径.mp3"
```
对比两端 bytes 和 CRC32，完全一致即可验证读取内容。耗时包括文件头串口输出，不是存储吞吐量测试。

## 错误定位
- FR_NO_FILESYSTEM：检查卡的文件系统。先备份歌曲，再决定如何转换格式；程序本身不格式化。
- FR_DISK_ERR / FR_NOT_READY：检查 SD 卡接触、电源和 SDIO；记录打印的 HAL 错误码。
- No MP3 in root：把一个 MP3 放到根目录，按 PA0 重试。
- 中文乱码：将串口显示编码改为 GBK。超出 GBK 字符集的名称可能显示短文件名。
- 全文件读取期间 LED 可能暂时停止闪烁，大文件需等到 EOF。此版本没有热插拔检测，请更换卡后复位。

## 实现及验证
- FatFs R0.10b 和 CP936 转码表来自本地例21 FATFS实验，保留原作者许可。
- _FS_READONLY=1、_USE_WRITE=0，仅一个 SD 卷，不使用动态分配。
- HAL 适配层使用有超时的轮询读取，512 字节对齐中转缓冲，并检查扇区边界。
- 中文长文件名使用静态缓冲；这是单线程测试程序，不可并发调用 FatFs。
- Keil ARM Compiler 6.23：0 错误、4 警告（3 个原 HAL Flash 参数警告，1 个 FatFs 只读配置下未使用函数警告）。
- 主机模拟 FAT16 碎片文件测试通过：挂载、目录、完整读取、EOF、跨扇区定位读取和文件不存在。
- 未连接开发板进行烧录、真实 SDIO 或串口实测。
- CubeMX 尚未配置中间件；重新生成代码可能移除自定义 Keil 源文件/包含路径及手工 SDIO 初始化逻辑，请先备份。
