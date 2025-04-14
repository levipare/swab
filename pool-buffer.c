#include "pool-buffer.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pixman.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int create_shm_file(void) {
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

int allocate_shm_file(size_t size) {
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void pool_buffer_create(struct pool_buffer *pb, struct wl_shm *shm,
                        uint32_t width, uint32_t height) {
    pb->width = width;
    pb->height = height;

    int stride = width * 4;
    pb->size = stride * height;

    int fd = allocate_shm_file(pb->size);
    assert(fd != -1);

    pb->data = mmap(NULL, pb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(pb->data != MAP_FAILED);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, pb->size);
    pb->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                           WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    pb->pix = pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, width, height,
                                                pb->data, stride);
}

void pool_buffer_destroy(struct pool_buffer *buffer) {
    if (buffer->buffer) {
        wl_buffer_destroy(buffer->buffer);
    }
    if (buffer->pix) {
        pixman_image_unref(buffer->pix);
    }
    if (buffer->data) {
        munmap(buffer->data, buffer->size);
    }
}
