#ifndef UNDVD_H_INCLUDED
#define UNDVD_H_INCLUDED

#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf

#define SAFE_FREE(X) { if ((X)) { free((X)); (X) = NULL; } }

enum task_enum {
    TASK_NONE = 0,
    TASK_LIST,
    TASK_EXTRACT_STDOUT,
    TASK_EXTRACT_FILE,
};

typedef enum task_enum task_t;

extern      char     *rar_exe;


static void options       (  char *prog  );
void        arguments     ( int argc, char **argv );
void        undvd_list    ( char *archive, char *rar_name);
void        undvd_extract ( char *archive_name, char *rar_name,
                            char *extract_name, FILE * );

void        dvd_list_recurse(dvd_reader_t *dvdread, char *directory);

#endif
