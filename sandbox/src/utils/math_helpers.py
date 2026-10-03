"""
Mathematische Hilfsfunktionen für den VINOX Sandbox Microservice.
"""

import math

def compute_hypotenuse(a: float, b: float) -> float:
    """Berechnet die Hypotenuse nach dem Satz des Pythagoras."""
    return math.sqrt(a**2 + b**2)

def calculate_growth_rate(start_val: float, end_val: float, periods: int) -> float:
    """Berechnet die durchschnittliche Wachstumsrate pro Periode in Prozent."""
    if start_val <= 0 or periods <= 0:
        return 0.0
    return ((end_val / start_val) ** (1.0 / periods) - 1.0) * 100.0

def kinetic_energy(mass_kg: float, velocity_ms: float) -> float:
    """Berechnet die kinetische Energie E_k = 0.5 * m * v^2 in Joule."""
    return 0.5 * mass_kg * (velocity_ms ** 2)
