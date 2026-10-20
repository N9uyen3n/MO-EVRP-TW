"""Sanity checks for the independent fixed-order charging LP."""
import unittest

from validate_legacy_front import lp_schedule


class ChargingLPTests(unittest.TestCase):
    def setUp(self):
        self.nodes = {
            "D0": dict(kind="d", x=0, y=0, demand=0, ready=0, due=100, service=0),
            "C1": dict(kind="c", x=8, y=0, demand=1, ready=0, due=100, service=0),
            "S1": dict(kind="f", x=5, y=0, demand=0, ready=0, due=100, service=0),
        }
        self.params = dict(Q=10, C=10, r=1, g=1, v=1)

    def solve(self, sequence, mode="free"):
        return lp_schedule([dict(node=n) for n in sequence], self.nodes, self.params, mode)

    def test_cannot_recharge_before_reaching_station(self):
        self.nodes["S1"]["x"] = 11
        self.assertFalse(self.solve(["D0", "S1", "D0"])["feasible"])

    def test_requires_return_energy(self):
        self.assertFalse(self.solve(["D0", "C1", "D0"])["feasible"])

    def test_repeated_station_and_partial_charge(self):
        result = self.solve(["D0", "S1", "C1", "S1", "D0"])
        self.assertTrue(result["feasible"])
        self.assertAlmostEqual(result["distance"], 16)
        self.assertAlmostEqual(result["charge"], 6)
        self.assertAlmostEqual(result["duration"], 22)

    def test_customer_deadline(self):
        self.nodes["C1"]["due"] = 7
        self.assertFalse(self.solve(["D0", "S1", "C1", "S1", "D0"])["feasible"])

    def test_station_must_finish_charging_before_closing(self):
        self.nodes["C1"]["x"] = 3
        self.nodes["S1"].update(x=2, due=3)
        self.params["Q"] = 4
        self.assertFalse(self.solve(["D0", "S1", "C1", "D0"])["feasible"])
        self.nodes["S1"]["due"] = 4
        self.assertTrue(self.solve(["D0", "S1", "C1", "D0"])["feasible"])

    def test_service_start_deadline_allows_service_to_finish_later(self):
        self.nodes["C1"].update(x=2, due=2, service=10)
        result = self.solve(["D0", "C1", "D0"])
        self.assertTrue(result["feasible"])
        self.assertAlmostEqual(result["duration"], 14)

    def test_rounding_and_wrong_battery_are_separate_from_actions(self):
        self.nodes["C1"]["x"] = 2
        rows = [dict(node="D0", arrival=0, departure=0, battery=10, charge=0),
                dict(node="C1", arrival=2, departure=2, battery=1, charge=0),
                dict(node="D0", arrival=4, departure=4, battery=0, charge=0)]
        self.assertFalse(lp_schedule(rows, self.nodes, self.params, "all")["feasible"])
        self.assertTrue(lp_schedule(rows, self.nodes, self.params, "actions")["feasible"])
        self.assertTrue(lp_schedule(rows, self.nodes, self.params, "free")["feasible"])
        rows[1]["battery"], rows[2]["battery"] = 8, 6
        rows[1]["arrival"] = 2.004  # Exact arrival 2 lies inside the rounding interval.
        self.assertTrue(lp_schedule(rows, self.nodes, self.params, "all")["feasible"])


if __name__ == "__main__":
    unittest.main()
