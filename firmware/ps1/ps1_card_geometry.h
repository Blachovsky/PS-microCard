#ifndef PS1_CARD_GEOMETRY_H
#define PS1_CARD_GEOMETRY_H

/* PS1 memory-card geometry shared by protocol, storage, and host tests. */
#define PS1_FRAME_SIZE  128
#define PS1_FRAME_COUNT 1024
#define PS1_CARD_SIZE   (PS1_FRAME_COUNT * PS1_FRAME_SIZE)

#endif // PS1_CARD_GEOMETRY_H
