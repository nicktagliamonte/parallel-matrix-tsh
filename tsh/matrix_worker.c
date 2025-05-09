#include <stdio.h>
#include <stdlib.h>
#include <string.h> // For memcpy
#include "tshlib.h"
#include <unistd.h>
#include <time.h>
#include <signal.h>

// Add alarm signal handling
volatile sig_atomic_t worker_timeout = 0;

void handle_worker_alarm(int sig) {
    worker_timeout = 1;
}

// Read matrix B from file instead of tuple space
int read_matrix_b_from_file(const char *filename, int *rows_out, int *cols_out, double **matrix_out)
{
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return -1;
    }
    
    // Read dimensions
    int rows, cols;
    if (fread(&rows, sizeof(int), 1, file) != 1) {
        fclose(file);
        return -1;
    }
    
    if (fread(&cols, sizeof(int), 1, file) != 1) {
        fclose(file);
        return -1;
    }
    
    // Allocate memory for the matrix
    double *matrix = malloc(rows * cols * sizeof(double));
    if (!matrix) {
        fclose(file);
        return -1;
    }
    
    // Read the entire matrix at once
    if (fread(matrix, sizeof(double), rows * cols, file) != rows * cols) {
        free(matrix);
        fclose(file);
        return -1;
    }
    
    fclose(file);
    
    *rows_out = rows;
    *cols_out = cols;
    *matrix_out = matrix;
    return 0;
}

// Get a matrix row from the tuple space
int get_matrix_row(TSH_CONN *conn, const char *prefix, int row_idx, double **row_out, int *cols_out, unsigned short port)
{
    char tuple_name[64];
    unsigned long len = sizeof(double) * 8192; // Support for large matrices
    double *row_data = malloc(len);
    
    if (!row_data) {
        return -1;
    }
    
    // Create a new connection specifically for this read operation
    TSH_CONN *read_conn = tsh_connect(port);
    if (!read_conn) {
        free(row_data);
        return -1;
    }
    
    snprintf(tuple_name, sizeof(tuple_name), "%s_row_%d", prefix, row_idx);
    
    if (tsh_read(read_conn, tuple_name, (char *)row_data, &len) != 0) {
        free(row_data);
        tsh_disconnect(read_conn);
        return -1;
    }
    
    *row_out = row_data;
    *cols_out = len / sizeof(double);
    
    // Disconnect after the read operation
    tsh_disconnect(read_conn);
    return 0;
}

// Get multiple matrix rows from the tuple space with a single connection
int get_matrix_rows(unsigned short port, const char *prefix, int start_row, int num_rows, double ***rows_out, int *cols_out)
{
    // Try to use a single connection for all rows
    TSH_CONN *conn = tsh_connect(port);
    if (!conn) {
        return -1;
    }
    
    double **rows = malloc(num_rows * sizeof(double*));
    if (!rows) {
        tsh_disconnect(conn);
        return -1;
    }
    
    int cols = 0;
    int success = 1;
    
    for (int i = 0; i < num_rows; i++) {
        int row_idx = start_row + i;
        char tuple_name[64];
        snprintf(tuple_name, sizeof(tuple_name), "%s_row_%d", prefix, row_idx);
        
        // Allocate space for this row
        unsigned long len = 8192 * sizeof(double); // Support for large matrices
        double *row_data = malloc(len);
        
        if (!row_data) {
            success = 0;
            // Clean up previously allocated rows
            for (int j = 0; j < i; j++) {
                free(rows[j]);
            }
            break;
        }
        
        // Try to read with our single connection
        if (tsh_read(conn, tuple_name, (char *)row_data, &len) != 0) {
            free(row_data);
            success = 0;
            // Clean up previously allocated rows
            for (int j = 0; j < i; j++) {
                free(rows[j]);
            }
            break;
        }
        
        rows[i] = row_data;
        
        // Set cols from the first row
        if (i == 0) {
            cols = len / sizeof(double);
        }
    }
    
    tsh_disconnect(conn);
    
    // If batched read failed, fall back to individual connections
    if (!success) {
        free(rows);
        return -1;
    }
    
    *rows_out = rows;
    *cols_out = cols;
    return 0;
}

int main(int argc, char **argv)
{
    // Record the worker's start time for enforcing maximum lifetime
    time_t start_time = time(NULL);
    int max_lifetime_seconds = 30; // Workers self-terminate after 30 seconds maximum
    
    // Set up alarm signal handling
    signal(SIGALRM, handle_worker_alarm);
    alarm(max_lifetime_seconds);
    
    if (argc < 4)
    {
        return 1;
    }
    
    unsigned short port = atoi(argv[1]);
    int max_rows = atoi(argv[2]);
    const char *matrix_b_file = argv[3];
    
    srand(time(NULL) ^ getpid());
    
    // Read all of matrix B from file at the start - each worker needs a full copy
    double *matrix_B = NULL;
    int rows_B = 0, cols_B = 0;
    if (read_matrix_b_from_file(matrix_b_file, &rows_B, &cols_B, &matrix_B) != 0) {
        return 1;
    }
    
    // First, read the total number of chunks to know when we're done
    int total_chunks = 0;
    {
        TSH_CONN *conn = tsh_connect(port);
        if (conn) {
            char chunk_count_tuple[64] = "total_chunks";
            unsigned long len = sizeof(total_chunks);
            
            if (tsh_read(conn, chunk_count_tuple, (char*)&total_chunks, &len) != 0) {
                // If we can't read the count, use a safe default based on matrix size
                total_chunks = (max_rows + 4) / 5; // Assume average granularity of 5
            }
            tsh_disconnect(conn);
        }
    }
    
    // Check if the "all_done" termination signal has been placed
    TSH_CONN *check_conn = tsh_connect(port);
    if (check_conn) {
        unsigned long len = sizeof(int);
        int dummy;
        char done_tuple[] = "all_work_complete";
        
        // If we can read the termination signal tuple, then all work is already done
        if (tsh_read(check_conn, done_tuple, (char*)&dummy, &len) == 0) {
            tsh_disconnect(check_conn);
            free(matrix_B);
            return 0; // Exit immediately, all work is done
        }
        tsh_disconnect(check_conn);
    }
    
    // Process loop
    int chunks_processed = 0;
    int max_chunk_index = total_chunks; // Use the known number of chunks
    
    // Pre-allocate result buffer once and reuse
    double *result_buffer = malloc(max_rows * sizeof(double));
    if (!result_buffer) {
        free(matrix_B);
        return 1;
    }
    
    int total_results = 0;
    int work_finished = 0;
    int consecutive_misses = 0;
    
    while (!work_finished) {
        // Check if alarm triggered for graceful termination
        if (worker_timeout) {
            // Try to report our progress before terminating
            TSH_CONN *report_conn = tsh_connect(port);
            if (report_conn) {
                char progress_tuple[64];
                snprintf(progress_tuple, sizeof(progress_tuple), "worker_progress_%d", getpid());
                int progress_data[] = {getpid(), chunks_processed, total_results};
                
                tsh_put(report_conn, progress_tuple, 1, progress_data, sizeof(progress_data));
                tsh_disconnect(report_conn);
            }
            
            work_finished = 1;
            break;
        }
        
        // Check if we've exceeded our maximum lifetime
        if (time(NULL) - start_time > max_lifetime_seconds) {
            work_finished = 1;
            break;
        }
        
        int claimed = 0;
        
        // Look for work chunks only within the valid range
        for (int chunk_idx = 0; chunk_idx < max_chunk_index && !claimed; chunk_idx++) {
            // Get a work chunk (one connection)
            TSH_CONN *conn = tsh_connect(port);
            if (!conn) {
                usleep(1000); // Short sleep on connection failure
                continue;
            }
            
            // Try to claim a work chunk
            char chunk_name[64];
            snprintf(chunk_name, sizeof(chunk_name), "work_chunk_%d", chunk_idx);
            
            unsigned long len = sizeof(int) * 2;
            int work_data[2]; // [start_row, num_rows]
            
            if (tsh_get(conn, chunk_name, (char*)work_data, &len) == 0) {
                claimed = 1;
                chunks_processed++;
                consecutive_misses = 0;
                
                int start_row = work_data[0];
                int num_rows = work_data[1];
                
                tsh_disconnect(conn); // Disconnect immediately after operation
                
                // Before processing, check if any rows in this chunk already have results
                int should_process_chunk = 0;
                
                // Check if results already exist for this chunk
                TSH_CONN *check_conn = tsh_connect(port);
                if (check_conn) {
                    int all_rows_exist = 1;
                    for (int row_offset = 0; row_offset < num_rows; row_offset++) {
                        int current_row = start_row + row_offset;
                        char result_name[64];
                        snprintf(result_name, sizeof(result_name), "C_row_%d", current_row);
                        
                        unsigned long check_len = max_rows * sizeof(double);
                        double check_buffer[1]; // We just need to see if it exists, don't need the full data
                        
                        if (tsh_read(check_conn, result_name, (char*)check_buffer, &check_len) != 0) {
                            // Row doesn't exist, we need to compute this chunk
                            all_rows_exist = 0;
                            should_process_chunk = 1;
                            break;
                        }
                    }
                    
                    tsh_disconnect(check_conn);
                    
                    if (all_rows_exist) {
                        continue; // Skip this chunk
                    }
                } else {
                    // If connection failed, process the chunk anyway
                    should_process_chunk = 1;
                }
                
                // If we've determined we should process the chunk
                if (should_process_chunk) {
                    // Process multiple rows in this chunk
                    for (int row_offset = 0; row_offset < num_rows; row_offset++) {
                        int current_row = start_row + row_offset;
                        
                        // Check one more time if this specific row already exists
                        TSH_CONN *row_check = tsh_connect(port);
                        if (row_check) {
                            char result_name[64];
                            snprintf(result_name, sizeof(result_name), "C_row_%d", current_row);
                            
                            unsigned long check_len = max_rows * sizeof(double);
                            double check_buffer[1];
                            
                            if (tsh_read(row_check, result_name, (char*)check_buffer, &check_len) == 0) {
                                // This row already has a result, skip it
                                tsh_disconnect(row_check);
                                continue;
                            }
                            tsh_disconnect(row_check);
                        }
                        
                        // Read this row from matrix A (one connection)
                        double *row_A = NULL;
                        int cols_A = 0;
                        
                        TSH_CONN *row_conn = tsh_connect(port);
                        if (!row_conn) continue;
                        
                        char tuple_name[64];
                        snprintf(tuple_name, sizeof(tuple_name), "A_row_%d", current_row);
                        
                        unsigned long row_len = sizeof(double) * max_rows;
                        double *row_data = malloc(row_len);
                        
                        if (!row_data) {
                            tsh_disconnect(row_conn);
                            continue;
                        }
                        
                        if (tsh_read(row_conn, tuple_name, (char*)row_data, &row_len) != 0) {
                            free(row_data);
                            tsh_disconnect(row_conn);
                            continue;
                        }
                        
                        tsh_disconnect(row_conn); // Disconnect immediately after operation
                        
                        row_A = row_data;
                        cols_A = row_len / sizeof(double);
                        
                        // Check for timeout again before heavy computation
                        if (worker_timeout) {
                            free(row_A);
                            break;
                        }
                        
                        // Clear result buffer
                        memset(result_buffer, 0, max_rows * sizeof(double));
                        
                        // Matrix multiplication (kij order for better cache performance)
                        for (int k = 0; k < cols_A; k++) {
                            double a_val = row_A[k]; // Cache this value
                            for (int j = 0; j < max_rows; j++) {
                                result_buffer[j] += a_val * matrix_B[k * cols_B + j];
                            }
                        }
                        
                        // Store result row (one connection)
                        TSH_CONN *result_conn = tsh_connect(port);
                        if (result_conn) {
                            char result_name[64];
                            snprintf(result_name, sizeof(result_name), "C_row_%d", current_row);
                            tsh_put(result_conn, result_name, 1, result_buffer, max_rows * sizeof(double));
                            total_results++;
                            tsh_disconnect(result_conn); // Disconnect immediately after operation
                        }
                        
                        free(row_A); // Clean up matrix A row
                    }
                }
                
                // If timeout occurred during chunk processing, break the loop
                if (worker_timeout) {
                    break;
                }
                
                break; // After processing one chunk
            }
            
            tsh_disconnect(conn); // Disconnect immediately after operation
        }
        
        // If we didn't claim any work this round, implement better termination logic:
        if (!claimed) {
            consecutive_misses++;
            
            // More aggressive termination:
            // If we've missed more than 3 times in a row, self-terminate
            if (consecutive_misses >= 3) {
                // Check if we've done at least some work
                if (chunks_processed > 0) {
                    work_finished = 1;
                    break;
                } else if (consecutive_misses >= 10) {
                    // If we haven't done any work and missed many times, exit anyway
                    work_finished = 1;
                    break;
                }
            }
            
            // Check for termination conditions
            
            // 1. We've processed enough chunks and had consecutive misses
            if (chunks_processed > 0 && consecutive_misses >= 3) {
                // Check if we've processed all chunks or most of them
                if (chunks_processed >= total_chunks) {
                    work_finished = 1;
                    break;
                }
            }
            
            // 2. Check for explicit termination signal
            TSH_CONN *term_conn = tsh_connect(port);
            if (term_conn) {
                char done_tuple[] = "all_work_complete";
                int done_val;
                unsigned long len = sizeof(done_val);
                
                if (tsh_read(term_conn, done_tuple, (char*)&done_val, &len) == 0) {
                    work_finished = 1;
                }
                
                // If we've processed more than 60% of chunks, place termination signal
                if (chunks_processed > 0 && chunks_processed >= (total_chunks * 6 / 10) && consecutive_misses >= 5) {
                    int done_val = 1;
                    tsh_put(term_conn, done_tuple, 1, &done_val, sizeof(done_val));
                    work_finished = 1;
                }
                
                tsh_disconnect(term_conn);
            }
            
            // Reduce the sleep time to make checks more frequent
            usleep(5000); // 5ms
        }
    }
    
    free(result_buffer);
    free(matrix_B);
    return 0;
}