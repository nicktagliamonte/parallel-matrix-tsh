#!/bin/bash

# Script to run matrix_master with multiple sizes and granularities
# Uses a fresh TSH server for each test run
# Usage: ./granularity.sh <port>

# Check for port argument
if [ $# -lt 1 ]; then
    echo "Usage: $0 <port>"
    exit 1
fi

BASE_PORT=$1
CURRENT_PORT=$BASE_PORT
CSV_FILE="matrix_performance.csv"

# Define matrix sizes and granularities to test
MATRIX_SIZES=(64 128 256 512)
GRANULARITIES=(1 2 4 8 16 32 64 128 256 512)

# If CSV file doesn't exist yet, create it with headers
if [ ! -f "$CSV_FILE" ]; then
    echo "Matrix Size,Granularity,Total Time (s),Multiplication Time (s)" > "$CSV_FILE"
fi

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

# Function to run a test with a specific size, granularity, and port
run_test() {
    local size=$1
    local granularity=$2
    local port=$3
    
    # Skip if granularity > size (invalid configuration)
    if [ "$granularity" -gt "$size" ]; then
        echo "Skipping invalid configuration: Size $size, Granularity $granularity"
        return
    fi
    
    echo "==================================================================="
    echo "Running test: Size $size, Granularity $granularity on port $port"
    
    # Start a fresh TSH server for this test
    echo "Starting new TSH server on port $port"
    ./tsh $port > /dev/null 2>&1 &
    
    # Give the server time to initialize
    sleep 2
    
    # Run the matrix multiplication
    echo "Running matrix_master on port $port..."
    timeout 120s ./matrix_master $port $size $granularity
    result_status=$?
    
    # Check if test completed successfully or timed out
    if [ $result_status -eq 124 ]; then
        echo "ERROR: Test timed out after 120 seconds"
        echo "$size,$granularity,TIMEOUT,TIMEOUT" >> "$CSV_FILE"
    elif [ $result_status -ne 0 ]; then
        echo "ERROR: Test failed with exit code $result_status"
        echo "$size,$granularity,ERROR,ERROR" >> "$CSV_FILE"
    fi
    
    # Kill the server after the test is complete
    kill_server $port
    
    echo "Test completed for Size $size, Granularity $granularity"
    echo "==================================================================="
}

# Kill any existing TSH server on the base port before starting
kill_server $CURRENT_PORT

# Run tests for all combinations of matrix size and granularity
for size in "${MATRIX_SIZES[@]}"; do
    for granularity in "${GRANULARITIES[@]}"; do
        # Skip invalid configurations
        if [ "$granularity" -gt "$size" ]; then
            continue
        fi
        
        # Run the test on the current port
        run_test "$size" "$granularity" "$CURRENT_PORT"
        
        # Increment port for the next test
        CURRENT_PORT=$((CURRENT_PORT + 1))
        
        # Give the system a moment to settle
        sleep 2
    done
done

echo "Testing complete. Results saved to $CSV_FILE"
echo "Done!"