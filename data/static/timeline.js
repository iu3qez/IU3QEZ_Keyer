// Timeline Renderer - Canvas-based scrolling timeline visualization

class TimelineRenderer {
    constructor(canvasId, config = {}) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) {
            console.error('Canvas not found:', canvasId);
            return;
        }

        this.ctx = this.canvas.getContext('2d');

        // Configuration
        this.config = {
            duration: config.duration || 3,         // seconds visible (ridotto per zoom)
            wpm: config.wpm || 20,                  // current WPM
            updateRate: config.updateRate || 100,   // ms between renders
            ...config
        };

        // Canvas dimensions
        this.width = this.canvas.width;
        this.height = this.canvas.height;

        // Row layout (scalato per canvas 400px)
        this.ROW_HEIGHT = 120;
        this.ROW_MARGIN = 10;
        this.DOT_ROW_Y = 20;
        this.DASH_ROW_Y = 150;
        this.OUTPUT_ROW_Y = 280;

        // State heights
        this.STATE_HIGH = 20;   // Pressed/ON
        this.STATE_LOW = 90;    // Released/OFF

        // Colors
        this.COLORS = {
            DOT: '#3498db',      // Blue
            DASH: '#e74c3c',     // Red
            OUTPUT: '#27ae60',   // Green
            IAMBIC: '#f39c12',   // Orange
            BACKGROUND: '#ecf0f1',
            GRID: '#bdc3c7',
            TEXT: '#2c3e50'
        };

        // Events storage
        this.events = [];             // All events with absolute timestamps
        this.firstTimestamp = null;   // First event timestamp (anchor from ESP32)
        this.lastTimestamp = null;    // Most recent event timestamp
        this.firstReceivedTime = null; // performance.now() quando ricevuto primo evento

        // WebSocket
        this.ws = null;
        this.wsConnected = false;

        // State tracking for drawing
        this.dotState = false;
        this.dashState = false;
        this.outputState = false;

        // Animation
        this.animationId = null;
        this.lastRenderTime = 0;

        // Decoded text
        this.decodedText = '';
        this.decodedTextElement = null;

        this.initCanvas();
    }

    initCanvas() {
        // Set canvas size to full viewport
        this.canvas.width = window.innerWidth;
        this.canvas.height = window.innerHeight;
        this.width = this.canvas.width;
        this.height = this.canvas.height;

        // Update row layout for full screen
        this.ROW_HEIGHT = Math.floor(this.height / 3) - 40;
        this.DOT_ROW_Y = 60;
        this.DASH_ROW_Y = this.DOT_ROW_Y + this.ROW_HEIGHT + 20;
        this.OUTPUT_ROW_Y = this.DASH_ROW_Y + this.ROW_HEIGHT + 20;
        this.STATE_HIGH = 20;
        this.STATE_LOW = this.ROW_HEIGHT - 40;

        console.log('Timeline canvas initialized:', this.width, 'x', this.height);

        // Re-init on window resize
        window.addEventListener('resize', () => {
            this.canvas.width = window.innerWidth;
            this.canvas.height = window.innerHeight;
            this.width = this.canvas.width;
            this.height = this.canvas.height;

            // Recalculate row layout
            this.ROW_HEIGHT = Math.floor(this.height / 3) - 40;
            this.DOT_ROW_Y = 60;
            this.DASH_ROW_Y = this.DOT_ROW_Y + this.ROW_HEIGHT + 20;
            this.OUTPUT_ROW_Y = this.DASH_ROW_Y + this.ROW_HEIGHT + 20;
            this.STATE_LOW = this.ROW_HEIGHT - 40;
        });
    }

    // Connect to WebSocket
    connectWebSocket() {
        const wsUrl = `ws://${window.location.hostname}/ws/timeline`;
        console.log('Connecting to WebSocket:', wsUrl);

        this.ws = new WebSocket(wsUrl);

        this.ws.onopen = () => {
            console.log('WebSocket connected');
            this.wsConnected = true;
        };

        this.ws.onclose = () => {
            console.log('WebSocket disconnected');
            this.wsConnected = false;
            // Reconnect after 5 seconds
            setTimeout(() => this.connectWebSocket(), 5000);
        };

        this.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };

        this.ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                this.processEvents(data);
            } catch (e) {
                console.error('Error parsing WebSocket message:', e);
            }
        };
    }

    // Process incoming events from WebSocket
    processEvents(data) {
        if (!data.events || data.events.length === 0) {
            return;
        }

        if (typeof data.wpm === 'number' && data.wpm > 0) {
            this.config.wpm = data.wpm;
        }

        data.events.forEach(evt => {
            // DEBUG: stampa evento ricevuto se è DECODED_CHAR o SPACE
            if (evt.type === 'DECODED_CHAR' || evt.type === 'SPACE_CHAR' || evt.type === 'SPACE_WORD') {
                console.log('[EVENT]', evt.type, evt);
            }

            // Handle decoded characters
            if (evt.type === 'DECODED_CHAR') {
                this.handleDecodedChar(evt.char);
                return;  // Non aggiungiamo DECODED_CHAR alla timeline grafica
            }

            // Store event with absolute timestamp (microseconds)
            this.events.push({
                type: evt.type,
                timestamp: evt.ts,
                flags: evt.flags || []
            });

            // Track first and last timestamps
            if (this.firstTimestamp === null) {
                this.firstTimestamp = evt.ts;
                this.firstReceivedTime = performance.now(); // Anchor per sync real-time
            }
            this.lastTimestamp = evt.ts;

            // Update state tracking
            this.updateState(evt.type);
        });

        // Clean old events
        this.cleanOldEvents();
    }

    handleDecodedChar(char) {
        // DEBUG: stampa carattere ricevuto
        console.log('[DECODED_CHAR] Received:', JSON.stringify(char), 'charCode:', char ? char.charCodeAt(0) : 'null');

        // Aggiungi carattere al testo decodificato
        this.decodedText += char;

        // Limita lunghezza (ultimi 200 caratteri)
        if (this.decodedText.length > 200) {
            this.decodedText = this.decodedText.substring(this.decodedText.length - 200);
        }

        // Aggiorna display HTML
        this.updateDecodedTextDisplay();
    }

    updateDecodedTextDisplay() {
        if (!this.decodedTextElement) {
            this.decodedTextElement = document.getElementById('decoded-text-output');
        }

        if (this.decodedTextElement) {
            if (this.decodedText.length === 0) {
                this.decodedTextElement.textContent = 'Waiting for input...';
            } else {
                this.decodedTextElement.textContent = this.decodedText;
            }
        }
    }

    updateState(eventType) {
        switch (eventType) {
            case 'DOT_PRESS':
                this.dotState = true;
                break;
            case 'DOT_RELEASE':
                this.dotState = false;
                break;
            case 'DASH_PRESS':
                this.dashState = true;
                break;
            case 'DASH_RELEASE':
                this.dashState = false;
                break;
            case 'KEY_ON':
                this.outputState = true;
                break;
            case 'KEY_OFF':
                this.outputState = false;
                break;
        }
    }

    cleanOldEvents() {
        if (!this.lastTimestamp) return;

        // Keep events within visible duration + 5 second buffer
        const cutoffTime = this.lastTimestamp - ((this.config.duration + 5) * 1000 * 1000); // microseconds
        this.events = this.events.filter(evt => evt.timestamp > cutoffTime);

        // Update first timestamp
        if (this.events.length > 0) {
            this.firstTimestamp = this.events[0].timestamp;
        }
    }

    // Start animation loop
    start() {
        this.connectWebSocket();
        this.lastRenderTime = performance.now();
        this.animate();
    }

    // Stop animation
    stop() {
        if (this.animationId) {
            cancelAnimationFrame(this.animationId);
            this.animationId = null;
        }
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }

    // Animation loop
    animate() {
        // Render
        this.render();

        this.animationId = requestAnimationFrame(() => this.animate());
    }

    // Calculate current time window for rendering
    getTimeWindow() {
        if (!this.lastTimestamp) {
            return null;
        }

        const durationUs = this.config.duration * 1000 * 1000; // microseconds

        // Usa SEMPRE lastTimestamp come riferimento (finestra ferma quando eventi finiscono)
        // La finestra mostra gli ultimi N secondi di eventi ricevuti
        const windowEnd = this.lastTimestamp;
        const windowStart = windowEnd - durationUs;

        return {
            start: windowStart,
            end: windowEnd,
            durationUs: durationUs,
            pixelsPerUs: this.width / durationUs
        };
    }

    // Main render function
    render() {
        // Clear canvas
        this.ctx.fillStyle = this.COLORS.BACKGROUND;
        this.ctx.fillRect(0, 0, this.width, this.height);

        // Calculate time window ONCE for all draw functions
        const timeWindow = this.getTimeWindow();
        if (!timeWindow) {
            console.log('[RENDER] No timeWindow - lastTimestamp:', this.lastTimestamp);
            return;
        }

        // DEBUG
        if (this.events.length === 0 && this.lastTimestamp) {
            console.log('[RENDER] No events but lastTimestamp exists:', this.lastTimestamp);
        }

        // Draw time grid
        this.drawTimeGrid(timeWindow);

        // Draw rows
        this.drawPaddleRow('DOT', this.DOT_ROW_Y, timeWindow);
        this.drawPaddleRow('DASH', this.DASH_ROW_Y, timeWindow);
        this.drawOutputRow(this.OUTPUT_ROW_Y, timeWindow);

        // Draw space markers spanning all three rows
        this.drawSpaceMarkers(timeWindow);

        // Draw labels (SEMPRE disegnate)
        this.drawLabels();

        // Draw connection status
        this.drawConnectionStatus();
    }

    // Draw time grid
    drawTimeGrid(timeWindow) {
        const { start: windowStart, pixelsPerUs, durationUs } = timeWindow;

        // DOT duration = 1200ms / WPM (classical Morse relationship)
        const safeWpm = this.config.wpm > 0 ? this.config.wpm : 1;
        const dotDurationUs = (1200 / safeWpm) * 1000;

        // Draw vertical lines every DOT duration
        this.ctx.strokeStyle = this.COLORS.GRID;
        this.ctx.lineWidth = 0.5;

        const windowEnd = windowStart + durationUs;
        const firstDot = Math.floor(windowStart / dotDurationUs) * dotDurationUs;

        for (let t = firstDot; t <= windowEnd; t += dotDurationUs) {
            const x = (t - windowStart) * pixelsPerUs;
            if (x >= 0 && x <= this.width) {
                this.ctx.beginPath();
                this.ctx.moveTo(x, 0);
                this.ctx.lineTo(x, this.height);
                this.ctx.stroke();
            }
        }

        // Draw second markers (bolder) with labels at BOTTOM
        const secondUs = 1000 * 1000;
        const firstSecond = Math.floor(windowStart / secondUs) * secondUs;

        this.ctx.strokeStyle = this.COLORS.TEXT;
        this.ctx.lineWidth = 1.5;

        for (let t = firstSecond; t <= this.lastTimestamp; t += secondUs) {
            const x = (t - windowStart) * pixelsPerUs;
            if (x >= 0 && x <= this.width) {
                this.ctx.beginPath();
                this.ctx.moveTo(x, 0);
                this.ctx.lineTo(x, this.height);
                this.ctx.stroke();

                // Time label at BOTTOM (seconds relative to window start)
                const secRelative = Math.round((t - windowStart) / secondUs);
                this.ctx.fillStyle = this.COLORS.TEXT;
                this.ctx.font = 'bold 14px monospace';
                this.ctx.fillText(`${secRelative}s`, x + 2, this.height - 10);
            }
        }
    }

    // Draw paddle row (DOT or DASH)
    drawPaddleRow(type, yOffset, timeWindow) {
        const color = type === 'DOT' ? this.COLORS.DOT : this.COLORS.DASH;
        const eventPress = type === 'DOT' ? 'DOT_PRESS' : 'DASH_PRESS';
        const eventRelease = type === 'DOT' ? 'DOT_RELEASE' : 'DASH_RELEASE';

        const { start: windowStart, pixelsPerUs } = timeWindow;

        // Filter events for this paddle
        const paddleEvents = this.events.filter(evt =>
            evt.type === eventPress || evt.type === eventRelease
        ).sort((a, b) => a.timestamp - b.timestamp);

        // Draw state changes as rectangles
        let currentState = false;
        let stateStartTs = 0;

        for (let i = 0; i < paddleEvents.length; i++) {
            const evt = paddleEvents[i];

            if (evt.type === eventPress && !currentState) {
                // Start of press
                currentState = true;
                stateStartTs = evt.timestamp;
            } else if (evt.type === eventRelease && currentState) {
                // End of press - draw rectangle
                currentState = false;

                const x1 = (stateStartTs - windowStart) * pixelsPerUs;
                const x2 = (evt.timestamp - windowStart) * pixelsPerUs;

                if (x2 >= 0 && x1 <= this.width) {
                    const rectX = Math.max(0, x1);
                    const rectWidth = Math.min(this.width, x2) - rectX;

                    // Draw pressed state
                    this.ctx.fillStyle = color;
                    this.ctx.fillRect(rectX, yOffset + this.STATE_HIGH, rectWidth, this.ROW_HEIGHT - this.STATE_HIGH - 10);

                    // Check if iambic
                    if (evt.flags && evt.flags.includes('IAMBIC')) {
                        this.ctx.strokeStyle = this.COLORS.IAMBIC;
                        this.ctx.lineWidth = 3;
                        this.ctx.strokeRect(rectX, yOffset + this.STATE_HIGH, rectWidth, this.ROW_HEIGHT - this.STATE_HIGH - 10);
                    }
                }
            }
        }

        // Draw current state if still pressed
        if (currentState && stateStartTs) {
            const x1 = (stateStartTs - windowStart) * pixelsPerUs;
            if (x1 < this.width) {
                const rectX = Math.max(0, x1);
                const rectWidth = this.width - rectX;
                this.ctx.fillStyle = color;
                this.ctx.fillRect(rectX, yOffset + this.STATE_HIGH, rectWidth, this.ROW_HEIGHT - this.STATE_HIGH - 10);
            }
        }

        // Draw baseline
        this.ctx.strokeStyle = this.COLORS.GRID;
        this.ctx.lineWidth = 1;
        this.ctx.beginPath();
        this.ctx.moveTo(0, yOffset + this.STATE_LOW);
        this.ctx.lineTo(this.width, yOffset + this.STATE_LOW);
        this.ctx.stroke();
    }

    // Draw output row
    drawOutputRow(yOffset, timeWindow) {
        const color = this.COLORS.OUTPUT;
        const { start: windowStart, pixelsPerUs } = timeWindow;

        // Filter KEY events
        const outputEvents = this.events.filter(evt =>
            evt.type === 'KEY_ON' || evt.type === 'KEY_OFF'
        ).sort((a, b) => a.timestamp - b.timestamp);

        let currentState = false;
        let stateStartTs = 0;

        for (let i = 0; i < outputEvents.length; i++) {
            const evt = outputEvents[i];

            if (evt.type === 'KEY_ON' && !currentState) {
                currentState = true;
                stateStartTs = evt.timestamp;
            } else if (evt.type === 'KEY_OFF' && currentState) {
                currentState = false;

                const x1 = (stateStartTs - windowStart) * pixelsPerUs;
                const x2 = (evt.timestamp - windowStart) * pixelsPerUs;

                if (x2 >= 0 && x1 <= this.width) {
                    const rectX = Math.max(0, x1);
                    const rectWidth = Math.min(this.width, x2) - rectX;

                    // Draw ON state
                    this.ctx.fillStyle = color;
                    this.ctx.fillRect(rectX, yOffset + this.STATE_HIGH, rectWidth, this.ROW_HEIGHT - this.STATE_HIGH - 10);
                }
            }
        }

        // Draw current state if still ON
        if (currentState && stateStartTs) {
            const x1 = (stateStartTs - windowStart) * pixelsPerUs;
            if (x1 < this.width) {
                const rectX = Math.max(0, x1);
                const rectWidth = this.width - rectX;
                this.ctx.fillStyle = color;
                this.ctx.fillRect(rectX, yOffset + this.STATE_HIGH, rectWidth, this.ROW_HEIGHT - this.STATE_HIGH - 10);
            }
        }

        // Draw baseline
        this.ctx.strokeStyle = this.COLORS.GRID;
        this.ctx.lineWidth = 1;
        this.ctx.beginPath();
        this.ctx.moveTo(0, yOffset + this.STATE_LOW);
        this.ctx.lineTo(this.width, yOffset + this.STATE_LOW);
        this.ctx.stroke();
    }

    // Draw space markers spanning all three rows
    drawSpaceMarkers(timeWindow) {
        const { start: windowStart, pixelsPerUs } = timeWindow;

        // Filter space events
        const spaceEvents = this.events.filter(evt =>
            evt.type === 'SPACE_CHAR' || evt.type === 'SPACE_WORD'
        );

        for (const evt of spaceEvents) {
            const x = (evt.timestamp - windowStart) * pixelsPerUs;

            // Only draw if within visible window
            if (x >= 0 && x <= this.width) {
                if (evt.type === 'SPACE_CHAR') {
                    // Inter-character space: single red vertical line spanning all rows
                    this.ctx.strokeStyle = '#e74c3c';  // Red (distinguibile dalla griglia)
                    this.ctx.lineWidth = 2;
                    this.ctx.beginPath();
                    this.ctx.moveTo(x, this.DOT_ROW_Y + this.STATE_HIGH);
                    this.ctx.lineTo(x, this.OUTPUT_ROW_Y + this.STATE_LOW);
                    this.ctx.stroke();
                } else if (evt.type === 'SPACE_WORD') {
                    // Inter-word space: double red vertical lines spanning all rows
                    const offset = 1.5;  // Pixel spacing between double lines (più vicine)
                    this.ctx.strokeStyle = '#e74c3c';  // Red (distinguibile dalla griglia)
                    this.ctx.lineWidth = 2;

                    // First line
                    this.ctx.beginPath();
                    this.ctx.moveTo(x - offset, this.DOT_ROW_Y + this.STATE_HIGH);
                    this.ctx.lineTo(x - offset, this.OUTPUT_ROW_Y + this.STATE_LOW);
                    this.ctx.stroke();

                    // Second line
                    this.ctx.beginPath();
                    this.ctx.moveTo(x + offset, this.DOT_ROW_Y + this.STATE_HIGH);
                    this.ctx.lineTo(x + offset, this.OUTPUT_ROW_Y + this.STATE_LOW);
                    this.ctx.stroke();
                }
            }
        }
    }

    // Draw row labels
    drawLabels() {
        this.ctx.fillStyle = this.COLORS.TEXT;
        this.ctx.font = 'bold 18px sans-serif';

        this.ctx.fillText('DOT', 10, this.DOT_ROW_Y + 55);
        this.ctx.fillText('DASH', 10, this.DASH_ROW_Y + 55);
        this.ctx.fillText('OUT', 10, this.OUTPUT_ROW_Y + 55);
    }

    // Draw connection status indicator
    drawConnectionStatus() {
        const x = this.width - 100;
        const y = 10;

        this.ctx.fillStyle = this.wsConnected ? '#27ae60' : '#e74c3c';
        this.ctx.beginPath();
        this.ctx.arc(x, y, 5, 0, 2 * Math.PI);
        this.ctx.fill();

        this.ctx.fillStyle = this.COLORS.TEXT;
        this.ctx.font = '10px sans-serif';
        this.ctx.fillText(this.wsConnected ? 'Connected' : 'Disconnected', x + 10, y + 4);
    }

    // Update configuration
    updateConfig(newConfig) {
        this.config = {...this.config, ...newConfig};
    }
}
