#!/bin/bash

# Script to test fault tolerance by randomly killing worker processes during matrix multiplication
# Usage: ./fault_tolerance_test.sh <port> <kill_probability>

# Check arguments
if [ $# -lt 2 ]; then
    echo "Usage: $0 <port> <kill_probability>"
    echo "  port: Starting port number for TSH server"
    echo "  kill_probability: Probability (0.0-1.0) of killing a worker process during execution"
    exit 1
fi

BASE_PORT=$1
KILL_PROBABILITY=$2
CURRENT_PORT=$BASE_PORT
CSV_FILE="matrix_performance_fault_tolerance.csv"

# Define matrix sizes and granularities to test
MATRIX_SIZES=(64 128 256)
GRANULARITIES=(4 8 16 32 64)

# Use a temporary file to track killed workers between processes
KILL_COUNT_FILE="/tmp/worker_kill_count.tmp"

# Function to kill a random worker process
kill_random_worker() {
    local probability=$1
    local kill_count_file=$2
    
    # Pure bash arithmetic for integer comparison
    local prob_value=0
    
    # Handle special cases with hardcoded values
    case "$probability" in
        "0.0") prob_value=0 ;;
        "0.1") prob_value=1 ;;
        "0.2") prob_value=2 ;;
        "0.3") prob_value=3 ;;
        "0.4") prob_value=4 ;;
        "0.5") prob_value=5 ;;
        "0.6") prob_value=6 ;;
        "0.7") prob_value=7 ;;
        "0.8") prob_value=8 ;;
        "0.9") prob_value=9 ;;
        "1.0") prob_value=10 ;;
        *) prob_value=5 ;; # Default to 50% if unknown value
    esac
    
    # Calculate threshold (0-10 scale)
    local random_val=$((RANDOM % 10))
    
    if [ $random_val -lt $prob_value ]; then
        # Get list of running worker processes
        WORKER_PIDS=$(pgrep -f "matrix_worker")
        
        if [ -n "$WORKER_PIDS" ]; then
            # Convert to array
            WORKER_ARRAY=($WORKER_PIDS)
            
            # Get array length
            WORKER_COUNT=${#WORKER_ARRAY[@]}
            
            if [ $WORKER_COUNT -gt 0 ]; then
                # Choose random worker to kill
                RANDOM_INDEX=$((RANDOM % WORKER_COUNT))
                VICTIM_PID=${WORKER_ARRAY[$RANDOM_INDEX]}
                
                echo "Killing worker process $VICTIM_PID"
                if kill -9 $VICTIM_PID 2>/dev/null; then
                    # Increment the kill counter in the shared file
                    current_count=0
                    if [ -f "$kill_count_file" ]; then
                        current_count=$(cat "$kill_count_file")
                    fi
                    echo $((current_count + 1)) > "$kill_count_file"
                    echo "Kill count updated to $((current_count + 1))"
                    return 0  # Successfully killed a worker
                fi
            fi
        fi
    fi
    
    return 1  # Didn't kill any worker
}

# Function to ensure no TSH server is running on a given port
kill_server() {
    local port=$1
    echo "Checking for TSH server on port $port..."
    
    if pgrep -f "./tsh $port" > /dev/null; then
        echo "Terminating tuple server on port $port"
        pkill -f "./tsh $port" || true
        sleep 1
        
        # Double-check if it's still running
        if pgrep -f "./tsh $port" > /dev/null; then
            echo "Server didn't terminate gracefully, force killing..."
            pkill -9 -f "./tsh $port" || true
            sleep 1
        fi
    fi
}

# Function to run a fault tolerance test with random worker kills
run_fault_tolerance_test() {
    local size=$1
    local granularity=$2
    local port=$3
    local kill_probability=$4
    
    # Reset the kill counter
    echo 0 > "$KILL_COUNT_FILE"
    
    # Skip if granularity > size (invalid configuration)
    if [ "$granularity" -gt "$size" ]; then
        echo "Skipping invalid configuration: Size $size, Granularity $granularity"
        return
    fi
    
    echo "==================================================================="
    echo "Running test: Size $size, Granularity $granularity on port $port"
    echo "Kill probability: $kill_probability"
    
    # Start a fresh TSH server for this test
    echo "Starting new TSH server on port $port"
    ./tsh $port > /dev/null 2>&1 &
    
    # Give the server time to initialize
    sleep 2
    
    # Start the matrix_master in the background
    echo "Running matrix_master on port $port..."
    ./matrix_master $port $size $granularity > master_output.txt 2>&1 &
    MASTER_PID=$!
    
    # Start the worker-killer loop in the background
    (
        # Wait a bit for workers to start
        sleep 3
        
        # Keep killing workers periodically until master completes
        while ps -p $MASTER_PID > /dev/null; do
            if kill_random_worker $kill_probability "$KILL_COUNT_FILE"; then
                # Kill successful, counter already updated in the function
                :
            fi
            
            # Wait a short time before potentially killing another
            sleep 1
        done
    ) &
    KILLER_PID=$!
    
    # Wait for master process to complete
    wait $MASTER_PID
    RESULT_STATUS=$?
    
    # Kill the worker-killer process
    kill $KILLER_PID 2>/dev/null || true
    
    # Get the final kill count
    local num_kills=0
    if [ -f "$KILL_COUNT_FILE" ]; then
        num_kills=$(cat "$KILL_COUNT_FILE")
    fi
    
    # Process the output
    if [ $RESULT_STATUS -ne 0 ]; then
        echo "ERROR: Test failed with exit code $RESULT_STATUS"
        echo "$size,$granularity,$kill_probability,ERROR,ERROR,$num_kills" >> "$CSV_FILE"
    else
        # Extract timing information
        TOTAL_TIME=$(grep "Total time:" master_output.txt | awk '{print $3}')
        MULT_TIME=$(grep "Pure multiplication time:" master_output.txt | awk '{print $4}')
        
        if [ -z "$TOTAL_TIME" ] || [ -z "$MULT_TIME" ]; then
            echo "ERROR: Could not extract timing values"
            echo "$size,$granularity,$kill_probability,ERROR,ERROR,$num_kills" >> "$CSV_FILE"
        else
            echo "Test completed successfully. Total time: $TOTAL_TIME s, Multiplication time: $MULT_TIME s"
            echo "Killed $num_kills worker processes during execution"
            echo "$size,$granularity,$kill_probability,$TOTAL_TIME,$MULT_TIME,$num_kills" >> "$CSV_FILE"
        fi
    fi
    
    # Kill the server after the test is complete
    kill_server $port
    
    # Clean up output file
    rm -f master_output.txt
    
    echo "Test completed for Size $size, Granularity $granularity"
    echo "==================================================================="
}

# Create CSV file with headers
echo "Matrix Size,Granularity,Kill Probability,Total Time (s),Multiplication Time (s),Workers Killed" > "$CSV_FILE"

# Kill any existing TSH server on the base port before starting
kill_server $CURRENT_PORT

# Run tests for all combinations of matrix size and granularity
for size in "${MATRIX_SIZES[@]}"; do
    for granularity in "${GRANULARITIES[@]}"; do
        # Run normal test with no killing
        run_fault_tolerance_test "$size" "$granularity" "$CURRENT_PORT" 0.0
        CURRENT_PORT=$((CURRENT_PORT + 1))
        
        # Run test with specified kill probability
        run_fault_tolerance_test "$size" "$granularity" "$CURRENT_PORT" "$KILL_PROBABILITY"
        CURRENT_PORT=$((CURRENT_PORT + 1))
        
        # Give the system a moment to settle
        sleep 2
    done
done

# Clean up the temporary file
rm -f "$KILL_COUNT_FILE"

echo "Testing complete. Results saved to $CSV_FILE"