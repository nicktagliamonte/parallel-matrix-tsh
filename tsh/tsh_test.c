/*.........................................................................*/
/*                  TSH_TEST.C ------> TSH library test program             */
/*                                                                          */
/*.........................................................................*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tshlib.h"

int main(int argc, char **argv)
{
    TSH_CONN *conn;
    int result;
    char tuple_data[256];
    const char *tuple_name = "test_tuple";
    unsigned short priority = 10;

    if (argc < 2)
    {
        printf("Usage: %s port\n", argv[0]);
        exit(1);
    }

    /* Connect to TSH server */
    printf("Connecting to TSH server on port %d...\n", atoi(argv[1]));
    conn = tsh_connect(atoi(argv[1]));

    if (conn == NULL)
    {
        printf("Failed to connect to TSH server.\n");
        exit(1);
    }

    printf("Successfully connected to TSH server on port %d\n", atoi(argv[1]));

    /* Test PUT functionality */
    printf("\nTesting PUT operation...\n");

    /* Create a simple tuple with a message */
    strcpy(tuple_data, "Hello, this is a test tuple from the tshlib API!");
    unsigned long length = strlen(tuple_data) + 1; /* +1 for null terminator */

    printf("Putting tuple with name '%s', priority %d, length %lu\n",
           tuple_name, priority, length);
    printf("Tuple content: '%s'\n", tuple_data);

    result = tsh_put(conn, tuple_name, priority, tuple_data, length);

    if (result == 0)
    {
        printf("Successfully put tuple into tuple space.\n");
    }
    else
    {
        printf("Failed to put tuple into tuple space.\n");
    }

    /* Disconnect after PUT */
    printf("\nDisconnecting from TSH server after PUT...\n");
    tsh_disconnect(conn);
    printf("Disconnected successfully.\n");

    /* Reconnect for READ */
    printf("\nReconnecting to TSH server for READ operation...\n");
    conn = tsh_connect(atoi(argv[1]));
    if (conn == NULL)
    {
        printf("Failed to reconnect to TSH server for READ.\n");
        exit(1);
    }
    printf("Successfully reconnected to TSH server on port %d\n", atoi(argv[1]));

    char read_buf[1024];
    unsigned long read_len;
    if (tsh_read(conn, "test_tuple", read_buf, &read_len) == 0)
    {
        printf("Read tuple: %s\n", read_buf);
    }
    else
    {
        printf("Failed to read tuple\n");
    }

    /* Disconnect from TSH server after READ */
    printf("\nDisconnecting from TSH server after READ...\n");
    tsh_disconnect(conn);
    printf("Disconnected successfully.\n");

    /* Reconnect for GET */
    printf("\nReconnecting to TSH server for GET operation...\n");
    conn = tsh_connect(atoi(argv[1]));
    if (conn == NULL)
    {
        printf("Failed to reconnect to TSH server.\n");
        exit(1);
    }
    printf("Successfully reconnected to TSH server on port %d\n", atoi(argv[1]));

    char buf[1024];
    unsigned long len;
    if (tsh_get(conn, "test_tuple", buf, &len) == 0)
    {
        printf("Got tuple: %s\n", buf);
    }
    else
    {
        printf("Failed to get tuple\n");
    }

    /* Disconnect from TSH server after GET */
    printf("\nDisconnecting from TSH server after GET...\n");
    tsh_disconnect(conn);
    printf("Disconnected successfully.\n");

    // Test: double
    printf("\nTest: double\n");
    double test_double = 3.141592653589793;
    double double_out = 0.0;
    unsigned long double_len = sizeof(double);

    // PUT
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for double put)\n"); return 1; }
    if (tsh_put(conn, "test_double", 1, &test_double, sizeof(double)) != 0) {
        printf("FAIL (put)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);

    // READ
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for double read)\n"); return 1; }
    if (tsh_read(conn, "test_double", (char*)&double_out, &double_len) != 0) {
        printf("FAIL (read)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);
    if (double_len != sizeof(double) || double_out != test_double) {
        printf("FAIL (read value mismatch: got %.15f, expected %.15f)\n", double_out, test_double);
        return 1;
    }

    // GET
    double_out = 0.0; double_len = sizeof(double);
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for double get)\n"); return 1; }
    if (tsh_get(conn, "test_double", (char*)&double_out, &double_len) != 0) {
        printf("FAIL (get)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);
    if (double_len != sizeof(double) || double_out != test_double) {
        printf("FAIL (get value mismatch: got %.15f, expected %.15f)\n", double_out, test_double);
        return 1;
    }
    printf("PASS\n");

    // Test: double array
    printf("\nTest: double array\n");
    double test_array[5] = {1.1, 2.2, 3.3, 4.4, 5.5};
    double array_out[5] = {0};
    unsigned long array_len = sizeof(test_array);

    // PUT
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for array put)\n"); return 1; }
    if (tsh_put(conn, "test_double_array", 1, test_array, sizeof(test_array)) != 0) {
        printf("FAIL (put)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);

    // READ
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for array read)\n"); return 1; }
    if (tsh_read(conn, "test_double_array", (char*)array_out, &array_len) != 0) {
        printf("FAIL (read)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);
    if (array_len != sizeof(test_array)) {
        printf("FAIL (read length mismatch: got %lu, expected %lu)\n", array_len, sizeof(test_array));
        return 1;
    }
    for (int i = 0; i < 5; ++i) {
        if (array_out[i] != test_array[i]) {
            printf("FAIL (read value mismatch at %d: got %.15f, expected %.15f)\n", i, array_out[i], test_array[i]);
            return 1;
        }
    }

    // GET
    memset(array_out, 0, sizeof(array_out));
    array_len = sizeof(test_array);
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for array get)\n"); return 1; }
    if (tsh_get(conn, "test_double_array", (char*)array_out, &array_len) != 0) {
        printf("FAIL (get)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);
    if (array_len != sizeof(test_array)) {
        printf("FAIL (get length mismatch: got %lu, expected %lu)\n", array_len, sizeof(test_array));
        return 1;
    }
    for (int i = 0; i < 5; ++i) {
        if (array_out[i] != test_array[i]) {
            printf("FAIL (get value mismatch at %d: got %.15f, expected %.15f)\n", i, array_out[i], test_array[i]);
            return 1;
        }
    }
    printf("PASS\n");

    // Test: matrix (2D array)
    printf("\nTest: matrix (2D array)\n");
    int m_rows = 2, m_cols = 3;
    double test_matrix[2][3] = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
    double matrix_out[2][3] = {{0}};
    unsigned long matrix_len = sizeof(test_matrix);

    // PUT
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for matrix put)\n"); return 1; }
    if (tsh_put(conn, "test_matrix", 1, test_matrix, sizeof(test_matrix)) != 0) {
        printf("FAIL (put)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);

    // READ
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for matrix read)\n"); return 1; }
    if (tsh_read(conn, "test_matrix", (char*)matrix_out, &matrix_len) != 0) {
        printf("FAIL (read)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);
    if (matrix_len != sizeof(test_matrix)) {
        printf("FAIL (read length mismatch: got %lu, expected %lu)\n", matrix_len, sizeof(test_matrix));
        return 1;
    }
    for (int i = 0; i < m_rows; ++i) {
        for (int j = 0; j < m_cols; ++j) {
            if (matrix_out[i][j] != test_matrix[i][j]) {
                printf("FAIL (read value mismatch at [%d][%d]: got %.15f, expected %.15f)\n", i, j, matrix_out[i][j], test_matrix[i][j]);
                return 1;
            }
        }
    }

    // GET
    memset(matrix_out, 0, sizeof(matrix_out));
    matrix_len = sizeof(test_matrix);
    conn = tsh_connect(atoi(argv[1]));
    if (!conn) { printf("FAIL (connect for matrix get)\n"); return 1; }
    if (tsh_get(conn, "test_matrix", (char*)matrix_out, &matrix_len) != 0) {
        printf("FAIL (get)\n"); tsh_disconnect(conn); return 1;
    }
    tsh_disconnect(conn);
    if (matrix_len != sizeof(test_matrix)) {
        printf("FAIL (get length mismatch: got %lu, expected %lu)\n", matrix_len, sizeof(test_matrix));
        return 1;
    }
    for (int i = 0; i < m_rows; ++i) {
        for (int j = 0; j < m_cols; ++j) {
            if (matrix_out[i][j] != test_matrix[i][j]) {
                printf("FAIL (get value mismatch at [%d][%d]: got %.15f, expected %.15f)\n", i, j, matrix_out[i][j], test_matrix[i][j]);
                return 1;
            }
        }
    }
    printf("PASS\n");

    return 0;
}