# Review: Phase 3 VND Speedup Plan

Đối chiếu plan tại [implementation_plan.md](file:///d:/Work/SLSCM-LaB/24092025/untitled/update/implementation_plan.md) với code thực tế.

---

## Đánh giá từng IDEA

### [IDEA-A] Incremental route evaluation — ✅ Đúng: tạm hoãn

Hoàn toàn đồng ý. Đã phân tích chi tiết tại [smd_ologn_root_cause.md](file:///C:/Users/ASUS/.gemini/antigravity-ide/brain/e58a788a-67f0-4e0f-bfd2-8c634864f01c/smd_ologn_root_cause.md) — backward pass cascade + partial recharge coupling + absorption pass làm incremental bất khả thi ở mức O(log n). Tối đa chỉ tiết kiệm ~25-40% forward pass.

**Verdict: Skip — đúng.**

---

### [IDEA-B] Thread-local scratch Route cho `evaluateMove` — ⚠️ Đúng ý tưởng, nhưng plan có lỗi kỹ thuật

**Vấn đề trong plan:**
Plan nói sẽ tạo `thread_local Route r1_copy(0, nullptr, nullptr)` — nhưng Route constructor yêu cầu `shared_ptr<Vehicle>` và `shared_ptr<Instance>` không null ([Route.cpp:L54-L61](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/core/Route.cpp#L54-L61)). Constructor gọi `evaluate()` ngay, mà evaluate truy cập `vehicle->getBatteryCapacity()` → **segfault nếu nullptr**.

**Sửa đúng:**
```cpp
// Không tạo thread_local Route với nullptr.
// Thay vào đó, dùng thread_local pointer + copy-assign khi đã có Route thật:
static thread_local std::unique_ptr<Route> tl_r1, tl_r2;
if (!tl_r1 || tl_r1->getVehicle() != routes[move.routeIdx1].getVehicle()) {
    tl_r1 = std::make_unique<Route>(routes[move.routeIdx1]); // first-time copy
} else {
    *tl_r1 = routes[move.routeIdx1]; // copy-assign, reuse capacity
}
```

Hoặc đơn giản hơn (khuyến nghị): giữ nguyên `Route r1_copy = ...` vì:
- Route đã remove COW (OPT-1) → copy assignment chỉ là memcpy vector data
- vector `nodeSequence` đã `reserve(100)` → không allocate trừ route > 100 nodes
- Tiết kiệm thật sự rất nhỏ so với chi phí `evaluate()` (luôn O(n))

> [!IMPORTANT]
> **Khuyến nghị: Bỏ qua IDEA-B hoặc chỉ làm phiên bản đơn giản.** ROI quá thấp so với rủi ro segfault. Bottleneck thật sự là `evaluate()` không phải vector copy.

---

### [IDEA-C] Dọn searchSwap cũ — ✅ Đúng, chỉ dọn code

Plan nói đúng: searchSwap active ở [L1410-L1581](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1410-L1581), code cũ bị comment out ở [L1583-L1728](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1583-L1728). Ngoài ra, `searchTwoOpt` cũng có bản cũ bị comment ở [L1132-L1191](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1132-L1191).

**Verdict: Dọn cả hai block. ~750 dòng giảm.**

---

### [IDEA-D] Đổi thứ tự vòng lặp searchOrOpt — ⚠️ Plan mô tả đúng, nhưng cần xem xét kỹ hơn

**Hiện tại** tại [L1744-L1832](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1744-L1832):
```
for (segLen : {1,2,3})          ← outer
  for (r1 : routes)             ← middle
    for (i : nodes in r1)       ← inner
      for (r2 : neighbors[r1])
        for (j : KNN positions)
```

Plan muốn đổi thành:
```
for (r1 : routes)               ← outer
  for (segLen : {1,2,3})        ← middle
    for (i : nodes in r1)
      for (r2 : neighbors[r1])
        for (j : KNN positions)
```

**Phân tích:**
- **Pro:** Cache locality tốt hơn cho `nodes1` (duyệt r1 một lần, thử tất cả segLen)
- **Con:** Hiện tại khi `return true` (first-improvement), nó restart từ segLen=1 cho MỌI routes. Nếu đổi, nó restart từ segLen=1 cho cùng route r1 hiện tại → có thể bỏ sót improvement tốt hơn ở route khác
- **Thực tế:** First-improvement sẽ `return true` và restart toàn bộ VND (k=0) — nên thứ tự bên trong không quan trọng lắm

**Verdict: Làm được, lợi ích nhỏ. Áp dụng cho `searchOrOpt` và `searchOrOptReversed` ([L6341-L6448](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L6341-L6448)).**

> [!NOTE]
> `searchIntraOrOpt` ([L1843-L1965](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1843-L1965)) **đã đúng thứ tự** `for r → for segLen` rồi — không cần đổi.

---

### [IDEA-E] KNN pruning cho searchCrossExchange — ✅ Đúng và quan trọng nhất

**Hiện tại** tại [L2062](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L2062):
```cpp
for (int j = 1; j < n2 - 1; ++j) {  // O(n2) brute force!
```

Đây là operator tốn kém nhất, và inner loop j vẫn brute-force O(N). Plan muốn dùng `knnCache_[first1]` + `posInRoute` — **hoàn toàn chính xác**.

**Cách implement cụ thể:**
```cpp
// Thay for (int j = 1; j < n2 - 1; ++j)
// Bằng:
for (int nd : knnCache_[first1]) {
    int j = posInRoute_r2[nd];
    if (j < 1 || j >= n2 - 1) continue;
    // Thêm j-1 và j+1 vì segment có thể dài 2-3
    for (int jj : {j-1, j, j+1}) {
        if (jj < 1 || jj >= n2 - 1) continue;
        // ... existing len2 loop ...
    }
}
```

**Cần thêm:** Build `posInRoute_r2` O(n2) một lần per (r1, r2) pair, giống pattern đã dùng trong `searchTwoOpt` và `searchSwap`.

**Verdict: PHẢI LÀM — đây là speedup lớn nhất trong plan.**

---

### [IDEA-F] Lazy multi-objective neighbors — ✅ Đúng

**Hiện tại** `buildMultiNeighbors()` chạy mỗi lần `patchSearchContext()` ([L1024](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L1024)) — tức mỗi lần bất kỳ operator nào improve. Nhưng `workloadNeighbors` và `slackNeighbors` chỉ được dùng bởi k=8 (`searchWorkloadRelocate`) và k=9 (`searchMakespanRelocate`) — chạy cuối cùng, thường ít khi đến.

**Tiết kiệm:** ~O(R²) sort + build mỗi improvement, chỉ thực hiện khi k=8/9 cần.

**Verdict: Làm — dễ, rủi ro thấp.**

---

### [IDEA-G] Compile-time flag cho chrono timers — ✅ Đúng

`EvalScope` tại [L291-L316](file:///d:/Work/SLSCM-LaB/24092025/untitled/src/alns/LocalSearch.cpp#L291-L316) gọi `chrono::now()` 2-3 lần mỗi `evaluateMove()` call. Trong VND hot path, đây là overhead ~100-200ns mỗi call.

**Cách đơn giản nhất:**
```cpp
static constexpr bool ENABLE_EVAL_TIMERS = false; // Bật khi cần diagnose
```
Compiler sẽ DCE (dead code eliminate) toàn bộ chrono khi `false`.

**Verdict: Làm — 5 phút, zero risk.**

---

## Thứ tự triển khai đề xuất (đã điều chỉnh)

| # | Thay đổi | Lý do ưu tiên | Độ khó |
|---|----------|---------------|--------|
| 1 | **IDEA-G**: Compile-time timer flag | 5 phút, zero risk, giảm overhead mỗi evaluateMove | ★☆☆☆☆ |
| 2 | **IDEA-E**: KNN pruning searchCrossExchange | Speedup lớn nhất — loại bỏ O(n²) inner loop | ★★★☆☆ |
| 3 | **IDEA-F**: Lazy multi-obj neighbors | Giảm overhead patchSearchContext | ★★☆☆☆ |
| 4 | **IDEA-D**: Đổi loop order searchOrOpt + searchOrOptReversed | Cache locality tốt hơn | ★★☆☆☆ |
| 5 | **IDEA-C**: Dọn code cũ | ~750 dòng bớt, file dễ đọc | ★☆☆☆☆ |
| 6 | **IDEA-B**: ~~Thread-local scratch Route~~ → **Bỏ qua** | ROI thấp, rủi ro segfault | - |

---

## Open Questions

> [!IMPORTANT]
> 1. **IDEA-B**: Đồng ý bỏ qua? Copy assignment của Route đã rất rẻ nhờ OPT-1 (no COW). Bottleneck thật sự là `evaluate()` O(n), không phải vector copy.
>
> 2. **IDEA-E (CrossExchange KNN)**: Hiện `knnCache_` chứa K=40 nearest customers. Với CrossExchange, `first1` có thể là depot-adjacent → KNN list không cover đủ. Có muốn thêm fallback `if (jCandidates.empty()) brute-force`?
>
> 3. **Xác nhận thứ tự triển khai** ở trên có ok không?
