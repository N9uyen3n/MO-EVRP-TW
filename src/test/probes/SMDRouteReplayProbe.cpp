// Diagnostic re-evaluation of traced node sequences. This deliberately creates
// fresh routes; it is NOT certification of the original exported schedules.
// Link to the matching Release library with ALNS_INSERTION_TIME_BOUNDS=1.
#include "io/Parser.h"
#include "core/Route.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
  try {
    if(argc!=5) throw std::runtime_error("input sequences results.csv details.txt required");
    const auto instance=Parser::parse(argv[1]);
    auto vehicle=std::make_shared<Vehicle>(0,instance->getVehicleCapacity(),
                     instance->getVehicleBattery(),instance->getVehicleEnergyRate());
    std::ifstream input(argv[2]); std::ofstream result(argv[3]),details(argv[4]);
    if(!input || !result || !details) throw std::runtime_error("Cannot open replay files");
    result<<"event,stage,iteration,phase,op,routes,invalid_routes\n";
    int event,iteration,op,count,snapshots=0,invalidSnapshots=0;
    std::string stage,phase;
    while(input>>event>>stage>>iteration>>phase>>op>>count) {
      int invalid=0;
      for(int r=0;r<count;++r) {
        int length,node;
        if(!(input>>length) || length<2) throw std::runtime_error("Invalid replay route length");
        Route route(r,vehicle,instance);
        for(int p=0;p<length;++p) {
          if(!(input>>node)) throw std::runtime_error("Truncated replay route");
          if(p==0 || p+1==length) { if(node!=0) throw std::runtime_error("Invalid depot"); }
          else route.addNode(node);
        }
        route.evaluate();
        if(!route.isFeasible()) {
          ++invalid;
          details<<"event="<<event<<" stage="<<stage<<" iteration="<<iteration
                 <<" phase="<<phase<<" op="<<op<<" route="<<r<<'\n'<<route.toString()<<'\n';
        }
      }
      result<<event<<','<<stage<<','<<iteration<<','<<phase<<','<<op<<','<<count<<','<<invalid<<'\n';
      ++snapshots; invalidSnapshots+=invalid>0;
    }
    if(!input.eof()) throw std::runtime_error("Invalid replay input");
    std::cout<<"REPLAY snapshots="<<snapshots<<" with_infeasible_fresh_route="<<invalidSnapshots<<'\n';
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
