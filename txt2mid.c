#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>

#define write16(fd, n)  write((fd), (uint8_t []) {(n) >> 8, (n) & 0xFF}, 2)
#define write32(fd, n)  do { \
                            write16((fd), (n) >> 16); \
                            write16((fd), (n) & 0xFFFF); \
                        } while (0)

#define TPQ         240
#define WORD_MAX    64


typedef struct Event {
    uint32_t offset;
    uint8_t note, value;
} Event;

typedef struct Queue {
    unsigned bulk, count;
    Event events[];
} Queue;

size_t
write_vlv(int fd, uint32_t n)
{
    uint8_t bytes[4] = {0x80, 0x80, 0x80, 0x00};
    int i;
    size_t len;

    for (i = 3; i >= 0; i--) {
        bytes[i] |= n & 0x7F;
        if (n < 0x80)
            break;
        n >>= 7;
    }
    len = 4 - i;
    write(fd, &bytes[i], len);
    return len;
}

size_t
read_word(int fd, char *buffer, size_t n)
{
    size_t i;

    do {
        if (read(fd, buffer, 1) == 0)
            return 0;
    } while (isspace(*buffer));
    for (i = 1; i < n; i++) {
        if (read(fd, &buffer[i], 1) == 0)
            break;
        if (isspace(buffer[i]))
            break;
    }
    buffer[i] = 0;
    return i;
}

Queue *
new_queue(unsigned init_bulk)
{
    Queue *queue;

    queue = malloc(sizeof(*queue) + init_bulk * sizeof(queue->events[0]));
    if (queue) {
        queue->bulk = init_bulk;
        queue->count = 0;
    }
    return queue;
}

void
add_event(Queue **pq, Event *ev)
{
    Queue *q = *pq;
    if (q->count == q->bulk) {
        q->bulk += q->bulk / 2;
        q = realloc(q, sizeof(*q) + q->bulk * sizeof(q->events[0]));
        *pq = q;
    }
    memcpy(&q->events[q->count++], ev, sizeof(*ev));
}

int
cmp_ev(const void *a, const void *b)
{
    return ((Event *) a)->offset - ((Event *) b)->offset;
}

void
sort_queue(Queue *q)
{
    qsort(q->events, q->count, sizeof(q->events[0]), cmp_ev);
}

void
save_track(int fd, Queue *q)
{
    unsigned i;
    off_t len_off;
    uint32_t length;
    uint32_t offset;

    write(fd, "MTrk", 4);
    len_off = lseek(fd, 0, SEEK_CUR);
    write(fd, (uint8_t []) {0x00, 0x00, 0x00, 0x00}, 4);
    length = 4;
    offset = 0;
    for (i = 0; i < q->count; i++) {
        Event *ev = &q->events[i];
        length += write_vlv(fd, ev->offset - offset);
        offset = ev->offset;
        length += write(fd, (uint8_t []) {0x90, ev->note, ev->value}, 3);
    }
    write(fd, (uint8_t []) {0x00, 0xFF, 0x2F, 0x00}, 4);
    lseek(fd, len_off, SEEK_SET);
    write32(fd, length);
}

int
main()
{
    char word[WORD_MAX];
    size_t n;
    uint32_t offset, endoff, duration, percent;
    uint8_t note;
    char *c;
    Queue *q;
    int mid;

    mid = 1;
    write(mid, "MThd", 4);
    write32(mid, 6);
    write16(mid, 1);
    write16(mid, 1); /* # of tracks */
    write16(mid, TPQ); /* ticks per quarter-note */

    offset = 0;
    duration = 4;
    percent = 95;
    q = new_queue(0x10);
    while (1) {
        n = read_word(0, word, WORD_MAX);
        if (n == 0) break;
        c = strchr(word, '%');
        if (c != NULL) {
            percent = atoi(&c[1]);
            *c = 0;
        }
        c = strchr(word, ':');
        if (c != NULL) {
            duration = 4 * TPQ / atoi(&c[1]);
            *c = 0;
        }
        endoff = offset+duration*percent/100;
        c = strtok(word, ",");
        while (c != NULL) {
            note = atoi(c);
            add_event(&q, &((Event) {offset, note, 127}));
            add_event(&q, &((Event) {endoff, note, 0}));
            c = strtok(NULL, ",");
        }
        offset += duration;
    }
    sort_queue(q);
    save_track(mid, q);
    free(q);
    close(mid);
    return 0;
}
