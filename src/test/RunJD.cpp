#include "alns/ALNSSolver.h"
#include "core/Customer.h"
#include "core/Depot.h"
#include "core/Station.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>

// Delivery-only projections; optionally average opposite distances, preserving times.
static std::shared_ptr<Instance> loadJD(const std::string& path, const std::string& demandMode,
                                        bool symmetricDistance) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("Cannot open " + path);
  std::map<std::string, double> params;
  std::vector<std::shared_ptr<Node>> nodes;
  std::vector<double> distances, times;
  std::string line;
  int section = 0;
  size_t arcCount = 0;
  double deliveryTotal = 0, pickupTotal = 0;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "NODE_SECTION") { section = 1; continue; }
    if (line == "DISTANCETIME_SECTION") {
      section = 2;
      const size_t n = nodes.size();
      distances.assign(n*n, std::numeric_limits<double>::quiet_NaN());
      times = distances;
      for (size_t i = 0; i < n; ++i) distances[i*n+i] = times[i*n+i] = 0;
      continue;
    }
    if (line == "DEPOT_SECTION") break;
    if (line.empty()) continue;
    if (section == 0) {
      auto colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        key.erase(key.find_last_not_of(" \t") + 1);
        std::istringstream value(line.substr(colon+1));
        double number;
        if (value >> number) params[key] = number;
      }
      continue;
    }
    if (line.rfind("ID,", 0) == 0) continue;
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream row(line);
    if (section == 1) {
      int id; char type; double x,y,d,p,ready,due,service;
      if (!(row >> id >> type >> x >> y >> d >> p >> ready >> due >> service))
        throw std::runtime_error("Malformed JD node");
      if (id != static_cast<int>(nodes.size())) throw std::runtime_error("Non-contiguous JD IDs");
      if (type == 'd') nodes.push_back(std::make_shared<Depot>(id,"D"+std::to_string(id),x,y,ready,due));
      else if (type == 'c') {
        deliveryTotal += d; pickupTotal += p;
        const double demand = demandMode == "mean" ? (d+p)/2.0 :
                              (demandMode == "sum" ? d+p : d);
        nodes.push_back(std::make_shared<Customer>(id,"C"+std::to_string(id),x,y,demand,ready,due,service));
      } else if (type == 'f') nodes.push_back(std::make_shared<Station>(id,"S"+std::to_string(id),x,y,ready,due,params.at("RECHARGING_RATE")));
      else throw std::runtime_error("Unknown JD node type");
    } else {
      int id,from,to; double d,t;
      if (!(row >> id >> from >> to >> d >> t)) throw std::runtime_error("Malformed JD arc");
      const size_t n = nodes.size();
      if (from < 0 || to < 0 || from >= static_cast<int>(n) || to >= static_cast<int>(n) || from == to)
        throw std::runtime_error("Invalid JD arc IDs");
      size_t k = from*n+to;
      if (std::isfinite(distances[k])) throw std::runtime_error("Duplicate JD arc");
      distances[k] = d; times[k] = t; ++arcCount;
    }
  }
  if (nodes.empty() || nodes[0]->getType() != NodeType::DEPOT ||
      nodes.size() != params.at("DIMENSION") || arcCount != nodes.size()*(nodes.size()-1))
    throw std::runtime_error("Incomplete JD instance");
  auto inst = std::make_shared<Instance>(nodes,params.at("CAPACITY"),params.at("ELECTRIC_POWER"),params.at("CONSUMPTION_RATE"),1.0);
  if (symmetricDistance) {
    const size_t n = nodes.size();
    for (size_t i = 0; i < n; ++i)
      for (size_t j = i+1; j < n; ++j) {
        const double mean = (distances[i*n+j] + distances[j*n+i]) / 2.0;
        distances[i*n+j] = distances[j*n+i] = mean;
      }
  }
  inst->setTravelMatrices(std::move(distances),std::move(times));
  std::cout << "Customers=" << inst->getCustomers().size() << " Stations=" << inst->getStations().size()
            << " Arcs=" << arcCount << " Delivery=" << deliveryTotal << " Pickup=" << pickupTotal
            << " ProjectedDemand=" << (demandMode == "mean" ? (deliveryTotal+pickupTotal)/2.0 :
                                      (demandMode == "sum" ? deliveryTotal+pickupTotal : deliveryTotal))
            << " Distance=" << (symmetricDistance?"symmetric_mean":"directed")
            << " Time=original_directed" << std::endl;
  return inst;
}

int main(int argc, char** argv) {
  try {
    if (argc < 3) throw std::runtime_error("Usage: RunJD input delivery|sum|mean [seconds=60] [seed=1] [directed|symmetric]");
    std::string mode = argv[2];
    if (mode != "delivery" && mode != "sum" && mode != "mean") throw std::runtime_error("Invalid mode");
    const std::string matrixMode = argc>5?argv[5]:"directed";
    if (matrixMode != "directed" && matrixMode != "symmetric")
      throw std::runtime_error("Invalid matrix mode");
    auto instance = loadJD(argv[1], mode, matrixMode == "symmetric");
    alns::ALNSConfig config;
    config.maxIterations=25000; config.segmentIterations=200;
    config.decayParameter=0.85; config.scoreDominating=40; config.scoreNonDominated=25;
    config.scoreDominated=10; config.scoreIdentical=15;
    config.minRemoval=0.15; config.maxRemoval=0.55;
    config.localSearchIntensity=25; config.useLocalSearch=true; config.useScatterSearch=false;
    config.startTemperature=200; config.coolingRate=0.9993; config.minTemperature=0.05; config.SAScale=50;
    config.hvStagnationLimit=20;
    const long long seconds=argc>3?std::stoll(argv[3]):60;
    if(seconds<=0) throw std::runtime_error("Seconds must be positive");
    config.maxTime=1000LL*seconds;
    config.randomSeed=argc>4?std::stoul(argv[4]):1;
    config.enableLogging=true;
    const std::string output="logs_jd/"+std::filesystem::path(argv[1]).stem().string()+"/"+mode+"_seed"+std::to_string(config.randomSeed)+(matrixMode=="symmetric"?"_symmetric":"");
    std::filesystem::create_directories(output);
    alns::ALNSSolver solver(instance,config,output,mode);
    auto start=std::chrono::steady_clock::now();
    auto front=solver.solve();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::ofstream summary(output+"/results.csv"), routes(output+"/routes.csv");
    summary << "mode,seed,elapsed_ms,solution,vehicles,distance,gini,max_time,feasible\n" << std::setprecision(15);
    routes << "solution,route,position,node\n";
    for (size_t i=0;i<front.size();++i) {
      front[i].evaluateRoutes();
      summary << mode << ',' << config.randomSeed << ',' << elapsed << ',' << i << ',' << front[i].getTotalVehicles() << ','
              << front[i].getTotalDistance() << ',' << front[i].getWorkloadGini() << ',' << front[i].getMaxTime() << ',' << front[i].isFeasible() << '\n';
      for (size_t r=0;r<front[i].getNumRoutes();++r) {
        const auto& seq=front[i].getRoutes()[r].getNodes();
        for(size_t p=0;p<seq.size();++p) routes << i << ',' << r << ',' << p << ',' << seq[p] << '\n';
      }
    }
    std::cout << "RESULT mode=" << mode << " front=" << front.size() << " elapsed_ms=" << elapsed << " output=" << output << std::endl;
    return front.empty()?2:0;
  } catch(const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
