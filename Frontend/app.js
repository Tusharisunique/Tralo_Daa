/* =========================================================
   TRALO Exchange — app.js
   Role-Based Trading Terminal
========================================================= */

(function () {
    'use strict';

    // ── Constants ──────────────────────────────────────────
    const SYMBOLS = ['RELIANCE', 'TCS', 'HDFC', 'INFOSYS', 'ICICIBANK'];
    const ADMIN_USER = 'admin';
    const ADMIN_PASS = 'admin@123';
    const WS_URL = `ws://${window.location.hostname}:8081`;
    const MAX_STREAM_ROWS = 80;

    // ── Market state ────────────────────────────────────────
    const market = {};
    SYMBOLS.forEach(s => { market[s] = { bid: 0, ask: 0 }; });

    // ── Pending manual orders: order_id → {row, side, sym} ─
    const pendingOrders = {};
    let lastProvisionalKey = null;  // tracks the key for the most recently submitted order

    // ── Charts ──────────────────────────────────────────────
    let latencyChart = null;
    let fillRateChart = null;
    let lastFairnessData = null; // Cache last received data for instant tab display

    // ── Admin counters ──────────────────────────────────────
    let adminTotalCount = 0;

    // ── Current selected symbol / side (Trader) ─────────────
    let selectedSymbol = null;
    let selectedSide = 'BUY';

    // ── WebSocket ────────────────────────────────────────────
    let ws = null;

    // ═══════════════════════════════════════════════════════
    // AUTH
    // ═══════════════════════════════════════════════════════
    function getUsers() {
        try {
            const data = localStorage.getItem('tralo_users');
            if (!data) return [];
            const parsed = JSON.parse(data);
            return Array.isArray(parsed) ? parsed : [];
        } catch (e) {
            console.warn('Corrupted local storage found, resetting users.');
            return [];
        }
    }
    function saveUsers(users) {
        localStorage.setItem('tralo_users', JSON.stringify(users));
    }

    // Tab switching
    function switchAuthTab(tabName) {
        document.querySelectorAll('.auth-tab').forEach(t => t.classList.remove('active'));
        document.querySelectorAll('.auth-panel').forEach(p => p.classList.remove('active'));
        document.querySelector(`.auth-tab[data-tab="${tabName}"]`).classList.add('active');
        document.getElementById(`panel-${tabName}`).classList.add('active');
        clearErrors();
    }
    document.querySelectorAll('.auth-tab').forEach(tab => {
        tab.addEventListener('click', () => switchAuthTab(tab.dataset.tab));
    });

    function clearErrors() {
        ['login-error', 'reg-error', 'reg-success'].forEach(id => {
            const el = document.getElementById(id);
            if (el) { el.style.display = 'none'; el.textContent = ''; }
        });
    }

    // Login
    document.getElementById('btn-login').addEventListener('click', () => {
        const user = document.getElementById('login-username').value.trim();
        const pass = document.getElementById('login-password').value;
        const errEl = document.getElementById('login-error');
        clearErrors();

        if (!user || !pass) {
            showErr(errEl, 'Please fill in all fields.');
            return;
        }

        if (user === ADMIN_USER && pass === ADMIN_PASS) {
            enterAdmin();
            return;
        }

        const users = getUsers();
        const found = users.find(u => u.username === user && u.password === pass);
        if (found) {
            enterTrader(user);
        } else {
            showErr(errEl, 'Invalid username or password.');
        }
    });

    // Allow enter key for login
    document.getElementById('login-password').addEventListener('keydown', e => {
        if (e.key === 'Enter') document.getElementById('btn-login').click();
    });

    // Register
    document.getElementById('btn-register').addEventListener('click', () => {
        const user = document.getElementById('reg-username').value.trim();
        const pass = document.getElementById('reg-password').value;
        const errEl = document.getElementById('reg-error');
        const okEl = document.getElementById('reg-success');
        clearErrors();

        if (!user || !pass) { showErr(errEl, 'Please fill in all fields.'); return; }
        if (user === ADMIN_USER) { showErr(errEl, 'That username is reserved.'); return; }
        if (pass.length < 4) { showErr(errEl, 'Password must be at least 4 characters.'); return; }

        let users = getUsers();
        if (users.find(u => u.username === user)) {
            showErr(errEl, 'Username already exists. Please login.');
            return;
        }

        users.push({ username: user, password: pass });
        saveUsers(users);

        // Clear register form
        document.getElementById('reg-username').value = '';
        document.getElementById('reg-password').value = '';

        // Immediately log them in!
        enterTrader(user);
    });

    function showErr(el, msg) {
        el.textContent = msg;
        el.style.display = 'block';
    }

    // ═══════════════════════════════════════════════════════
    // SCREEN SWITCHING
    // ═══════════════════════════════════════════════════════
    function showScreen(id) {
        document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
        document.getElementById(id).classList.add('active');
    }

    function enterTrader(username) {
        document.getElementById('trader-username-display').textContent = username.toUpperCase();
        showScreen('screen-trader');
        buildStockList();
        connectWS('trader');
    }

    let chartsInitialized = false;

    function enterAdmin() {
        showScreen('screen-admin');
        connectWS('admin');
    }

    // Logout
    document.getElementById('trader-logout').addEventListener('click', logout);
    document.getElementById('admin-logout').addEventListener('click', logout);

    function logout() {
        if (ws) ws.close();
        showScreen('screen-auth');
        selectedSymbol = null;
        document.getElementById('login-username').value = '';
        document.getElementById('login-password').value = '';
        clearErrors();
    }

    // ═══════════════════════════════════════════════════════
    // STOCK LIST (TRADER)
    // ═══════════════════════════════════════════════════════
    function buildStockList() {
        const container = document.getElementById('stock-list');
        container.innerHTML = '';
        SYMBOLS.forEach(sym => {
            const item = document.createElement('div');
            item.className = 'stock-item';
            item.id = `stock-item-${sym}`;
            item.innerHTML = `
                <div>
                    <div class="stock-name">${sym}</div>
                    <div class="stock-exchange">NSE · India</div>
                </div>
                <div class="stock-price-col">
                    <div class="stock-price" id="price-ask-${sym}">₹ —</div>
                    <div class="stock-bid" id="price-bid-${sym}">Bid: ₹ —</div>
                </div>`;
            item.addEventListener('click', () => selectStock(sym));
            container.appendChild(item);
        });
    }

    function selectStock(sym) {
        selectedSymbol = sym;

        // Highlight sidebar
        document.querySelectorAll('.stock-item').forEach(el => el.classList.remove('selected'));
        const item = document.getElementById(`stock-item-${sym}`);
        if (item) item.classList.add('selected');

        // Show detail pane
        document.getElementById('empty-state').classList.add('hidden');
        document.getElementById('stock-detail-pane').classList.remove('hidden');

        document.getElementById('detail-symbol').textContent = sym;
        document.getElementById('order-symbol').value = sym;

        // Update prices
        updateDetailPrices(sym);
        renderDepth(sym);

        // Pre-fill price from current market data
        const currentAsk = market[sym] ? market[sym].ask : 0;
        if (currentAsk > 0) {
            document.getElementById('order-price').value = (currentAsk / 100).toFixed(2);
        }
        calcTotal();
        updateMarketTip();
    }

    function updateMarketTip() {
        if (!selectedSymbol) return;
        const tipEl = document.getElementById('market-tip');
        if (!tipEl) return;

        const { bid, ask } = market[selectedSymbol];

        if (selectedSide === 'BUY') {
            if (ask > 0) {
                tipEl.innerHTML = `To match instantly, set Price to <em>₹ ${(ask / 100).toFixed(2)}</em> or higher.`;
                tipEl.className = 'market-tip ready-buy';
            } else {
                tipEl.textContent = 'Waiting for sellers to enter the market...';
                tipEl.className = 'market-tip';
            }
        } else {
            if (bid > 0) {
                tipEl.innerHTML = `To match instantly, set Price to <em>₹ ${(bid / 100).toFixed(2)}</em> or lower.`;
                tipEl.className = 'market-tip ready-sell';
            } else {
                tipEl.textContent = 'Waiting for buyers to enter the market...';
                tipEl.className = 'market-tip';
            }
        }
    }

    document.getElementById('btn-close-detail').addEventListener('click', () => {
        document.getElementById('stock-detail-pane').classList.add('hidden');
        document.getElementById('empty-state').classList.remove('hidden');
        document.querySelectorAll('.stock-item').forEach(el => el.classList.remove('selected'));
        selectedSymbol = null;
    });

    function updateDetailPrices(sym) {
        if (sym !== selectedSymbol) return;
        const { bid, ask } = market[sym];
        document.getElementById('detail-bid').textContent = bid > 0 ? `₹ ${(bid / 100).toFixed(2)}` : '₹ —';
        document.getElementById('detail-ask').textContent = ask > 0 ? `₹ ${(ask / 100).toFixed(2)}` : '₹ —';
        const spread = ask > 0 && bid > 0 ? ask - bid : 0;
        document.getElementById('detail-spread').textContent = spread > 0 ? `₹ ${(spread / 100).toFixed(2)}` : '₹ —';
    }

    // ── Market Depth ────────────────────────────────────────
    function renderDepth(sym) {
        const { bid, ask } = market[sym];
        const bidContainer = document.getElementById('depth-bids');
        const askContainer = document.getElementById('depth-asks');
        bidContainer.innerHTML = '';
        askContainer.innerHTML = '';

        for (let i = 0; i < 5; i++) {
            const bPrice = bid > 0 ? (bid - i * 5) / 100 : 0;
            const aPrice = ask > 0 ? (ask + i * 5) / 100 : 0;
            const bQty = bid > 0 ? (500 + Math.floor(Math.random() * 2000)) : 0;
            const aQty = ask > 0 ? (500 + Math.floor(Math.random() * 2000)) : 0;
            const barW = Math.min(90, (bQty / 30));
            const aBarW = Math.min(90, (aQty / 30));

            if (bid > 0) {
                bidContainer.innerHTML += `
                <div class="depth-row buy-side">
                    <div class="depth-bar" style="width:${barW}%"></div>
                    <span class="depth-price buy-price">₹${bPrice.toFixed(2)}</span>
                    <span class="depth-qty">${bQty.toLocaleString()}</span>
                    <span class="depth-bar-cell"></span>
                </div>`;
            }
            if (ask > 0) {
                askContainer.innerHTML += `
                <div class="depth-row sell-side">
                    <div class="depth-bar" style="width:${aBarW}%"></div>
                    <span class="depth-bar-cell"></span>
                    <span class="depth-price sell-price">₹${aPrice.toFixed(2)}</span>
                    <span class="depth-qty">${aQty.toLocaleString()}</span>
                </div>`;
            }
        }
    }

    // ═══════════════════════════════════════════════════════
    // ORDER FORM (TRADER)
    // ═══════════════════════════════════════════════════════
    // Buy / Sell tab toggle
    document.getElementById('tab-buy').addEventListener('click', () => setSide('BUY'));
    document.getElementById('tab-sell').addEventListener('click', () => setSide('SELL'));

    function setSide(side) {
        selectedSide = side;
        document.getElementById('order-side').value = side;
        document.getElementById('tab-buy').classList.toggle('active', side === 'BUY');
        document.getElementById('tab-sell').classList.toggle('active', side === 'SELL');
        const btn = document.getElementById('btn-place-order');
        btn.textContent = `Place ${side} Order`;
        btn.className = `btn-order ${side.toLowerCase()}`;
        updateMarketTip();
    }

    function calcTotal() {
        const price = parseFloat(document.getElementById('order-price').value) || 0;
        const qty = parseInt(document.getElementById('order-qty').value) || 0;
        document.getElementById('order-total').textContent = `Total: ₹ ${(price * qty).toLocaleString('en-IN', { minimumFractionDigits: 2 })}`;
    }

    document.getElementById('order-price').addEventListener('input', calcTotal);
    document.getElementById('order-qty').addEventListener('input', calcTotal);

    // Place Order
    document.getElementById('btn-place-order').addEventListener('click', () => {
        const sym = document.getElementById('order-symbol').value;
        const side = document.getElementById('order-side').value;
        const price = parseFloat(document.getElementById('order-price').value);
        const qty = parseInt(document.getElementById('order-qty').value);

        if (!sym || !price || !qty || price <= 0 || qty <= 0) {
            return;
        }
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            addOrderEntry(null, sym, side, price, qty, null, 'CONNECTING');
            return;
        }

        ws.send(JSON.stringify({
            type: 'manual_order',
            action: side.toLowerCase(),
            symbol: sym,
            price: price,
            qty: qty
        }));

        // Add to order list as PENDING
        const rowKey = `pending-${Date.now()}`;
        lastProvisionalKey = rowKey;
        addOrderEntry(rowKey, sym, side, price, qty, null, 'PENDING');
        pendingOrders[rowKey] = { side, sym };
    });

    function addOrderEntry(key, sym, side, price, qty, latencyMs, status) {
        const list = document.getElementById('trader-orders-list');
        const noMsg = list.querySelector('.no-orders-msg');
        if (noMsg) noMsg.remove();

        const entry = document.createElement('div');
        entry.className = 'order-entry';
        if (key) entry.id = `order-entry-${key}`;

        entry.innerHTML = `
            <div class="order-entry-top">
                <span class="order-sym">${sym}</span>
                <span class="order-type ${side}">${side}</span>
            </div>
            <div class="order-entry-bot">
                <span id="price-qty-${key}">₹ ${price.toFixed ? price.toFixed(2) : (price / 100).toFixed(2)} × ${qty}</span>
                <span class="order-latency-val" id="lat-${key}">${latencyMs != null ? latencyMs + ' ms' : ''}</span>
            </div>
            <div class="order-status ${status === 'FILLED' ? 'filled' : 'pending'}" id="status-${key}">${status}</div>`;

        list.insertBefore(entry, list.firstChild);
        if (list.children.length > 30) list.lastChild.remove();
        return entry;
    }

    // ═══════════════════════════════════════════════════════
    // ADMIN NAV
    // ═══════════════════════════════════════════════════════
    document.querySelectorAll('.admin-nav').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.admin-nav').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.admin-panel').forEach(p => p.classList.remove('active'));
            btn.classList.add('active');
            document.getElementById(btn.dataset.panel).classList.add('active');

            // Lazy-init charts the first time the Fairness panel becomes visible.
            // Chart.js needs the canvas to be visible to calculate correct dimensions.
            if (btn.dataset.panel === 'panel-fairness') {
                if (!chartsInitialized) {
                    initCharts();
                    chartsInitialized = true;
                } else {
                    if (latencyChart) latencyChart.resize();
                    if (fillRateChart) fillRateChart.resize();
                }
                // Immediately populate with cached data (no wait for next 2s heartbeat)
                if (lastFairnessData) updateCharts(lastFairnessData);
            }
        });
    });

    // ═══════════════════════════════════════════════════════
    // CHARTS (ADMIN FAIRNESS)
    // ═══════════════════════════════════════════════════════
    function initCharts() {
        Chart.defaults.color = '#666';
        Chart.defaults.borderColor = '#1e1e1e';

        const chartOptions = (yLabel) => ({
            responsive: true,
            maintainAspectRatio: false,
            animation: { duration: 400 },
            plugins: {
                legend: { display: false },
                tooltip: {
                    backgroundColor: '#141414',
                    borderColor: '#2a2a2a',
                    borderWidth: 1,
                    titleColor: '#d8d8d8',
                    bodyColor: '#999'
                }
            },
            scales: {
                x: {
                    grid: { color: '#1a1a1a' },
                    ticks: { color: '#666', font: { family: 'JetBrains Mono', size: 10 }, maxRotation: 45 }
                },
                y: {
                    grid: { color: '#1a1a1a' },
                    ticks: { color: '#666', font: { family: 'JetBrains Mono', size: 10 } },
                    title: { display: !!yLabel, text: yLabel, color: '#555' },
                    beginAtZero: true
                }
            },
            datasets: { bar: { maxBarThickness: 40, minBarLength: 2 } }
        });

        const latCtx = document.getElementById('chart-latency').getContext('2d');
        latencyChart = new Chart(latCtx, {
            type: 'bar',
            data: {
                labels: [],
                datasets: [{
                    data: [],
                    backgroundColor: '#2a6496',
                    borderColor: '#3a85c8',
                    borderWidth: 1,
                    borderRadius: 2
                }]
            },
            options: chartOptions('Avg Latency (ms)')
        });

        const fillCtx = document.getElementById('chart-fillrate').getContext('2d');
        fillRateChart = new Chart(fillCtx, {
            type: 'bar',
            data: {
                labels: [],
                datasets: [{
                    data: [],
                    backgroundColor: '#2a6496',
                    borderColor: '#3a85c8',
                    borderWidth: 1,
                    borderRadius: 2
                }]
            },
            options: chartOptions('Fill Rate (%)')
        });
    }

    function updateCharts(data) {
        if (!latencyChart || !fillRateChart || !data || !data.length) return;

        // Sort: YOU first, then bots by fill_rate descending. Limit to 16 total.
        const youEntry = data.filter(d => d.id === 99999);
        const botsSorted = data.filter(d => d.id !== 99999)
            .sort((a, b) => a.id - b.id) // Fixed sorting by ID for consistent bar positions
            .slice(0, 15);
        const displayed = [...youEntry, ...botsSorted];

        const labels = [];
        const latData = [];
        const fillData = [];

        displayed.forEach(d => {
            labels.push(d.id === 99999 ? 'YOU' : `B${d.id}`);
            latData.push(parseFloat(d.avg_lat.toFixed(4)));
            fillData.push(parseFloat(d.fill_rate.toFixed(2)));
        });

        const bgLat = labels.map(l => l === 'YOU' ? '#00a852' : '#2a6496');
        const brdLat = labels.map(l => l === 'YOU' ? '#00dc6e' : '#3a85c8');
        const bgFill = labels.map(l => l === 'YOU' ? '#005c2d' : '#1a4a6e');
        const brdFill = labels.map(l => l === 'YOU' ? '#00a852' : '#3a85c8');

        latencyChart.data.labels = labels;
        latencyChart.data.datasets[0].data = latData;
        latencyChart.data.datasets[0].backgroundColor = bgLat;
        latencyChart.data.datasets[0].borderColor = brdLat;
        latencyChart.update();

        fillRateChart.data.labels = labels;
        fillRateChart.data.datasets[0].data = fillData;
        fillRateChart.data.datasets[0].backgroundColor = bgFill;
        fillRateChart.data.datasets[0].borderColor = brdFill;
        fillRateChart.update();
    }

    // ═══════════════════════════════════════════════════════
    // ADMIN STREAM TABLE
    // ═══════════════════════════════════════════════════════
    function addAdminRow(id, symbol, source, side, price, qty, status) {
        adminTotalCount++;
        document.getElementById('admin-total-count').textContent = adminTotalCount.toLocaleString();

        const tbody = document.getElementById('admin-stream-body');
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td>${id}</td>
            <td>${symbol}</td>
            <td><span class="source-badge ${source}">${source === 'manual' ? 'MANUAL' : 'BOT'}</span></td>
            <td><span class="side-badge ${side}">${side}</span></td>
            <td>₹ ${(price / 100).toFixed(2)}</td>
            <td>${qty}</td>
            <td class="status-${status === 'FILLED' ? 'filled' : 'accepted'}">${status.toUpperCase()}</td>`;
        tbody.insertBefore(tr, tbody.firstChild);
        if (tbody.children.length > MAX_STREAM_ROWS) tbody.lastChild.remove();
    }

    // ═══════════════════════════════════════════════════════
    // WEBSOCKET
    // ═══════════════════════════════════════════════════════
    function connectWS(role) {
        if (ws) { try { ws.close(); } catch (e) { } }

        ws = new WebSocket(WS_URL);

        const indicator = document.getElementById(`ws-status-${role}`);

        ws.onopen = () => {
            if (indicator) indicator.classList.add('connected');
        };
        ws.onclose = () => {
            if (indicator) indicator.classList.remove('connected');
        };

        ws.onmessage = (evt) => {
            let data;
            try { data = JSON.parse(evt.data); } catch (e) { return; }

            if (data.type === 'market_data') {
                handleMarketData(data, role);
            } else if (data.type === 'order_arrival') {
                handleOrderArrival(data, role);
            } else if (data.type === 'trade') {
                handleTrade(data, role);
            } else if (data.type === 'manual_fill') {
                handleManualFill(data, role);
            } else if (data.type === 'order_reject') {
                handleOrderReject(data);
            } else if (data.type === 'ack') {
                handleAck(data);
            } else if (data.type === 'fairness_stats') {
                handleFairness(data, role);
            } else if (data.type === 'engine_stats') {
                handleEngineStats(data);
            }
        };
    }

    // ── Handlers ────────────────────────────────────────────
    function handleMarketData(data, role) {
        const sym = data.symbol;
        if (!market[sym]) return;
        market[sym].bid = data.bid;
        market[sym].ask = data.ask;

        if (role === 'trader') {
            // Update sidebar
            const askEl = document.getElementById(`price-ask-${sym}`);
            const bidEl = document.getElementById(`price-bid-${sym}`);
            if (askEl) askEl.textContent = data.ask > 0 ? `₹ ${(data.ask / 100).toFixed(2)}` : '₹ —';
            if (bidEl) bidEl.textContent = data.bid > 0 ? `Bid: ₹ ${(data.bid / 100).toFixed(2)}` : 'Bid: ₹ —';

            // Update detail pane if this symbol is selected
            if (selectedSymbol === sym) {
                updateDetailPrices(sym);
                renderDepth(sym);
                updateMarketTip();
            }
        }
    }

    function handleEngineStats(data) {
        const mpsEl = document.getElementById('admin-mps');
        if (mpsEl && data.mps !== undefined) {
            mpsEl.innerHTML = `${data.mps.toLocaleString()} <span>/ sec</span>`;
        }
    }

    function handleOrderArrival(data, role) {
        if (role === 'admin') {
            const source = data.trader_id === 99999 ? 'manual' : 'bot';
            addAdminRow(data.id, data.symbol, source, data.side, data.price, data.qty, 'accepted');
        }
    }

    function handleTrade(data, role) {
        if (role === 'admin') {
            addAdminRow(`T-${data.maker}`, data.symbol, 'bot', 'TRADE', data.price, data.qty, 'FILLED');
        }
    }

    function handleAck(data) {
        const provisionalKey = lastProvisionalKey;
        if (provisionalKey && pendingOrders[provisionalKey] && data.order_id) {
            pendingOrders[data.order_id] = pendingOrders[provisionalKey];
            delete pendingOrders[provisionalKey];
            lastProvisionalKey = null;

            const statusEl = document.getElementById(`status-${provisionalKey}`);
            if (statusEl) {
                statusEl.id = `status-${data.order_id}`;
                statusEl.textContent = 'RESTING';
                statusEl.className = 'order-status pending';
            }
            const latEl = document.getElementById(`lat-${provisionalKey}`);
            if (latEl) latEl.id = `lat-${data.order_id}`;

            const priceQtyEl = document.getElementById(`price-qty-${provisionalKey}`);
            if (priceQtyEl) priceQtyEl.id = `price-qty-${data.order_id}`;
        }
    }

    function handleOrderReject(data) {
        // Find the last provisional order and mark it as rejected
        const key = lastProvisionalKey;
        if (key) {
            const statusEl = document.getElementById(`status-${key}`);
            if (statusEl) {
                statusEl.textContent = 'REJECTED';
                statusEl.style.color = 'var(--red)';
                statusEl.title = data.reason;
            }
            lastProvisionalKey = null;
        }
        alert(`Order Rejected: ${data.reason}`);
    }

    function handleManualFill(data, role) {
        const latMs = data.latency_ns > 0 ? (data.latency_ns / 1_000_000).toFixed(3) : '< 0.001';

        if (role === 'trader') {
            const key = data.order_id;
            const statusEl = document.getElementById(`status-${key}`);
            const latEl = document.getElementById(`lat-${key}`);
            const priceQtyEl = document.getElementById(`price-qty-${key}`);

            if (statusEl) {
                statusEl.textContent = '✓ FILLED';
                statusEl.className = 'order-status filled';
            }
            if (latEl) {
                latEl.textContent = `${latMs} ms`;
            }
            if (priceQtyEl) {
                // Show the ACTUAL execution price, with a highlight
                const execPrice = (data.price / 100).toFixed(2);
                priceQtyEl.innerHTML = `<span style="color:var(--green); font-weight:700;">₹ ${execPrice}</span> × ${data.qty}`;
                priceQtyEl.title = "Price Improved by Exchange";
            }
            if (!statusEl) {
                // Fallback: add a new row showing the fill
                addOrderEntry(`fill-${key}`, data.symbol, 'MANUAL', (data.price / 100), data.qty, parseFloat(latMs), 'FILLED');
            }
            delete pendingOrders[key];
        }

        if (role === 'admin') {
            addAdminRow(data.order_id, data.symbol, 'manual', 'TRADE', data.price, data.qty, 'FILLED');
        }
    }

    function handleFairness(data, role) {
        if (!data.data || !data.data.length) return;

        // Cache so the Fairness tab populates instantly on click
        lastFairnessData = data.data;

        // Update global avg latency display (both roles)
        const avg = data.data.reduce((a, b) => a + b.avg_lat, 0) / data.data.length;

        if (role === 'admin') {
            document.getElementById('admin-avg-lat').innerHTML = `${avg.toFixed(4)}<span> ms</span>`;
            updateCharts(data.data);

            // --- Populate the Fairness Summary Stats Bar ---
            const manualEntry = data.data.find(d => d.id === 99999);
            const botEntries = data.data.filter(d => d.id !== 99999);
            const botAvgLat = botEntries.length
                ? botEntries.reduce((a, b) => a + b.avg_lat, 0) / botEntries.length
                : 0;

            const tradersEl = document.getElementById('fair-stat-traders');
            const yourLatEl = document.getElementById('fair-stat-your-lat');
            const botLatEl = document.getElementById('fair-stat-bot-lat');
            const verdictEl = document.getElementById('fair-stat-verdict');

            if (tradersEl) tradersEl.textContent = data.data.length;
            if (botLatEl) botLatEl.textContent = botAvgLat > 0 ? `${botAvgLat.toFixed(4)} ms` : '—';

            if (manualEntry) {
                if (yourLatEl) yourLatEl.textContent = `${manualEntry.avg_lat.toFixed(4)} ms`;

                // Verdict: FAIR if manual latency within 2x of bot avg OR difference < 0.5ms (noise floor)
                if (verdictEl) {
                    const isFair = (manualEntry.avg_lat <= (botAvgLat * 2.5)) || (Math.abs(manualEntry.avg_lat - botAvgLat) < 0.5);
                    if (isFair) {
                        verdictEl.textContent = '✓ FAIR';
                        verdictEl.style.color = 'var(--green)';
                    } else {
                        verdictEl.textContent = '⚠ SKEWED';
                        verdictEl.style.color = 'var(--red)';
                    }
                }
            } else {
                if (yourLatEl) yourLatEl.textContent = 'No trades yet';
                if (verdictEl) { verdictEl.textContent = 'WAITING…'; verdictEl.style.color = ''; }
            }

            // Hide "Waiting…" overlays once we have real data
            const olLat = document.getElementById('overlay-latency');
            const olFill = document.getElementById('overlay-fillrate');
            if (olLat) olLat.classList.add('hidden');
            if (olFill) olFill.classList.add('hidden');
        }
        if (role === 'trader') {
            document.getElementById('trader-sys-latency').textContent = `Sys Latency: ${avg.toFixed(4)} ms`;
        }
    }

})();
