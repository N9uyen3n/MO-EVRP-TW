#include "../include/Instance.h"
#include "../include/Utils.h"
#include "../include/Customer.h"
#include "../include/Station.h"
#include "../include/Depot.h"

void Utils::printInstance(const Instance& instance) {
    int customer_count = 0;
    int station_count = 0;
    int depot_count = 0;
    std::cout << "--- Instance Info ---" << std::endl;
    for (const auto& node : instance.getNodes()) {
        if (dynamic_cast<Customer*>(node.get())) {
            customer_count++;
        } else if (dynamic_cast<Station*>(node.get())) {
            station_count++;
        } else if (dynamic_cast<Depot*>(node.get())) {
            depot_count++;
        }
        std::cout << node->toString() << "\n"; // Just to avoid unused variable warning
    }

    std::cout << "-----------------------" << std::endl;
    std::cout << "Total nodes: " << instance.getNodes().size() << std::endl;
    std::cout << "  - Customers: " << customer_count << std::endl;
    std::cout << "  - Stations: " << station_count << std::endl;
    std::cout << "  - Depots: " << depot_count << std::endl;
    std::cout << "Vehicle Capacity: " << instance.getVehicleCapacity() << std::endl;
    std::cout << "Vehicle Battery: " << instance.getVehicleBattery() << std::endl;
    std::cout << "-----------------------" << std::endl;
}
