<template>
  <div id="app" class="app-bg">
    <router-view/>
  </div>
</template>

<script>
export default { name: 'App' }
</script>

<style>
html, body, #app { height: 100%; background-color: #0b1120; }
.app-bg { min-height: 100%; background-color: #0b1120; }
</style>
