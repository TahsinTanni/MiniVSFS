#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <getopt.h>
#include <unistd.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u

uint64_t g_random_seed = 0;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t data_bitmap_start;
    uint64_t data_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;
    uint64_t mtime_epoch;
    uint32_t flags;
    
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum;
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    uint16_t mode;
    uint16_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size_bytes;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct[12];
    uint32_t reserved_0;
    uint32_t reserved_1;
    uint32_t reserved_2;
    uint32_t proj_id;
    uint32_t uid16_gid16;
    uint64_t xattr_ptr;

    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc;

} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;
    uint8_t type;
    char name[58];

    uint8_t  checksum;
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");

#define MODE_FILE 0100000
#define MODE_DIR  0040000

#define TYPE_FILE 1
#define TYPE_DIR  2

uint32_t calculate_crc32(const void* data, size_t length);
static uint32_t superblock_crc_finalize(superblock_t* sb);
void inode_crc_finalize(inode_t* inode);
void dirent_checksum_finalize(dirent64_t* dirent);

uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER INODE ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; 
    memcpy(tmp, ino, INODE_SIZE);
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER DIRENT ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];
    de->checksum = x;
}

void print_usage(const char* program_name) {
    printf("Usage: %s --image <output.img> --size-kib <180..4096> --inodes <128..512>\n", program_name);
    printf("  --image: Output image file name\n");
    printf("  --size-kib: Total size in kilobytes (multiple of 4, range 180-4096)\n");
    printf("  --inodes: Number of inodes (range 128-512)\n");
}

int main(int argc, char* argv[]) {
    crc32_init();
    
    char* image_name = NULL;
    uint64_t size_kib = 0;
    uint64_t inode_count = 0;
    
    static struct option long_options[] = {
        {"image", required_argument, 0, 'i'},
        {"size-kib", required_argument, 0, 's'},
        {"inodes", required_argument, 0, 'n'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                image_name = optarg;
                break;
            case 's':
                size_kib = atoi(optarg);
                break;
            case 'n':
                inode_count = atoi(optarg);
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (!image_name || size_kib == 0 || inode_count == 0) {
        fprintf(stderr, "Error: All arguments are required\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (size_kib < 180 || size_kib > 4096 || size_kib % 4 != 0) {
        fprintf(stderr, "Error: size-kib must be between 180-4096 and multiple of 4\n");
        return 1;
    }
    
    if (inode_count < 128 || inode_count > 512) {
        fprintf(stderr, "Error: inodes must be between 128-512\n");
        return 1;
    }
    
    uint64_t total_blocks = (size_kib * 1024) / BS;
    uint64_t inode_table_blocks = (inode_count * INODE_SIZE + BS - 1) / BS;
    
    uint64_t metadata_blocks = 1 + 1 + 1 + inode_table_blocks;
    if (metadata_blocks >= total_blocks) {
        fprintf(stderr, "Error: Not enough space for metadata with given parameters\n");
        return 1;
    }
    
    uint64_t data_region_blocks = total_blocks - metadata_blocks;
    
    superblock_t superblock = {0};
    superblock.magic = 0x4D565346;
    superblock.version = 1;
    superblock.block_size = BS;
    superblock.total_blocks = total_blocks;
    superblock.inode_count = inode_count;
    superblock.inode_bitmap_start = 1;
    superblock.inode_bitmap_blocks = 1;
    superblock.data_bitmap_start = 2;
    superblock.data_bitmap_blocks = 1;
    superblock.inode_table_start = 3;
    superblock.inode_table_blocks = inode_table_blocks;
    superblock.data_region_start = 3 + inode_table_blocks;
    superblock.data_region_blocks = data_region_blocks;
    superblock.root_inode = 1;
    superblock.mtime_epoch = time(NULL);
    superblock.flags = 0;
    
    FILE* img_file = fopen(image_name, "wb");
    if (!img_file) {
        fprintf(stderr, "Error: Cannot create image file '%s': %s\n", image_name, strerror(errno));
        return 1;
    }
    
    uint8_t* block_buffer = calloc(1, BS);
    if (!block_buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(img_file);
        return 1;
    }
    
    superblock_crc_finalize(&superblock);
    memcpy(block_buffer, &superblock, sizeof(superblock));
    fwrite(block_buffer, 1, BS, img_file);
    memset(block_buffer, 0, BS);
    
    block_buffer[0] = 0x01;
    fwrite(block_buffer, 1, BS, img_file);
    memset(block_buffer, 0, BS);
    
    block_buffer[0] = 0x01;
    fwrite(block_buffer, 1, BS, img_file);
    memset(block_buffer, 0, BS);
    
    inode_t root_inode = {0};
    root_inode.mode = MODE_DIR;
    root_inode.links = 2;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.size_bytes = 2 * sizeof(dirent64_t);
    root_inode.atime = superblock.mtime_epoch;
    root_inode.mtime = superblock.mtime_epoch;
    root_inode.ctime = superblock.mtime_epoch;
    root_inode.direct[0] = superblock.data_region_start;
    for (int i = 1; i < 12; i++) {
        root_inode.direct[i] = 0;
    }
    root_inode.proj_id = 0;
    inode_crc_finalize(&root_inode);
    
    for (uint64_t block = 0; block < inode_table_blocks; block++) {
        memset(block_buffer, 0, BS);
        
        if (block == 0) {
            memcpy(block_buffer, &root_inode, sizeof(inode_t));
        }
        
        fwrite(block_buffer, 1, BS, img_file);
    }
    
    dirent64_t dot_entry = {0};
    dot_entry.inode_no = 1;
    dot_entry.type = TYPE_DIR;
    strcpy(dot_entry.name, ".");
    dirent_checksum_finalize(&dot_entry);
    
    dirent64_t dotdot_entry = {0};
    dotdot_entry.inode_no = 1;
    dotdot_entry.type = TYPE_DIR;
    strcpy(dotdot_entry.name, "..");
    dirent_checksum_finalize(&dotdot_entry);
    
    for (uint64_t block = 0; block < data_region_blocks; block++) {
        memset(block_buffer, 0, BS);
        
        if (block == 0) {
            memcpy(block_buffer, &dot_entry, sizeof(dirent64_t));
            memcpy(block_buffer + sizeof(dirent64_t), &dotdot_entry, sizeof(dirent64_t));
        }
        
        fwrite(block_buffer, 1, BS, img_file);
    }
    
    free(block_buffer);
    fclose(img_file);
    
    printf("Successfully created MiniVSFS image '%s'\n", image_name);
    printf("Size: %lu KiB (%lu blocks)\n", size_kib, total_blocks);
    printf("Inodes: %lu\n", inode_count);
    
    return 0;
}
