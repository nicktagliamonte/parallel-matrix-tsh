/*.........................................................................*/
/*                  TSHTEST.H ------> TSH test program                     */
/*                  February '13, Oct '18 updated by Justin Y. Shi         */
/*.........................................................................*/

#include "synergy.h"

/* Shell-specific constants */
#define MAX_STDOUT 4096
#define fixed_buff 64
#define SHELL_LINE_DELIM " \t\r\n\a"

/* Shell operation structures */
typedef struct
{
   unsigned long length;        /* Length of command */
   char padding[TUPLENAME_LEN]; /* Padding to maintain structure alignment */
} tsh_shell_it;

typedef struct
{
   short int status;            /* Status of operation */
   short int error;             /* Error code if any */
   char username[64];           /* Username for prompt */
   char cwd_loc[256];           /* Current working directory for prompt */
   char out_buffer[MAX_STDOUT]; /* Command output buffer */
} tsh_shell_ot;

char login[NAME_LEN];

void OpPut(/*void*/);
void OpGet(/*void*/);
void OpExit(/*void*/);
void OpShell(/*void*/); // Added shell operation
void OpRetrieve(/*void*/);

int tshsock;

char *read_input(size_t *length); // Added read_input function prototype

int connectTsh(unsigned short);
unsigned short drawMenu(/*void*/);