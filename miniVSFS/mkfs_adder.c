#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12

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
    uint32_t checksum;                // crc32(superblock[0..4091])
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
    uint32_t direct[DIRECT_MAX];      
    uint32_t reserved_0;              
    uint32_t reserved_1;              
    uint32_t reserved_2;              
    uint32_t proj_id;                 
    uint32_t uid16_gid16;             
    uint64_t xattr_ptr;               

    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc;   // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0

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

// File modes
#define MODE_FILE 0100000  
#define MODE_DIR  0040000  

// Directory entry types
#define TYPE_FILE 1
#define TYPE_DIR  2

// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
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
// ====================================CRC32====================================

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
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER DIRENT ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   // covers ino(4) + type(1) + name(58)
    de->checksum = x;
}

// Helper functions
int find_free_inode(uint8_t* bitmap, uint64_t inode_count) {
    for (uint64_t i = 0; i < inode_count; i++) {
        uint64_t byte_index = i / 8;
        uint64_t bit_index = i % 8;
        if (!(bitmap[byte_index] & (1 << bit_index))) {
            return i + 1;  // Return 1-indexed inode number
        }
    }
    return -1;  // No free inode found
}

int find_free_data_block(uint8_t* bitmap, uint64_t data_blocks) {
    for (uint64_t i = 0; i < data_blocks; i++) {
        uint64_t byte_index = i / 8;
        uint64_t bit_index = i % 8;
        if (!(bitmap[byte_index] & (1 << bit_index))) {
            return i;  // Return 0-indexed block number relative to data region
        }
    }
    return -1;  // No free block found
}

void set_bit(uint8_t* bitmap, int bit_index) {
    uint64_t byte_index = bit_index / 8;
    uint64_t bit_pos = bit_index % 8;
    bitmap[byte_index] |= (1 << bit_pos);
}

void print_usage(const char* program_name) {
    printf("Usage: %s --input <input.img> --output <output.img> --file <filename>\n", program_name);
    printf("  --input: Input image file name\n");
    printf("  --output: Output image file name\n");
    printf("  --file: File to add to the filesystem\n");
}

int main(int argc, char* argv[]) {
    crc32_init();
    
    
    char* input_name = NULL;
    char* output_name = NULL;
    char* file_name = NULL;
    
    
    static struct option long_options[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"file", required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                input_name = optarg;
                break;
            case 'o':
                output_name = optarg;
                break;
            case 'f':
                file_name = optarg;
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    
    if (!input_name || !output_name || !file_name) {
        fprintf(stderr, "Error: All arguments are required\n");
        print_usage("mkfs_adder");
        return 1;
    }
    
    struct stat file_stat;
    if (stat(file_name, &file_stat) != 0) {
        fprintf(stderr, "Error: File '%s' not found: %s\n", file_name, strerror(errno));
        return 1;
    }
    
    if (!S_ISREG(file_stat.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a regular file\n", file_name);
        return 1;
    }
    
    uint64_t file_size = file_stat.st_size;
    uint64_t blocks_needed = (file_size + BS - 1) / BS;
    if (blocks_needed > DIRECT_MAX) {
        fprintf(stderr, "Error: File '%s' is too large (max %dKB with %d direct blocks)\n", 
                file_name, (DIRECT_MAX * BS) / 1024, DIRECT_MAX);
        return 1;
    }
    
   
    FILE* input_file = fopen(input_name, "rb");
    if (!input_file) {
        fprintf(stderr, "Error: Cannot open input image '%s': %s\n", input_name, strerror(errno));
        return 1;
    }
    
    
    superblock_t superblock;
    if (fread(&superblock, sizeof(superblock), 1, input_file) != 1) {
        fprintf(stderr, "Error: Cannot read superblock\n");
        fclose(input_file);
        return 1;
    }
    
    if (superblock.magic != 0x4D565346) {  
        fprintf(stderr, "Error: Invalid filesystem magic number\n");
        fclose(input_file);
        return 1;
    }
    
    
    uint64_t image_size = superblock.total_blocks * BS;
    uint8_t* image_data = malloc(image_size);
    if (!image_data) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(input_file);
        return 1;
    }
    
    
    fseek(input_file, 0, SEEK_SET);
    if (fread(image_data, 1, image_size, input_file) != image_size) {
        fprintf(stderr, "Error: Cannot read image data\n");
        free(image_data);
        fclose(input_file);
        return 1;
    }
    fclose(input_file);
    
    
    uint8_t* inode_bitmap = image_data + superblock.inode_bitmap_start * BS;
    uint8_t* data_bitmap = image_data + superblock.data_bitmap_start * BS;
    inode_t* inode_table = (inode_t*)(image_data + superblock.inode_table_start * BS);
    uint8_t* data_region = image_data + superblock.data_region_start * BS;
    
    
    int free_inode_num = find_free_inode(inode_bitmap, superblock.inode_count);
    if (free_inode_num == -1) {
        fprintf(stderr, "Error: No free inodes available\n");
        free(image_data);
        return 1;
    }
    
    
    int* free_blocks = malloc(blocks_needed * sizeof(int));
    if (!free_blocks) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        free(image_data);
        return 1;
    }
    
    
    uint64_t blocks_found = 0;
    for (uint64_t i = 0; i < superblock.data_region_blocks && blocks_found < blocks_needed; i++) {
        uint64_t byte_index = i / 8;
        uint64_t bit_index = i % 8;
        if (!(data_bitmap[byte_index] & (1 << bit_index))) {
            free_blocks[blocks_found] = (int)i;
            blocks_found++;
        }
    }
    
    if (blocks_found < blocks_needed) {
        fprintf(stderr, "Error: Not enough free data blocks (need %lu, found %lu)\n", blocks_needed, blocks_found);
        free(free_blocks);
        free(image_data);
        return 1;
    }
    
    
    FILE* add_file = fopen(file_name, "rb");
    if (!add_file) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", file_name, strerror(errno));
        free(free_blocks);
        free(image_data);
        return 1;
    }
    
    
    inode_t* new_inode = &inode_table[free_inode_num - 1];  
    memset(new_inode, 0, sizeof(inode_t));
    new_inode->mode = MODE_FILE;
    new_inode->links = 1;
    new_inode->uid = 0;
    new_inode->gid = 0;
    new_inode->size_bytes = file_size;
    time_t now = time(NULL);
    new_inode->atime = now;
    new_inode->mtime = now;
    new_inode->ctime = now;
    
    
    for (uint64_t i = 0; i < blocks_needed; i++) {
        new_inode->direct[i] = superblock.data_region_start + free_blocks[i];
        
        
        uint8_t* block_ptr = data_region + free_blocks[i] * BS;
        memset(block_ptr, 0, BS);
        
        size_t bytes_to_read = BS;
        if (i == blocks_needed - 1) {
            bytes_to_read = file_size % BS;
            if (bytes_to_read == 0) bytes_to_read = BS;
        }
        
        if (fread(block_ptr, 1, bytes_to_read, add_file) != bytes_to_read) {
            fprintf(stderr, "Error reading file data\n");
            fclose(add_file);
            free(free_blocks);
            free(image_data);
            return 1;
        }
        
        
        set_bit(data_bitmap, free_blocks[i]);
    }
    
    fclose(add_file);
    
    
    for (uint64_t i = blocks_needed; i < DIRECT_MAX; i++) {
        new_inode->direct[i] = 0;
    }
    
    new_inode->proj_id = 0;
    new_inode->uid16_gid16 = 0;
    new_inode->xattr_ptr = 0;
    new_inode->reserved_0 = 0;
    new_inode->reserved_1 = 0;
    new_inode->reserved_2 = 0;
    
    
    inode_crc_finalize(new_inode);
    
    
    set_bit(inode_bitmap, free_inode_num - 1);  
    
    
    uint8_t* root_data_block = data_region;
    dirent64_t* dirents = (dirent64_t*)root_data_block;
    
    int max_dirents = BS / sizeof(dirent64_t);
    int free_dirent_slot = -1;
    
    // Check for duplicate filenames and find free slot
    for (int i = 0; i < max_dirents; i++) {
        if (dirents[i].inode_no != 0) {
            // Check if filename already exists
            if (strcmp(dirents[i].name, file_name) == 0) {
                fprintf(stderr, "Error: File '%s' already exists in filesystem\n", file_name);
                free(free_blocks);
                free(image_data);
                return 1;
            }
        } else if (free_dirent_slot == -1) {
            // Found first free slot
            free_dirent_slot = i;
        }
    }
    
    if (free_dirent_slot == -1) {
        fprintf(stderr, "Error: No free directory entry slots in root directory\n");
        free(free_blocks);
        free(image_data);
        return 1;
    }
    
    
    dirent64_t* new_dirent = &dirents[free_dirent_slot];
    memset(new_dirent, 0, sizeof(dirent64_t));
    new_dirent->inode_no = free_inode_num;
    new_dirent->type = TYPE_FILE;
    strncpy(new_dirent->name, file_name, 57);
    new_dirent->name[57] = '\0';
    
    
    dirent_checksum_finalize(new_dirent);
    
    
    inode_t* root_inode = &inode_table[0];  
    root_inode->links++;  
    root_inode->size_bytes += sizeof(dirent64_t);
    root_inode->mtime = now;
    inode_crc_finalize(root_inode);
    
    
    memcpy(image_data, &superblock, sizeof(superblock));
    superblock_crc_finalize((superblock_t*)image_data);
    
    
    
    
    
    FILE* output_file = fopen(output_name, "wb");
    if (!output_file) {
        fprintf(stderr, "Error: Cannot create output image '%s': %s\n", output_name, strerror(errno));
        free(free_blocks);
        free(image_data);
        return 1;
    }
    
    if (fwrite(image_data, 1, image_size, output_file) != image_size) {
        fprintf(stderr, "Error: Cannot write output image\n");
        fclose(output_file);
        free(free_blocks);
        free(image_data);
        return 1;
    }
    
    fclose(output_file);
    free(free_blocks);
    free(image_data);
    
    printf("Successfully added file '%s' to filesystem image '%s'\n", file_name, output_name);
    printf("File size: %lu bytes (%lu blocks)\n", file_size, blocks_needed);
    printf("Assigned inode: %d\n", free_inode_num);
    
    return 0;
}
