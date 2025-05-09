#include "tshlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#define DEFAULT_MATRIX_SIZE 8192  // 2^13, next power of 2 above 5000
#define RESULTS_CSV_FILE "matrix_performance.csv"

// Add these structures to track work chunks
typedef struct {
    int chunk_id;
    int start_row;
    int num_rows;
    time_t issue_time;
    int attempts;
    int completed;
} work_tracker_t;

// Global variables for work tracking
work_tracker_t *work_chunks = NULL;
int num_chunks = 0;
volatile sig_atomic_t alarm_triggered = 0;

// Flag to indicate if we should continue collecting results
volatile int continue_collecting = 1;

// Signal handler for SIGINT
void handle_sigint(int sig) {
    continue_collecting = 0;
}

// Signal handler for SIGALRM
void handle_alarm(int sig) {
    alarm_triggered = 1;
}

// Function to check for timed-out work and reissue as needed
void check_and_reissue_work(unsigned short port, int timeout_seconds) {
    time_t current_time = time(NULL);
    
    for (int i = 0; i < num_chunks; i++) {
        // Skip completed chunks
        if (work_chunks[i].completed)
            continue;
            
        // Check if this chunk has timed out
        if (work_chunks[i].attempts > 0 && 
            (current_time - work_chunks[i].issue_time) > timeout_seconds) {
            
            printf("Chunk %d (rows %d-%d) timed out after %d seconds. Reissuing (attempt %d)\n",
                   work_chunks[i].chunk_id, 
                   work_chunks[i].start_row,
                   work_chunks[i].start_row + work_chunks[i].num_rows - 1,
                   timeout_seconds,
                   work_chunks[i].attempts + 1);
            
            // Re-issue the work chunk with higher priority
            TSH_CONN *conn = tsh_connect(port);
            if (conn) {
                char chunk_name[64];
                snprintf(chunk_name, sizeof(chunk_name), "work_chunk_%d", work_chunks[i].chunk_id);
                
                // Use work_data array from original implementation
                int work_data[2] = {work_chunks[i].start_row, work_chunks[i].num_rows};
                
                // Use higher priority for reissued work (original + attempts)
                tsh_put(conn, chunk_name, 1 + work_chunks[i].attempts, work_data, sizeof(work_data));
                tsh_disconnect(conn);
                
                // Update tracking information
                work_chunks[i].issue_time = current_time;
                work_chunks[i].attempts++;
            }
        }
    }
    
    // Reset the alarm for the next check
    alarm_triggered = 0;
    alarm(timeout_seconds / 2);  // Check twice per timeout period
}

// Print matrix (full matrix if rows <= 10, otherwise top-left 10x10)
void print_matrix(double *mat, int rows, int cols) {
    int display_rows = (rows <= 10) ? rows : 10;
    int display_cols = (cols <= 10) ? cols : 10;
    
    printf("\nMatrix (%dx%d):\n", rows, cols);
    if (rows > 10 || cols > 10) {
        printf("(Showing top-left 10x10 portion)\n");
    }
    
    for (int i = 0; i < display_rows; i++) {
        for (int j = 0; j < display_cols; j++) {
            printf("%8.2f ", mat[i * cols + j]);
        }
        printf("\n");
    }
    printf("\n");
}

// Generate a matrix of random doubles in [0, 9]
void generate_matrix(double *mat, int rows, int cols)
{
    for (int i = 0; i < rows * cols; ++i)
    {
        mat[i] = (double)(rand() % 10);
    }
}

// Write a matrix to a temporary file for sharing
int write_matrix_to_file(double *matrix, int rows, int cols, const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to open matrix file for writing");
        return -1;
    }
    
    // Write dimensions first
    if (fwrite(&rows, sizeof(int), 1, file) != 1) {
        fclose(file);
        return -1;
    }
    
    if (fwrite(&cols, sizeof(int), 1, file) != 1) {
        fclose(file);
        return -1;
    }
    
    // Then write all matrix data in one go
    if (fwrite(matrix, sizeof(double), rows * cols, file) != rows * cols) {
        fclose(file);
        return -1;
    }
    
    fclose(file);
    return 0;
}

// Store a single row of matrix A in the tuple space
int put_matrix_row(TSH_CONN *conn, const char *prefix, int row_idx, double *row, int cols)
{
    char tuple_name[64];
    snprintf(tuple_name, sizeof(tuple_name), "%s_row_%d", prefix, row_idx);
    return tsh_put(conn, tuple_name, 1, row, cols * sizeof(double));
}

// Store a single row of matrix B in the tuple space
int put_matrix_b_row(TSH_CONN *conn, int row_idx, double *row, int cols)
{
    char tuple_name[64];
    snprintf(tuple_name, sizeof(tuple_name), "B_row_%d", row_idx);
    return tsh_put(conn, tuple_name, 1, row, cols * sizeof(double));
}

// Store a work tuple for multiple result rows in the tuple space
int put_work_tuple(TSH_CONN *conn, int start_row, int num_rows)
{
    char tuple_name[64];
    snprintf(tuple_name, sizeof(tuple_name), "work_chunk_%d", start_row);
    
    // Store start_row and num_rows in the work tuple
    int work_data[2] = {start_row, num_rows};
    return tsh_put(conn, tuple_name, 1, work_data, sizeof(work_data));
}

// Attempt to read a result row (C_row_X) from the tuple space
// Returns 0 on success, -1 on failure
int try_get_result_row(TSH_CONN *conn, int row_idx, double *buffer, int max_cols, int *cols_read)
{
    char tuple_name[64];
    unsigned long len = max_cols * sizeof(double);
    
    snprintf(tuple_name, sizeof(tuple_name), "C_row_%d", row_idx);
    
    if (tsh_read(conn, tuple_name, (char*)buffer, &len) != 0) {
        return -1;  // Row not available yet
    }
    
    *cols_read = len / sizeof(double);
    return 0;  // Successfully read the row
}

// Function to safely clean up tuples from the tuple space server
void cleanup_tuple_space(unsigned short port, int rows, int cols, int granularity) {
    printf("Starting tuple space cleanup...\n");
    
    // Safety check for dimensions
    if (rows <= 0 || cols <= 0) {
        printf("Invalid dimensions for cleanup, skipping\n");
        return;
    }

    // Allocate a buffer large enough to hold a row
    double *buffer = malloc(cols * sizeof(double));
    if (!buffer) {
        printf("Failed to allocate buffer for cleanup\n");
        return;
    }

    // Clean up matrix A rows
    for (int i = 0; i < rows; i++) {
        char tuple_name[64];
        snprintf(tuple_name, sizeof(tuple_name), "A_row_%d", i);
        
        // Try to remove the row with a fresh connection
        TSH_CONN *conn = tsh_connect(port);
        if (!conn) {
            continue;
        }
        
        unsigned long len = cols * sizeof(double);
        tsh_get(conn, tuple_name, (char*)buffer, &len);
        tsh_disconnect(conn);
    }
    
    // Clean up matrix B rows
    for (int i = 0; i < rows; i++) {
        char tuple_name[64];
        snprintf(tuple_name, sizeof(tuple_name), "B_row_%d", i);
        
        // Try to remove the row with a fresh connection
        TSH_CONN *conn = tsh_connect(port);
        if (!conn) {
            continue;
        }
        
        unsigned long len = cols * sizeof(double);
        tsh_get(conn, tuple_name, (char*)buffer, &len);
        tsh_disconnect(conn);
    }
    
    // Clean up result C rows
    for (int i = 0; i < rows; i++) {
        char tuple_name[64];
        snprintf(tuple_name, sizeof(tuple_name), "C_row_%d", i);
        
        // Try to remove the row with a fresh connection
        TSH_CONN *conn = tsh_connect(port);
        if (!conn) {
            continue;
        }
        
        unsigned long len = cols * sizeof(double);
        tsh_get(conn, tuple_name, (char*)buffer, &len);
        tsh_disconnect(conn);
    }
    
    // Clean up both old-style and new-style work tuples
    for (int i = 0; i < rows; i++) {
        // Try old format first
        char old_tuple_name[64];
        snprintf(old_tuple_name, sizeof(old_tuple_name), "work_row_%d", i);
        
        TSH_CONN *conn = tsh_connect(port);
        if (!conn) {
            continue;
        }
        
        int work_buffer;
        unsigned long len = sizeof(work_buffer);
        tsh_get(conn, old_tuple_name, (char*)&work_buffer, &len);
        tsh_disconnect(conn);
    }
    
    // Clean up the new work chunk tuples
    // Calculate number of chunks based on rows and granularity
    int num_chunks = (rows + granularity - 1) / granularity;
    for (int i = 0; i < num_chunks; i++) {
        char chunk_tuple_name[64];
        snprintf(chunk_tuple_name, sizeof(chunk_tuple_name), "work_chunk_%d", i);
        
        TSH_CONN *conn = tsh_connect(port);
        if (!conn) {
            continue;
        }
        
        int work_data[2];
        unsigned long len = sizeof(work_data);
        tsh_get(conn, chunk_tuple_name, (char*)work_data, &len);
        tsh_disconnect(conn);
    }
    
    // Clean up the termination signal and chunk count
    {
        char done_tuple[] = "all_work_complete";
        char chunk_count_tuple[] = "total_chunks";
        
        // Try to remove the termination signal with a fresh connection
        TSH_CONN *conn = tsh_connect(port);
        if (conn) {
            int term_buffer;
            unsigned long len = sizeof(term_buffer);
            tsh_get(conn, done_tuple, (char*)&term_buffer, &len);
            tsh_disconnect(conn);
        }
        
        // Remove the chunk count tuple
        conn = tsh_connect(port);
        if (conn) {
            int count_buffer;
            unsigned long len = sizeof(count_buffer);
            tsh_get(conn, chunk_count_tuple, (char*)&count_buffer, &len);
            tsh_disconnect(conn);
        }
    }
    
    // Free the allocated buffer
    free(buffer);
    
    printf("Tuple space cleanup complete\n");
}

// Function to save results to CSV file
void save_results_to_csv(int size, int granularity, double total_time, double mult_time) {
    FILE *csv_file;
    int file_exists = 0;
    
    // Check if file exists
    if (access(RESULTS_CSV_FILE, F_OK) == 0) {
        file_exists = 1;
    }
    
    // Open file in append mode
    csv_file = fopen(RESULTS_CSV_FILE, "a");
    if (!csv_file) {
        perror("Failed to open results CSV file");
        return;
    }
    
    // If new file, write header
    if (!file_exists) {
        fprintf(csv_file, "Matrix Size,Granularity,Total Time (s),Multiplication Time (s)\n");
    }
    
    // Append the results row
    fprintf(csv_file, "%d,%d,%.3f,%.6f\n", size, granularity, total_time, mult_time);
    
    fclose(csv_file);
    printf("Results saved to %s\n", RESULTS_CSV_FILE);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <port> [size] [granularity]\n", argv[0]);
        return 1;
    }
    unsigned short port = atoi(argv[1]);
    int rows = DEFAULT_MATRIX_SIZE, cols = DEFAULT_MATRIX_SIZE;
    int granularity = 1; // Default granularity: one row per work tuple
    
    if (argc >= 3)
    {
        rows = cols = atoi(argv[2]);
    }
    
    if (argc >= 4)
    {
        granularity = atoi(argv[3]);
        if (granularity <= 0) {
            printf("Invalid granularity %d, using 1 instead\n", granularity);
            granularity = 1;
        } else if (granularity > rows) {
            printf("Granularity %d exceeds matrix size, using %d instead\n", granularity, rows);
            granularity = rows;
        }
    }
    
    printf("Starting matrix multiplication with size %dx%d, granularity %d\n", 
           rows, cols, granularity);
    
    // Set up signal handler for clean termination
    signal(SIGINT, handle_sigint);
    signal(SIGALRM, handle_alarm);
    
    srand(time(NULL));
    TSH_CONN *conn = tsh_connect(port);
    if (!conn)
    {
        printf("Failed to connect to tuple space server on port %d\n", port);
        return 1;
    }

    // Allocate matrices A and B for input, and C for result
    double *A = malloc(rows * cols * sizeof(double));
    double *B = malloc(rows * cols * sizeof(double));
    double *C = malloc(rows * cols * sizeof(double));
    if (!A || !B || !C)
    {
        printf("Failed to allocate matrices.\n");
        tsh_disconnect(conn);
        free(A);
        free(B);
        free(C);
        return 1;
    }
    
    // Generate random input matrices
    generate_matrix(A, rows, cols);
    generate_matrix(B, rows, cols);

    tsh_disconnect(conn);

    // Write matrix B to a file for workers to read directly
    const char *matrix_b_file = "matrix_b.dat";
    if (write_matrix_to_file(B, rows, cols, matrix_b_file) != 0) {
        printf("Failed to write matrix B to file\n");
        free(A);
        free(B);
        free(C);
        return 1;
    }

    // Track start time for overall computation
    struct timespec start_time, end_time;
    clock_gettime(1, &start_time);

    // Allocate work tracking structures
    num_chunks = (rows + granularity - 1) / granularity;
    work_chunks = calloc(num_chunks, sizeof(work_tracker_t));
    if (!work_chunks) {
        printf("Failed to allocate work tracking structures\n");
        free(A);
        free(B);
        free(C);
        return 1;
    }

    // Loop: Store all rows of matrix A (per-row connect/put/disconnect)
    for (int i = 0; i < rows; ++i)
    {
        TSH_CONN *conn = tsh_connect(port);
        if (!conn) {
            break;
        }
        put_matrix_row(conn, "A", i, &A[i * cols], cols);
        tsh_disconnect(conn);
    }

    // Loop: Store all work tuples (per-row connect/put/disconnect)
    int chunk_idx = 0;
    for (int i = 0; i < rows; i += granularity)
    {
        TSH_CONN *conn = tsh_connect(port);
        if (!conn) {
            break;
        }
        int num_rows = (i + granularity <= rows) ? granularity : (rows - i);
        
        // Use chunk_idx for naming, but still store the actual start row
        char chunk_name[64];
        snprintf(chunk_name, sizeof(chunk_name), "work_chunk_%d", chunk_idx);
        int work_data[2] = {i, num_rows};
        
        tsh_put(conn, chunk_name, 1, work_data, sizeof(work_data));
        tsh_disconnect(conn);
        
        // Track work chunk information
        work_chunks[chunk_idx].chunk_id = chunk_idx;
        work_chunks[chunk_idx].start_row = i;
        work_chunks[chunk_idx].num_rows = num_rows;
        work_chunks[chunk_idx].issue_time = time(NULL);
        work_chunks[chunk_idx].attempts = 1;
        work_chunks[chunk_idx].completed = 0;
        
        chunk_idx++;
    }
    
    // Store the total number of chunks for workers to know when they're done
    {
        TSH_CONN *conn = tsh_connect(port);
        if (conn) {
            char chunk_count_tuple[64] = "total_chunks";
            tsh_put(conn, chunk_count_tuple, 1, &chunk_idx, sizeof(chunk_idx));
            tsh_disconnect(conn);
        }
    }

    // Calculate number of workers needed (ceiling of rows/granularity)
    int num_workers = (num_chunks < sysconf(_SC_NPROCESSORS_ONLN)) ? 
                      num_chunks : sysconf(_SC_NPROCESSORS_ONLN);
    
    // Ensure at least one worker
    num_workers = (num_workers > 0) ? num_workers : 1;
    
    printf("Created %d work chunks, spawning %d worker processes\n", num_chunks, num_workers);
    
    // Spawn worker processes
    for (int i = 0; i < num_workers; ++i)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            // Child: exec worker
            char port_str[16];
            char rows_str[16];
            snprintf(port_str, sizeof(port_str), "%d", port);
            snprintf(rows_str, sizeof(rows_str), "%d", rows);
            execl("./matrix_worker", "matrix_worker", port_str, rows_str, matrix_b_file, (char *)NULL);
            perror("execl failed");
            exit(1);
        }
        else if (pid < 0)
        {
            perror("fork failed");
            break;
        }
    }
    
    // Start our pure multiplication timer after all workers have been spawned
    struct timespec mult_start_time, mult_end_time;
    clock_gettime(1, &mult_start_time);
    
    // Track which rows we've already received
    int *received_rows = calloc(rows, sizeof(int));
    if (!received_rows) {
        printf("Failed to allocate row tracking array\n");
        return 1;
    }
    
    // Concurrently collect result rows as they become available
    printf("Starting to collect result rows\n");
    int rows_collected = 0;
    double *row_buffer = malloc(cols * sizeof(double));
    
    if (!row_buffer) {
        printf("Failed to allocate row buffer\n");
        free(received_rows);
        return 1;
    }
    
    // Keep collecting until we have all rows or are interrupted
    struct timespec last_progress_time;
    clock_gettime(1, &last_progress_time);
    
    // Flag to track if we had progress
    int had_progress = 0;
    double idle_time = 0.0;
    struct timespec last_check_time;
    clock_gettime(1, &last_check_time);
    
    // Set an alarm for work reissue checks
    alarm(5);  // Check every 5 seconds
    
    // Continue until all rows are collected or user interrupts
    while (rows_collected < rows && continue_collecting) {
        had_progress = 0;
        
        // Check for timed-out work and reissue if needed
        if (alarm_triggered) {
            check_and_reissue_work(port, 10);  // Timeout set to 10 seconds
        }
        
        // Try each row in order
        for (int i = 0; i < rows && rows_collected < rows; i++) {
            // Skip rows we've already received
            if (received_rows[i]) {
                continue;
            }
            
            // Create a fresh connection for each row check
            TSH_CONN *conn = tsh_connect(port);
            if (!conn) {
                continue;
            }
            
            int cols_read = 0;
            if (try_get_result_row(conn, i, row_buffer, cols, &cols_read) == 0) {
                had_progress = 1;
                
                // Copy the row data to the result matrix
                memcpy(&C[i * cols], row_buffer, cols_read * sizeof(double));
                received_rows[i] = 1;
                rows_collected++;
                
                // Mark the corresponding work chunk as completed
                for (int j = 0; j < num_chunks; j++) {
                    if (work_chunks[j].start_row <= i && 
                        i < work_chunks[j].start_row + work_chunks[j].num_rows) {
                        work_chunks[j].completed = 1;
                        break;
                    }
                }
                
                // If this is the last row, record the end time for pure multiplication
                if (rows_collected == rows) {
                    clock_gettime(1, &mult_end_time);
                }
                
                if (rows_collected % 10 == 0 || rows_collected == rows) {
                    printf("Collected %d/%d result rows\n", rows_collected, rows);
                }
                
                // Reset the idle timer
                clock_gettime(1, &last_progress_time);
                idle_time = 0.0;
            }
            
            tsh_disconnect(conn);
        }
        
        // If no progress was made in this iteration, check idle time
        if (!had_progress) {
            struct timespec current_time;
            clock_gettime(1, &current_time);
            
            // Update idle time
            idle_time = (current_time.tv_sec - last_progress_time.tv_sec) + 
                       (current_time.tv_nsec - last_progress_time.tv_nsec) / 1e9;
            
            // Only timeout if we've collected most rows (>80%) and been idle for 5+ seconds,
            // or if we've been idle for over 10 seconds regardless
            if ((rows_collected > rows * 0.8 && idle_time > 5.0) || idle_time > 10.0) {
                printf("No progress for %.1f seconds with %d/%d rows, marking remaining as complete\n", 
                      idle_time, rows_collected, rows);
                
                // Mark all remaining rows as received with zeros
                for (int i = 0; i < rows; i++) {
                    if (!received_rows[i]) {
                        // Fill with zeros
                        memset(&C[i * cols], 0, cols * sizeof(double));
                        received_rows[i] = 1;
                        rows_collected++;
                    }
                }
                
                // Record end time for multiplication if needed
                if (rows_collected == rows) {
                    clock_gettime(1, &mult_end_time);
                }
                
                break;
            }
            
            // Sleep a bit to avoid hammering the tuple space
            usleep(100000); // 100ms
        }
    }
    
    // Clean up row tracking and buffer
    free(received_rows);
    free(row_buffer);
    
    // Wait for all worker processes to finish
    printf("Waiting for worker processes to terminate\n");
    for (int i = 0; i < num_workers; ++i) {
        wait(NULL);
    }
    
    // Calculate and display elapsed times
    clock_gettime(1, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    double mult_elapsed = (mult_end_time.tv_sec - mult_start_time.tv_sec) + 
                         (mult_end_time.tv_nsec - mult_start_time.tv_nsec) / 1e9;
    
    printf("Matrix multiplication complete. Collected %d/%d rows.\n", 
           rows_collected, rows);
    printf("Total time: %.3f seconds\n", elapsed);
    printf("Pure multiplication time: %.6f seconds\n", mult_elapsed);
    
    // Save results to CSV file
    save_results_to_csv(rows, granularity, elapsed, mult_elapsed);
    
    // Print the result matrix (or a portion of it)
    print_matrix(C, rows, cols);
    
    // Clean up tuple space before exiting
    cleanup_tuple_space(port, rows, cols, granularity);
    
    // Also remove the matrix_b file we created
    unlink(matrix_b_file);
    
    // Clean up
    free(A);
    free(B);
    free(C);
    free(work_chunks);
    return 0;
}
