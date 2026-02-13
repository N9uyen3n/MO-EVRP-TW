import random
import math

def generate_cbd_instance(filename, num_customers=100, k_min=3, k_max=8, seed=42):
    """
    Generate CBD Logistics instance
    
    Args:
        filename: Output filename
        num_customers: Number of customers (default 100)
        k_min: Minimum stops per route
        k_max: Maximum stops per route
        seed: Random seed for reproducibility (same seed = same customer positions)
    """
    random.seed(seed)  # Fixed seed for reproducible positions
    
    # Parameters from update.txt
    DEPOT_X, DEPOT_Y = 50, 50
    SERVICE_TIME_MEAN = 10
    SERVICE_TIME_STD = 2
    T_MAX = 100

    # Infinite Battery setup
    BATTERY_CAPACITY = 1e9 
    VEHICLE_CAPACITY = 200
    VEHICLE_VELOCITY = 1.0

    # Depot Time Window (7:00 AM - 11:00 AM = 240 mins)
    HORIZON_START = 0
    HORIZON_END = 240

    with open(filename, "w") as f:
        f.write("StringID   Type       x          y          demand     ReadyTime  DueDate    ServiceTime\n")
        
        # Depot
        f.write(f"D0         d          {DEPOT_X:.1f}       {DEPOT_Y:.1f}       0.0        {HORIZON_START:.1f}        {HORIZON_END:.1f}     0.0\n")

        # Stations (1 station at depot for format consistency)
        f.write(f"S0         f          {DEPOT_X:.1f}       {DEPOT_Y:.1f}       0.0        {HORIZON_START:.1f}        {HORIZON_END:.1f}     0.0\n")

        # Customers
        customers = []
        for i in range(1, num_customers + 1):
            cust_id = f"C{i}"
            cust_type = "c"
            x = random.uniform(0, 100)
            y = random.uniform(0, 100)
            demand = random.choice([10, 20, 30])
            
            # Service Time (Gaussian)
            service_time = max(1, random.gauss(SERVICE_TIME_MEAN, SERVICE_TIME_STD))
            
            # Generate 2-3 Time Windows
            num_tws = random.choice([2, 3])
            tws = []
            
            attempts = 0
            while len(tws) < num_tws and attempts < 100:
                start = random.uniform(0, HORIZON_END - 30)
                end = start + random.uniform(30, 60)
                if end > HORIZON_END: end = HORIZON_END
                
                # Check overlap
                is_overlap = False
                for t in tws:
                    if not (end < t[0] or start > t[1]):
                        is_overlap = True
                        break
                
                if not is_overlap:
                    tws.append((start, end))
                    tws.sort()
                attempts += 1
            
            if not tws:
                tws.append((0, HORIZON_END))

            # Primary TW
            primary_tw = tws[0]
            
            f.write(f"{cust_id:<10} {cust_type:<10} {x:<10.1f} {y:<10.1f} {demand:<10.1f} {primary_tw[0]:<10.1f} {primary_tw[1]:<10.1f} {service_time:<10.1f}\n")
            
            if len(tws) > 1:
                customers.append((cust_id, tws[1:]))

        # Parameters section
        f.write("\n")
        f.write(f"Q Vehicle fuel tank capacity /{BATTERY_CAPACITY}/\n")
        f.write(f"C Vehicle load capacity /{VEHICLE_CAPACITY}/\n")
        f.write(f"r fuel consumption rate /1.0/\n")
        f.write(f"g inverse refueling rate /1.0/\n")
        f.write(f"v average Velocity /{VEHICLE_VELOCITY}/\n")
        f.write(f"k_min /{k_min}/\n")
        f.write(f"k_max /{k_max}/\n")
        f.write(f"T_max /{T_MAX}/\n")
        
        # Extra TWs section
        if customers:
            f.write("\nMULTIPLE_TIME_WINDOWS\n")
            for cid, extra_tws in customers:
                for tw in extra_tws:
                    f.write(f"{cid} {tw[0]:.1f} {tw[1]:.1f}\n")
    
    print(f"Generated: {filename} (N={num_customers}, k_min={k_min}, k_max={k_max})")

if __name__ == "__main__":
    # Generate 4 scenarios for 100 nodes (same customer positions, different k_min/k_max)
    SEED = 42  # Fixed seed for reproducible customer positions
    
    scenarios = [
        ("data/cbd_100_3_8.txt", 100, 3, 8),
        ("data/cbd_100_3_5.txt", 100, 3, 5),
        ("data/cbd_100_5_8.txt", 100, 5, 8),
        ("data/cbd_100_5_5.txt", 100, 5, 5),
    ]
    
    for filename, n, k_min, k_max in scenarios:
        generate_cbd_instance(filename, n, k_min, k_max, SEED)
