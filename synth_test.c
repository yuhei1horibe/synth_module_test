#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <stdint.h>
#include <time.h>

//#define DEBUG

#define UIO_DEV_PATH "/sys/class/uio/"
#define DEVNAME_MAX 128

// Synthesizer module test
int synth_test(void* mem_base, unsigned int mem_size)
{
    static const uint32_t num_units = 32;
    uint32_t freq        = 0;
    uint32_t amp         = 0;
    uint32_t vca_attack  = 0;
    uint32_t vca_decay   = 0;
    uint32_t vca_sustain = 0;
    uint32_t vca_release = 0;
    uint32_t vca_eg      = 0;
    uint32_t wave_type   = 0;
    uint32_t read_data   = 0;
    uint32_t* write_addr = NULL;
    uint32_t* read_addr  = NULL;
    uint32_t errors      = 0;

    //uint32_t i;
    uint32_t j;

    //srand((unsigned)time(NULL));

    printf("TDM multiplier test started.\n");

    // Generate and write operands
    for (j = 0; j < num_units; j++) {
        // VCO
        wave_type = j % 3;
        freq = 2;

        // VCA
        vca_attack  = j % 4;
        vca_decay   = j % 4;
        vca_sustain = 0x80;
        vca_release = j % 4;
        vca_eg      = (vca_release << 24) |
                      (vca_sustain << 16) |
                      (vca_decay   << 8)  |
                      vca_attack;

        amp = 0x100;

        printf("Writing(unit%u): %u[Hz] and %0.2f\n", j, freq, (float)amp/256);
        printf("A: %u, D: %u, S: %u, R: %u\n", vca_attack, vca_decay, vca_sustain, vca_release);

        // Write to the register
        write_addr        = (uint32_t*)(mem_base + sizeof(uint32_t) * 3 * j);
        *write_addr       = (amp << 16) | freq;
        *(write_addr + 1) = wave_type;
        *(write_addr + 2) = vca_eg;
    }
    usleep(100);
    for (j = 0; j < num_units * 3; j++) {
        read_addr = (uint32_t*)(mem_base + sizeof(uint32_t) * j);
        read_data = *read_addr;
        printf("Data read(%x): %x\n", (uint32_t)read_addr, read_data);
    }

    // Trigger
    for (j = 0; j < num_units; j++) {
        write_addr = (uint32_t*)(mem_base + sizeof(uint32_t) * 3 * j);
        printf("Unit%u on\n", j);
        *(write_addr + 1) |= 0x4;
        sleep(2);
        printf("Unit%u off\n", j);
        *(write_addr + 1) &= ~0x4;
        sleep(2);
    }
    return 0;
}

static bool find_uio_dev(const char* device_name, char* device_dir, int sz_dev_dir)
{
    DIR*           d;
    struct dirent* dir;
    bool           dev_found = false;
    char dev_dir[DEVNAME_MAX];

    if (!device_name || !device_dir) {
        return false;
    }

	// Find UIO device directory
    d = opendir(UIO_DEV_PATH);
    if (d) {
        int  name_fd    = -1;
        int  bytes_read = 0;
        char name_filename[DEVNAME_MAX];
        char dev_name[DEVNAME_MAX];

		while ((dir = readdir(d)) != NULL) {
            strncpy(dev_dir, UIO_DEV_PATH, DEVNAME_MAX);
            strncat(dev_dir, dir->d_name, DEVNAME_MAX);
            strncpy(name_filename, dev_dir, DEVNAME_MAX);
            strncat(name_filename, "/name", DEVNAME_MAX);

            name_fd = open(name_filename, O_RDONLY);

            // Check "name" file and match the device
            if (name_fd >= 0) {
                bytes_read = read(name_fd, dev_name, DEVNAME_MAX);
                if (bytes_read != 0) {
                    dev_name[bytes_read-1] = '\0';
                    if (strncmp(device_name, dev_name, DEVNAME_MAX) == 0) {
                        strncpy(device_dir, dev_dir, sz_dev_dir);
                        return true;
                    }
                }
                close(name_fd);
            }
		}
		closedir(d);
	}
    return false;
}

static int get_uio_mapping(const char* map_dir, unsigned int* base_addr, unsigned int* mem_size, unsigned int* mem_offset)
{
    char filename[DEVNAME_MAX];
    char buff[DEVNAME_MAX];
    int map_fd = 0;
    int bytes_read = 0;

    if (!base_addr ||
        !mem_size  ||
        !mem_offset) {
        return -1;
    }
    *base_addr  = 0;
    *mem_size   = 0;
    *mem_offset = 0;

    strncpy(filename, map_dir, DEVNAME_MAX);

    // Address
    strncat(filename, "addr", DEVNAME_MAX);
    map_fd = open(filename, O_RDONLY);
    if (map_fd >= 0) {
        bytes_read = read(map_fd, buff, DEVNAME_MAX);
        if (bytes_read > 0) {
            *base_addr = strtoul(buff, NULL, 16);
        }
        close(map_fd);
    }

    // Size
    filename[strlen(map_dir)] = '\0';
    strncat(filename, "size", DEVNAME_MAX);
    map_fd = open(filename, O_RDONLY);
    if (map_fd >= 0) {
        bytes_read = read(map_fd, buff, DEVNAME_MAX);
        if (bytes_read > 0) {
            *mem_size = strtoul(buff, NULL, 16);
        }
        close(map_fd);
    }

    // Offset
    filename[strlen(map_dir)] = '\0';
    strncat(filename, "offset", DEVNAME_MAX);
    map_fd = open(filename, O_RDONLY);
    if (map_fd >= 0) {
        bytes_read = read(map_fd, buff, DEVNAME_MAX);
        if (bytes_read > 0) {
            *mem_offset = strtoul(buff, NULL, 16);
        }
        close(map_fd);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char dev_dir[DEVNAME_MAX];
    char dev_map_dir[DEVNAME_MAX];
    char map_dir[] = "/maps/map0/";
    unsigned int base_addr  = 0;
    unsigned int mem_size   = 0;
    unsigned int mem_offset = 0;

    if (find_uio_dev("zed_uio_module", dev_dir, DEVNAME_MAX) == true) {
        printf("Device found in %s\n", dev_dir);

        strncpy(dev_map_dir, dev_dir, DEVNAME_MAX);
        strncat(dev_map_dir, map_dir, DEVNAME_MAX);
        if (get_uio_mapping(dev_map_dir, &base_addr, &mem_size, &mem_offset) != 0) {
            printf("Failed to get memory mapping from sysfs\n");
            return 0;
        }
        printf("Base address: %x, Memory size: %x, Offset: %u\n", base_addr, mem_size, mem_offset);
        if ((base_addr > 0) && (mem_size > 0)) {
            int uio_fd = 0;
            char uio_filename[DEVNAME_MAX];

            strncpy(uio_filename, "/dev/", DEVNAME_MAX);
            strncat(uio_filename, &dev_dir[strlen(UIO_DEV_PATH)], DEVNAME_MAX);

            // Memory map
            uio_fd = open(uio_filename, O_RDWR);
            if (uio_fd >= 0) {
                printf("UIO device: %s opened: %d\n", uio_filename, uio_fd);
                void* dev_mem_base = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, mem_offset);
                if (dev_mem_base != NULL) {
                    int result = 0;
                    printf("mmap() success\n\n");

                    // Add test here
                    result = synth_test(dev_mem_base, mem_size);

                    munmap(dev_mem_base, mem_size);
                }
                close(uio_fd);
            }

            else {
                printf("Failed to open device: %s\n", uio_filename);
            }
        }
        else {
            printf("Invalid memory address or size\n");
            return 0;
        }
    }
    return 0;
}
