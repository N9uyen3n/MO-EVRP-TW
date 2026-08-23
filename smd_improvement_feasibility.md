# Xác minh Khả thi: 10 Cải tiến SMD++ cho EVRP-TW-PR MO-ALNS

Đối chiếu từng đề xuất với source code thực tế tại:
- [Route.h](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/core/Route.h)
- [Route.cpp](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp)
- [LocalSearch.h](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/alns/LocalSearch.h)
- [LocalSearch.cpp](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp)
- [Solution.cpp](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Solution.cpp)

---

## 1. Diagnostic Counters — ✅ Khả thi cao, NÊN LÀM

### Hiện trạng code
`DiagCounters` đã tồn tại tại [LocalSearch.h:L111-L127](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/alns/LocalSearch.h#L111-L127), nhưng chỉ track:

| Có rồi | Chưa có |
|--------|---------|
| `opImproveCount[8]` | `generatedMoves` |
| `opCallCount[8]` | `rejectedByOperatorFilter` |
| `crossingDetected/typeA/applied` | `rejectedByEvaluateMoveFastPath` |
| `stationSegTotal/stationSegReject` | `copiedAndEvaluated` |
| | `infeasibleAfterEvaluate` |
| | `appliedMoves` |
| | Timing counters |

### Đánh giá

| Metric | Khó? | Ghi chú |
|--------|------|---------|
| Đếm move categories | Dễ | Thêm `++counter` vào `evaluateMove()` trước/sau mỗi return |
| `timeInOperatorFilter` | Trung bình | Cần `chrono::high_resolution_clock` mỗi operator, overhead ~50ns/call |
| `timeInRouteCopy` | Dễ | Wrap `Route r1_copy = ...` trong timing |
| `timeInRouteEvaluate` | Dễ | Wrap `r1_copy.evaluate()` |
| `timeInPatchContext` | Dễ | Đã có 1 call site tại [L807](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L807) |

> [!TIP]
> Gợi ý: Dùng `thread_local` accumulator + conditional compilation (`#ifdef DIAG_ENABLED`) để zero-cost khi không cần.
> Cần thêm ~30 dòng vào `evaluateMove()` và ~10 dòng mỗi operator.

**Độ khó: ★☆☆☆☆ | Rủi ro: Không | Ưu tiên: 1**

---

## 2. Route-level Prefix Cache (`RouteSMDCache`) — ✅ Khả thi, nhưng cần chú ý

### Hiện trạng code

Route đã có một số cached field tại [Route.h:L107-L123](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/core/Route.h#L107-L123):

```
Đã có:                              Chưa có:
─────                               ─────
cachedTotalDemand_ (lazy)            demandPrefix[]
cachedCustomers_ (lazy)              stationPrefix[]
minBatteryReq[] (backward pass)      forwardTimeSlack[]
timeSlack_ (member nhưng chưa       energyMargin[]
  dùng trong evaluate)               version counter
hasStation_ (member nhưng chưa
  dùng trong evaluate)
states[] (full NodeState per pos)
```

### Phân tích chi tiết

**`demandPrefix`**: Hiện code tính segment demand bằng vòng lặp nhỏ trong `evaluateMove()` [L384-L387](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L384-L387) (INTER_OR_OPT) và [L454-L457](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L454-L457) (CROSS_EXCHANGE). Segment length chỉ 1-3, nên vòng lặp chạy tối đa 3 lần. Prefix sum sẽ giảm từ 3 operations xuống 1 subtraction — lợi ích nhỏ nhưng có.

**`stationPrefix`**: Đây là cải tiến **rõ ràng nhất**. `searchTwoOpt` tại [L1142-L1144](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1142-L1144) quét đoạn `[i, j]` — có thể O(n) nếu j - i lớn. Thay bằng `stationPrefix[j+1] - stationPrefix[i] > 0` là O(1).

**`forwardTimeSlack` + `energyMargin`**: Route đã có method [getTimeSlack()](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L848-L863) và [getEnergySlack()](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L836-L846) — nhưng chúng tạo vector mới mỗi lần gọi, không cache, và không dùng trong hot path. Cần chuyển sang lazy-cached member.

> [!IMPORTANT]
> **Vấn đề invalidation**: Route mutators (`addNode`, `removeNode`, `reverseNodes`) tại [L66-L119](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L66-L119) đã set `isDirty = true`. Nếu thêm prefix cache, cần thêm dirty flag cho nó, hoặc build lại trong `evaluate()`.
>
> **Nhưng**: trong `evaluateMove()`, code tạo `Route r1_copy` (bản copy) rồi apply move lên copy. Prefix cache trên bản copy **sẽ bị invalid ngay sau** `addNode/removeNode` trên copy, nên phải rebuild trước khi dùng → vô nghĩa. Prefix cache chỉ hữu ích cho **original route** (pre-move check), không cho route copy.

### Kết luận

| Feature | Lợi ích | Khả thi? | Ghi chú |
|---------|---------|----------|---------|
| `stationPrefix` trên original route | **Cao** — O(1) thay O(n) | ✅ | Dùng trong `searchTwoOpt` inline check |
| `demandPrefix` trên original route | Thấp — segment 1-3 | ✅ nhưng ROI thấp | Vòng lặp 1-3 iter đã gần O(1) |
| `forwardTimeSlack` cache | Trung bình | ✅ | Dùng cho filter mới (đề xuất #4) |
| `energyMargin` cache | Trung bình | ✅ | Dùng cho filter mới (đề xuất #4) |
| `version` counter | **Cao** — dùng cho negative cache | ✅ | Thêm 1 int vào Route, ++ mỗi mutation |

**Độ khó: ★★☆☆☆ | Rủi ro: Thấp | Ưu tiên: 2**

---

## 3. `evaluateFrom(startIndex)` — ⚠️ Khả thi NHƯNG phức tạp hơn mô tả

### Vấn đề cốt lõi

`Route::evaluate()` tại [L126-L284](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L126-L284) gồm **2 pass**:

```
Backward pass: L146-L160
    minBatteryReq[i] = energy(i→i+1) + minBatteryReq[i+1]   (or capped at station)
    Chạy từ n-2 xuống 0

Forward pass: L177-L278
    states[i+1] = f(states[i], distance, nodeType)
    Chạy từ 0 đến n-1
```

> [!WARNING]
> **Backward pass là trở ngại chính**. `minBatteryReq[i]` phụ thuộc vào `minBatteryReq[i+1]` — nghĩa là thay đổi ở vị trí `p` ảnh hưởng **TẤT CẢ `minBatteryReq[i]` với `i < p`**. Đây là điểm khác biệt quan trọng so với forward-only evaluation.
>
> Ví dụ: chèn station tại vị trí p → `minBatteryReq[p] = cap(...)` → thay đổi cascade ngược về 0.

Kịch bản `evaluateFrom(p)`:

| Bước | Forward pass | Backward pass |
|------|-------------|---------------|
| `INTRA_RELOCATE` (di chuyển trong route) | Chạy forward từ `min(old, new)-1` | Phải chạy backward **full** từ `n-2` |
| `INTRA_TWO_OPT` (reverse segment) | Chạy forward từ `i-1` | Phải chạy backward **full** |
| `INTER_RELOCATE` (xóa khỏi route1) | Forward từ `pos-1` trên r1 | Backward full trên r1 |
| `INTER_RELOCATE` (chèn vào route2) | Forward từ `pos-1` trên r2 | Backward full trên r2 |

**Forward pass**: giảm được ~50% trung bình (move ở giữa route).

**Backward pass**: KHÔNG giảm được — phải chạy full vì dependency ngược.

### Giải pháp thực tế

Có 3 lựa chọn:

**Lựa chọn A**: `evaluateFrom()` chỉ optimize forward pass, backward pass vẫn full.
- Lợi ích: ~25-40% tổng thể (forward thường chiếm ~55% evaluate time vì nặng hơn backward)
- Dễ implement

**Lựa chọn B**: Backward pass chỉ chạy lại từ `max(modifiedPos, lastStationPos)` về 0.
- Nếu không có station sau `modifiedPos`, `minBatteryReq` phía trước chỉ cần cộng thêm delta energy
- Phức tạp hơn nhưng lợi ích lớn khi station ít

**Lựa chọn C**: Cache backward pass riêng, lazy rebuild.
- `minBatteryReq` chỉ phụ thuộc vào distance matrix + station positions
- Nếu move không thay đổi station sequence, backward pass result gần giống
- Nhưng cần verify cẩn thận

> [!IMPORTANT]
> Phân tích gốc claim *"khi move chỉ thay đổi từ vị trí p, phần trước p không đổi"* — **đúng cho forward pass, SAI cho backward pass**. Backward pass bị cascade ngược toàn bộ. Đây là điểm phân tích cần sửa.

### Thêm một vấn đề: `checkInsertionCost()` đã gần với `evaluateFrom`

[checkInsertionCost()](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L386-L501) thực chất đã chạy backward+forward pass đầy đủ trên virtual sequence (dùng `simNodeAt` lambda, không copy vector). Nó **hiệu quả tương đương** `evaluateFrom()` cho single-node insertion.

Hiện `INTER_RELOCATE` fast-path [dùng `checkInsertionCost()`](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L299) rồi return — KHÔNG copy route. Nếu mở rộng pattern này cho các move type khác, có thể có lợi hơn `evaluateFrom`.

**Độ khó: ★★★★☆ | Rủi ro: Trung bình–Cao | Ưu tiên: 3 (hạ từ mức 3 ban đầu)**

---

## 4. Filter theo Time Slack + Energy Margin — ✅ Khả thi, tốt

### Hiện trạng

Route đã có 2 method:
- [getTimeSlack()](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L848-L863): backward pass tính `slack[i] = min(local_slack, slack[i+1] + wait[i+1])`
- [getEnergySlack()](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L836-L846): `slack[i] = remainingBattery[i] - minBatteryReq[i]`

**Nhưng chúng:**
1. Tạo `std::vector<double>` mới mỗi lần gọi — allocation trong hot loop
2. Không được dùng trong bất kỳ operator nào

### Cách cải tiến

1. Cache `timeSlack_` và `energySlack_` vào Route (Route.h đã có `mutable std::vector<double> timeSlack_` tại [L111](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/core/Route.h#L111) nhưng chưa dùng!)
2. Build lazy khi cần, invalidate khi route dirty
3. Dùng trong filter kiểu:

```cpp
// Trong searchRelocate, trước evaluateMove():
double addedTime = travel(prev2, nodeId) + service(nodeId) + travel(nodeId, next2) - travel(prev2, next2);
if (addedTime > routes[r2].getTimeSlackAt(insertPos))
    continue; // Reject: chắc chắn violate TW downstream
```

> [!NOTE]
> `Route.h` đã khai báo `timeSlack_` ([L111](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/core/Route.h#L111)) nhưng không build/use nó trong `evaluate()`. Đây là **low-hanging fruit** — chỉ cần wire up cái đã có sẵn.

### Tác động dự kiến

Operator `searchRelocate` hiện dùng `canPossiblyInsert()` [L689-L715](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L689-L715) — check capacity, energy tới điểm chèn, và TW tại điểm chèn. Nhưng nó **KHÔNG check ripple effect** (downstream TW violation). Time slack filter bổ sung chính xác phần này.

**Độ khó: ★★☆☆☆ | Rủi ro: Thấp | Ưu tiên: 2 (nâng lên cùng mức prefix cache)**

---

## 5. Negative Cache cho Move Infeasible — ⚠️ Khả thi nhưng cần cân nhắc kỹ

### Phân tích

Ý tưởng: dùng `MoveKey + routeVersion` để skip move đã fail.

### Vấn đề

1. **Memory**: Mỗi `MoveKey` ~40 bytes. Với R=10 routes, N=100 customers, potential moves rất nhiều. Nếu cache unbounded → OOM. Cần bounded cache (LRU hoặc hash set có evict).

2. **VND first-improvement**: Code hiện dùng **first-improvement** (return ngay khi tìm được move tốt). Khi move được apply, routes thay đổi → version thay đổi → cache entries cho routes đó bị invalidate. Vì first-improvement, mỗi operator call thường chỉ evaluate vài chục move trước khi tìm thấy improvement.

3. **Khi nào hữu ích nhất**: Khi VND gần convergence (k cao, ít improvement) — operator phải duyệt NHIỀU candidate mà không tìm được cải thiện. Đây đúng là lúc negative cache phát huy.

4. **Route version**: Route hiện KHÔNG có version counter. Cần thêm `int version_ = 0;` vào Route, increment trong mỗi mutator (`addNode`, `removeNode`, `reverseNodes`).

> [!WARNING]
> **Xung đột với §4 MASTER_RULE**: "Uncontrolled `vector.push_back`" và "Creating new evaluation objects per move" bị cấm trong hot loops. Negative cache nếu dùng `std::unordered_map<MoveKey, ...>` sẽ vi phạm nếu hash collision gây allocation. Nên dùng **fixed-size open-addressing hash table** hoặc **Bloom filter**.

**Độ khó: ★★★☆☆ | Rủi ro: Trung bình | Ưu tiên: 5 (giữ nguyên)**

---

## 6. Multi-neighbor List — ✅ Khả thi, tốt cho Z3

### Hiện trạng

`SearchContext` tại [LocalSearch.h:L136-L158](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/alns/LocalSearch.h#L136-L158) chỉ có **1 loại** neighbor list:

```cpp
std::vector<std::vector<int>> neighborLists; // spatial only
```

Tất cả inter-route operators (`searchRelocate`, `searchSwap`, `searchInterTwoOpt`, `searchOrOpt`, `searchCrossExchange`) đều dùng cùng một `neighborLists`.

### Cách implement

```cpp
struct SearchContext {
    // Existing
    std::vector<std::vector<int>> neighborLists;      // spatial (giữ nguyên)
    
    // New
    std::vector<std::vector<int>> workloadNeighbors;  // routes có workload chênh lệch lớn
    // energyNeighbors có thể để phase 2 vì ít dùng trong VND
};
```

`workloadNeighbors[r]` = các route có `|activeTime[r] - activeTime[r2]|` lớn nhất → ưu tiên swap/relocate giữa route nặng ↔ nhẹ.

### Tác động

Hiện VND tại [runDistanceOptimization](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L734-L817) chỉ optimize Z2 (distance). Nếu thêm workload neighbor list, có thể tạo operator `searchWorkloadRelocate` chạy ở VND phase riêng, hoặc dùng trong cùng VND với weight `w.gini > 0`.

> [!IMPORTANT]
> Hiện `LocalSearchWeights` tại [LocalSearch.h:L64-L69](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/alns/LocalSearch.h#L64-L69) có `dist, time, energy, vehicle` nhưng **KHÔNG có `gini`**. Cần thêm nếu muốn VND quan tâm Z3.

**Độ khó: ★★★☆☆ | Rủi ro: Thấp | Ưu tiên: 4 (nâng lên vì tốt cho Z3)**

---

## 7. Incremental Gini Delta — ✅ Khả thi, giá trị cao

### Hiện trạng Gini

Gini được tính trong [Solution::evaluateRoutes()](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Solution.cpp#L145-L221) bằng sorted formula:

```cpp
// L194: sort route_durations
// L199-204: Gini = (2 * Σ(i+1)*x_i) / (n * Σx_i) - (n+1)/n
```

Gini dùng **activeTime** (travel + service + charge, không bao gồm wait) — [L176](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Solution.cpp#L176).

### Incremental delta

Đề xuất tính delta Gini khi chỉ 1-2 route thay đổi là **đúng về nguyên tắc nhưng cần cẩn thận**:

1. Formula sorted Gini yêu cầu **sorted order** → khi 1 route thay đổi activeTime, thứ tự có thể đổi
2. Nhưng vì R nhỏ (thường 5-15 routes), `O(R)` cho tính Gini delta vẫn rất rẻ
3. Không cần Fenwick tree — O(R) sorted insert đủ

**Incremental approach thực tế:**

```cpp
// Lưu sẵn trong SearchContext:
std::vector<double> routeActiveTimes;  // sorted
double giniSum;
double giniWeightedSum;

// Khi route r thay đổi activeTime từ T_old sang T_new:
// 1. Remove T_old từ sorted array: O(R)
// 2. Insert T_new: O(R)
// 3. Recompute Gini từ sorted array: O(R)
// Total: O(R) — vẫn tốt hơn evaluateRoutes() vì không phải evaluate ALL routes
```

Quan trọng hơn, cái cần thiết nhất là **dùng Gini delta trong `evaluateMove()`** để reject move tăng Gini quá nhiều:

```cpp
// Sau khi tính distanceDelta trong evaluateMove():
if (weights.gini > 0) {
    double giniDelta = estimateGiniDelta(solution, move);
    move.eval.objectiveDelta += giniDelta * weights.gini;
}
```

> [!NOTE]
> Lưu ý: `evaluateRoutes()` gọi `route.evaluate()` cho TẤT CẢ routes ([L148-L151](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Solution.cpp#L148-L151)), nhưng nhờ `isDirty` flag, chỉ route bị thay đổi mới chạy evaluate thật. Bottleneck thật sự của `evaluateRoutes()` là:
> - Remove empty routes (O(R))
> - Sort + Gini computation (O(R log R))
> 
> Cả hai đều O(R), không đáng lo. Nhưng `evaluateRoutes()` cũng gọi `detach()` → potential COW copy → **đây** mới là bottleneck ẩn nếu Solution shared.

**Độ khó: ★★☆☆☆ | Rủi ro: Thấp | Ưu tiên: 4 (cùng mức multi-neighbor)**

---

## 8. Giảm rủi ro `patchSearchContext` — ✅ Khả thi

### Cách nhẹ: periodic full rebuild

Đơn giản và an toàn. Thêm counter vào VND loop:

```cpp
// Trong runDistanceOptimization, sau patchSearchContext():
static int patchCount = 0;
if (++patchCount % 50 == 0) {
    searchContext_.invalidate();
    updateSearchContext(solution);
}
```

### Cách thông minh: reverse-neighbor list

Hiện `patchSearchContext` tại [L851-L891](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L851-L891) đã duyệt tất cả routes để check xem neighbor list cũ có chứa dirty route không:

```cpp
for (int r = 0; r < numRoutes; ++r) {
    if (!needRebuild) {
        for (int nb : searchContext_.neighborLists[r]) {  // scan neighbor list
```

Thêm `reverseNeighbors[r]` = set of routes that have `r` in their neighbor list → check chính xác O(1) thay vì scan.

Nhưng overhead quản lý reverse-neighbor list có thể không worth it vì R nhỏ (5-15).

> [!TIP]
> **Khuyến nghị**: Dùng cách nhẹ (periodic rebuild mỗi 30-50 improvements). Chi phí full rebuild là O(R²) với R=10 → ~100 operations, rất rẻ.

**Độ khó: ★☆☆☆☆ (cách nhẹ) | Rủi ro: Không | Ưu tiên: 6**

---

## 9. Tối ưu Bộ nhớ trong Hot Path — ✅ Đã làm phần lớn

### Hiện trạng

Code hiện tại đã optimize rất mạnh:

| Tối ưu | Đã có? | Code |
|--------|--------|------|
| `thread_local` buffers | ✅ | `tl_dp`, `tl_simStates` [L379-L380](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L379-L380); `jCandidates`, `jSeen` [L1092-L1094](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1092-L1094) |
| Flat O(1) lookup | ✅ | `nodeTypeById_`, `demandById_`, `readyTimeById_`, etc. |
| `getNodes()` trả `const&` | ✅ | [L510](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L510) |
| `getNodeRaw()` thay `getNodeById()` | ✅ | [OPT-A] toàn bộ Route.cpp |
| Remove COW cho nodeSequence | ✅ | [OPT-1] |
| KNN buffer reuse | ✅ | [OPT-11] |

### Còn cải tiến được

1. **`evaluateMove()` copy route**: `Route r1_copy = routes[move.routeIdx1]` tại [L476](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L476) — copy toàn bộ `nodeSequence` (reserved 100) + `states` vector. Có thể dùng **scratch route buffer** pre-allocated.

2. **`segment` vector trong OR_OPT**: [L498-L499](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L498-L499) tạo `std::vector<int>` mỗi lần — thay bằng `std::array<int, 3>` vì `segmentLength` chỉ 1-3.

3. **`seg1`, `seg2` trong CROSS_EXCHANGE**: [L530-L531](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L530-L531) — tương tự, thay bằng `std::array<int, 3>`.

4. **`ranked` vector trong `patchSearchContext`**: [L874](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L874) tạo `std::vector<RN>` mỗi lần — nên dùng `thread_local`.

> [!CAUTION]
> **Item 1 (scratch route)** là phức tạp nhất vì Route có `shared_ptr<Instance>` và `shared_ptr<Vehicle>` → copy constructor copy shared_ptr (atomic refcount). Nếu dùng scratch buffer, phải đảm bảo vehicle/instance pointer đúng sau reset.

**Độ khó: ★★☆☆☆ (item 2-4) / ★★★☆☆ (item 1) | Rủi ro: Thấp | Ưu tiên: 7**

---

## 10. Segment Descriptor (O(log n)) — ❌ KHÔNG nên làm ngay

### Lý do cụ thể từ code

1. **Backward pass phụ thuộc station positions**: `minBatteryReq[i]` tại [L146-L160](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L146-L160) chỉ reset tại station → descriptor merge không thể O(1) đơn giản vì phải biết station positions trong segment.

2. **Partial recharge (§5 MASTER_RULE)**: `chargeAmount = min(chargeNeeded, chargePossible)` tại [L247](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L247) phụ thuộc vào `minBatteryReq[i+1]` (backward) và `remainingBattery` (forward) — 2 pass coupling. Descriptor merge cho partial recharge cần paper-level research.

3. **Absorption pass**: [runAbsorptionIfNeeded()](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L293-L374) là pass thứ 3 với backward-forward coupling phức tạp — descriptor phải encode cả absorption state.

4. **Đủ để viết một bài paper riêng** — nên để future work.

**Độ khó: ★★★★★ | Rủi ro: Cao | Ưu tiên: 8 (giữ nguyên)**

---

## Thứ tự Ưu tiên Điều chỉnh (sau xác minh code)

| Ưu tiên | Cải tiến | Lý do điều chỉnh | Độ khó |
|---------|---------|-------------------|--------|
| **1** | Diagnostic counters | Không đổi — cần đo trước khi sửa | ★☆☆☆☆ |
| **2** | `stationPrefix` + `timeSlack_` cache + route `version` | Gộp prefix cache (§2) và time slack filter (§4) vì chúng là prerequisites cho nhau. `timeSlack_` đã khai báo trong Route.h nhưng chưa dùng! | ★★☆☆☆ |
| **3** | `evaluateFrom()` — **chỉ forward pass** | Hạ kỳ vọng: backward pass vẫn phải full. Lợi ích ~25-40%. **Hoặc** mở rộng pattern `checkInsertionCost()` cho các move type khác | ★★★★☆ |
| **4** | Multi-neighbor list + incremental Gini + workload operator | Gộp §6 và §7 vì chúng serve cùng mục đích: làm Z3 có cơ chế local search riêng | ★★★☆☆ |
| **5** | Negative cache | Hữu ích nhưng cần careful memory management | ★★★☆☆ |
| **6** | `patchSearchContext` periodic rebuild | Rất dễ, ít rủi ro | ★☆☆☆☆ |
| **7** | Hot path memory optimization | Phần lớn đã làm; còn lại là marginal | ★★☆☆☆ |
| **8** | Segment Descriptor | Future work — phức tạp do backward pass + partial recharge | ★★★★★ |

---

## Phát hiện quan trọng từ xác minh code

> [!IMPORTANT]
> ### 3 điều đề xuất gốc chưa thấy nhưng code đã có sẵn:
>
> 1. **`timeSlack_`** đã khai báo tại [Route.h:L111](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/core/Route.h#L111) nhưng chưa populate/dùng — wire up = low-hanging fruit.
>
> 2. **`hasStation_`** đã khai báo tại [Route.h:L113](file:///d:/Work/SLSCM-LaB/24092025/untitled/include/core/Route.h#L113) nhưng chưa dùng — có thể dùng cho stationPrefix hoặc quick check.
>
> 3. **`checkInsertionCost()`** tại [L386-L501](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L386-L501) đã là virtual evaluate (không copy nodeSequence, dùng `simNodeAt` lambda) — pattern này có thể mở rộng cho swap/2-opt thay vì viết `evaluateFrom()`.

> [!WARNING]
> ### 1 điểm đề xuất gốc SAI:
>
> **§3 (`evaluateFrom`)**: Claim "phần trước vị trí p không đổi" chỉ đúng cho forward pass. **Backward pass bị ảnh hưởng toàn bộ** vì `minBatteryReq` propagate ngược. Phải tính lại backward pass full hoặc dùng incremental backward update (phức tạp).
