#include "light.h"

int main(void) {
	if (init_lights() != 0) return 1;

    while(next_light() == 0) {
    	usleep(30000);
    }
/*
    int x = 0, dir = 1;
    while (1) {
        fb_clear();
        fb_set_pixel(x, 3, 1);
        if (fb_flush() < 0) break;

        x += dir;
        if (x <= 0) { x = 0; dir = 1; }
        if (x >= WIDTH - 1) { x = WIDTH - 1; dir = -1; }

        usleep(30000);
    }
*/
    finish_lights();
    return 0;
}
