#ifndef UNMMS_H_INCLUDED
#define UNMMS_H_INCLUDED


#define SAFE_FREE(X) { if ((X)) { free((X)); (X) = NULL; } }

enum task_enum {
    TASK_NONE = 0,
    TASK_LIST,
    TASK_EXTRACT_STDOUT,
    TASK_EXTRACT_FILE,
};

typedef enum task_enum task_t;


static void options       (  char *prog  );
void        arguments     ( int argc, char **argv );
void        unmms_list    ( char *archive, char *rar_name);
void        unmms_extract ( char *archive_name, char *rar_name,
                            char *extract_name, FILE * );


#endif
