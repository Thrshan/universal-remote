#include "host_support.h"
#include "app_state.h"
#include "nvs.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

uint32_t host_time;
int host_fail_save;
static uint8_t saved[32768], staged[32768], legacy[1024];
static size_t saved_size, staged_size, legacy_size;
struct host_queue { unsigned count, size, head, tail; uint8_t *data; };
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    struct host_queue *queue = calloc(1, sizeof(*queue));
    queue->count = count + 1;
    queue->size = size;
    queue->data = calloc(queue->count, size);
    return queue;
}
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout)
{
    (void)timeout;
    unsigned next = (queue->tail + 1) % queue->count;
    if (next == queue->head) return pdFALSE;
    memcpy(queue->data + queue->tail * queue->size, item, queue->size);
    queue->tail = next;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout)
{
    (void)timeout;
    if (queue->head == queue->tail) return pdFALSE;
    memcpy(item, queue->data + queue->head * queue->size, queue->size);
    queue->head = (queue->head + 1) % queue->count;
    return pdTRUE;
}
int64_t esp_timer_get_time(void) { return (int64_t)host_time * 1000; }
void esp_fill_random(void *buffer, size_t length)
{
    // Deterministic host-only entropy source; firmware uses ESP-IDF's RNG.
    static uint8_t sequence = 1;
    uint8_t *bytes = buffer;
    for (size_t i = 0; i < length; ++i) bytes[i] = sequence++;
}
const char *esp_err_to_name(int error) { (void)error; return "host error"; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, unsigned mode, nvs_handle_t *handle)
{
    *handle = strcmp(name, "ir") == 0 ? 2 : 1;
    if (mode == NVS_READONLY && !(*handle == 2 ? legacy_size : saved_size)) return ESP_ERR_NVS_NOT_FOUND;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *blob, size_t *size)
{
    (void)key;
    size_t length = handle == 2 ? legacy_size : saved_size;
    if (!length) return ESP_ERR_NVS_NOT_FOUND;
    if (!blob) { *size = length; return ESP_OK; }
    if (*size < length) return ESP_ERR_INVALID_SIZE;
    memcpy(blob, handle == 2 ? legacy : saved, length);
    *size = length;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *blob, size_t size)
{
    (void)handle; (void)key;
    if (host_fail_save == 1) return ESP_FAIL;
    assert(size <= sizeof(staged));
    memcpy(staged, blob, size);
    staged_size = size;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    if (host_fail_save == 2) return ESP_FAIL;
    memcpy(saved, staged, staged_size);
    saved_size = staged_size;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
void host_clear_storage(void) { saved_size = staged_size = legacy_size = 0; host_fail_save = 0; }
void host_corrupt_storage(void) { assert(saved_size); saved[20] ^= 1; }
void host_version_storage(const remote_t *remote, unsigned version)
{
    struct { uint32_t magic; uint16_t version, count; remote_t remotes[MAX_REMOTES]; uint32_t checksum; } blob = {0};
    blob.magic = 0x554E4952u; blob.version = version; blob.count = 1; blob.remotes[0] = *remote;
    const uint8_t *bytes = (const uint8_t *)&blob;
    blob.checksum = 2166136261u;
    for (size_t i = 0; i < sizeof(blob) - sizeof(blob.checksum); ++i)
        blob.checksum = (blob.checksum ^ bytes[i]) * 16777619u;
    memcpy(saved, &blob, sizeof(blob)); saved_size = sizeof(blob);
}
void host_v1_storage(const remote_t *remote) { host_version_storage(remote, 1); }
void host_legacy_storage(unsigned count)
{
    typedef struct { char name[8]; ir_nec_code_t code; } slot_t;
    struct { uint8_t count; slot_t slots[32]; } blob = {0};
    assert(count <= 32);
    blob.count = count;
    for (unsigned i = 0; i < count; ++i) blob.slots[i].code = (ir_nec_code_t){i, 100 + i};
    memcpy(legacy, &blob, sizeof(blob));
    legacy_size = sizeof(blob);
}
