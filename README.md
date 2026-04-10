# TRALO - High-Performance Exchange Simulation

TRALO is a professional-grade stock exchange simulator designed with a C++ matching engine and a modern web interface. It simulates low-latency market mechanics including price-time priority, price improvement, and circuit breakers.

## Project Structure

### `/Frontend`
The web-based trading terminal.
- **index.html**: Main trading interface and admin dashboard.
- **app.js**: Client-side logic for WebSocket communication and UI updates.
- **style.css**: Monotonous charcoal-themed professional trading UI.
- **guide.html**: Comprehensive guide on exchange mechanics and glossary.

### `/Backend/src`
The entry point of the exchange system.
- **exchange_main.cpp**: The unified C++ master process that coordinates the Load Generator, Matching Engine, and Fairness Analyzer.

### `/Backend/Order Matching Engine`
The core matching logic.
- **OrderBook.h/cpp**: Implements $O(1)$ limit order matching using linear price arrays and doubly linked lists.
- **MemoryPool.h**: Static object pool for nanosecond order allocation without OS overhead.

### `/Backend/Load Generator`
Bot simulation layer.
- **LoadGenerator.h/cpp**: Parallel manager for trading bots.
- **TraderProfiles.h**: Defines behavior for Retail, HFT, Market Maker, and Sniper bots.

### `/Backend/Fairness Analyzer`
The auditing system.
- **FairnessAnalyzer.h/cpp**: Tracks order-to-match latency in real-time to ensure no bias between bot and manual trades.

### Support Files
- **server.py**: The Python-based bridge connecting the C++ engine logic to the Frontend.
- **run.sh**: Unified build and launch script.

## Core Technologies
- **Engine**: C++ 17 (optimized for $O(1)$ complexity).
- **Communication**: WebSockets (Python `websockets` library).
- **Frontend**: Vanilla JavaScript (Chart.js for analytics).
- **Data Structures**: Linear Price Arrays, Doubly Linked Lists, Object Pools, Concurrency-Safe blocking queues.

## How to Run
```bash
bash run.sh
```
Open **http://localhost:8080** in your browser.
