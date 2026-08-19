#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "PalApi.h"

#define LOAD(name) do { \
    *(void **)(&p_##name) = dlsym(lib, #name); \
    if (!p_##name) { fprintf(stderr, "missing %s: %s\n", #name, dlerror()); return 1; } \
} while (0)

static int32_t cb(pal_stream_handle_t *h, uint32_t event, uint32_t *data,
                  uint32_t size, uint64_t cookie) {
    (void)h; (void)event; (void)data; (void)size; (void)cookie;
    return 0;
}

int main(int argc, char **argv) {
    int32_t (*p_pal_init)(void);
    void (*p_pal_deinit)(void);
    int32_t (*p_pal_stream_open)(struct pal_stream_attributes *, uint32_t,
        struct pal_device *, uint32_t, struct modifier_kv *, pal_stream_callback,
        uint64_t, pal_stream_handle_t **);
    int32_t (*p_pal_stream_start)(pal_stream_handle_t *);
    ssize_t (*p_pal_stream_write)(pal_stream_handle_t *, struct pal_buffer *);
    int32_t (*p_pal_stream_stop)(pal_stream_handle_t *);
    int32_t (*p_pal_stream_close)(pal_stream_handle_t *);
    void *lib = dlopen("/vendor/lib64/libar-pal.so", RTLD_NOW | RTLD_LOCAL);
    struct pal_stream_attributes attr;
    struct pal_device dev;
    pal_stream_handle_t *stream = NULL;
    const uint32_t channels = 8, frames = 960;
    int16_t *samples;
    struct pal_buffer buffer;
    const char *custom_key = argc > 1 ? argv[1] : "";
    int32_t rc;
    int pal_init_owned;

    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    LOAD(pal_init); LOAD(pal_deinit); LOAD(pal_stream_open); LOAD(pal_stream_start);
    LOAD(pal_stream_write); LOAD(pal_stream_stop); LOAD(pal_stream_close);

    memset(&attr, 0, sizeof(attr));
    attr.type = PAL_STREAM_DEEP_BUFFER;
    attr.direction = PAL_AUDIO_OUTPUT;
    attr.out_media_config.sample_rate = 48000;
    attr.out_media_config.bit_width = 16;
    attr.out_media_config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
    attr.out_media_config.ch_info.channels = channels;
    attr.out_media_config.ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
    attr.out_media_config.ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
    attr.out_media_config.ch_info.ch_map[2] = PAL_CHMAP_CHANNEL_C;
    attr.out_media_config.ch_info.ch_map[3] = PAL_CHMAP_CHANNEL_LFE;
    attr.out_media_config.ch_info.ch_map[4] = PAL_CHMAP_CHANNEL_LB;
    attr.out_media_config.ch_info.ch_map[5] = PAL_CHMAP_CHANNEL_RB;
    attr.out_media_config.ch_info.ch_map[6] = PAL_CHMAP_CHANNEL_LS;
    attr.out_media_config.ch_info.ch_map[7] = PAL_CHMAP_CHANNEL_RS;

    memset(&dev, 0, sizeof(dev));
    dev.id = PAL_DEVICE_OUT_SPEAKER;
    dev.config = attr.out_media_config;
    snprintf(dev.custom_config.custom_key, PAL_MAX_CUSTOM_KEY_SIZE, "%s", custom_key);

    rc = p_pal_init();
    pal_init_owned = (rc == 0);
    fprintf(stderr, "pal_init=%d\n", rc);
    if (rc != 0 && rc != -EALREADY) return 1;
    rc = p_pal_stream_open(&attr, 1, &dev, 0, NULL, cb, 0, &stream);
    fprintf(stderr, "pal_stream_open=%d handle=%p\n", rc, (void *)stream);
    if (rc) {
        if (pal_init_owned) p_pal_deinit();
        return 2;
    }
    rc = p_pal_stream_start(stream);
    fprintf(stderr, "pal_stream_start=%d\n", rc);
    if (rc) {
        p_pal_stream_close(stream);
        if (pal_init_owned) p_pal_deinit();
        return 3;
    }

    samples = calloc((size_t)frames * channels, sizeof(*samples));
    memset(&buffer, 0, sizeof(buffer));
    buffer.buffer = (uint8_t *)samples;
    buffer.size = (size_t)frames * channels * sizeof(*samples);
    rc = (int32_t)p_pal_stream_write(stream, &buffer);
    fprintf(stderr, "pal_stream_write=%d requested=%zu\n", rc, buffer.size);

    free(samples);
    p_pal_stream_stop(stream);
    p_pal_stream_close(stream);
    if (pal_init_owned) p_pal_deinit();
    dlclose(lib);
    return rc < 0 ? 4 : 0;
}
