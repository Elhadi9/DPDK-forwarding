#include "ring_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

void ring_buffer_init(ring_buffer_t *ring, int size)
{
    ring->buffer = (void *)malloc(sizeof(void *) * size);
    ring->size = size;
    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
}

void ring_buffer_free(ring_buffer_t *ring)
{
    free(ring->buffer);
    ring->buffer = NULL;
    ring->size = 0;
    atomic_store(&ring->head, 0);
    atomic_store(&ring->tail, 0);
}

int ring_buffer_push(ring_buffer_t *ring, void *data)
{
    int current_tail = atomic_load(&ring->tail);
    int next_tail = (current_tail + 1) % ring->size;
    if (next_tail == atomic_load(&ring->head))
    {
        // Buffer is full
        return -1;
    }
    ring->buffer[current_tail] = data;
    atomic_store(&ring->tail, next_tail);
    return 0;
}

int ring_buffer_pop(ring_buffer_t *ring, void **data)
{
    int current_head = atomic_load(&ring->head);
    if (current_head == atomic_load(&ring->tail))
    {
        // Buffer is empty
        return -1;
    }
    *data = ring->buffer[current_head];
    atomic_store(&ring->head, (current_head + 1) % ring->size);
    return 0;
}
