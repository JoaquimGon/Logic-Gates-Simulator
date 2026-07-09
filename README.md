# Logic Gates Simulator

A C++20 digital logic simulator designed to evaluate gate networks and propagate states through a directed graph architecture. The project features topological sorting for event evaluation and built-in dependency cycle detection.

---

## Project Objectives

The ultimate goal of this project is to build a complete, virtual hardware simulation engine from the ground up:
1. **Gate-Level Foundations:** Implement basic logical primitives with deterministic evaluation orders.
2. **Clock-Staged Architecture:** Introduce Latches and Flip-Flops to manage state feedback over synchronized clock cycles.
3. **Sub-Circuit Bundling:** Group discrete logic arrays into modular components (Adders, Multiplexers, Registers).
4. **RISC Core Implementation:** Combine components to construct a functional arithmetic logic unit (ALU) and control units.

---

## System Architecture

The simulation engine evaluates circuits using a three-phase pipeline:



1. **Netlist Construction:** Gates (`AND`, `OR`, `XOR`, `NOT`) are declared in memory and explicitly connected via input and output pin indices.
2. **Evaluation Ordering:** A linear evaluation sequence is generated using a Depth-First Search (DFS) post-order topological sort to map dependencies.
3. **State Propagation:** The circuit steps forward, to forward updated logic values to child pins.

---

## Building the Project

This project requires a compiler that supports **C++20** and **CMake (version 3.10 or higher)**.

To generate the build files and compile the executable, run the following commands in your terminal:

```bash
# Create and navigate to the build directory
mkdir build && cd build

# Generate build system configurations
cmake ..

# Compile the target executable
cmake --build .

# Run the simulation binary
./Logic-Gates-Simulator
