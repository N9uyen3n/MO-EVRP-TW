
import sys
import matplotlib.pyplot as plt

def parse_instance(file_path):
    """
    Parses an instance file for the EVRP.

    Args:
        file_path (str): The path to the instance file.

    Returns:
        tuple: A tuple containing three lists: depots, stations, and customers.
               Each list contains dictionaries with node data.
    """
    depots = []
    stations = []
    customers = []

    with open(file_path, 'r') as f:
        lines = f.readlines()

    # Skip header and empty lines
    node_lines = [line for line in lines if line.strip() and not line.startswith(('Q', 'C', 'r', 'g', 'v')) and "StringID" not in line]

    for line in node_lines:
        parts = line.split()
        node_id = parts[0]
        node_type = parts[1]
        x = float(parts[2])
        y = float(parts[3])
        
        node_data = {'id': node_id, 'x': x, 'y': y}

        if node_type == 'd':
            depots.append(node_data)
        elif node_type == 'f':
            stations.append(node_data)
        elif node_type == 'c':
            customers.append(node_data)
            
    return depots, stations, customers

def visualize_instance(depots, stations, customers):
    """
    Visualizes the instance data on a 2D plot.

    Args:
        depots (list): A list of depot nodes.
        stations (list): A list of station nodes.
        customers (list): A list of customer nodes.
    """
    plt.figure(figsize=(12, 8))

    # Plot depots
    if depots:
        depot_x = [d['x'] for d in depots]
        depot_y = [d['y'] for d in depots]
        plt.scatter(depot_x, depot_y, c='black', marker='*', s=200, label='Depot')

    # Plot stations
    if stations:
        station_x = [s['x'] for s in stations]
        station_y = [s['y'] for s in stations]
        plt.scatter(station_x, station_y, c='green', marker='s', s=100, label='Station')

    # Plot customers
    if customers:
        customer_x = [c['x'] for c in customers]
        customer_y = [c['y'] for c in customers]
        plt.scatter(customer_x, customer_y, c='blue', marker='o', label='Customer')

    # Add labels for each point
    all_nodes = depots + stations + customers
    for node in all_nodes:
        plt.text(node['x'], node['y'], f"  {node['id']}", fontsize=9)

    plt.title('Instance Visualization')
    plt.xlabel('X Coordinate')
    plt.ylabel('Y Coordinate')
    plt.legend()
    plt.grid(True)
    plt.axis('equal')
    plt.show()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python visualize_instance.py <path_to_instance_file>")
        # As an example, using a default file if no argument is provided.
        # You should replace this with a proper file path.
        print("Running with a default example path...")
        instance_file = r'D:\Work\SLSCM-LaB\24092025\test3\MO-EVRP-TW\data\solomon\c101C5.txt'
    else:
        instance_file = sys.argv[1]

    try:
        depots, stations, customers = parse_instance(instance_file)
        visualize_instance(depots, stations, customers)
    except FileNotFoundError:
        print(f"Error: File not found at {instance_file}")
    except Exception as e:
        print(f"An error occurred: {e}")
