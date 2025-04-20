# P2P Gossip Network Simulation

A network simulation project that models peer-to-peer gossip protocol communication using NS-3. This simulation creates a configurable P2P network where nodes generate and exchange messages (shares) using a gossip protocol.

## Overview

This project simulates a peer-to-peer network where nodes communicate using a gossip protocol. Each node in the network:

- Connects to other nodes based on a configurable probability
- Periodically generates new message shares
- Forwards received shares to its connected peers
- Tracks statistics on shares generated, received, and forwarded

The simulation ensures that no nodes remain isolated, maintaining network connectivity for all participants.

## Features

- Random network topology generation with configurable connection probability
- Guaranteed connectivity (no isolated nodes)
- Configurable latency between nodes
- TCP-based communication using NS-3 socket API
- Network visualization with NetAnim
- Detailed statistics and logs
- Automatic share generation and propagation

## Requirements

- NS-3 (Network Simulator 3)
- C++ compiler with C++11 support
- NetAnim (for visualization)

## Project Structure

- `p2pnode.h` - Header file for P2P node implementation
- `p2pnode.cpp` - Implementation of P2P node functionality
- `p2pnetwork.cpp` - Main simulation class and entry point

## Building and Running

1. Place these files in your NS-3 scratch directory:
   ```
   cp *.h *.cpp $NS3_DIR/scratch/
   ```

2. run the simulation:
   ```
   ./ns3 run scratch/p2pnetwork.cc
   ```

## Command Line Arguments

- `--numNodes`: Number of nodes in the network (default: 10)
- `--connectionProb`: Probability of connection between nodes (default: 0.3)
- `--simTime`: Simulation time in seconds (default: 60.0)
- `--Latency`: Network latency in milliseconds (default: 5.0)

## Demo Video

A demonstration video of this simulation is available in the same directory as this README:
[View Demo Video](./demo.webm)

## Understanding the Output

The simulation provides detailed logging output:

- Periodic statistics at regular intervals
- Final statistics at the end of the simulation
- Per-node metrics including:
  - Shares generated
  - Shares received
  - Shares forwarded
  - Total shares processed
  - Number of peer connections

## How It Works

1. The simulation creates a network with the specified number of nodes
2. A random topology is generated based on the connection probability
3. Any isolated nodes are connected to ensure network connectivity
4. Each node starts generating shares at random intervals
5. When a node receives a new share, it forwards it to all its peers
6. Statistics are collected and reported throughout the simulation
