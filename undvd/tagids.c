#include <stdio.h>
#include <fcntl.h>
#include <inttypes.h>


char *TagIDStr(int id)
{
    switch (id) {
    case 1:
        return "   1 TAGID_PRI_VOL";
    case 2:
        return "   2 TAGID_ANCHOR";
    case 3:
        return "   3 TAGID_VOL";
    case 4:
        return "   4 TAGID_IMP_VOL";
    case 5:
        return "   5 TAGID_PARTITION";
    case 6:
        return "   6 TAGID_LOGVOL";
    case 7:
        return "   7 TAGID_UNALLOC_SPACE";
    case 8:
        return "   8 TAGID_TERM";
    case 9:
        return "   9 TAGID_LOGVOL_INTEGRITY";
    case 256:
        return " 256 TAGID_FSD";
    case 257:
        return " 257 TAGID_FID";
    case 258:
        return " 258 TAGID_ALLOCEXTENT";
    case 259:
        return " 259 TAGID_INDIRECTENTRY";
    case 260:
        return " 260 TAGID_ICB_TERM";
    case 261:
        return " 261 TAGID_FENTRY";
    case 262:
        return " 262 TAGID_EXTATTR_HDR";
    case 263:
        return " 263 TAGID_UNALL_SP_ENTRY";
    case 264:
        return " 264 TAGID_SPACE_BITMAP";
    case 265:
        return " 265 TAGID_PART_INTEGRETY";
    case 266:
        return " 266 TAGID_EXTFENTRY";
    }

    return NULL;
}

#define GETN2(p) ((uint16_t)buffer[p] | ((uint16_t)buffer[(p) + 1] << 8))

int main(int argc, char **argv)
{
    int i;
    int fd;
    char buffer[2048], *str;
    uint16_t TagID;

    if (argc != 2) exit(2);

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) exit(3);

    for (i = 0; i < 20000; i++) {
        if (read(fd, buffer, 2048) != 2048) break;

        TagID = GETN2(0);

        str = TagIDStr(TagID);
        if (str) printf("Block %3d TagID: %s\n", i, str);

    }
    close(fd);
}
