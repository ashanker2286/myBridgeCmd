#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bridge.h>

int main(int argc, char **argv) {
        int retVal = 0;
        int vlanId = 0;
        char *ifName = NULL;
        int bridge = 0;

        if (argc != 4) {
                fprintf(stderr, "Invalid Argument\n");
                return -1;
        }

        ifName = argv[1];
        vlanId = atoi(argv[2]);
        bridge = atoi(argv[3]);

        if (bridge == 0) {
                retVal = setInterfacePvid(ifName, vlanId, 0);
                if (retVal < 0) {
                        printf("Error setting Interface pvid: %d\n", retVal);
                }
        } else {
                retVal = setInterfacePvid(ifName, vlanId, 1);
                if (retVal < 0) {
                        printf("Error setting Interface pvid: %d\n", retVal);
                }
        }
        return retVal;
}

