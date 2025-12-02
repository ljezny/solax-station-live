// Solar Station Live - Web Dashboard App
(function() {
    'use strict';

    const REFRESH_INTERVAL = 5000; // 5 seconds
    let lastData = null;

    // DOM Elements
    const elements = {
        // PV
        pvPower: document.getElementById('pvPower'),
        pv1Power: document.getElementById('pv1Power'),
        pv2Power: document.getElementById('pv2Power'),
        pvPercent: document.getElementById('pvPercent'),
        pvToday: document.getElementById('pvToday'),
        pvTotal: document.getElementById('pvTotal'),
        
        // Battery
        soc: document.getElementById('soc'),
        batteryPowerAbs: document.getElementById('batteryPowerAbs'),
        batteryBar: document.getElementById('batteryBar'),
        batteryTime: document.getElementById('batteryTime'),
        batteryTemp: document.getElementById('batteryTemp'),
        batteryTile: document.getElementById('batteryTile'),
        batteryChargedToday: document.getElementById('batteryChargedToday'),
        batteryDischargedToday: document.getElementById('batteryDischargedToday'),
        
        // Inverter
        inverterPower: document.getElementById('inverterPower'),
        L1Power: document.getElementById('L1Power'),
        L2Power: document.getElementById('L2Power'),
        L3Power: document.getElementById('L3Power'),
        L1Bar: document.getElementById('L1Bar'),
        L2Bar: document.getElementById('L2Bar'),
        L3Bar: document.getElementById('L3Bar'),
        inverterSN: document.getElementById('inverterSN'),
        inverterMode: document.getElementById('inverterMode'),
        inverterTemp: document.getElementById('inverterTemp'),
        
        // Grid
        gridPower: document.getElementById('gridPower'),
        gridPowerDisplay: document.getElementById('gridPowerDisplay'),
        gridL1Power: document.getElementById('gridL1Power'),
        gridL2Power: document.getElementById('gridL2Power'),
        gridL3Power: document.getElementById('gridL3Power'),
        gridL1Bar: document.getElementById('gridL1Bar'),
        gridL2Bar: document.getElementById('gridL2Bar'),
        gridL3Bar: document.getElementById('gridL3Bar'),
        gridTile: document.getElementById('gridTile'),
        gridBars: document.getElementById('gridBars'),
        gridBuyToday: document.getElementById('gridBuyToday'),
        gridSellToday: document.getElementById('gridSellToday'),
        
        // Load
        loadPower: document.getElementById('loadPower'),
        loadTile: document.getElementById('loadTile'),
        homeLoadPower: document.getElementById('homeLoadPower'),
        loadToday: document.getElementById('loadToday'),
        selfUsePercent: document.getElementById('selfUsePercent'),
        
        // Intelligence
        intelligenceSavings: document.getElementById('intelligenceSavings'),
        intelligenceMode: document.getElementById('intelligenceMode'),
        spotPrice: document.getElementById('spotPrice'),
        
        // Status
        clock: document.getElementById('clock'),
        statusIndicator: document.getElementById('statusIndicator'),
        statusText: document.getElementById('statusText'),
        version: document.getElementById('version')
    };

    // Fetch data from API
    async function fetchData() {
        try {
            setStatus('loading', 'Updating...');
            
            const response = await fetch('/api/data');
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            
            const data = await response.json();
            updateUI(data);
            lastData = data;
            
            setStatus('ok', 'Connected');
        } catch (error) {
            console.error('Fetch error:', error);
            setStatus('error', 'Connection error');
        }
    }

    // Update UI with data
    function updateUI(data) {
        // PV Power
        const pvPower = (data.pv1Power || 0) + (data.pv2Power || 0) + (data.pv3Power || 0) + (data.pv4Power || 0);
        updateValue(elements.pvPower, pvPower);
        updateValue(elements.pv1Power, data.pv1Power || 0);
        updateValue(elements.pv2Power, data.pv2Power || 0);
        
        // PV percentage (estimate based on typical max)
        const maxPvPower = 10000; // 10kW typical max
        const pvPercent = Math.min(100, Math.round((pvPower / maxPvPower) * 100));
        elements.pvPercent.textContent = pvPercent + '%';
        
        // Battery
        updateValue(elements.soc, data.soc || 0);
        elements.batteryBar.style.width = (data.soc || 0) + '%';
        
        const batteryPower = data.batteryPower || 0;
        updateValue(elements.batteryPowerAbs, Math.abs(batteryPower));
        
        // Battery time estimate
        if (data.batteryCapacityWh && batteryPower !== 0) {
            const remainingWh = (data.soc / 100) * data.batteryCapacityWh;
            if (batteryPower > 0) {
                // Discharging
                const hours = remainingWh / batteryPower;
                elements.batteryTime.textContent = formatTime(hours);
                elements.batteryTile.classList.add('discharging');
                elements.batteryTile.classList.remove('charging');
            } else {
                // Charging
                const toFillWh = ((100 - data.soc) / 100) * data.batteryCapacityWh;
                const hours = toFillWh / Math.abs(batteryPower);
                elements.batteryTime.textContent = formatTime(hours);
                elements.batteryTile.classList.remove('discharging');
                elements.batteryTile.classList.add('charging');
            }
        } else {
            elements.batteryTime.textContent = '';
            elements.batteryTile.classList.remove('discharging', 'charging');
        }
        
        if (elements.batteryTemp) {
            elements.batteryTemp.textContent = (data.batteryTemperature || '--') + '°C';
        }
        updateValue(elements.batteryChargedToday, (data.batteryChargedToday || 0).toFixed(1));
        updateValue(elements.batteryDischargedToday, (data.batteryDischargedToday || 0).toFixed(1));
        
        // Inverter
        const inverterPower = (data.L1Power || 0) + (data.L2Power || 0) + (data.L3Power || 0);
        updateValue(elements.inverterPower, inverterPower);
        updateValue(elements.L1Power, data.L1Power || 0);
        updateValue(elements.L2Power, data.L2Power || 0);
        updateValue(elements.L3Power, data.L3Power || 0);
        
        // Phase bars (max 5000W per phase)
        const maxPhase = 5000;
        elements.L1Bar.style.width = Math.min(100, (Math.abs(data.L1Power || 0) / maxPhase) * 100) + '%';
        elements.L2Bar.style.width = Math.min(100, (Math.abs(data.L2Power || 0) / maxPhase) * 100) + '%';
        elements.L3Bar.style.width = Math.min(100, (Math.abs(data.L3Power || 0) / maxPhase) * 100) + '%';
        
        elements.inverterSN.textContent = data.sn || '--';
        if (elements.inverterTemp) {
            elements.inverterTemp.textContent = (data.inverterTemperature || '--') + '°C';
        }
        
        // Inverter mode
        const modeNames = {
            0: 'UNKNOWN',
            1: 'SELF USE',
            2: 'CHARGE',
            3: 'DISCHARGE',
            4: 'HOLD',
            5: 'NORMAL'
        };
        const modeName = modeNames[data.inverterMode] || 'NORMAL';
        elements.inverterMode.textContent = modeName;
        
        // Update intelligence mode too
        if (elements.intelligenceMode) {
            elements.intelligenceMode.textContent = modeName;
            elements.intelligenceMode.className = 'intelligence-badge mode';
            if (modeName === 'SELF USE') {
                elements.intelligenceMode.classList.add('self-use');
            }
        }
        
        // Grid
        const gridPower = (data.gridPowerL1 || 0) + (data.gridPowerL2 || 0) + (data.gridPowerL3 || 0);
        updateValue(elements.gridPower, Math.abs(gridPower));
        if (elements.gridPowerDisplay) {
            updateValue(elements.gridPowerDisplay, gridPower);
        }
        updateValue(elements.gridL1Power, data.gridPowerL1 || 0);
        updateValue(elements.gridL2Power, data.gridPowerL2 || 0);
        updateValue(elements.gridL3Power, data.gridPowerL3 || 0);
        
        // Grid bars
        elements.gridL1Bar.style.width = Math.min(100, (Math.abs(data.gridPowerL1 || 0) / maxPhase) * 100) + '%';
        elements.gridL2Bar.style.width = Math.min(100, (Math.abs(data.gridPowerL2 || 0) / maxPhase) * 100) + '%';
        elements.gridL3Bar.style.width = Math.min(100, (Math.abs(data.gridPowerL3 || 0) / maxPhase) * 100) + '%';
        
        // Grid tile color (green when selling)
        const isSelling = gridPower < 0;
        if (elements.gridTile) {
            elements.gridTile.classList.toggle('selling', isSelling);
        }
        if (elements.gridBars) {
            elements.gridBars.classList.toggle('selling', isSelling);
        }
        
        updateValue(elements.gridBuyToday, (data.gridBuyToday || 0).toFixed(1));
        updateValue(elements.gridSellToday, (data.gridSellToday || 0).toFixed(1));
        
        // Load power
        const loadPower = data.loadPower || 0;
        updateValue(elements.loadPower, loadPower);
        if (elements.homeLoadPower) {
            updateValue(elements.homeLoadPower, loadPower);
        }
        
        // Stats
        updateValue(elements.pvToday, (data.pvToday || 0).toFixed(1));
        updateValue(elements.pvTotal, (data.pvTotal || 0).toFixed(0));
        updateValue(elements.loadToday, (data.loadToday || 0).toFixed(1));
        
        // Self-use percentage
        const loadToday = data.loadToday || 0;
        const gridBuyToday = data.gridBuyToday || 0;
        const selfUse = loadToday > 0 ? Math.round(((loadToday - gridBuyToday) / loadToday) * 100) : 0;
        updateValue(elements.selfUsePercent, Math.max(0, Math.min(100, selfUse)));
        
        // Intelligence savings (placeholder)
        if (elements.intelligenceSavings) {
            elements.intelligenceSavings.textContent = '+12 CZK';
        }
        
        // Spot price (placeholder)
        if (elements.spotPrice) {
            elements.spotPrice.textContent = '3,11 CZK / kWh';
        }
    }

    // Helper to update value with animation
    function updateValue(element, value) {
        if (!element) return;
        
        const newText = String(value);
        if (element.textContent !== newText) {
            element.textContent = newText;
            element.classList.add('updated');
            setTimeout(() => element.classList.remove('updated'), 300);
        }
    }

    // Format time duration
    function formatTime(hours) {
        if (!isFinite(hours) || hours < 0 || hours > 99) return '--';
        
        const d = Math.floor(hours / 24);
        const h = Math.floor(hours % 24);
        const m = Math.floor((hours - Math.floor(hours)) * 60);
        
        if (d > 0) {
            return d + 'd ' + h + 'h ' + m + 'm';
        }
        if (h > 0) {
            return h + 'h ' + m + 'm';
        }
        return m + 'm';
    }

    // Set connection status
    function setStatus(status, text) {
        elements.statusIndicator.className = 'status-indicator ' + status;
        elements.statusText.textContent = text;
    }

    // Update clock
    function updateClock() {
        const now = new Date();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        elements.clock.textContent = hours + ':' + minutes;
    }

    // Initialize
    function init() {
        console.log('Solar Station Live - Web Dashboard');
        
        // Initial clock
        updateClock();
        setInterval(updateClock, 1000);
        
        // Initial fetch
        fetchData();
        
        // Periodic refresh
        setInterval(fetchData, REFRESH_INTERVAL);
    }

    // Start when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
