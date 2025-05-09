/*.........................................................................*/
/*                  TSHLIB.C ------> TSH library API                        */
/*                                                                          */
/*                  Based on the TSH test program                           */
/*.........................................................................*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "tshlib.h"

/*---------------------------------------------------------------------------
  Function    : tsh_connect
  Parameters  : port - port number of the TSH server
  Returns     : Pointer to TSH_CONN structure or NULL on failure
  Description : Establishes a connection to the TSH server
---------------------------------------------------------------------------*/
TSH_CONN *tsh_connect(unsigned short port)
{
    TSH_CONN *conn = NULL;
    int sock;
    struct hostent *host;
    short tsh_port;
    unsigned long tsh_host;

    /* Allocate connection structure */
    conn = (TSH_CONN *)malloc(sizeof(TSH_CONN));
    if (conn == NULL)
    {
        perror("tsh_connect: Failed to allocate connection handle");
        return NULL;
    }

    /* Use localhost for server address */
    tsh_host = inet_addr("127.0.0.1");
    tsh_port = htons(port);

    /* Create socket and connect to TSH */
    if ((sock = get_socket()) == -1)
    {
        perror("tsh_connect: Failed to get socket");
        free(conn);
        return NULL;
    }

    if (!do_connect(sock, tsh_host, tsh_port))
    {
        perror("tsh_connect: Failed to connect to TSH server");
        close(sock);
        free(conn);
        return NULL;
    }

    /* Store connection information */
    conn->sock = sock;
    conn->port = port;

    return conn;
}

/*---------------------------------------------------------------------------
  Function    : tsh_disconnect
  Parameters  : conn - pointer to TSH connection handle
  Returns     : 0 on success, -1 on failure
  Description : Closes the connection to the TSH server
---------------------------------------------------------------------------*/
int tsh_disconnect(TSH_CONN *conn)
{
    if (conn == NULL)
    {
        fprintf(stderr, "tsh_disconnect: NULL connection handle\n");
        return -1;
    }

    /* Close the socket */
    close(conn->sock);

    /* Free the connection structure */
    free(conn);

    return 0;
}

/*---------------------------------------------------------------------------
  Function    : tsh_put
  Parameters  : conn - pointer to TSH connection handle
                name - name of the tuple to store
                priority - priority of the tuple (higher value = higher priority)
                tuple - pointer to the tuple data
                length - length of the tuple data in bytes
  Returns     : 0 on success, -1 on failure
  Description : Puts a tuple into the tuple space
---------------------------------------------------------------------------*/
int tsh_put(TSH_CONN *conn, const char *name, unsigned short priority,
            const void *tuple, unsigned long length)
{
    tsh_put_it out;
    tsh_put_ot in;

    if (conn == NULL || name == NULL || tuple == NULL)
    {
        fprintf(stderr, "tsh_put: Invalid parameters\n");
        return -1;
    }

    /* Send PUT operation code to TSH */
    if (tsh_send_op(conn, TSH_OP_PUT) != 0)
    {
        return -1;
    }

    /* Initialize the PUT input structure */
    memset(&out, 0, sizeof(out));
    strncpy(out.name, name, TUPLENAME_LEN - 1);
    out.name[TUPLENAME_LEN - 1] = '\0'; /* ensure null-termination */
    out.priority = htons(priority);     /* convert to network byte order */
    out.length = htonl(length);         /* convert to network byte order */
    out.host = inet_addr("127.0.0.1");  /* localhost for now */
    out.proc_id = htonl(getpid());      /* process ID */

    /* Send tuple metadata to TSH server */
    if (!writen(conn->sock, (char *)&out, sizeof(out)))
    {
        perror("tsh_put: Failed to send tuple metadata");
        return -1;
    }

    /* Send the actual tuple data to TSH server */
    if (!writen(conn->sock, (char *)tuple, length))
    {
        perror("tsh_put: Failed to send tuple data");
        return -1;
    }

    /* Read response from TSH server */
    if (!readn(conn->sock, (char *)&in, sizeof(in)))
    {
        perror("tsh_put: Failed to read server response");
        return -1;
    }

    /* Check response status */
    if (ntohs(in.status) != SUCCESS)
    {
        fprintf(stderr, "tsh_put: Server reported failure, error code: %d\n",
                ntohs(in.error));
        return -1;
    }

    return 0;
}

/*---------------------------------------------------------------------------
  Function    : tsh_send_op
  Parameters  : conn - pointer to TSH connection handle
                op_code - operation code to send
  Returns     : 0 on success, -1 on failure
  Description : Sends an operation code to the TSH server
---------------------------------------------------------------------------*/
int tsh_send_op(TSH_CONN *conn, unsigned short op_code)
{
    unsigned short network_op;

    if (conn == NULL)
    {
        fprintf(stderr, "tsh_send_op: NULL connection handle\n");
        return -1;
    }

    /* Convert operation code to network byte order */
    network_op = htons(op_code);

    /* Send operation code to TSH */
    if (!writen(conn->sock, (char *)&network_op, sizeof(network_op)))
    {
        perror("tsh_send_op: Failed to send operation code");
        return -1;
    }

    return 0;
}

/*---------------------------------------------------------------------------
  Function    : tsh_get
  Parameters  : conn - pointer to TSH connection handle
                expr - expression to match the tuple
                outbuf - buffer to store the tuple data
                outlen - pointer to store the length of the tuple data
  Returns     : 0 on success, -1 on failure
  Description : Retrieves a tuple from the tuple space
---------------------------------------------------------------------------*/
int tsh_get(TSH_CONN *conn, const char *expr, char *outbuf, unsigned long *outlen)
{
    tsh_get_it out;
    tsh_get_ot1 in1;
    tsh_get_ot2 in2;

    memset(&out, 0, sizeof(out));
    strncpy(out.expr, expr, TUPLENAME_LEN - 1);
    out.proc_id = htonl(getpid());
    out.host = inet_addr("127.0.0.1");

    /* Send GET operation code */
    if (tsh_send_op(conn, TSH_OP_GET) != 0)
        return -1;

    /* Send GET request structure */
    if (!writen(conn->sock, (char *)&out, sizeof(out)))
        return -1;

    /* Read status */
    if (!readn(conn->sock, (char *)&in1, sizeof(in1)))
        return -1;

    if (ntohs(in1.status) != SUCCESS)
        return -1;

    /* Read tuple metadata */
    if (!readn(conn->sock, (char *)&in2, sizeof(in2)))
        return -1;

    unsigned long len = ntohl(in2.length);

    /* Read tuple data */
    if (!readn(conn->sock, outbuf, len))
        return -1;

    if (outlen)
        *outlen = len;

    return 0;
}

/*---------------------------------------------------------------------------
  Function    : tsh_read
  Parameters  : conn - pointer to TSH connection handle
                expr - expression to match the tuple
                outbuf - buffer to store the tuple data
                outlen - pointer to store the length of the tuple data
  Returns     : 0 on success, -1 on failure
  Description : Reads a tuple from the tuple space without removing it
---------------------------------------------------------------------------*/
int tsh_read(TSH_CONN *conn, const char *expr, char *outbuf, unsigned long *outlen)
{
    tsh_get_it out;
    tsh_get_ot1 in1;
    tsh_get_ot2 in2;

    memset(&out, 0, sizeof(out));
    strncpy(out.expr, expr, TUPLENAME_LEN - 1);
    out.proc_id = htonl(getpid());
    out.host = inet_addr("127.0.0.1");

    /* Send READ operation code (TSH_OP_READ = 403) */
    if (tsh_send_op(conn, TSH_OP_READ) != 0)
        return -1;

    /* Send READ request structure */
    if (!writen(conn->sock, (char *)&out, sizeof(out)))
        return -1;

    /* Read status */
    if (!readn(conn->sock, (char *)&in1, sizeof(in1)))
        return -1;

    if (ntohs(in1.status) != SUCCESS)
        return -1;

    /* Read tuple metadata */
    if (!readn(conn->sock, (char *)&in2, sizeof(in2)))
        return -1;

    unsigned long len = ntohl(in2.length);

    /* Read tuple data */
    if (!readn(conn->sock, outbuf, len))
        return -1;

    if (outlen)
        *outlen = len;

    return 0;
}

/*---------------------------------------------------------------------------
  Function    : tsh_shell
  Parameters  : conn - pointer to TSH connection handle
                command - shell command to execute
                output - buffer to store the command output
                username - buffer to store the username
                cwd - buffer to store the current working directory
  Returns     : 0 on success, -1 on failure
  Description : Executes a shell command through TSH
---------------------------------------------------------------------------*/
int tsh_shell(TSH_CONN *conn, char *command, char *output, char *username, char *cwd)
{
    if (conn == NULL || command == NULL) {
        return -1; // Invalid parameters
    }

    tsh_shell_it out;
    tsh_shell_ot in;
    unsigned short op = TSH_OP_SHELL;
    int result;

    /* Send operation code */
    if (tsh_send_op(conn, TSH_OP_SHELL) != 0) {
        return -1;
    }

    /* Prepare shell command parameters */
    out.length = htonl(strlen(command) + 1);

    /* Send command structure */
    if (!writen(conn->sock, (char *)&out, sizeof(out))) {
        return -1;
    }

    /* Send command string */
    if (!writen(conn->sock, command, ntohl(out.length))) {
        return -1;
    }

    /* Read response */
    if (!readn(conn->sock, (char *)&in, sizeof(in))) {
        return -1;
    }

    /* Copy results to provided buffers if not NULL */
    if (output != NULL) {
        strcpy(output, in.out_buffer);
    }

    if (username != NULL) {
        strcpy(username, in.username);
    }

    if (cwd != NULL) {
        strcpy(cwd, in.cwd_loc);
    }

    result = ntohs(in.status);
    return result;
}