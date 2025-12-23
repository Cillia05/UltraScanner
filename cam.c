#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // -o : output file
    // -t 200 : wait 200 ms (camera settle)
    // --width/--height : set resolution (tweak as needed)
    const char *cmd =
        "rpicam-still -t 200 --width 2592 --height 1944 -o shot.jpg";
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Failed to run libcamera-still (ret=%d)\n", ret);
        fprintf(stderr, "Try: check camera connection, enable camera interface, and run: libcamera-still -o test.jpg\n");
        return 1;
    }

    printf("Saved: shot.jpg\n");
    return 0;
}
