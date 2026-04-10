import asyncio
import websockets
import subprocess
import json
import os
import threading
from http.server import HTTPServer, SimpleHTTPRequestHandler

import queue

engine_process = subprocess.Popen(
    ['./build/tralo_exchange'],
    stdout=subprocess.PIPE,
    stdin=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
    cwd=os.path.dirname(os.path.abspath(__file__))
)

connected_clients = set()

# Thread-safe queue to pair incoming manual orders with their source WebSocket
# This ensures that 'ack' messages from the engine are routed back to the correct client.
order_sync_queue = queue.Queue()

# Maps order_id -> websocket so we can send match confirmation back to the right client
pending_manual_orders = {}
pending_lock = threading.Lock()

async def broadcast(message):
    if connected_clients:
        dead = set()
        for client in connected_clients:
            try:
                await client.send(message)
            except Exception:
                dead.add(client)
        connected_clients.difference_update(dead)

async def send_to(websocket, message):
    try:
        await websocket.send(message)
    except Exception:
        pass

def stdout_reader_thread(loop):
    for line in engine_process.stdout:
        line = line.strip()
        if not line:
            continue
        if not line.startswith('{'):
            print(f"[C++] {line}")
            continue
        try:
            data = json.loads(line)
        except Exception:
            continue

        if data.get('type') == 'trade':
            maker_id = data.get('maker')
            taker_id = data.get('taker')
            match_ws = None
            user_order_id = None

            with pending_lock:
                if maker_id in pending_manual_orders:
                    match_ws = pending_manual_orders.get(maker_id)
                    user_order_id = maker_id
                elif taker_id in pending_manual_orders:
                    match_ws = pending_manual_orders.get(taker_id)
                    user_order_id = taker_id

            if match_ws:
                print(f"[Match Found] User Order {user_order_id} matched {data['qty']} @ {data['price']}")
                msg = json.dumps({
                    "type": "manual_fill",
                    "symbol": data.get('symbol'),
                    "order_id": user_order_id,
                    "qty": data['qty'],
                    "price": data['price'],
                    "latency_ns": data.get('latency_ns', 0)
                })
                asyncio.run_coroutine_threadsafe(send_to(match_ws, msg), loop)
            
            asyncio.run_coroutine_threadsafe(broadcast(line), loop)

        elif data.get('type') == 'ack':
            order_id = data.get('order_id')
            with pending_lock:
                try:
                    match_ws = order_sync_queue.get_nowait()
                    pending_manual_orders[order_id] = match_ws
                except queue.Empty:
                    print(f"[Warning] Received ACK for order {order_id} but sync queue was empty!")
            asyncio.run_coroutine_threadsafe(broadcast(line), loop)

        elif data.get('type') == 'reject':
            with pending_lock:
                try:
                    match_ws = order_sync_queue.get_nowait()
                    msg = json.dumps({
                        "type": "order_reject",
                        "symbol": data.get('symbol'),
                        "reason": data.get('reason')
                    })
                    asyncio.run_coroutine_threadsafe(send_to(match_ws, msg), loop)
                except queue.Empty:
                    pass
            asyncio.run_coroutine_threadsafe(broadcast(line), loop)

        else:
            asyncio.run_coroutine_threadsafe(broadcast(line), loop)

async def websocket_handler(websocket):
    connected_clients.add(websocket)
    print(f"Client connected. Total: {len(connected_clients)}")
    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                if data.get('type') == 'manual_order':
                    action  = data.get('action', 'buy').upper()   # BUY or SELL
                    symbol  = data.get('symbol', 'RELIANCE').upper()
                    price   = int(float(data.get('price', 0)) * 100) # Rupees to Paise
                    qty     = int(data.get('qty', 0))
                    trader_id = 99999

                    if price > 0 and qty > 0:
                        # Format: BUY RELIANCE 250000 10 99999
                        cmd = f"{action} {symbol} {price} {qty} {trader_id}\n"
                        # Enqueue the WebSocket BEFORE sending to engine to maintain FIFO ordering
                        order_sync_queue.put(websocket)
                        engine_process.stdin.write(cmd)
                        engine_process.stdin.flush()
                        print(f"[Manual Order] {cmd.strip()}")
            except Exception as e:
                print(f"Error handling websocket message: {e}")
    finally:
        connected_clients.discard(websocket)
        print(f"Client disconnected. Total: {len(connected_clients)}")

def start_http_server():
    frontend_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Frontend')
    os.chdir(frontend_path)
    server = HTTPServer(('0.0.0.0', 8080), SimpleHTTPRequestHandler)
    print("HTTP server running on http://localhost:8080")
    server.serve_forever()

async def main():
    loop = asyncio.get_running_loop()
    http_thread = threading.Thread(target=start_http_server, daemon=True)
    http_thread.start()
    stdout_thread = threading.Thread(target=stdout_reader_thread, args=(loop,), daemon=True)
    stdout_thread.start()
    async with websockets.serve(websocket_handler, "0.0.0.0", 8081):
        await asyncio.Future()

if __name__ == '__main__':
    asyncio.run(main())
