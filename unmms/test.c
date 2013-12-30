#include <stdio.h>
#include <libmms/mms.h>

int main(int argc, char **argv)
{
    mms_t *mms;
    unsigned char buffer[1024];
    FILE *out;
    unsigned int red, total;

    printf("Test\n");


    out = fopen("stream.out", "w");
    if (!out) {
        perror("fopen: ");
        exit(2);
    }


    mms = mms_connect(NULL, NULL, "mms://wm-live.sr.se/SR-P1-High", 128 * 1024);

    if (!mms) {
        printf("Unable to connect to URL, stop.\n");
        exit(0);
    }

    while((red = mms_read(NULL, mms, buffer, sizeof(buffer))) > 0) {

        printf("red %d bytes\n", red);
        fwrite(buffer, red, 1, out);

        total += red;

        if (total > 1024 * 1024) break;

    }

    fclose(out);
    mms_close(mms);

    return (0);
}
