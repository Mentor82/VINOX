#!/usr/bin/env python3
"""
VINOX Sandbox Microservice
Demonstriert einen einfachen API-Endpunkt mit Health-Check und Rechenoperationen.
"""

import sys
from utils.math_helpers import calculate_growth_rate, compute_hypotenuse

def health_check():
    return {"status": "healthy", "service": "vinox-sandbox-worker", "version": "1.0.0"}

def main():
    print("Starte VINOX Sandbox Worker...")
    status = health_check()
    print(f"Status: {status}")
    
    # Beispielberechnungen
    hyp = compute_hypotenuse(12, 16)
    print(f"Test-Hypotenuse (12, 16): {hyp}")
    
    growth = calculate_growth_rate(1000, 1500, 5)
    print(f"Wachstumsrate: {growth:.2f}%")

if __name__ == "__main__":
    main()
