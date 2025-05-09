import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import ScalableMajorLocator
import matplotlib.cm as cm

# Load and prepare the data
def prepare_data(filename):
    data = pd.read_csv(filename)
    data = data.dropna()
    return data

# Create a plot showing the U-shaped curve of performance vs. granularity
def plot_brachistochrone_pattern(data, output_filename="brachistochrone_pattern.png"):
    """Create a visualization showing the U-shaped curve of performance vs. granularity"""
    plt.figure(figsize=(14, 8))
    
    # Get unique matrix sizes
    matrix_sizes = sorted(data["Matrix Size"].unique())
    
    # Create a colormap for different matrix sizes
    colors = cm.viridis(np.linspace(0, 1, len(matrix_sizes)))
    
    # Add curve for each matrix size
    for i, size in enumerate(matrix_sizes):
        size_data = data[data["Matrix Size"] == size]
        if len(size_data) < 3:  # Skip if not enough data points for this size
            continue
            
        # Group by granularity and calculate average time
        # For data with worker killings, use only the non-killed runs
        avg_times = size_data[size_data["Kill Probability"] == 0.0].groupby("Granularity")["Total Time (s)"].mean()
        
        # Sort by granularity
        granularities = sorted(avg_times.index.values)
        times = [avg_times[g] for g in granularities]
        
        # Plot the curve
        plt.plot(granularities, times, 'o-', color=colors[i], 
                 linewidth=2, markersize=8, label=f'{size}x{size}')
        
        # Find minimum point
        min_idx = np.argmin(times)
        min_granularity = granularities[min_idx]
        min_time = times[min_idx]
        
        # Mark the minimum with a star
        plt.plot(min_granularity, min_time, '*', color=colors[i], 
                 markersize=15, markeredgecolor='black')
        
        # Annotate the minimum point
        plt.annotate(f'Optimal: {min_granularity}', 
                     xy=(min_granularity, min_time),
                     xytext=(5, -15), 
                     textcoords='offset points',
                     ha='center',
                     bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="gray", alpha=0.8))
    
    # Add theoretical brachistochrone curve for comparison
    if len(matrix_sizes) > 0:
        # Create a stylized brachistochrone curve
        x = np.linspace(min(granularities)*0.9, max(granularities)*1.1, 100)
        
        # Define area of interest
        plt.axvspan(min(granularities), max(granularities), alpha=0.1, color='gray')
        
        # Add annotations explaining the extremes
        plt.annotate('High overhead from\nexcessive parallelism', 
                    xy=(min(granularities), np.mean(times)), 
                    xytext=(min(granularities)-5, np.mean(times)),
                    arrowprops=dict(arrowstyle='->'),
                    ha='right', va='center',
                    bbox=dict(boxstyle="round,pad=0.3", fc="lightyellow", ec="orange", alpha=0.8))
                    
        plt.annotate('Limited parallelism\n(sequential bottleneck)', 
                    xy=(max(granularities), np.mean(times)), 
                    xytext=(max(granularities)+5, np.mean(times)),
                    arrowprops=dict(arrowstyle='->'),
                    ha='left', va='center',
                    bbox=dict(boxstyle="round,pad=0.3", fc="lightyellow", ec="orange", alpha=0.8))
    
    # Add curve representing the theoretical ideal path
    plt.annotate('Theoretical Brachistochrone\n(Optimal Path)', 
                xy=(np.mean(granularities), np.min(times)*0.7),
                ha='center', va='center',
                bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="gray", alpha=0.8))

    # Add a title and labels
    plt.title('The Brachistochrone Pattern in Parallel Matrix Multiplication', fontsize=16)
    plt.xlabel('Granularity (Chunk Size)', fontsize=14)
    plt.ylabel('Execution Time (s)', fontsize=14)
    
    # Format y-axis to log scale to better show the U-shape
    plt.yscale('log')
    
    # Add explanatory text
    explanation = (
        "The Brachistochrone Pattern: Finding the optimal granularity\n"
        "• Too small: Excessive overhead from creating and managing many small tasks\n"
        "• Too large: Limited parallelism, underutilizing available processors\n"
        "• Optimal: Balance between parallelism and overhead"
    )
    plt.figtext(0.5, 0.01, explanation, ha="center", fontsize=10, 
                bbox=dict(boxstyle="round,pad=0.5", fc="aliceblue", ec="steelblue", alpha=0.8))
    
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend(title="Matrix Size")
    
    # Save the figure
    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(output_filename, dpi=300, bbox_inches='tight')
    plt.close()
    
    print(f"Brachistochrone pattern plot saved as {output_filename}")

# Create a 3D surface plot showing the performance landscape
def plot_3d_performance_surface(data, output_filename="brachistochrone_3d.png"):
    """Create a 3D surface visualization of the performance landscape"""
    from mpl_toolkits.mplot3d import Axes3D
    
    # Filter to only non-killing runs for clean visualization
    data = data[data["Kill Probability"] == 0.0]
    
    # Extract unique matrix sizes and granularities
    matrix_sizes = sorted(data["Matrix Size"].unique())
    granularities = sorted(data["Granularity"].unique())
    
    # Create a grid of matrix sizes and granularities
    X, Y = np.meshgrid(matrix_sizes, granularities)
    
    # Create an empty Z matrix for the execution times
    Z = np.zeros_like(X, dtype=float)
    
    # Fill the Z matrix with execution times
    for i, size in enumerate(matrix_sizes):
        for j, granularity in enumerate(granularities):
            matching_data = data[(data["Matrix Size"] == size) & 
                                (data["Granularity"] == granularity)]
            if len(matching_data) > 0:
                Z[j, i] = matching_data["Total Time (s)"].values[0]
            else:
                Z[j, i] = np.nan
    
    # Replace NaN values with interpolated values
    mask = np.isnan(Z)
    Z[mask] = np.interp(np.flatnonzero(mask), np.flatnonzero(~mask), Z[~mask])
    
    # Create the 3D surface plot
    fig = plt.figure(figsize=(12, 10))
    ax = fig.add_subplot(111, projection='3d')
    
    # Plot the surface
    surf = ax.plot_surface(X, Y, Z, cmap='viridis', 
                          linewidth=0, antialiased=True, alpha=0.8)
    
    # Add a color bar
    cbar = fig.colorbar(surf, ax=ax, shrink=0.5, aspect=10)
    cbar.set_label('Execution Time (s)')
    
    # Add labels
    ax.set_xlabel('Matrix Size')
    ax.set_ylabel('Granularity')
    ax.set_zlabel('Execution Time (s)')
    
    # Set the z-axis to logarithmic scale for better visualization
    ax.set_zscale('log')
    
    # Add a title
    ax.set_title('3D Performance Landscape: Finding the Optimal Granularity', fontsize=14)
    
    # Highlight the valley of optimal performance
    # Find minimum time for each matrix size
    for size in matrix_sizes:
        size_data = data[data["Matrix Size"] == size]
        if len(size_data) == 0:
            continue
            
        min_granularity = size_data.loc[size_data["Total Time (s)"].idxmin()]["Granularity"]
        min_time = size_data["Total Time (s)"].min()
        
        # Mark the minimum point
        ax.scatter([size], [min_granularity], [min_time], 
                  color='red', s=100, marker='*', label='_nolegend_')
    
    # Save the figure
    plt.tight_layout()
    plt.savefig(output_filename, dpi=300, bbox_inches='tight')
    plt.close()
    
    print(f"3D performance surface plot saved as {output_filename}")

# Main function
def main():
    # Analyze both CSV files to see which shows the brachistochrone pattern better
    regular_data = prepare_data("matrix_performance.csv")
    fault_data = prepare_data("matrix_performance_fault_tolerance.csv")
    
    # Generate the plots
    plot_brachistochrone_pattern(regular_data, "matrix_brachistochrone.png")
    plot_3d_performance_surface(regular_data, "matrix_performance_3d.png")
    
    print("Analysis complete!")

if __name__ == "__main__":
    main()