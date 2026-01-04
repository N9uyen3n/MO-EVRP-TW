# Phân tích nút thắt & kế hoạch tối ưu hiệu năng (ĐÃ CẬP NHẬT)

Tài liệu này tổng hợp các điểm nghẽn, hướng tối ưu và **tiến độ đã thực hiện**.

---

## 🔴 CÁC NÚT THẮT CỔ CHAI CHÍNH

### 1. **Route::evaluate() – CRITICAL**

**Vấn đề:** Luôn tính lại toàn bộ route `O(N)` dù chỉ thay đổi 1 node.
**Hướng giải quyết:**
1. **[✅ DONE] Lazy Evaluation**: Dùng cờ `isDirty`, chỉ evaluate khi thực sự cần.
2. **[✅ DONE] Incremental Update**: Triển khai theo chiến lược 3-tier (xem Phase 2).
3. **Cache Intermediate Results**: (Chưa thực hiện)
4. **[✅ DONE] Fast Feasibility Check**: Đã thêm `canPossiblyInsert` và `quickCapacityCheck`.

**Ưu tiên:** ⭐⭐⭐⭐⭐

---

### 2. **Route::checkInsertionCost() – CRITICAL**

**Vấn đề:** Được gọi hàng chục nghìn lần, mỗi lần chạy full DP `O(N)`.
**Hướng giải quyết:**
1. **[✅ DONE] Delta Calculation**: Đã triển khai trong `fastForwardCheck`.
2. **Analytical Formula**: (Không áp dụng trực tiếp, thay bằng mô phỏng tăng dần).
3. **[✅ DONE] Bounding Techniques**: Đã triển khai `canPossiblyInsert` và tích hợp vào các repair operators.
4. **Reuse DP Arrays**: (Chưa thực hiện).

**Ưu tiên:** ⭐⭐⭐⭐⭐

---

### 3. **Solution Copy Operations**

**Vấn đề:** `s_new = s_current` trong vòng lặp ALNS copy toàn bộ solution.
**Hướng giải quyết:**
1. **[✅ DONE] Object Pool**: Đã triển khai `SolutionPool` và tích hợp vào `ALNSSolver`.
2. **Move Semantics**: (Không cần thiết sau khi có Object Pool).
3. **In-place Modification + Rollback**: (Giải pháp thay thế, chưa cần lúc này).

**Ưu tiên:** ⭐⭐⭐⭐

---

### 4. **DistanceMatrix Lookups**

**Vấn đề:** `getDistance(i,j)` phải tra map `nodeId_to_index`.
**Hướng giải quyết:**
1. **[✅ DONE] Direct Array Access**: Đã được người dùng implement và tôi đã xác minh.
2. **[✅ DONE] Flatten 2D Vector**: Đã được người dùng implement.
3. **[✅ DONE] Inline / constexpr**: Đã được người dùng implement.
4. **Cache Locality**: (Được cải thiện nhờ flatten vector).

**Ưu tiên:** ⭐⭐⭐

---

### 5. **Local Search – Quét không hiệu quả**

**Vấn đề:** `searchRelocate()` duyệt `O(R² × N²)`.
**Hướng giải quyết:** **[IN PROGRESS]**
1. **[✅ DONE] Spatial Indexing (Lọc không gian)**: Đã triển khai `computeCentroids` và `areRoutesClose` để tạo `neighborLists`.
2. **First Improvement**: (Chưa thực hiện).
3. **[✅ DONE] Candidate List (Node Ranking)**: Đã triển khai `rankNodesByRemovalSavings` để ưu tiên top N node tốt nhất.
4. **Adaptive Granularity**: (Chưa thực hiện).
5. **[✅ DONE] Bounding Checks**: Đã thêm `quickDistanceCheck` vào `searchRelocate`.

**Ưu tiên:** ⭐⭐⭐⭐

---
(Các mục 6-11 giữ nguyên, chưa thực hiện)
---

## 📊 CHIẾN LƯỢC ƯU TIÊN TRIỂN KHAI (ĐÃ CẬP NHẬT)

### Phase 1 – Quick Wins *(~2–3× speedup)*

1. **[✅ DONE]** Lazy evaluation cho Route (`isDirty` flag).
2. **[✅ DONE]** Delta calculation cho `checkInsertionCost()` (Được thay thế bằng chiến lược 3-tier).
3. **[✅ DONE]** Direct array access cho DistanceMatrix.
4. **[✅ DONE]** Giảm copy Solution (Bằng `SolutionPool`).

---

### Phase 2 – Major Improvements *(~5–10× speedup)*

1. **[✅ DONE] Incremental route updates**
   - **[✅ DONE] Tier 3: Bounding Checks**: Đã tạo và tích hợp `canPossiblyInsert`.
   - **[✅ DONE] Tier 2: Forward Approximation**: Đã tạo `fastForwardCheck`.
   - **[✅ DONE] Integration**: Đã tích hợp logic 3-tier vào tất cả các repair operators.
2. **[IN PROGRESS] Pruning trong local search.**
   - **[✅ DONE] Quick Wins**: Hoàn thành 3 bước (Spatial filtering, Node ranking, Distance bound).
   - **[TODO] Medium Term**: Triển khai các bước tiếp theo (Position-level filtering, Two-phase evaluation).
3. **[TODO]** Initial solution tốt hơn.
4. **[✅ DONE]** Fast feasibility checks (Là một phần của Tier 3).

---

### Phase 3 – Advanced *(~2–3× nữa)*

1. **[TODO]** Song song hóa (OpenMP)
2. **[TODO]** Spatial indexing
3. **[TODO]** Operator selection thông minh

**Tổng ước tính:** *20–30× faster nếu triển khai đầy đủ*
