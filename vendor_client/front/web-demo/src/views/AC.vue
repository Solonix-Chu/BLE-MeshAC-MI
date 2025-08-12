<template>
  <div class="container">
    <div class="header">
      <h1>🏠 空调控制系统</h1>
      <p>BLE Mesh AC Control Interface</p>
    </div>

    <div class="status-bar">
      <div class="status-item">
        <span class="status-value">{{ totalDevices }}</span>
        <div class="status-label">总设备数</div>
      </div>
      <div class="status-item">
        <span class="status-value">{{ onlineDevices }}</span>
        <div class="status-label">在线设备</div>
      </div>
      <div class="status-item">
        <span class="status-value">{{ activeDevices }}</span>
        <div class="status-label">运行中</div>
      </div>
      <div class="status-item">
        <span class="status-value">{{ avgTempDisplay }}</span>
        <div class="status-label">平均设定温度</div>
      </div>
    </div>

    <div class="controls-bar">
      <button class="batch-control-btn btn-all-on" @click="controlAllDevices('power', AC_POWER_ON)">🔥 全部开启</button>
      <button class="batch-control-btn btn-all-off" @click="controlAllDevices('power', AC_POWER_OFF)">❄️ 全部关闭</button>
      <button class="batch-control-btn btn-refresh" @click="refreshAllDevices">🔄 刷新状态</button>
    </div>

    <div class="loading" v-if="loading">
      <div class="spinner"></div>
      正在处理请求...
    </div>

    <div class="devices-grid">
      <div
        class="device-card"
        :class="{ offline: !(device.is_online && device.is_configured) }"
        v-for="(device, index) in devices"
        :key="device.addr"
      >
        <div class="device-header">
          <div>
            <div class="device-name">
              <span class="status-indicator" :class="(device.is_online && device.is_configured) ? 'status-online' : 'status-offline'"></span>
              {{ device.device_name }}
            </div>
            <div class="device-addr">0x{{ device.addr.toString(16).toUpperCase() }}</div>
          </div>
          <div style="text-align: right; font-size: 0.8rem; color: #666;">
            {{ (device.is_online && device.is_configured) ? '在线' : '离线' }}
            {{ !device.is_configured ? ' (未配置)' : '' }}
          </div>
        </div>

        <div class="power-section">
          <button
            class="power-button"
            :class="device.power_state === AC_POWER_ON ? 'power-on' : 'power-off'"
            :disabled="!(device.is_online && device.is_configured)"
            @click="togglePower(device.addr)"
          >
            {{ device.power_state === AC_POWER_ON ? '🔥' : '❄️' }}
          </button>
          <div>{{ device.power_state === AC_POWER_ON ? '运行中' : '已关闭' }}</div>
        </div>

        <div class="controls-grid">
          <div class="control-group">
            <div class="control-label">温度控制</div>
            <div class="temperature-control">
              <button
                class="temp-btn temp-down"
                :disabled="!(device.is_online && device.is_configured) || device.power_state === AC_POWER_OFF"
                @click="adjustTemperature(device.addr, -1)"
              >-</button>
              <span class="temp-display">{{ device.temperature }}°C</span>
              <button
                class="temp-btn temp-up"
                :disabled="!(device.is_online && device.is_configured) || device.power_state === AC_POWER_OFF"
                @click="adjustTemperature(device.addr, 1)"
              >+</button>
            </div>
          </div>

          <div class="control-group">
            <div class="control-label">运行模式</div>
            <div class="mode-buttons">
              <button
                v-for="(name, index) in MODE_NAMES"
                :key="index"
                class="mode-btn"
                :class="{ active: device.mode === index }"
                :disabled="!(device.is_online && device.is_configured) || device.power_state === AC_POWER_OFF"
                @click="setMode(device.addr, index)"
              >{{ name }}</button>
            </div>
          </div>

          <div class="control-group">
            <div class="control-label">风速设置</div>
            <div class="fan-buttons">
              <button
                v-for="(name, index) in FAN_NAMES"
                :key="index"
                class="fan-btn"
                :class="{ active: device.fan_speed === index }"
                :disabled="!(device.is_online && device.is_configured) || device.power_state === AC_POWER_OFF"
                @click="setFanSpeed(device.addr, index)"
              >{{ name }}</button>
            </div>
          </div>
        </div>

        <div class="last-update">最后更新: {{ formatLastUpdate(device.last_update_time) }}</div>
      </div>
    </div>

    <div v-if="notificationVisible" :class="['notification', notificationType, { show: notificationVisible }]">
      {{ notificationMessage }}
    </div>
  </div>
</template>

<script>
export default {
  name: 'AC',
  data() {
    const AC_POWER_OFF = 0;
    const AC_POWER_ON = 1;
    const AC_MODE_COOL = 0;
    const AC_MODE_HEAT = 1;
    const AC_MODE_FAN = 2;
    const AC_MODE_DRY = 3;
    const AC_MODE_AUTO = 4;
    const AC_FAN_SPEED_AUTO = 0;
    const AC_FAN_SPEED_LOW = 1;
    const AC_FAN_SPEED_MEDIUM = 2;
    const AC_FAN_SPEED_HIGH = 3;

    return {
      // constants
      AC_POWER_OFF,
      AC_POWER_ON,
      AC_MODE_COOL,
      AC_MODE_HEAT,
      AC_MODE_FAN,
      AC_MODE_DRY,
      AC_MODE_AUTO,
      AC_FAN_SPEED_AUTO,
      AC_FAN_SPEED_LOW,
      AC_FAN_SPEED_MEDIUM,
      AC_FAN_SPEED_HIGH,
      MODE_NAMES: ['制冷', '制热', '送风', '除湿', '自动'],
      FAN_NAMES: ['自动', '低风', '中风', '高风'],

      // ui state
      loading: false,
      notificationVisible: false,
      notificationMessage: '',
      notificationType: 'success',
      timer: null,

      // devices from backend
      devices: []
    }
  },
  computed: {
    totalDevices() {
      return this.devices.length;
    },
    onlineDevices() {
      return this.devices.filter(d => d.is_online && d.is_configured).length;
    },
    activeDevices() {
      return this.devices.filter(d => d.is_online && d.is_configured && d.power_state === this.AC_POWER_ON).length;
    },
    avgTempDisplay() {
      const actives = this.devices.filter(d => d.is_online && d.is_configured && d.power_state === this.AC_POWER_ON);
      if (actives.length === 0) return 'N/A';
      const avg = Math.round(actives.reduce((sum, d) => sum + d.temperature, 0) / actives.length);
      return `${avg}°C`;
    }
  },
  methods: {
    async fetchDevices() {
      try {
        const res = await fetch('/api/v1/devices');
        if (!res.ok) throw new Error('获取设备失败');
        const data = await res.json();
        const now = Date.now();
        this.devices = (data.devices || []).map((d) => ({
          addr: d.addr,
          is_online: !!d.online,
          is_configured: true,
          power_state: d.power,
          temperature: d.temperature,
          mode: d.mode,
          fan_speed: d.fan_speed,
          device_name: d.name || `AC ${d.addr}`,
          last_update_time: now
        }));
      } catch (e) {
        this.showNotification(e.message || '获取设备失败', 'error');
      }
    },
    async postControl(index, body) {
      const res = await fetch(`/api/v1/devices/${index}/control`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      });
      if (!res.ok) throw new Error('控制失败');
      const data = await res.json();
      // 刷新设备列表
      const now = Date.now();
      this.devices = (data.devices || []).map((d) => ({
        addr: d.addr,
        is_online: !!d.online,
        is_configured: true,
        power_state: d.power,
        temperature: d.temperature,
        mode: d.mode,
        fan_speed: d.fan_speed,
        device_name: d.name || `AC ${d.addr}`,
        last_update_time: now
      }));
    },
    showLoading(show) {
      this.loading = show;
    },
    showNotification(message, type = 'success') {
      this.notificationMessage = message;
      this.notificationType = type;
      this.notificationVisible = true;
      setTimeout(() => {
        this.notificationVisible = false;
      }, 3000);
    },
    formatLastUpdate(timestamp) {
      const now = Date.now();
      const diff = now - timestamp;
      const minutes = Math.floor(diff / 60000);
      const hours = Math.floor(diff / 3600000);
      if (diff < 60000) return '刚刚更新';
      if (minutes < 60) return `${minutes}分钟前`;
      if (hours < 24) return `${hours}小时前`;
      return '很久以前';
    },
    findIndexByAddr(deviceAddr) {
      return this.devices.findIndex(d => d.addr === deviceAddr);
    },
    async togglePower(deviceAddr) {
      const idx = this.findIndexByAddr(deviceAddr);
      if (idx < 0) return;
      const device = this.devices[idx];
      const newPower = device.power_state === this.AC_POWER_ON ? this.AC_POWER_OFF : this.AC_POWER_ON;
      this.showLoading(true);
      try {
        await this.postControl(idx, { power: newPower });
        this.showNotification(`${device.device_name} 已${newPower === this.AC_POWER_ON ? '开启' : '关闭'}`, 'success');
      } catch (e) {
        this.showNotification(e.message, 'error');
      }
      this.showLoading(false);
    },
    async adjustTemperature(deviceAddr, delta) {
      const idx = this.findIndexByAddr(deviceAddr);
      if (idx < 0) return;
      const device = this.devices[idx];
      const newTemp = Math.max(16, Math.min(30, device.temperature + delta));
      if (newTemp === device.temperature) return;
      this.showLoading(true);
      try {
        await this.postControl(idx, { temperature: newTemp });
        this.showNotification(`${device.device_name} 温度已设置为 ${newTemp}°C`, 'success');
      } catch (e) {
        this.showNotification(e.message, 'error');
      }
      this.showLoading(false);
    },
    async setMode(deviceAddr, mode) {
      const idx = this.findIndexByAddr(deviceAddr);
      if (idx < 0) return;
      const device = this.devices[idx];
      if (device.mode === mode) return;
      this.showLoading(true);
      try {
        await this.postControl(idx, { mode });
        this.showNotification(`${device.device_name} 模式已设置为 ${this.MODE_NAMES[mode]}`, 'success');
      } catch (e) {
        this.showNotification(e.message, 'error');
      }
      this.showLoading(false);
    },
    async setFanSpeed(deviceAddr, fanSpeed) {
      const idx = this.findIndexByAddr(deviceAddr);
      if (idx < 0) return;
      const device = this.devices[idx];
      if (device.fan_speed === fanSpeed) return;
      this.showLoading(true);
      try {
        await this.postControl(idx, { fan_speed: fanSpeed });
        this.showNotification(`${device.device_name} 风速已设置为 ${this.FAN_NAMES[fanSpeed]}`, 'success');
      } catch (e) {
        this.showNotification(e.message, 'error');
      }
      this.showLoading(false);
    },
    async controlAllDevices(commandType, value) {
      const online = this.devices.filter(d => d.is_online && d.is_configured);
      if (online.length === 0) {
        this.showNotification('没有在线设备可以控制', 'error');
        return;
      }
      this.showLoading(true);
      try {
        const bodyKey = commandType === 'power' ? 'power' : commandType === 'temperature' ? 'temperature' : commandType === 'mode' ? 'mode' : 'fan_speed';
        await Promise.all(online.map(d => this.postControl(this.findIndexByAddr(d.addr), { [bodyKey]: value })));
        const action = commandType === 'power' ? (value === this.AC_POWER_ON ? '开启' : '关闭') : '控制';
        this.showNotification(`所有在线设备已${action}`, 'success');
      } catch (e) {
        this.showNotification(`批量控制部分失败: ${e.message}`, 'error');
      }
      this.showLoading(false);
    },
    async refreshAllDevices() {
      this.showLoading(true);
      try {
        await this.fetchDevices();
        this.showNotification('设备状态已刷新', 'success');
      } catch (_) {}
      this.showLoading(false);
    }
  },
  mounted() {
    this.fetchDevices();
    this.timer = setInterval(() => {
      this.fetchDevices();
    }, 10000);
  },
  beforeDestroy() {
    if (this.timer) clearInterval(this.timer);
  }
}
</script>

<style scoped>
* { margin: 0; padding: 0; box-sizing: border-box; }
.container { max-width: 1400px; margin: 0 auto; padding: 20px; }
.header { text-align: center; color: #e5e7eb; margin-bottom: 30px; }
.header h1 { font-size: 2.0rem; margin-bottom: 10px; text-shadow: 0 2px 8px rgba(0,0,0,0.6); }
.status-bar { background: rgba(17,24,39,0.65); backdrop-filter: blur(10px); border: 1px solid rgba(255,255,255,0.06); border-radius: 15px; padding: 20px; margin-bottom: 30px; display: flex; justify-content: space-around; align-items: center; flex-wrap: wrap; gap: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.45); }
.status-item { text-align: center; color: #e5e7eb; }
.status-value { font-size: 2rem; font-weight: 800; display: block; color: #ffffff; text-shadow: 0 1px 4px rgba(0,0,0,0.5); }
.status-label { font-size: 0.9rem; opacity: 0.85; margin-top: 5px; }
.controls-bar { background: rgba(17,24,39,0.65); backdrop-filter: blur(10px); border: 1px solid rgba(255,255,255,0.06); border-radius: 15px; padding: 20px; margin-bottom: 30px; display: flex; gap: 15px; justify-content: center; flex-wrap: wrap; box-shadow: 0 10px 30px rgba(0,0,0,0.45); }
.batch-control-btn { padding: 12px 24px; border: none; border-radius: 25px; font-size: 1rem; font-weight: bold; cursor: pointer; transition: all 0.3s ease; text-transform: uppercase; letter-spacing: 1px; }
.btn-all-on { background: linear-gradient(135deg, #22c55e, #16a34a); color: white; }
.btn-all-off { background: linear-gradient(135deg, #ef4444, #dc2626); color: white; }
.btn-refresh { background: linear-gradient(135deg, #3b82f6, #2563eb); color: white; }
.batch-control-btn:hover { transform: translateY(-2px); box-shadow: 0 8px 20px rgba(0,0,0,0.4); }
.devices-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(400px, 1fr)); gap: 25px; margin-bottom: 30px; }
.device-card { background: #ffffff; border-radius: 20px; padding: 25px; box-shadow: 0 25px 40px rgba(0,0,0,0.35); border: 1px solid #eef2f7; transition: all 0.3s ease; position: relative; overflow: hidden; }
.device-card:hover { transform: translateY(-5px); box-shadow: 0 30px 50px rgba(0,0,0,0.45); }
.device-card.offline { opacity: 0.7; }
.device-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; padding-bottom: 15px; border-bottom: 1px dashed #e5e7eb; }
.device-name { font-size: 1.1rem; font-weight: 700; color: #111827; }
.device-addr { font-size: 0.85rem; color: #6b7280; }
.status-indicator { width: 10px; height: 10px; border-radius: 50%; display: inline-block; margin-right: 8px; border: 2px solid #ffffff; }
.status-online { background: #22c55e; box-shadow: 0 0 10px rgba(34,197,94,0.6); }
.status-offline { background: #ef4444; }
.power-section { text-align: center; margin-bottom: 25px; }
.power-button { width: 72px; height: 72px; border-radius: 50%; border: none; font-size: 1.4rem; cursor: pointer; transition: all 0.2s ease; margin: 0 auto 10px auto; display: flex; align-items: center; justify-content: center; }
.power-on { background: linear-gradient(135deg, #22c55e, #16a34a); color: white; box-shadow: 0 12px 24px rgba(34,197,94,0.35); }
.power-off { background: linear-gradient(135deg, #9ca3af, #6b7280); color: white; }
.power-button:hover { transform: scale(1.06); }
.controls-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
.control-group { background: #f9fafb; border-radius: 12px; padding: 15px; border: 1px solid #eef2f7; }
.control-label { font-size: 0.85rem; font-weight: 800; color: #374151; margin-bottom: 10px; text-transform: uppercase; letter-spacing: 1px; }
.temperature-control { display: flex; align-items: center; justify-content: space-between; }
.temp-display { font-size: 1.6rem; font-weight: 800; color: #111827; min-width: 80px; text-align: center; }
.temp-btn { width: 38px; height: 38px; border-radius: 50%; border: none; font-size: 1.1rem; font-weight: 800; cursor: pointer; transition: all 0.2s ease; }
.temp-btn:hover { transform: scale(1.06); }
.temp-up { background: linear-gradient(135deg, #f97316, #ea580c); color: white; }
.temp-down { background: linear-gradient(135deg, #3b82f6, #2563eb); color: white; }
.mode-buttons { display: flex; flex-wrap: wrap; gap: 8px; }
.mode-btn { flex: 1; min-width: 60px; padding: 8px 12px; border: 2px solid #e5e7eb; border-radius: 20px; background: white; cursor: pointer; transition: all 0.2s ease; font-size: 0.8rem; text-align: center; color: #111827; }
.mode-btn.active { background: linear-gradient(135deg, #3b82f6, #2563eb); color: white; border-color: transparent; box-shadow: 0 8px 16px rgba(37,99,235,0.25); }
.mode-btn:hover { border-color: #93c5fd; }
.fan-buttons { display: flex; gap: 8px; }
.fan-btn { flex: 1; padding: 10px; border: 2px solid #e5e7eb; border-radius: 12px; background: white; cursor: pointer; transition: all 0.2s ease; font-size: 0.8rem; text-align: center; color: #111827; }
.fan-btn.active { background: linear-gradient(135deg, #22c55e, #16a34a); color: white; border-color: transparent; box-shadow: 0 8px 16px rgba(34,197,94,0.25); }
.fan-btn:hover { border-color: #86efac; }
.last-update { text-align: center; font-size: 0.8rem; color: #6b7280; margin-top: 15px; padding-top: 15px; border-top: 1px dashed #e5e7eb; }
@media (max-width: 768px) {
  .devices-grid { grid-template-columns: 1fr; }
  .status-bar { flex-direction: column; gap: 10px; }
  .controls-bar { flex-direction: column; align-items: center; }
  .header h1 { font-size: 1.6rem; }
}
.loading { text-align: center; color: #e5e7eb; font-size: 1.2rem; margin: 20px 0; }
.spinner { border: 3px solid rgba(255,255,255,0.25); border-radius: 50%; border-top: 3px solid #ffffff; width: 30px; height: 30px; animation: spin 1s linear infinite; margin: 0 auto 10px auto; }
@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
.notification { position: fixed; top: 20px; right: 20px; padding: 15px 20px; border-radius: 10px; color: white; font-weight: bold; z-index: 1000; transform: translateX(400px); transition: transform 0.3s ease; box-shadow: 0 20px 40px rgba(0,0,0,0.35); }
.notification.show { transform: translateX(0); }
.notification.success { background: linear-gradient(135deg, #22c55e, #16a34a); }
.notification.error { background: linear-gradient(135deg, #ef4444, #dc2626); }
</style> 