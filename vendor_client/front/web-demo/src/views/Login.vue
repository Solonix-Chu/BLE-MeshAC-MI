<template>
  <div class="login-bg">
    <div class="login-card">
      <div class="title">空调管理登录</div>
      <div class="subtitle">请输入管理口令</div>
      <input
        class="input"
        type="password"
        placeholder="密码"
        v-model="password"
        @keyup.enter="login"
      />
      <button class="btn" @click="login">登录</button>
      <div v-if="error" class="error">{{ error }}</div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Login',
  data() {
    return { password: '', error: '' }
  },
  methods: {
    login() {
      if (this.password === '12345678') {
        localStorage.setItem('auth', '1')
        this.$router.replace('/ac')
      } else {
        this.error = '密码错误'
      }
    }
  }
}
</script>

<style scoped>
.login-bg {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #1f2937, #0f172a);
}
.login-card {
  width: 360px;
  padding: 28px 28px 22px;
  border-radius: 16px;
  background: #ffffff;
  box-shadow: 0 20px 50px rgba(0,0,0,0.35);
}
.title { font-size: 22px; font-weight: 700; color: #111827; }
.subtitle { margin-top: 6px; font-size: 12px; color: #6b7280; }
.input {
  width: 100%;
  margin-top: 18px;
  padding: 12px 14px;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  outline: none;
  font-size: 14px;
}
.input:focus { border-color: #2563eb; box-shadow: 0 0 0 3px rgba(37,99,235,0.15); }
.btn {
  width: 100%;
  margin-top: 16px;
  background: #2563eb;
  color: #fff;
  border: none;
  border-radius: 10px;
  padding: 12px 0;
  font-weight: 600;
  cursor: pointer;
}
.error { margin-top: 12px; color: #ef4444; font-size: 13px; }
</style> 