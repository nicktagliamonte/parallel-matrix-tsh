import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# Load the CSV file
data = pd.read_csv("matrix_performance_fault_tolerance.csv")

# Ensure numeric columns are properly parsed
data["Total Time (s)"] = pd.to_numeric(data["Total Time (s)"], errors="coerce")
data["Multiplication Time (s)"] = pd.to_numeric(data["Multiplication Time (s)"], errors="coerce")
data["Workers Killed"] = pd.to_numeric(data["Workers Killed"], errors="coerce")

# Drop rows with invalid data
data = data.dropna()

# Group data by matrix size and granularity
matrix_sizes = data["Matrix Size"].unique()
granularities = data["Granularity"].unique()

# Create a plot for each matrix size comparing normal vs fault tolerance runs
for size in matrix_sizes:
    fig, ax = plt.subplots(figsize=(12, 6))
    
    # Filter data for this matrix size
    size_data = data[data["Matrix Size"] == size]
    
    # Get data for normal runs (kill probability = 0)
    normal_data = size_data[size_data["Kill Probability"] == 0.0]
    
    # Get data for fault tolerance runs (kill probability > 0)
    ft_data = size_data[size_data["Kill Probability"] > 0.0]
    
    # Set up bar positions
    bar_width = 0.35
    index = np.arange(len(granularities))
    
    # Create bars - ensure data is aligned by granularity
    normal_times = []
    ft_times = []
    workers_killed = []
    
    for g in granularities:
        normal_time = normal_data[normal_data["Granularity"] == g]["Total Time (s)"].values
        normal_times.append(normal_time[0] if len(normal_time) > 0 else np.nan)
        
        ft_time_data = ft_data[ft_data["Granularity"] == g]
        if len(ft_time_data) > 0:
            ft_times.append(ft_time_data["Total Time (s)"].values[0])
            workers_killed.append(ft_time_data["Workers Killed"].values[0])
        else:
            ft_times.append(np.nan)
            workers_killed.append(0)
    
    # Plot the bars
    bar1 = ax.bar(index - bar_width/2, normal_times, bar_width, label='Normal Execution')
    bar2 = ax.bar(index + bar_width/2, ft_times, bar_width, label='With Worker Failures')
    
    # Add the number of killed workers as text on top of the bars
    for i, v in enumerate(workers_killed):
        if v > 0:
            ax.text(index[i] + bar_width/2, ft_times[i] + 0.1, f'{int(v)} killed', 
                    ha='center', va='bottom', fontsize=9)
    
    # Add labels and title
    ax.set_xlabel('Granularity')
    ax.set_ylabel('Total Time (s)')
    ax.set_title(f'Performance Comparison with Worker Failures - Matrix Size {size}x{size}')
    ax.set_xticks(index)
    ax.set_xticklabels(granularities)
    ax.legend()
    plt.grid(True, axis='y', linestyle='--', alpha=0.7)
    
    # Save the figure
    plt.savefig(f'fault_tolerance_size_{size}.png')
    plt.close()

# Create an overall summary plot showing resilience efficiency
plt.figure(figsize=(12, 8))

# Calculate the overhead percentage for each configuration
overhead_data = []

for size in matrix_sizes:
    for granularity in granularities:
        normal = data[(data["Matrix Size"] == size) & 
                       (data["Granularity"] == granularity) & 
                       (data["Kill Probability"] == 0.0)]
        
        fault = data[(data["Matrix Size"] == size) & 
                      (data["Granularity"] == granularity) & 
                      (data["Kill Probability"] > 0.0)]
        
        if len(normal) > 0 and len(fault) > 0:
            normal_time = normal["Total Time (s)"].values[0]
            fault_time = fault["Total Time (s)"].values[0]
            killed_workers = fault["Workers Killed"].values[0]
            
            # Only include if workers were actually killed
            if killed_workers > 0:
                overhead_pct = ((fault_time - normal_time) / normal_time) * 100
                overhead_data.append({
                    'Matrix Size': size,
                    'Granularity': granularity,
                    'Overhead %': overhead_pct,
                    'Workers Killed': killed_workers
                })

# Convert to DataFrame
overhead_df = pd.DataFrame(overhead_data)

if len(overhead_df) > 0:
    # Create scatter plot with bubble size representing number of workers killed
    plt.scatter(overhead_df['Granularity'], overhead_df['Overhead %'], 
                s=overhead_df['Workers Killed'] * 50, alpha=0.6, 
                c=overhead_df['Matrix Size'], cmap='viridis')

    # Add a horizontal line at 0% overhead
    plt.axhline(y=0, color='r', linestyle='-', alpha=0.3)

    # Add color bar for matrix size
    cbar = plt.colorbar()
    cbar.set_label('Matrix Size')

    # Add labels and title
    plt.xlabel('Granularity')
    plt.ylabel('Overhead %')
    plt.title('Fault Tolerance Overhead by Configuration')
    plt.grid(True, linestyle='--', alpha=0.7)
    
    # Save the figure
    plt.savefig('fault_tolerance_overhead.png')
else:
    print("Not enough data with killed workers to create overhead plot")

print("Plots saved as PNG files.")
