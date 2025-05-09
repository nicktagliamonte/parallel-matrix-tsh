/*.........................................................................*/
/*                  TSHLIB.H ------> TSH library API                        */
/*                                                                          */
/*.........................................................................*/

#ifndef TSHLIB_H
#define TSHLIB_H

#include "synergy.h"

/* Connection handle for TSH operations */
typedef struct {
    int sock;            /* Socket connection to TSH server */
    unsigned short port; /* Port number of TSH server */
} TSH_CONN;

/* Shell operation code */
#define TSH_OP_SHELL 0x0005

/* Shell-specific constants */
#define MAX_STDOUT 4096

/* Shell operation structures */
typedef struct {
    unsigned long length;        /* Length of command */
    char padding[TUPLENAME_LEN]; /* Padding to maintain structure alignment */
} tsh_shell_it;

typedef struct {
    short int status;            /* Status of operation */
    short int error;             /* Error code if any */
    char username[64];           /* Username for prompt */
    char cwd_loc[256];           /* Current working directory for prompt */
    char out_buffer[MAX_STDOUT]; /* Command output buffer */
} tsh_shell_ot;

/* Function prototypes */
/* Initialize connection to TSH server */
TSH_CONN* tsh_connect(unsigned short port);

/* Close connection to TSH server */
int tsh_disconnect(TSH_CONN* conn);

/* Put a tuple into the tuple space */
int tsh_put(TSH_CONN* conn, const char* name, unsigned short priority, 
            const void* tuple, unsigned long length);

/* Get a tuple from the tuple space (API version, returns tuple data in outbuf, length in outlen) */
int tsh_get(TSH_CONN* conn, const char* expr, char* outbuf, unsigned long* outlen);

/* Read a tuple from the tuple space (API version, returns tuple data in outbuf, length in outlen) */
int tsh_read(TSH_CONN* conn, const char* expr, char* outbuf, unsigned long* outlen);

/* Execute a shell command through TSH server */
int tsh_shell(TSH_CONN* conn, char* command, char* output, char* username, char* cwd);

/* Helper function - internal use only */
int tsh_send_op(TSH_CONN* conn, unsigned short op_code);

#endif /* TSHLIB_H */