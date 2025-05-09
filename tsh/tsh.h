/*.........................................................................*/
/*                     TSH.H ------> Tuple Space Handler                   */
/*                     February '13, updated by Justin Y. Shi              */
/*.........................................................................*/

#include "synergy.h"

/* Shell-specific constants and variables */
#define MAX_STDOUT 4096
#define fixed_buff 64
#define SHELL_LINE_DELIM " \t\r\n\a"
int filedes[2]; /* Pipe for shell output capture */
int background = 0;
char MyShell_output[MAX_STDOUT];

/*  Tuples data structure.  */

struct t_space1
{
   char name[TUPLENAME_LEN]; /* tuple name */
   char *tuple;              /* pointer to tuple */
   unsigned short priority;  /* priority of the tuple */
   unsigned long length;     /* length of tuple */
   struct t_space1 *next;
   struct t_space1 *prev;
};
typedef struct t_space1 space1_t;

/*  Backup tuple list. FSUN 09/94 */
/*  host1(tp) -> host2(tp) -> ... */
struct t_space2
{
   char name[TUPLENAME_LEN];
   char *tuple;
   unsigned short priority;
   unsigned long length;
   unsigned long host;
   unsigned short port;
   unsigned short cidport; /* for dspace. ys'96 */
   int proc_id;
   int fault;
   struct t_space2 *next;
};
typedef struct t_space2 space2_t;

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
   char out_buffer[MAX_STDOUT]; /* Command output buffer (4096 bytes) */
} tsh_shell_ot;

/*  Pending requests data structure.  */

struct t_queue
{
   char expr[TUPLENAME_LEN]; /* tuple name */
   unsigned long host;       /* host from where the request came */
   unsigned short port;      /* corresponding port # */
   unsigned short cidport;   /* for dspace. ys'96 */
   int proc_id;              /* FSUN 10/94. For FDD */
   unsigned short request;   /* read/get */
   struct t_queue *next;
   struct t_queue *prev;
};
typedef struct t_queue queue1_t;

/*  Tuple space data structure.  */

struct
{
   char appid[NAME_LEN]; /* application id */
   char name[NAME_LEN];  /* name of the tuple space */
   unsigned short port;  /* port where it receives commands */

   space1_t *space;    /* list of tuples */
   space2_t *retrieve; /* list of tuples propobly retrieved. FSUN 09/94 */
   queue1_t *queue_hd; /* queue of waiting requests */
   queue1_t *queue_tl; /* new requests added at the end */
} tsh;

queue1_t *tid_q;
int oldsock;            /* socket on which requests are accepted */
int newsock;            /* new socket identifying a connection */
unsigned short this_op; /* the current operation that is serviced */
char mapid[MAP_LEN];
int EOT = 0; /* End of task tuples mark */
int TIDS = 0;
int total_fetched = 0;

/*  Prototypes.  */

void OpPut(/*void*/);
void OpGet(/*void*/);
void OpExit(/*void*/);
void OpShell(/*void*/); /* Added shell operation */

int initCommon(unsigned short);
void start(/*void*/);
space1_t *createTuple(char *, char *, unsigned long, unsigned short);
int consumeTuple(space1_t *);
short int storeTuple(space1_t *, int);
space1_t *findTuple(char *);
void deleteTuple(space1_t *, tsh_get_it *);
int storeRequest(tsh_get_it);
int sendTuple(queue1_t *, space1_t *);
void deleteSpace(/*void*/);
void deleteQueue(/*void*/);
queue1_t *findRequest(char *);
void deleteRequest(queue1_t *);
void sigtermHandler(/*void*/);
int getTshport(unsigned short);
int match(char *, char *);
int guardf(unsigned long, int);

/* Shell function prototypes */
char** tokenize_input(char* line, const char* delimiters);
void parse_input(char *input, char ***args, int *arg_count);
void handle_pipes_and_redirection(char **args);
void prepare_output_redirection(char **args, char *file_name);
void execute_with_output_redirection(char **args, char *file_name);
void execute_command(char **args);
int shell_launch(char **args);
char *builtin_str[3];
int (*builtin_func[])(char **);
int shell_num_builtins();
int shell_cd(char **args);
int shell_help(char **args);
int shell_exit(char **args);