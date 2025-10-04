// robOS Web Interface JavaScript

class RobOSAPI {
    constructor() {
        this.baseURL = '';
        this.updateInterval = null;
    }

    async get(endpoint) {
        try {
            const response = await fetch(this.baseURL + endpoint);
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            return await response.json();
        } catch (error) {
            console.error(`API Error (${endpoint}):`, error);
            throw error;
        }
    }

    async getNetworkInfo() {
        return this.get('/api/network');
    }

    async getSystemInfo() {
        return this.get('/api/system');
    }
}

class WebInterface {
    constructor() {
        this.api = new RobOSAPI();
        this.init();
    }

    init() {
        this.setupEventListeners();
        this.startPeriodicUpdates();
        this.loadInitialData();
    }

    setupEventListeners() {
        const networkBtn = document.getElementById('network-btn');
        const systemBtn = document.getElementById('system-btn');

        if (networkBtn) {
            networkBtn.addEventListener('click', () => this.loadNetworkInfo());
        }

        if (systemBtn) {
            systemBtn.addEventListener('click', () => this.loadSystemInfo());
        }
    }

    async loadNetworkInfo() {
        const btn = document.getElementById('network-btn');
        const result = document.getElementById('network-result');
        
        if (btn) btn.disabled = true;
        if (result) result.innerHTML = '<div class="loading">加载中...</div>';

        try {
            const data = await this.api.getNetworkInfo();
            if (result) {
                result.innerHTML = `<pre>${JSON.stringify(data, null, 2)}</pre>`;
            }
        } catch (error) {
            if (result) {
                result.innerHTML = `<div style="color: red;">错误: ${error.message}</div>`;
            }
        } finally {
            if (btn) btn.disabled = false;
        }
    }

    async loadSystemInfo() {
        const btn = document.getElementById('system-btn');
        const result = document.getElementById('system-result');
        
        if (btn) btn.disabled = true;
        if (result) result.innerHTML = '<div class="loading">加载中...</div>';

        try {
            const data = await this.api.getSystemInfo();
            if (result) {
                result.innerHTML = `<pre>${JSON.stringify(data, null, 2)}</pre>`;
            }
        } catch (error) {
            if (result) {
                result.innerHTML = `<div style="color: red;">错误: ${error.message}</div>`;
            }
        } finally {
            if (btn) btn.disabled = false;
        }
    }

    updateTime() {
        const timeElement = document.getElementById('current-time');
        if (timeElement) {
            const now = new Date();
            timeElement.textContent = now.toLocaleString('zh-CN');
        }
    }

    async loadInitialData() {
        // Load initial system status
        try {
            const systemInfo = await this.api.getSystemInfo();
            this.updateSystemStatus(systemInfo);
        } catch (error) {
            console.warn('Could not load initial system data:', error);
        }
    }

    updateSystemStatus(systemInfo) {
        // Update various UI elements with system info
        if (systemInfo.uptime) {
            const uptimeElement = document.getElementById('uptime');
            if (uptimeElement) {
                uptimeElement.textContent = this.formatUptime(systemInfo.uptime);
            }
        }

        if (systemInfo.free_heap) {
            const heapElement = document.getElementById('free-heap');
            if (heapElement) {
                heapElement.textContent = this.formatBytes(systemInfo.free_heap);
            }
        }
    }

    formatUptime(seconds) {
        const days = Math.floor(seconds / 86400);
        const hours = Math.floor((seconds % 86400) / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        
        if (days > 0) {
            return `${days}天 ${hours}小时 ${minutes}分钟`;
        } else if (hours > 0) {
            return `${hours}小时 ${minutes}分钟`;
        } else {
            return `${minutes}分钟`;
        }
    }

    formatBytes(bytes) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024;
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    startPeriodicUpdates() {
        // Update time every second
        this.updateTime();
        setInterval(() => this.updateTime(), 1000);

        // Update system info every 30 seconds
        this.updateInterval = setInterval(() => {
            this.loadInitialData();
        }, 30000);
    }

    destroy() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
        }
    }
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    window.robosInterface = new WebInterface();
});

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
    if (window.robosInterface) {
        window.robosInterface.destroy();
    }
});