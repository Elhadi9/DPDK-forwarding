#include <stdatomic.h>

typedef struct ring_buffer
{
    void **buffer;   // The buffer holding the data
    int size;        // Maximum number of elements in the buffer
    atomic_int head; // Points to the oldest data element
    atomic_int tail; // Points to the next insertion point
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t *ring, int size);

void ring_buffer_free(ring_buffer_t *ring);

int ring_buffer_push(ring_buffer_t *ring, void *data);

int ring_buffer_pop(ring_buffer_t *ring, void **data);
