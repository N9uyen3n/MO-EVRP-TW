# **A Hybrid Adaptive Large Neighborhood Search for the Multi-Objective Electric Vehicle Routing Problem with Time Windows**

**Author:** EVRP Research Group
**Date:** 2026-01-19

---

## Abstract

The Electric Vehicle Routing Problem with Time Windows (EVRPTW) is a complex combinatorial optimization problem with significant practical applications in logistics and transportation. This paper details a Hybrid Adaptive Large Neighborhood Search (ALNS) algorithm designed to solve the multi-objective variant of the EVRPTW. The primary objectives are to hierarchically minimize the number of vehicles used, and then to find a set of non-dominated (Pareto optimal) solutions considering three further objectives: minimizing total travel distance, minimizing workload variance among routes, and minimizing the makespan. The proposed method integrates a sophisticated ALNS framework with a Pareto archive, local search procedures, and a range of specialized destroy and repair operators. This report provides a comprehensive overview of the mathematical model, the algorithmic architecture, and key algorithmic components.

---

## 1. Introduction

The proliferation of electric vehicles (EVs) in commercial fleets presents new challenges and opportunities for vehicle routing optimization. The EVRPTW extends the classic Vehicle Routing Problem with Time Windows (VRPTW) by incorporating constraints unique to EVs, namely their limited battery capacity and the need for en-route recharging at stations. This adds significant complexity, as routes must be feasible not only in terms of time and vehicle capacity but also in terms of energy consumption.

Solving the EVRPTW often involves balancing multiple, often conflicting, objectives. For instance, minimizing the total distance might lead to longer workdays for some drivers, while strictly balancing workload might result in less efficient routes. This multi-objective nature makes finding a single "best" solution impractical. Instead, the goal is to identify the Pareto front: a set of solutions where no single objective can be improved without degrading at least one other objective.

This paper presents a solver for the multi-objective EVRPTW based on the Adaptive Large Neighborhood Search (ALNS) metaheuristic. ALNS is a powerful and flexible framework that has proven effective for a wide range of routing problems. Our implementation hybridizes ALNS with concepts from multi-objective optimization, such as a Pareto archive and crowding distance, to effectively explore the solution space and generate a diverse set of high-quality, non-dominated solutions.

---

## 2. Mathematical Model

The EVRPTW is formulated as a multi-objective optimization problem.

### 2.1. Sets and Parameters

-   **Sets:**
    -   $N$: The set of all nodes, $N = C \cup S \cup \{0\}$, where $C$ is the set of customers, $S$ is the set of charging stations, and $\{0\}$ is the depot.
    -   $V$: The set of available homogeneous vehicles.
-   **Parameters:**
    -   $d_{ij}, t_{ij}$: Distance and travel time between nodes $i$ and $j$.
    -   $q_i$: Demand of customer $i$. $q_0 = 0$, and $q_s = 0$ for any station $s \in S$.
    -   $[e_i, l_i]$: Time window at node $i$, with ready time $e_i$ and due date $l_i$.
    -   $s_i$: Service time required at customer $i$.
    -   $Q$: Maximum cargo capacity of a vehicle.
    -   $B$: Maximum battery capacity of a vehicle.
    -   $r$: Energy consumption rate per unit of distance.
    -   $g$: Battery charging rate at stations.

### 2.2. Decision Variables

-   $x_{ijk}$: A binary variable that is $1$ if vehicle $k \in V$ travels directly from node $i \in N$ to node $j \in N$, and $0$ otherwise.
-   $y_{ik}$: A binary variable that is $1$ if node $i \in N$ is visited by vehicle $k \in V$, and $0$ otherwise.
-   $A_{ik}$: The arrival time of vehicle $k$ at node $i$.
-   $b_{ik}$: The battery level of vehicle $k$ upon arrival at node $i$.
-   $u_{ik}$: The remaining load of vehicle $k$ upon arrival at node $i$.

### 2.3. Objective Functions

The optimization process is guided by a hierarchical, multi-objective approach.

**Primary Objective (Hierarchical):**
1.  **Minimize Total Vehicles ($f_{veh}$):**
    $$
    f_{veh} = \sum_{k \in V} y_{0k}
    $$

**Secondary Objectives (Pareto Optimization):**
If the number of vehicles is equal, the following objectives are optimized to find a set of Pareto optimal solutions:
2.  **Minimize Total Distance ($f_{dist}$):**
    $$
    f_{dist} = \sum_{i \in N} \sum_{j \in N} \sum_{k \in V} d_{ij} \cdot x_{ijk}
    $$
3.  **Minimize Workload Variance ($f_{workload}$):**
    $$
    f_{workload} = Var(T_k) = \frac{1}{|V_u|} \sum_{k \in V_u} (T_k - \mu_T)^2
    $$
    where $V_u$ is the set of used vehicles, $T_k$ is the total duration of route $k$, and $\mu_T$ is the mean route duration.
4.  **Minimize Makespan ($f_{makespan}$):**
    $$
    f_{makespan} = \max_{k \in V} (A_{0k, \text{end}})
    $$
    where $A_{0k, \text{end}}$ is the final arrival time of vehicle $k$ at the depot.

### 2.4. Constraints

1.  **Flow and Service Constraints:**
    -   Each customer must be served exactly once: $\sum_{k \in V} y_{ik} = 1, \forall i \in C$.
    -   Each vehicle used must start from and return to the depot: $\sum_{j \in N} x_{0jk} = y_{0k}, \forall k \in V$ and $\sum_{i \in N} x_{i0k} = y_{0k}, \forall k \in V$.
    -   Flow conservation at each node: $\sum_{i \in N} x_{ijk} = \sum_{j \in N} x_{jik} = y_{ik}, \forall i \in C \cup S, \forall k \in V$.

2.  **Capacity Constraint:**
    -   The vehicle's load must not be exceeded. If $x_{ijk} = 1$, then $u_{jk} = u_{ik} - q_j$. The constraint $0 \le u_{ik} \le Q$ must hold for all $i, k$.

3.  **Time Window Constraints:**
    -   Service must respect time windows: $e_i \le A_{ik} \le l_i, \forall i \in N, \forall k \in V$.
    -   The arrival time at node $j$ after departing from $i$ is $A_{jk} = D_{ik} + t_{ij}$, where $D_{ik}$ is the departure time from $i$. Departure time is calculated as $\max(A_{ik}, e_i) + \text{service\_time\_at\_i}$.

4.  **Energy Constraints:**
    -   The battery level must not be negative: $b_{ik} \ge 0, \forall i \in N, \forall k \in V$.
    -   The battery level cannot exceed the maximum capacity: $b_{ik} \le B$.
    -   Energy consumption between nodes: If $x_{ijk} = 1$, then $b_{jk} \le b'_{ik} - (d_{ij} \cdot r)$, where $b'_{ik}$ is the battery level after charging at node $i$ (if $i$ is a station).
    -   The heuristic nature of the ALNS solver ensures these constraints are satisfied through the `Route::evaluate` function rather than by being explicitly included in a mathematical programming solver.

---

## 3. Proposed Method: Hybrid ALNS Framework

The solver is built around an Adaptive Large Neighborhood Search framework, which iteratively destroys a part of the current solution and repairs it in search of better alternatives.

### 3.1. Main ALNS Algorithm

The main ALNS loop operates on a current solution $s_{current}$, a global best solution $s_{best}$, and a Pareto archive $P$. The general procedure is outlined in the pseudocode below. A key performance optimization is that the `copy(s_{current})` operation in line 10 is not a full deep copy. It is a fast, shallow copy managed by an object pool and a Copy-On-Write mechanism, avoiding expensive memory operations in every iteration.

**Algorithm 1: Main ALNS Loop**
```pseudocode
1.  Require: Initial solution s_init
2.  Ensure: Pareto front P
3.
4.  s_current <- s_init
5.  s_best <- s_init
6.  P <- {s_init}
7.  T <- T_start
8.  Initialize operator weights w_d, w_r
9.
10. for i = 1 to max_iterations do
11.     Select destroy operator op_d from D with probability based on w_d
12.     Select repair operator op_r from R with probability based on w_r
13.
14.     s_new <- copy(s_current)
15.     unserved_customers <- op_d(s_new)
16.     op_r(s_new, unserved_customers)
17.
18.     if use_local_search then
19.         LocalSearch(s_new)
20.     end if
21.
22.     if cost(s_new) < cost(s_best) then
23.         s_best <- s_new
24.     end if
25.
26.     // Simulated Annealing Acceptance Criterion
27.     if cost(s_new) < cost(s_current) or random(0,1) < exp(-(cost(s_new) - cost(s_current)) / T) then
28.         s_current <- s_new
29.     end if
30.
31.     T <- T * cooling_rate
32.
33.     // Update Pareto Archive and Operator Weights
34.     result <- P.tryAdd(s_new)
35.     UpdateWeights(op_d, op_r, result)
36. end for
37.
38. return P
```

### 3.2. The Pareto Archive

The `ParetoArchive` is a key component for multi-objective optimization.
-   **Function:** It stores a bounded-size set of all non-dominated solutions discovered during the search.
-   **Acceptance:** When a new solution is offered, the archive checks if it is dominated by, dominates, or is non-dominated with respect to the existing solutions. Dominated solutions are rejected. If the new solution dominates existing ones, they are removed.
-   **Pruning:** If the archive exceeds its maximum size, a pruning mechanism based on **Crowding Distance** (from the NSGA-II algorithm) is invoked. This method removes solutions from the densest regions of the objective space, promoting diversity in the final Pareto front.

### 3.3. Neighborhood Operators

A variety of destroy and repair operators are used to generate diverse neighborhoods. Their selection probability is adapted based on their historical performance.

#### 3.3.1. Destroy Operators

Destroy operators are responsible for removing a number of customers from the current solution, creating a partial solution that the repair operators will then attempt to complete.

##### 1. Worst Distance Node Removal
This operator removes customers that are most costly in terms of travel distance. It identifies customers who, if removed, would result in the largest reduction of their route's length. This targets customers who are geographically far from their neighbors in a route.

**Algorithm 2: Worst Distance Node Removal**
```pseudocode
1.  Require: current_solution, num_to_remove
2.  Ensure: list_of_removed_customers
3.
4.  candidates <- []
5.  for each route in current_solution do
6.      for each customer c at position i in route do
7.          prev_node <- node at i-1
8.          next_node <- node at i+1
9.          distance_with_c <- dist(prev_node, c) + dist(c, next_node)
10.         distance_without_c <- dist(prev_node, next_node)
11.         cost_saving <- distance_with_c - distance_without_c
12.         add {c, cost_saving} to candidates
13.     end for
14. end for
15.
16. Sort candidates in descending order of cost_saving
17.
18. removed_customers <- []
19. while |removed_customers| < num_to_remove and |candidates| > 0 do
20.     // Select a candidate using a randomized, biased method (Shaw selection)
21.     r <- random(0, 1)
22.     idx <- floor(|candidates| * r^determinism_param)
23.     selected_customer <- candidates[idx]
24.     add selected_customer to removed_customers
25.     remove selected_customer from candidates
26. end while
27.
28. Remove all customers in removed_customers from the solution's routes
29. return removed_customers
```

##### 2. Shaw Destroy (Relatedness Removal)
This operator removes customers that are similar or "related" to each other. It starts with a random customer and iteratively removes other customers that are most related to it based on a weighted formula of proximity, time window similarity, and demand similarity.

**Algorithm 3: Shaw Destroy**
```pseudocode
1.  Require: current_solution, num_to_remove
2.  Ensure: list_of_removed_customers
3.
4.  all_customers <- get all customers from current_solution
5.  removed_customers <- []
6.
7.  Select a random customer c_seed from all_customers
8.  Add c_seed to removed_customers and remove from all_customers
9.
10. while |removed_customers| < num_to_remove and |all_customers| > 0 do
11.     Select a random customer c_ref from removed_customers
12.     Find customer c_best in all_customers that has the minimum relatedness_score to c_ref
13.
14.     // relatedness_score(c1, c2) = w1*dist(c1,c2) + w2*|c1.ready - c2.ready| + w3*|c1.demand - c2.demand|
15.
16.     Add c_best to removed_customers
17.     Remove c_best from all_customers
18. end while
19.
20. Remove all customers in removed_customers from the solution's routes
21. return removed_customers
```

##### 3. Pareto Focus Destroy
This operator introduces multi-objective awareness into the destroy phase. It randomly selects one of the secondary objectives (e.g., distance, time, workload) and removes customers that are the worst contributors to that specific objective.

**Algorithm 4: Pareto Focus Destroy**
```pseudocode
1.  Require: current_solution, num_to_remove
2.  Ensure: list_of_removed_customers
3.
4.  focus_objective <- randomly select from {DISTANCE, TIME, WORKLOAD}
5.  candidates <- []
6.
7.  for each route in current_solution do
8.      for each customer c in route do
9.          if focus_objective is DISTANCE then
10.             cost <- calculate distance saving for removing c
11.         else if focus_objective is TIME then
12.             cost <- calculate time saving for removing c
13.         else if focus_objective is WORKLOAD then
14.             cost <- |route.duration - mean_route_duration|
15.         end if
16.         add {c, cost} to candidates
17.     end for
18. end for
19.
20. Sort candidates in descending order of cost
21. Select and return top num_to_remove customers using randomized selection (like WorstDistance)
```

##### 4. High Energy Node Removal
Similar to Worst Distance, but focuses on energy. It removes customers whose removal would lead to the greatest energy savings.

**Algorithm 5: High Energy Node Removal**
```pseudocode
1.  Require: current_solution, num_to_remove
2.  Ensure: list_of_removed_customers
3.  // Identical to WorstDistanceNodeRemoval, but cost_saving is calculated based on energy
4.  cost_saving <- (energy_to(c) + energy_from(c)) - energy_bypassing(c)
5.  // ... rest of the logic is the same
```

##### 5. Fewest Customers Route Removal
Removes an entire route. It targets the route that serves the fewest customers.

**Algorithm 6: Fewest Customers Route Removal**
```pseudocode
1.  Require: current_solution
2.  Ensure: list_of_removed_customers
3.
4.  Find route R with the minimum number of customers (> 0)
5.  if R is found then
6.      removed_customers <- all customers in R
7.      Remove route R from current_solution
8.  end if
9.  return removed_customers
```

##### 6. Longest Wait Time Route Removal
Removes the entire route that has the highest total waiting time.

**Algorithm 7: Longest Wait Time Route Removal**
```pseudocode
1.  Require: current_solution
2.  Ensure: list_of_removed_customers
3.
4.  Find route R with the maximum total_wait_time
5.  if R is found and wait_time > 0 then
6.      removed_customers <- all customers in R
7.      Remove route R from current_solution
8.  end if
9.  return removed_customers
```

#### 3.3.2. Repair Operators

Repair operators take the partial solution from a destroy operator and re-insert the unserved customers. All repair operators employ a **three-tier filtering strategy**.

##### 1. Optimized Greedy Distance Insertion
Finds the best insertion position by minimizing the increase in travel distance.

**Algorithm 8: Optimized Greedy Insertion**
```pseudocode
1.  Require: partial_solution, unserved_customers
2.  Ensure: (none; solution is modified in-place)
3.
4.  for each customer c in unserved_customers do
5.      candidate_insertions <- []
6.
7.      // Tier 3 (Bounding) & Tier 2 (Fast Approx. Check)
8.      for each route R in partial_solution do
9.          if not R.quickCapacityCheck(c) then continue end if
10.         for each position p in R do
11.             // Tier 3: O(1) check for obvious infeasibility
12.             if not R.canPossiblyInsert(c, p) then continue end if
13.
14.             // Tier 2: O(k) check with forward simulation
15.             fast_result <- R.fastForwardCheck(c, p)
16.             if fast_result.isFeasible then
17.                 add {route:R, pos:p, cost:fast_result.cost} to candidate_insertions
18.             end if
19.         end for
20.     end for
21.
22.     Sort candidate_insertions by estimated_cost
23.
24.     // Tier 1 (Exact Verification)
25.     best_insertion <- {cost: infinity}
26.     for i = 1 to min(K, |candidate_insertions|) do
27.         candidate <- candidate_insertions[i]
28.         exact_result <- candidate.route.checkInsertionCost(c, candidate.pos)
29.         if exact_result.isFeasible and exact_result.cost < best_insertion.cost then
30.             best_insertion <- {..., cost: exact_result.cost, pos: candidate.pos, route: candidate.route}
31.         end if
32.     end for
33.
34.     if best_insertion.cost < infinity then
35.         Insert c into best_insertion.route at best_insertion.position
36.     else
37.         Create a new route for c
38.     end if
39. end for
```

##### 2. Greedy Time Insertion
Objective is to minimize the increase in total route time (travel time + waiting time).

**Algorithm 9: Greedy Time Insertion**
```pseudocode
1.  Require: partial_solution, unserved_customers
2.  // Identical to GreedyDistanceInsertion, but the cost is calculated differently:
3.  cost <- insertion_result.deltaWaitTime + insertion_result.deltaTravelTime
4.  // ... rest of the logic is the same
```

##### 3. Regret-k Repair
Prioritizes inserting "difficult" customers first. For each customer, it calculates a "regret" value, which is the cost difference between their best insertion and their $k$-th best insertion.

**Algorithm 10: Regret-k Repair**
```pseudocode
1.  Require: partial_solution, unserved_customers, k
2.  Ensure: (none; solution is modified in-place)
3.
4.  while unserved_customers is not empty do
5.      best_regret_customer <- null
6.      max_regret <- -infinity
7.
8.      for each customer c in unserved_customers do
9.          Find the k best insertion positions for c, store costs [cost_1, cost_2, ..., cost_k]
10.         if no insertion is possible then continue end if
11.
12.         regret_value <- 0
13.         for i from 2 to k do
14.             regret_value <- regret_value + (cost_i - cost_1)
15.         end for
16.
17.         if regret_value > max_regret then
18.             max_regret <- regret_value
19.             best_regret_customer <- c
20.         end if
21.     end for
22.
23.     if best_regret_customer is not null then
24.         Insert best_regret_customer at its best position (cost_1)
25.         Remove it from unserved_customers
26.     else
27.         break // Handle case where no customer can be inserted
28.     end if
29. end while
```

##### 4. Pareto Focus Repair
Randomly selects a secondary objective and performs greedy insertion based on that objective.

**Algorithm 11: Pareto Focus Repair**
```pseudocode
1.  Require: partial_solution, unserved_customers
2.  Ensure: (none; solution is modified in-place)
3.
4.  focus_objective <- randomly select from {DISTANCE, TIME, WORKLOAD}
5.
6.  // Logic is identical to GreedyDistanceInsertion, but cost function changes:
7.  if focus_objective is DISTANCE then
8.      cost <- insertion_result.deltaDistance
9.  else if focus_objective is TIME then
10.     cost <- insertion_result.deltaWaitTime + insertion_result.deltaTravelTime
11. else if focus_objective is WORKLOAD then
12.     new_route_duration <- route.duration + estimated_delta_time
13.     cost <- |new_route_duration - mean_route_duration|
14. end if
15. // ... rest of the logic is the same
```

##### 5. Greedy Station Repair
Attempts to insert a customer along with a charging station visit if direct insertion is infeasible due to energy.

**Algorithm 12: Greedy Station Repair**
```pseudocode
1.  Require: partial_solution, unserved_customers
2.  Ensure: (none; solution is modified in-place)
3.
4.  for each customer c in unserved_customers do
5.      best_insertion <- {cost: infinity}
6.      nearest_station <- find nearest station to c
7.
8.      for each route R and position p do
9.          // Try inserting customer c alone
10.         res_cust_only <- R.checkInsertionCost(c, p)
11.         if res_cust_only.isFeasible and res_cust_only.deltaDistance < best_insertion.cost then
12.             best_insertion <- {..., type: CUSTOMER_ONLY}
13.         else
14.             // Try inserting station S then customer c
15.             res_with_station <- R.checkInsertionCostWithStation(c, p, nearest_station)
16.             if res_with_station.isFeasible and res_with_station.deltaDistance < best_insertion.cost then
17.                 best_insertion <- {..., type: WITH_STATION}
18.             end if
19.         end if
20.     end for
21.
22.     if best_insertion.cost < infinity then
23.         if best_insertion.type is CUSTOMER_ONLY then
24.             Insert c at best position
25.         else // type is WITH_STATION
26.             Insert nearest_station then c at best position
27.         end if
28.     else
29.         Create a new route for c
30.     end if
31. end for
```

### 3.4. Local Search

After a repair operation, a local search phase is triggered to perform fine-grained improvements. The Local Search module has been extensively optimized to reduce computational overhead while maintaining solution quality. This section was identified as the primary performance bottleneck (consuming 70-96% of total execution time), and several advanced techniques have been implemented to achieve a **2.5-3.3x speedup**.

#### 3.4.1. Local Search Framework

The main Local Search loop employs an **Adaptive Neighborhood Sizing** strategy that dynamically adjusts search intensity based on progress.

**Algorithm 13: Local Search Main Loop**
```pseudocode
1.  Require: current_solution
2.  Ensure: improved_solution
3.
4.  maxIterations <- 25
5.  noImprovementCount <- 0
6.  maxNodesToCheck <- 5 // Adaptive parameter
7.  maxSwapAttempts <- 3 // Adaptive parameter
8.
9.  for iter = 1 to maxIterations do
10.     improved <- false
11.
12.     // 1. Distance Optimization (Adaptive Operator Selection)
13.     if runDistanceOptimization(solution) then
14.         improved <- true
15.     end if
16.
17.     // 2. Charging Optimization (every 2nd iteration)
18.     if iter mod 2 = 0 then
19.         if runChargingOptimization(solution) then
20.             improved <- true
21.         end if
22.     end if
23.
24.     // 3. Vehicle Reduction (every 5th iteration or when stuck)
25.     if (iter mod 5 = 0 or not improved) and |routes| > 1 then
26.         if runVehicleReduction(solution) then
27.             improved <- true
28.             noImprovementCount <- 0
29.         end if
30.     end if
31.
32.     // 4. Adaptive Neighborhood Sizing
33.     if improved then
34.         maxNodesToCheck <- max(2, maxNodesToCheck - 1) // Intensification
35.         maxSwapAttempts <- max(1, maxSwapAttempts - 1)
36.     else
37.         noImprovementCount <- noImprovementCount + 1
38.         maxNodesToCheck <- min(10, maxNodesToCheck + 1) // Diversification
39.         maxSwapAttempts <- min(5, maxSwapAttempts + 1)
40.         if noImprovementCount > 2 then
41.             break // Early termination
42.         end if
43.     end if
44. end for
```

#### 3.4.2. Preprocessing Techniques

Before the main search loop, two preprocessing steps are performed to enable efficient filtering:

##### K-Nearest Neighbors (KNN) Preprocessing
For each customer, we precompute the $K=15$ nearest neighbors based on Euclidean distance. This cache is used to limit insertion position checks.

**Algorithm 14: KNN Preprocessing**
```pseudocode
1.  Require: instance (all customers)
2.  Ensure: knnCache
3.
4.  knnCache <- {}
5.  for each customer c_i in instance.getCustomers() do
6.      distances <- []
7.      for each customer c_j in instance.getCustomers() do
8.          if c_i != c_j then
9.              add {c_j, dist(c_i, c_j)} to distances
10.         end if
11.     end for
12.     Sort distances by distance
13.     knnCache[c_i.id] <- first K customers from distances
14. end for
15. return knnCache
```

##### Granular Neighborhoods Preprocessing
We compute a dynamic distance threshold $\theta$ based on the average distance between all customer pairs. Moves between customers/routes farther than $\theta$ are filtered out.

**Algorithm 15: Granular Neighborhoods Preprocessing**
```pseudocode
1.  Require: instance (all customers)
2.  Ensure: distanceThreshold
3.
4.  totalDistance <- 0
5.  count <- 0
6.  for each customer c_i in instance.getCustomers() do
7.      for each customer c_j in instance.getCustomers() do
8.          if c_i != c_j then
9.              totalDistance <- totalDistance + dist(c_i, c_j)
10.             count <- count + 1
11.         end if
12.     end for
13. end for
14. avgDistance <- totalDistance / count
15. distanceThreshold <- 1.5 * avgDistance // beta = 1.5
16. return distanceThreshold
```

#### 3.4.3. Distance Optimization Operators

The distance optimization phase uses **Operator Scoring & Adaptive Selection** via Roulette Wheel Selection.

**Algorithm 16: runDistanceOptimization (Adaptive Selection)**
```pseudocode
1.  Require: current_solution
2.  Ensure: improved (boolean)
3.
4.  // Calculate operator scores based on historical performance
5.  scoreRelocate <- relocateStats.getScore()
6.  scoreSwap <- swapStats.getScore()
7.  scoreOrOpt <- orOptStats.getScore()
8.  scoreTwoOpt <- twoOptStats.getScore()
9.  totalScore <- scoreRelocate + scoreSwap + scoreOrOpt + scoreTwoOpt
10.
11. // Roulette Wheel Selection
12. pick <- random(0, 1) * totalScore
13.
14. if pick < scoreRelocate then
15.     return searchRelocate(solution)
16. else if pick < scoreRelocate + scoreSwap then
17.     return searchSwap(solution)
18. else if pick < scoreRelocate + scoreSwap + scoreOrOpt then
19.     return searchOrOpt(solution)
20. else
21.     return searchTwoOpt(solution)
22. end if
```

##### searchRelocate (Optimized with KNN + Granular)
Attempts to move a single customer to a new position using multiple filtering layers.

**Algorithm 17: Optimized searchRelocate**
```pseudocode
1.  Require: current_solution, maxNodesToCheck
2.  Ensure: improved (boolean)
3.
4.  centroids <- computeAllCentroids(solution)
5.
6.  for each route r_1 in solution do
7.      // Adaptive Sizing: Rank and limit nodes to check
8.      rankedNodes <- rankNodesByRemovalSavings(r_1)
9.      nodesToCheck <- min(maxNodesToCheck, |rankedNodes|)
10.
11.     for i = 1 to nodesToCheck do
12.         nodeId <- rankedNodes[i]
13.
14.         // Granular Filter: Only check close routes
15.         for each route r_2 in solution do
16.             if not areRoutesClose(centroids[r_1], centroids[r_2], distanceThreshold) then
17.                 continue
18.             end if
19.
20.             // KNN Filter: Find best K=3 insertion positions
21.             candidatePositions <- findBestInsertionPositions_KNN(r_2, nodeId, 3)
22.
23.             for each position j in candidatePositions do
24.                 // Tier 3 Check + Evaluate
25.                 if not r_2.canPossiblyInsert(nodeId, j) then
26.                     continue
27.                 end if
28.
29.                 // Static Move Descriptor (SMD): Reuse memory
30.                 activeMove.reset()
31.                 activeMove.type <- RELOCATE
32.                 activeMove.routeIdx1 <- r_1, activeMove.nodeIdx1 <- i
33.                 activeMove.routeIdx2 <- r_2, activeMove.nodeIdx2 <- j
34.
35.                 evaluateMove(solution, activeMove)
36.                 if activeMove.isFeasible and activeMove.objectiveDelta < 0 then
37.                     applyMove(solution, activeMove)
38.                     return true // First Improvement
39.                 end if
40.             end for
41.         end for
42.     end for
43. end for
44. return false
```

##### searchSwap (Optimized with Granular)
Swaps two customers between routes (intra-route or inter-route).

**Algorithm 18: Optimized searchSwap**
```pseudocode
1.  Require: current_solution, maxSwapAttempts
2.  Ensure: improved (boolean)
3.
4.  centroids <- computeAllCentroids(solution)
5.  attempts <- 0
6.
7.  for each route r_1 in solution do
8.      for each customer c_1 at position i in r_1 do
9.          for each route r_2 in solution do
10.             // Granular Filter (Route-level)
11.             if r_1 != r_2 and not areRoutesClose(centroids[r_1], centroids[r_2], distanceThreshold) then
12.                 continue
13.             end if
14.
15.             for each customer c_2 at position j in r_2 do
16.                 if r_1 = r_2 and i >= j then
17.                     continue
18.                 end if
19.
20.                 // Granular Filter (Node-level for inter-route)
21.                 if r_1 != r_2 and dist(c_1, c_2) >= distanceThreshold then
22.                     continue
23.                 end if
24.
25.                 // Tier 3 Feasibility Check
26.                 if not r_1.canPossiblyInsert(c_2, i) or not r_2.canPossiblyInsert(c_1, j) then
27.                     continue
28.                 end if
29.
30.                 // SMD + Evaluate
31.                 activeMove.reset()
32.                 activeMove.type <- SWAP
33.                 evaluateMove(solution, activeMove)
34.                 if activeMove.isFeasible and activeMove.objectiveDelta < 0 then
35.                     applyMove(solution, activeMove)
36.                     return true // First Improvement
37.                 end if
38.
39.                 attempts <- attempts + 1
40.                 if attempts >= maxSwapAttempts then
41.                     return false
42.                 end if
43.             end for
44.         end for
45.     end for
46. end for
47. return false
```

##### searchOrOpt (NEW)
Moves a segment of 2 consecutive customers to a new position. This operator is called every 3 iterations to reduce overhead.

**Algorithm 19: Optimized searchOrOpt**
```pseudocode
1.  Require: current_solution
2.  Ensure: improved (boolean)
3.
4.  centroids <- computeAllCentroids(solution)
5.
6.  for each route r_1 in solution do
7.      // Limit segments checked (first 3 only)
8.      maxSegments <- min(3, |r_1.customers| - 1)
9.      for segStart = 1 to maxSegments do
10.         segment <- [r_1.nodes[segStart], r_1.nodes[segStart+1]]
11.
12.         for each route r_2 in solution do
13.             // Granular Filter
14.             if not areRoutesClose(centroids[r_1], centroids[r_2], distanceThreshold) then
15.                 continue
16.             end if
17.
18.             // KNN Filter: Find best K=3 insertion positions
19.             candidatePositions <- findBestInsertionPositions_KNN(r_2, segment[0], 3)
20.
21.             for each position j in candidatePositions do
22.                 // SMD + Evaluate
23.                 activeMove.reset()
24.                 activeMove.type <- OR_OPT
25.                 activeMove.segmentLength <- 2
26.                 evaluateMove(solution, activeMove)
27.                 if activeMove.isFeasible and activeMove.objectiveDelta < 0 then
28.                     applyMove(solution, activeMove)
29.                     return true // First Improvement
30.                 end if
31.             end for
32.         end for
33.     end for
34. end for
35. return false
```

##### searchTwoOpt
For each route, reverses segments to remove edge crossings. Uses **Best Improvement** strategy.

**Algorithm 20: searchTwoOpt**
```pseudocode
1.  Require: current_solution
2.  Ensure: improved (boolean)
3.
4.  bestMove <- {cost: 0}
5.  for each route R in solution do
6.      for i = 1 to |R.nodes| - 2 do
7.          for j = i + 1 to |R.nodes| - 1 do
8.              activeMove.reset()
9.              activeMove.type <- INTRA_TWO_OPT
10.             activeMove.routeIdx1 <- R, activeMove.nodeIdx1 <- i, activeMove.nodeIdx2 <- j
11.             evaluateMove(solution, activeMove)
12.             if activeMove.objectiveDelta < bestMove.cost then
13.                 bestMove <- activeMove
14.             end if
15.         end for
16.     end for
17. end for
18.
19. if bestMove.cost < 0 then
20.     applyMove(solution, bestMove)
21.     return true
22. end if
23. return false
```

#### 3.4.4. Vehicle Reduction Phase

This phase is critical for achieving the primary objective of minimizing the number of vehicles. It employs two aggressive strategies to escape local optima where the number of vehicles is stagnant.

##### 1. Smart Multi-Route Merge (Pool & Reconstruct)
Instead of simple pairwise merging, this strategy uses a "Pool & Reconstruct" approach. It identifies two routes that are spatially close (based on centroid distance) and have a combined size within a manageable limit (e.g., < 35 customers).
-   **Pooling:** All customers from both routes are removed and placed into a common pool.
-   **Reconstruction:** The solver attempts to rebuild two new routes from this pool. Crucially, if the reconstruction fails due to energy constraints, the algorithm employs **Station-Assisted Reconstruction**, proactively inserting charging stations to make the merge feasible.
-   **Acceptance:** If the solver can successfully service all combined customers using fewer vehicles (or significantly less distance), the merge is accepted.

##### 2. Smallest Route Elimination
To force a reduction in fleet size, this strategy targets the "weakest" route (the one with the fewest customers).
-   **Elimination:** The algorithm dissolves the smallest route and attempts to distribute its customers into the remaining routes.
-   **Station-Assisted Insertion:** If a customer cannot be inserted into any existing route due to energy or time limits, the solver attempts to insert the customer *along with a charging station*. This allows customers from the eliminated route to be absorbed into other routes even if they are far or energy-intensive.

**Algorithm 21: Vehicle Reduction Phase**
```pseudocode
1.  Require: current_solution
2.  Ensure: improved (boolean)
3.
4.  // Strategy 1: Smart Multi-Route Merge
5.  centroids <- computeAllCentroids(solution)
6.  for each pair of routes (r1, r2) do
7.      if areRoutesClose(centroids[r1], centroids[r2]) and (size(r1) + size(r2) < 35) then
8.          pool <- customers(r1) + customers(r2)
9.          new_routes <- reconstructRoutes(pool) // Try to build fewer/better routes
10.         
11.         // If reconstruction fails, try adding stations
12.         if reconstruction_failed then
13.             new_routes <- reconstructWithStations(pool)
14.         end if
15.
16.         if isValid(new_routes) and cost(new_routes) < cost({r1, r2}) then
17.             replace {r1, r2} with new_routes in solution
18.             return true
19.         end if
20.     end if
21. end for
22.
23. // Strategy 2: Smallest Route Elimination
24. target_route <- findSmallestRoute(solution)
25. if size(target_route) < 15 then
26.     orphaned_customers <- customers(target_route)
27.     temp_solution <- solution.remove(target_route)
28.     
29.     for each customer c in orphaned_customers do
30.         inserted <- false
31.         // Try standard insertion
32.         insertion <- findBestInsertion(temp_solution, c)
33.         if insertion.isFeasible then
34.             apply(insertion)
35.             inserted <- true
36.         else
37.             // Try station-assisted insertion
38.             insertion <- findBestInsertionWithStation(temp_solution, c)
39.             if insertion.isFeasible then
40.                 apply(insertion)
41.                 inserted <- true
42.             end if
43.         end if
44.         
45.         if not inserted then return false end if // Elimination failed
46.     end for
47.     
48.     solution <- temp_solution
49.     return true // Success: One vehicle removed
50. end if
51.
52. return false
```

#### 3.4.5. Performance Optimizations Summary

The Local Search module incorporates the following key optimizations:

-   **K-Nearest Neighbors (KNN):** Reduces insertion positions checked by 80-95%.
-   **Granular Neighborhoods:** Filters 70-90% of moves between distant customers/routes.
-   **Adaptive Neighborhood Sizing:** Dynamically adjusts search space (2-10 nodes) based on progress, achieving 20-30% faster convergence.
-   **Operator Scoring & Adaptive Selection:** Prioritizes effective operators using Roulette Wheel Selection based on historical success rates.
-   **Static Move Descriptor (SMD):** Reuses a single `activeMove` object across all operators, eliminating millions of memory allocations.
-   **Tiered Filtering:** Three-tier feasibility checks (Tier 1: capacity, Tier 2: time windows, Tier 3: full evaluation) filter 60-80% of infeasible moves early.
-   **First Improvement:** Most operators (Relocate, Swap, Or-Opt) use first improvement to terminate early.
-   **Early Termination:** Main loop stops after 2 iterations without improvement.

These optimizations collectively reduce the Local Search time from 96% to approximately 70% of total execution time, achieving an overall speedup of **2.5-3.3x** while maintaining solution quality.

---

## 4. Core Algorithmic Components

### 4.1. Route Evaluation and Feasibility

A critical component of the solver is the `evaluate` function, which calculates the objective costs of a single route and verifies its feasibility regarding time, capacity, and energy constraints. This is achieved via a two-pass mechanism.

**Algorithm 22: Route Evaluation**
```pseudocode
1.  Require: A route (sequence of nodes)
2.  Ensure: Feasibility status and calculated costs (distance, time, etc.)
3.
4.  // --- Pass 1: Backward Pass (Calculate minimum required energy) ---
5.  Initialize min_battery_req at last depot to 0.
6.  for i = (n-1) down to 0 do
7.      node_curr <- route[i]
8.      node_next <- route[i+1]
9.      consumption <- energy to travel from node_curr to node_next
10.     min_battery_req[i] <- min_battery_req[i+1] + consumption
11.     if node_curr is a station then
12.         min_battery_req[i] <- max(0, min_battery_req[i] - max_charge_at_station)
13.     end if
14.     if min_battery_req[i] > vehicle_battery_capacity then
15.         Mark route as INFEASIBLE and exit
16.     end if
17. end for
18.
19. // --- Pass 2: Forward Pass (Simulate route and calculate costs) ---
20. Initialize time, load, and battery at first depot.
21. for i = 0 to (n-1) do
22.     node_curr <- route[i]
23.     node_next <- route[i+1]
24.
25.     // Update Time
26.     arrival_time[i+1] <- departure_time[i] + travel_time(i, i+1)
27.     if arrival_time[i+1] > due_date[i+1] then
28.         Mark route as INFEASIBLE
29.     end if
30.     wait_time[i+1] <- max(0, ready_time[i+1] - arrival_time[i+1])
31.
32.     // Update Battery and Charge
33.     battery[i+1] <- battery[i] - energy_consumption(i, i+1)
34.     if battery[i+1] < 0 then
35.         Mark route as INFEASIBLE
36.     end if
37.     if node_next is a station then
38.         charge_needed <- min_battery_req[i+1] - battery[i+1]
39.         charge_amount <- max(0, charge_needed)
40.         battery[i+1] <- battery[i+1] + charge_amount
41.         charge_time <- charge_amount / charge_rate
42.     end if
43.
44.     // Update Load
45.     load[i+1] <- load[i] - demand[i+1]
46.     if load[i+1] < 0 then
47.         Mark route as INFEASIBLE
48.     end if
49.
50.     departure_time[i+1] <- arrival_time[i+1] + wait_time[i+1] + service_time[i+1] + charge_time
51. end for
52.
53. Aggregate total distance, time, etc. from the simulation.
```

This two-pass approach guarantees that for a fixed sequence of nodes, the feasibility is correctly assessed and the optimal charging decisions are made to minimize waiting time at stations.

---

## 5. Conclusion

This paper has described a C++ implementation of a Hybrid Adaptive Large Neighborhood Search algorithm for the Multi-Objective EVRPTW. The methodology combines a powerful metaheuristic with a Pareto archive to effectively handle conflicting objectives, including vehicle count, distance, workload balance, and makespan. The architecture is modular, allowing for the easy addition of new operators, and incorporates critical logic for ensuring time, capacity, and energy feasibility. Future work will involve benchmarking the solver against standard EVRPTW instances and further refining the operators and local search procedures for enhanced performance.
