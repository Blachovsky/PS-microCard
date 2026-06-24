#ifndef MICROSD_H
#define MICROSD_H

#include <stdbool.h>

bool load_card_image_from_sd(const char *path);
bool save_card_image_to_sd(const char *path);

#endif // MICROSD_H
