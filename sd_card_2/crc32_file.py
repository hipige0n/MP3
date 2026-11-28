import sys,zlib
crc=0
size=0
with open(sys.argv[1],'rb') as f:
    while True:
        block=f.read(65536)
        if not block: break
        size+=len(block)
        crc=zlib.crc32(block,crc)
print(f'bytes={size} CRC32={crc:08X}')
