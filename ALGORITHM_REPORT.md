# A Hybrid Adaptive Large Neighborhood Search for the Multi-Objective Electric Vehicle Routing Problem with Time Windows

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

*   **Sets:**
    *   `N`: The set of all nodes, `N = C ∪ S ∪ {0}`, where `C` is the set of customers, `S` is the set of charging stations, and `{0}` is the depot.
    *   `V`: The set of available homogeneous vehicles.
*   **Parameters:**
    *   `d_ij`, `t_ij`: Distance and travel time between nodes `i` and `j`.
    *   `q_i`: Demand of customer `i`. `q_0 = 0`, and `q_s = 0` for any station `s ∈ S`.
    *   `[e_i, l_i]`: Time window at node `i`, with ready time `e_i` and due date `l_i`.
    *   `s_i`: Service time required at customer `i`.
    *   `Q`: Maximum cargo capacity of a vehicle.
    *   `B`: Maximum battery capacity of a vehicle.
    *   `r`: Energy consumption rate per unit of distance.
    *   `g`: Battery charging rate at stations.

### 2.2. Decision Variables

*   `x_ijk`: A binary variable that is `1` if vehicle `k ∈ V` travels directly from node `i ∈ N` to node `j ∈ N`, and `0` otherwise.
*   `y_ik`: A binary variable that is `1` if node `i ∈ N` is visited by vehicle `k ∈ V`, and `0` otherwise.
*   `A_ik`: The arrival time of vehicle `k` at node `i`.
*   `b_ik`: The battery level of vehicle `k` upon arrival at node `i`.
*   `u_ik`: The remaining load of vehicle `k` upon arrival at node `i`.

### 2.3. Objective Functions

The optimization process is guided by a hierarchical, multi-objective approach.

**Primary Objective (Hierarchical):**
1.  **Minimize Total Vehicles (`f_veh`):**
    `f_veh = ∑_{k ∈ V} y_0k`

**Secondary Objectives (Pareto Optimization):**
If the number of vehicles is equal, the following objectives are optimized to find a set of Pareto optimal solutions:
2.  **Minimize Total Distance (`f_dist`):**
    `f_dist = ∑_{i ∈ N} ∑_{j ∈ N} ∑_{k ∈ V} d_ij * x_ijk`
3.  **Minimize Workload Variance (`f_workload`):**
    `f_workload = Var(T_k) = (1/|V_u|) * ∑_{k ∈ V_u} (T_k - μ_T)²`
    where `V_u` is the set of used vehicles, `T_k` is the total duration of route `k`, and `μ_T` is the mean route duration.
4.  **Minimize Makespan (`f_makespan`):**
    `f_makespan = max_{k ∈ V} (A_{0k, end})`
    where `A_{0k, end}` is the final arrival time of vehicle `k` at the depot.

### 2.4. Constraints

1.  **Flow and Service Constraints:**
    *   Each customer must be served exactly once: `∑_{k ∈ V} y_ik = 1, ∀i ∈ C`.
    *   Each vehicle used must start from and return to the depot: `∑_{j ∈ N} x_{0jk} = y_0k, ∀k ∈ V` and `∑_{i ∈ N} x_{i0k} = y_0k, ∀k ∈ V`.
    *   Flow conservation at each node: `∑_{i ∈ N} x_{ijk} = ∑_{j ∈ N} x_{jik} = y_ik, ∀i ∈ C ∪ S, ∀k ∈ V`.

2.  **Capacity Constraint:**
    *   The vehicle's load must not be exceeded. If `x_ijk = 1`, then `u_jk = u_ik - q_j`. The constraint `0 ≤ u_ik ≤ Q` must hold for all `i, k`.

3.  **Time Window Constraints:**
    *   Service must respect time windows: `e_i ≤ A_ik ≤ l_i, ∀i ∈ N, ∀k ∈ V`.
    *   The arrival time at node `j` after departing from `i` is `A_jk = D_ik + t_ij`, where `D_ik` is the departure time from `i`. Departure time is calculated as `max(A_ik, e_i) + service_time_at_i`.

4.  **Energy Constraints:**
    *   The battery level must not be negative: `b_ik ≥ 0, ∀i ∈ N, ∀k ∈ V`.
    *   The battery level cannot exceed the maximum capacity: `b_ik ≤ B`.
    *   Energy consumption between nodes: If `x_ijk = 1`, then `b_jk ≤ b_ik' - (d_ij * r)`, where `b_ik'` is the battery level after charging at node `i` (if `i` is a station).
    *   The heuristic nature of the ALNS solver ensures these constraints are satisfied through the `Route::evaluate` function rather than by being explicitly included in a mathematical programming solver.

---

## 3. Proposed Method: Hybrid ALNS Framework

The solver is built around an Adaptive Large Neighborhood Search framework, which iteratively destroys a part of the current solution and repairs it in search of better alternatives.

### 3.1. Main ALNS Algorithm

The main ALNS loop operates on a current solution `s_current`, a global best solution `s_best`, and a Pareto archive `P`. The general procedure is outlined in the pseudocode below.

```plaintext
Algorithm 1: Main ALNS Loop
-----------------------------
Input: Initial solution s_init
Output: Pareto front P

1: s_current ← s_init
2: s_best ← s_init
3: P ← {s_init}
4: T ← T_start
5: Initialize operator weights w_d, w_r

6: for i = 1 to max_iterations do
7:     Select destroy operator op_d from D with probability based on w_d
8:     Select repair operator op_r from R with probability based on w_r
9:
10:    s_new ← copy(s_current)
11:    unserved_customers ← op_d(s_new)
12:    op_r(s_new, unserved_customers)
13:
14:    if use_local_search then
15:        LocalSearch(s_new)
16:    end if
17:
18:    if cost(s_new) < cost(s_best) then
19:        s_best ← s_new
20:    end if
21:
22:    // Simulated Annealing Acceptance Criterion
23:    if cost(s_new) < cost(s_current) or random(0,1) < exp(-(cost(s_new) - cost(s_current)) / T) then
24:        s_current ← s_new
25:    end if
26:
27:    T ← T * cooling_rate
28:
29:    // Update Pareto Archive and Operator Weights
30:    result ← P.tryAdd(s_new)
31:    UpdateWeights(op_d, op_r, result)
32:
33: end for
34:
35: return P
```

### 3.2. The Pareto Archive

The `ParetoArchive` is a key component for multi-objective optimization.
*   **Function:** It stores a bounded-size set of all non-dominated solutions discovered during the search.
*   **Acceptance:** When a new solution is offered, the archive checks if it is dominated by, dominates, or is non-dominated with respect to the existing solutions. Dominated solutions are rejected. If the new solution dominates existing ones, they are removed.
*   **Pruning:** If the archive exceeds its maximum size, a pruning mechanism based on **Crowding Distance** (from the NSGA-II algorithm) is invoked. This method removes solutions from the densest regions of the objective space, promoting diversity in the final Pareto front.

### 3.3. Neighborhood Operators

A variety of destroy and repair operators are used to generate diverse neighborhoods. Their selection probability is adapted based on their historical performance.

---

#### **Destroy Operators**

Destroy operators are responsible for removing a number of customers from the current solution, creating a partial solution that the repair operators will then attempt to complete.

**1. Worst Distance Node Removal**

*   **Description:** This operator removes customers that are most costly in terms of travel distance. It identifies customers who, if removed, would result in the largest reduction of their route's length. This targets customers who are geographically far from their neighbors in a route.
*   **Pseudocode:**
    ```plaintext
    Algorithm: WorstDistanceNodeRemoval
    ------------------------------------
    Input: current_solution, num_to_remove
    Output: list_of_removed_customers

    1: candidates ← []
    2: for each route in current_solution do
    3:     for each customer c at position i in route do
    4:         prev_node ← node at i-1
    5:         next_node ← node at i+1
    6:         distance_with_c ← dist(prev_node, c) + dist(c, next_node)
    7:         distance_without_c ← dist(prev_node, next_node)
    8:         cost_saving ← distance_with_c - distance_without_c
    9:         add {c, cost_saving} to candidates
    10:    end for
    11: end for
    12:
    13: Sort candidates in descending order of cost_saving
    14:
    15: removed_customers ← []
    16: while |removed_customers| < num_to_remove and |candidates| > 0 do
    17:     // Select a candidate using a randomized, biased method (Shaw selection)
    18:     r ← random(0, 1)
    19:     idx ← floor(|candidates| * r^determinism_param)
    20:     selected_customer ← candidates[idx]
    21:     add selected_customer to removed_customers
    22:     remove selected_customer from candidates
    23: end while
    24:
    25: Remove all customers in removed_customers from the solution's routes
    26: return removed_customers
    ```

**2. Shaw Destroy (Relatedness Removal)**

*   **Description:** This operator removes customers that are similar or "related" to each other. It starts with a random customer and iteratively removes other customers that are most related to it based on a weighted formula of proximity, time window similarity, and demand similarity. This tends to remove geographically clustered customers.
*   **Pseudocode:**
    ```plaintext
    Algorithm: ShawDestroy
    -----------------------
    Input: current_solution, num_to_remove
    Output: list_of_removed_customers

    1: all_customers ← get all customers from current_solution
    2: removed_customers ← []
    3:
    4: Select a random customer c_seed from all_customers
    5: Add c_seed to removed_customers and remove from all_customers
    6:
    7: while |removed_customers| < num_to_remove and |all_customers| > 0 do
    8:     Select a random customer c_ref from removed_customers
    9:     Find customer c_best in all_customers that has the minimum relatedness_score to c_ref
    10:
    11:    // relatedness_score(c1, c2) = w1*dist(c1,c2) + w2*|c1.ready_time - c2.ready_time| + w3*|c1.demand - c2.demand|
    12:
    13:    Add c_best to removed_customers
    14:    Remove c_best from all_customers
    15: end while
    16:
    17: Remove all customers in removed_customers from the solution's routes
    18: return removed_customers
    ```

**3. Pareto Focus Destroy**

*   **Description:** This operator introduces multi-objective awareness into the destroy phase. It randomly selects one of the secondary objectives (e.g., distance, time, workload) and removes customers that are the worst contributors to that specific objective.
*   **Pseudocode:**
    ```plaintext
    Algorithm: ParetoFocusDestroy
    ------------------------------
    Input: current_solution, num_to_remove
    Output: list_of_removed_customers

    1: focus_objective ← randomly select from {DISTANCE, TIME, WORKLOAD}
    2: candidates ← []
    3:
    4: for each route in current_solution do
    5:     for each customer c in route do
    6:         if focus_objective is DISTANCE then
    7:             cost ← calculate distance saving for removing c
    8:         else if focus_objective is TIME then
    9:             cost ← calculate time saving for removing c
    10:        else if focus_objective is WORKLOAD then
    11:            cost ← |route.duration - mean_route_duration|
    12:        end if
    13:        add {c, cost} to candidates
    14:    end for
    15: end for
    16:
    17: Sort candidates in descending order of cost
    18: Select and return top num_to_remove customers using randomized selection (like WorstDistance)
    ```

**4. High Energy Node Removal**

*   **Description:** Similar to Worst Distance, but focuses on energy. It removes customers whose removal would lead to the greatest energy savings. This is particularly effective for customers located far from others, requiring long travel segments.
*   **Pseudocode:**
    ```plaintext
    Algorithm: HighEnergyNodeRemoval
    ---------------------------------
    Input: current_solution, num_to_remove
    Output: list_of_removed_customers

    1: // Identical to WorstDistanceNodeRemoval, but the cost_saving is calculated based on energy
    2: cost_saving ← (energy_to(c) + energy_from(c)) - energy_bypassing(c)
    3: // ... rest of the logic is the same
    ```

**5. Fewest Customers Route Removal**

*   **Description:** A simple but effective operator that removes an entire route. It targets the route that serves the fewest customers, aiming to eliminate under-utilized vehicles.
*   **Pseudocode:**
    ```plaintext
    Algorithm: FewestCustomersRouteRemoval
    ---------------------------------------
    Input: current_solution
    Output: list_of_removed_customers

    1: Find route R with the minimum number of customers (> 0)
    2: if R is found then
    3:     removed_customers ← all customers in R
    4:     Remove route R from current_solution
    5: end if
    6: return removed_customers
    ```

**6. Longest Wait Time Route Removal**

*   **Description:** This operator targets scheduling inefficiencies by removing the entire route that has the highest total waiting time. This frees up customers from a poorly scheduled route, allowing them to be re-inserted more efficiently elsewhere.
*   **Pseudocode:**
    ```plaintext
    Algorithm: LongestWaitTimeRouteRemoval
    ----------------------------------------
    Input: current_solution
    Output: list_of_removed_customers

    1: Find route R with the maximum total_wait_time
    2: if R is found and wait_time > 0 then
    3:     removed_customers ← all customers in R
    4:     Remove route R from current_solution
    5: end if
    6: return removed_customers
    ```

---

#### **Repair Operators**

Repair operators take the partial solution from a destroy operator and re-insert the unserved customers to form a new, complete solution.

**1. Greedy Distance Insertion**

*   **Description:** This is a classic greedy heuristic. For each unserved customer, it finds the best possible insertion position (across all routes) that results in the minimum increase in total travel distance. It iteratively inserts the customer with the overall best (least costly) insertion until all are served.
*   **Pseudocode:**
    ```plaintext
    Algorithm: GreedyDistanceInsertion
    ----------------------------------
    Input: partial_solution, unserved_customers
    Output: (none; solution is modified in-place)

    1: for each customer c in unserved_customers do
    2:     best_insertion ← {cost: infinity}
    3:     for each route R in partial_solution do
    4:         for each position p in R do
    5:             insertion_result ← R.checkInsertionCost(c, p)
    6:             if insertion_result.isFeasible and insertion_result.deltaDistance < best_insertion.cost then
    7:                 best_insertion ← {route: R, position: p, cost: insertion_result.deltaDistance}
    8:             end if
    9:         end for
    10:    end for
    11:
    12:    if best_insertion.cost < infinity then
    13:        Insert c into best_insertion.route at best_insertion.position
    14:    else
    15:        Create a new route for c
    16:    end if
    17: end for
    ```

**2. Greedy Time Insertion**

*   **Description:** Similar to the distance-based greedy operator, but the objective is to minimize the increase in total route time, which is a combination of travel time and waiting time.
*   **Pseudocode:**
    ```plaintext
    Algorithm: GreedyTimeInsertion
    ------------------------------
    Input: partial_solution, unserved_customers
    Output: (none; solution is modified in-place)

    1: // Identical to GreedyDistanceInsertion, but the cost is calculated differently:
    2: cost ← insertion_result.deltaWaitTime + insertion_result.deltaTravelTime
    3: // ... rest of the logic is the same
    ```

**3. Regret-k Repair**

*   **Description:** A more sophisticated heuristic that tries to avoid making locally optimal choices that are globally poor. It prioritizes inserting "difficult" customers first. For each customer, it calculates a "regret" value, which is the cost difference between their best insertion and their k-th best insertion. A high regret value means the customer has few good options, so inserting it is urgent. The operator iteratively inserts the customer with the highest regret.
*   **Pseudocode:**
    ```plaintext
    Algorithm: RegretKRepair
    ------------------------
    Input: partial_solution, unserved_customers, k
    Output: (none; solution is modified in-place)

    1: while unserved_customers is not empty do
    2:     best_regret_customer ← null
    3:     max_regret ← -infinity
    4:
    5:     for each customer c in unserved_customers do
    6:         Find the k best insertion positions for c, store their costs [cost_1, cost_2, ..., cost_k]
    7:         if no insertion is possible, continue
    8:
    9:         regret_value ← 0
    10:        for i from 2 to k do
    11:            regret_value ← regret_value + (cost_i - cost_1)
    12:        end for
    13:
    14:        if regret_value > max_regret then
    15:            max_regret ← regret_value
    16:            best_regret_customer ← c
    17:        end if
    18:    end for
    19:
    20:    if best_regret_customer is not null then
    21:        Insert best_regret_customer at its best position (cost_1)
    22:        Remove it from unserved_customers
    23:    else
    24:        // Handle case where no customer can be inserted
    25:        break
    26:    end if
    27: end while
    ```

**4. Pareto Focus Repair**

*   **Description:** The multi-objective counterpart to the greedy operators. It randomly selects a secondary objective (distance, time, or workload) and then performs a greedy insertion based on minimizing the cost increase for that specific objective.
*   **Pseudocode:**
    ```plaintext
    Algorithm: ParetoFocusRepair
    -----------------------------
    Input: partial_solution, unserved_customers
    Output: (none; solution is modified in-place)

    1: focus_objective ← randomly select from {DISTANCE, TIME, WORKLOAD}
    2:
    3: // Logic is identical to GreedyDistanceInsertion, but the cost function changes:
    4: if focus_objective is DISTANCE then
    5:     cost ← insertion_result.deltaDistance
    6: else if focus_objective is TIME then
    7:     cost ← insertion_result.deltaWaitTime + insertion_result.deltaTravelTime
    8: else if focus_objective is WORKLOAD then
    9:     new_route_duration ← route.duration + estimated_delta_time
    10:    cost ← |new_route_duration - mean_route_duration|
    11: end if
    12: // ... rest of the logic is the same
    ```

**5. Greedy Station Repair**

*   **Description:** This operator enhances the standard greedy insertion to handle energy constraints more proactively. When attempting to insert a customer, if the direct insertion is infeasible due to an energy deficit, this operator will also try to insert the customer *along with* a visit to a nearby charging station to make the move feasible.
*   **Pseudocode:**
    ```plaintext
    Algorithm: GreedyStationRepair
    -------------------------------
    Input: partial_solution, unserved_customers
    Output: (none; solution is modified in-place)

    1: for each customer c in unserved_customers do
    2:     best_insertion ← {cost: infinity}
    3:     nearest_station ← find nearest station to c
    4:
    5:     for each route R and position p do
    6:         // Try inserting customer c alone
    7:         res_cust_only ← R.checkInsertionCost(c, p)
    8:         if res_cust_only.isFeasible and res_cust_only.deltaDistance < best_insertion.cost then
    9:             best_insertion ← {..., type: CUSTOMER_ONLY}
    10:        else
    11:            // Try inserting station S then customer c
    12:            res_with_station ← R.checkInsertionCostWithStation(c, p, nearest_station)
    13:            if res_with_station.isFeasible and res_with_station.deltaDistance < best_insertion.cost then
    14:                best_insertion ← {..., type: WITH_STATION}
    15:            end if
    16:        end if
    17:    end for
    18:
    19:    if best_insertion.cost < infinity then
    20:        if best_insertion.type is CUSTOMER_ONLY then
    21:            Insert c at best position
    22:        else // type is WITH_STATION
    23:            Insert nearest_station then c at best position
    24:        end if
    25:    else
    26:        Create a new route for c
    27:    end if
    28: end for
    ```

### 3.4. Local Search

After a repair operation, a local search phase can be triggered to perform fine-grained improvements.
*   **`searchRelocate`:** Attempts to move a single customer to another position in the same or a different route.
*   **`searchTwoOpt`:** For each route, it reverses segments to remove edge crossings and reduce route length.
*   **`searchStationRemoval`:** Attempts to remove charging station visits from routes to see if they remain feasible, thus saving time.

---

## 4. Core Algorithmic Components

### 4.1. Route Evaluation and Feasibility

A critical component of the solver is the `evaluate` function, which calculates the objective costs of a single route and verifies its feasibility regarding time, capacity, and energy constraints. This is achieved via a two-pass mechanism.

```plaintext
Algorithm 2: Route Evaluation
--------------------------------
Input: A route (sequence of nodes)
Output: Feasibility status and calculated costs (distance, time, etc.)

// --- Pass 1: Backward Pass (Calculate minimum required energy) ---
1: Initialize min_battery_req at last depot to 0.
2: for i = (n-1) down to 0 do
3:     node_curr ← route[i], node_next ← route[i+1]
4:     consumption ← energy to travel from node_curr to node_next
5:     min_battery_req[i] ← min_battery_req[i+1] + consumption
6:     if node_curr is a station then
7:         min_battery_req[i] ← max(0, min_battery_req[i] - max_charge_at_station)
8:     end if
9:     if min_battery_req[i] > vehicle_battery_capacity then
10:        Mark route as INFEASIBLE and exit
11:    end if
12: end for

// --- Pass 2: Forward Pass (Simulate route and calculate costs) ---
13: Initialize time, load, and battery at first depot.
14: for i = 0 to (n-1) do
15:     node_curr ← route[i], node_next ← route[i+1]
16:
17:     // Update Time
18:     arrival_time[i+1] ← departure_time[i] + travel_time(i, i+1)
19:     if arrival_time[i+1] > due_date[i+1] then
20:         Mark route as INFEASIBLE
21:     end if
22:     wait_time[i+1] ← max(0, ready_time[i+1] - arrival_time[i+1])
23:
24:     // Update Battery and Charge
25:     battery[i+1] ← battery[i] - energy_consumption(i, i+1)
26:     if battery[i+1] < 0 then
27:         Mark route as INFEASIBLE
28:     end if
29:     if node_next is a station then
30:         charge_needed ← min_battery_req[i+1] - battery[i+1]
31:         charge_amount ← max(0, charge_needed)
32:         battery[i+1] ← battery[i+1] + charge_amount
33:         charge_time ← charge_amount / charge_rate
34:     end if
35:
36:     // Update Load
37:     load[i+1] ← load[i] - demand[i+1]
38:     if load[i+1] < 0 then
39:         Mark route as INFEASIBLE
40:     end if
41:
42:     departure_time[i+1] ← arrival_time[i+1] + wait_time[i+1] + service_time[i+1] + charge_time
43:
44: end for
45:
46: Aggregate total distance, time, etc. from the simulation.
```
This two-pass approach guarantees that for a fixed sequence of nodes, the feasibility is correctly assessed and the optimal charging decisions are made to minimize waiting time at stations.

---

## 5. Conclusion

This paper has described a C++ implementation of a Hybrid Adaptive Large Neighborhood Search algorithm for the Multi-Objective EVRPTW. The methodology combines a powerful metaheuristic with a Pareto archive to effectively handle conflicting objectives, including vehicle count, distance, workload balance, and makespan. The architecture is modular, allowing for the easy addition of new operators, and incorporates critical logic for ensuring time, capacity, and energy feasibility. Future work will involve benchmarking the solver against standard EVRPTW instances and further refining the operators and local search procedures for enhanced performance.